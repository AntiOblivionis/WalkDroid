#define _GNU_SOURCE
#include "sony_audio_bt_epoch_source.h"
#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <time.h>
#include <unistd.h>
#define EPOCH_JOBS 1024u
#define EPOCH_FRAMES 192000u
#define EPOCH_POOL_BYTES (EPOCH_FRAMES*8u)
#define EPOCH_MAX_NS 2000000000u
enum {SOURCE_DATA=1,SOURCE_END=2,SOURCE_FORMAT=3};
/* Jobs are immutable from write-release to read-release. Only main consumes
 * FORMAT; only sender consumes DATA/END. FORMAT at the head is their hand-off
 * fence, so a future producer never overwrites the active wire metadata. */
struct source_job {
    unsigned operation,at,frames,bytes,frame_bytes,duration_ns;
    uint64_t epoch;
    struct sony_bt_middleware_sound raw;
};
struct sony_bt_epoch_source {
    int fd,event,error;pid_t parent;uid_t uid;uint64_t cookie;
    pthread_t thread;unsigned sender_started,sender_exited,sender_joined,sender_stop;
    unsigned phase,peer,run,reconnect,stop_requested,stop_notified,configured;
    unsigned pause_generation,activity_gate,backend_ready,end_sent;
    unsigned cfg_sequence,cfg_busy,cfg_valid,cfg_words[5];
    unsigned producer_busy,producer_open,start_pause_generation,bootstrap;
    unsigned producer_frame_bytes,producer_rate;
    uint64_t producer_epoch;
    unsigned read,write,used,used_bytes,used_ns,write_at,consumer_busy;
    unsigned queued_peak_frames,queued_peak_ns,sealed,finish_attempted;
    struct source_job jobs[EPOCH_JOBS];
    unsigned char data[EPOCH_POOL_BYTES],packet[65536];
    /* Sender owns DATA counters. Main reads final counters only after join. */
    uint64_t sequence,control_sequence,epoch,epoch_frames,epoch_bytes,frames,bytes,packets;
    uint64_t ended_epoch,finish_notified_epoch;
    uint64_t offered_frames,offered_bytes,offered_packets,boundary_packets,boundary_bytes;
    uint64_t prefetched_packets,prefetched_bytes,unready_running_packets,unready_running_bytes;
    uint64_t future_packets,future_bytes;
    unsigned volume_read,volume_write,volume_gate;int32_t volume[64];
    uint64_t volume_sequence,volume_offered,volume_sent,volume_blocked_at;
    unsigned has_pending_control;struct sony_bt_live_header pending_control;
};
static uint64_t now(void){struct timespec t;return clock_gettime(CLOCK_MONOTONIC,&t)?0:(uint64_t)t.tv_sec*1000+t.tv_nsec/1000000;}
static int fail(struct sony_bt_epoch_source *s,int rc){if(rc>=0||rc< -4095)rc=-EPROTO;int zero=0;__atomic_compare_exchange_n(&s->error,&zero,rc,0,__ATOMIC_RELEASE,__ATOMIC_RELAXED);return __atomic_load_n(&s->error,__ATOMIC_ACQUIRE);}
static void wake(struct sony_bt_epoch_source *s){uint64_t one=1;ssize_t n=write(s->event,&one,8);if(n<0&&errno!=EAGAIN&&errno!=EINTR)fail(s,-errno);}
static int wait_event(struct sony_bt_epoch_source *s,unsigned ms){struct pollfd p={s->event,POLLIN,0};int n=poll(&p,1,(int)ms);if(n<0&&errno!=EINTR)return fail(s,-errno);if(n>0){uint64_t count;ssize_t ignored=read(s->event,&count,8);(void)ignored;}return 0;}
static struct sony_bt_live_header message(struct sony_bt_epoch_source *s,unsigned kind,unsigned bytes){return (struct sony_bt_live_header){SONY_BT_EPOCH_MAGIC,2,kind,bytes,s->cookie,s->epoch,s->sequence,s->epoch_frames};}
static unsigned accepting(const struct sony_bt_epoch_source *s){
    return s&&!__atomic_load_n(&s->error,__ATOMIC_ACQUIRE)&&!__atomic_load_n(&s->stop_requested,__ATOMIC_ACQUIRE)&&
        __atomic_load_n(&s->producer_open,__ATOMIC_ACQUIRE)&&__atomic_load_n(&s->peer,__ATOMIC_ACQUIRE)&&__atomic_load_n(&s->cfg_valid,__ATOMIC_ACQUIRE)&&
        (__atomic_load_n(&s->run,__ATOMIC_ACQUIRE)||__atomic_load_n(&s->bootstrap,__ATOMIC_ACQUIRE))&&
        __atomic_load_n(&s->pause_generation,__ATOMIC_ACQUIRE)==__atomic_load_n(&s->start_pause_generation,__ATOMIC_ACQUIRE);
}
unsigned sony_bt_epoch_source_ready(const struct sony_bt_epoch_source *s){return accepting(s)&&__atomic_load_n(&s->backend_ready,__ATOMIC_ACQUIRE)&&__atomic_load_n(&s->phase,__ATOMIC_ACQUIRE)==SONY_BT_EPOCH_ACTIVE&&__atomic_load_n(&s->epoch,__ATOMIC_ACQUIRE)==__atomic_load_n(&s->producer_epoch,__ATOMIC_ACQUIRE);}
unsigned sony_bt_epoch_source_phase(const struct sony_bt_epoch_source *s){return s?__atomic_load_n(&s->phase,__ATOMIC_ACQUIRE):SONY_BT_EPOCH_NEW;}
uint64_t sony_bt_epoch_source_generation(const struct sony_bt_epoch_source *s){return s?__atomic_load_n(&s->producer_epoch,__ATOMIC_ACQUIRE):0;}
/* Caller owns producer_busy. One job is reserved for the active producer's
 * END, which needs no PCM pool space and can seal a completely full pool. */
static int end_producer(struct sony_bt_epoch_source *s){
    if(!__atomic_load_n(&s->producer_open,__ATOMIC_ACQUIRE))return 0;
    unsigned at=__atomic_load_n(&s->write,__ATOMIC_RELAXED),rd=__atomic_load_n(&s->read,__ATOMIC_ACQUIRE);
    if(at-rd>=EPOCH_JOBS)return fail(s,-ENOBUFS);
    s->jobs[at%EPOCH_JOBS]=(struct source_job){.operation=SOURCE_END,.epoch=s->producer_epoch};
    __atomic_store_n(&s->producer_open,0,__ATOMIC_RELEASE);__atomic_store_n(&s->write,at+1,__ATOMIC_RELEASE);__atomic_store_n(&s->ended_epoch,s->producer_epoch,__ATOMIC_RELEASE);wake(s);return 0;
}
static int sync_producer_locked(struct sony_bt_epoch_source *s){
    unsigned pause=__atomic_load_n(&s->pause_generation,__ATOMIC_ACQUIRE),stop=__atomic_load_n(&s->stop_requested,__ATOMIC_ACQUIRE);
    unsigned peer=__atomic_load_n(&s->peer,__ATOMIC_ACQUIRE),valid=__atomic_load_n(&s->cfg_valid,__ATOMIC_ACQUIRE);
    if(__atomic_load_n(&s->producer_open,__ATOMIC_ACQUIRE)&&(stop||!peer||pause!=s->start_pause_generation)){int rc=end_producer(s);if(rc)return rc;}
    if(stop||!peer||!valid||__atomic_load_n(&s->producer_open,__ATOMIC_ACQUIRE))return 0;
    unsigned bootstrap=!s->producer_epoch||__atomic_load_n(&s->reconnect,__ATOMIC_ACQUIRE);
    if(!bootstrap&&!__atomic_load_n(&s->run,__ATOMIC_ACQUIRE))return 0;
    unsigned seq=__atomic_load_n(&s->cfg_sequence,__ATOMIC_ACQUIRE);if(seq&1u)return 0;
    unsigned words[5];for(unsigned n=0;n<5;++n)words[n]=__atomic_load_n(&s->cfg_words[n],__ATOMIC_RELAXED);
    if(seq!=__atomic_load_n(&s->cfg_sequence,__ATOMIC_ACQUIRE)||pause!=__atomic_load_n(&s->pause_generation,__ATOMIC_ACQUIRE)||!__atomic_load_n(&s->cfg_valid,__ATOMIC_ACQUIRE)||!__atomic_load_n(&s->peer,__ATOMIC_ACQUIRE)||__atomic_load_n(&s->stop_requested,__ATOMIC_ACQUIRE))return 0;
    if(s->producer_epoch==UINT64_MAX)return fail(s,-EOVERFLOW);
    unsigned at=__atomic_load_n(&s->write,__ATOMIC_RELAXED),rd=__atomic_load_n(&s->read,__ATOMIC_ACQUIRE);if(at-rd>=EPOCH_JOBS-1u)return fail(s,-ENOBUFS);
    struct sony_bt_middleware_sound raw;memcpy(&raw,words,sizeof(raw));uint64_t epoch=s->producer_epoch+1;
    s->jobs[at%EPOCH_JOBS]=(struct source_job){.operation=SOURCE_FORMAT,.epoch=epoch,.raw=raw};
    s->producer_frame_bytes=raw.codec==5?8u:4u;s->producer_rate=raw.rate;
    __atomic_store_n(&s->producer_epoch,epoch,__ATOMIC_RELEASE);__atomic_store_n(&s->start_pause_generation,pause,__ATOMIC_RELEASE);__atomic_store_n(&s->bootstrap,bootstrap,__ATOMIC_RELEASE);
    __atomic_store_n(&s->configured,1,__ATOMIC_RELEASE);__atomic_store_n(&s->reconnect,0,__ATOMIC_RELEASE);__atomic_store_n(&s->producer_open,1,__ATOMIC_RELEASE);
    __atomic_store_n(&s->write,at+1,__ATOMIC_RELEASE);wake(s);return 0;
}
static int sync_producer(struct sony_bt_epoch_source *s,unsigned callback){
    if(!callback&&__atomic_load_n(&s->activity_gate,__ATOMIC_ACQUIRE))return 0;
    if(__atomic_load_n(&s->producer_open,__ATOMIC_ACQUIRE)&&!__atomic_load_n(&s->stop_requested,__ATOMIC_ACQUIRE)&&__atomic_load_n(&s->peer,__ATOMIC_ACQUIRE)&&__atomic_load_n(&s->pause_generation,__ATOMIC_ACQUIRE)==__atomic_load_n(&s->start_pause_generation,__ATOMIC_ACQUIRE))return 0;
    unsigned zero=0;if(!__atomic_compare_exchange_n(&s->producer_busy,&zero,1,0,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED))return 0;
    int rc=__atomic_load_n(&s->error,__ATOMIC_ACQUIRE);if(!rc&&(!__atomic_load_n(&s->activity_gate,__ATOMIC_ACQUIRE)||callback))rc=sync_producer_locked(s);
    __atomic_store_n(&s->producer_busy,0,__ATOMIC_RELEASE);return rc;
}
static void *sender(void *context){
    struct sony_bt_epoch_source *s=context;
    for(;;){
        if(__atomic_load_n(&s->error,__ATOMIC_ACQUIRE))break;
        /* Acquire observes main's metadata before the DATA behind FORMAT. */
        unsigned zero=0;if(!__atomic_compare_exchange_n(&s->consumer_busy,&zero,1,0,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED)){if(wait_event(s,1))break;continue;}
        unsigned at=__atomic_load_n(&s->read,__ATOMIC_ACQUIRE),end=__atomic_load_n(&s->write,__ATOMIC_ACQUIRE);
        if(at==end){__atomic_store_n(&s->consumer_busy,0,__ATOMIC_RELEASE);if(__atomic_load_n(&s->sender_stop,__ATOMIC_ACQUIRE))break;if(wait_event(s,50))break;continue;}
        struct source_job job=s->jobs[at%EPOCH_JOBS];
        __atomic_store_n(&s->consumer_busy,0,__ATOMIC_RELEASE);
        if(job.operation==SOURCE_FORMAT||(job.operation==SOURCE_DATA&&!__atomic_load_n(&s->backend_ready,__ATOMIC_ACQUIRE))){if(wait_event(s,20))break;continue;}
        if(job.epoch!=s->epoch||s->sequence==UINT64_MAX){fail(s,job.epoch!=s->epoch?-ESTALE:-EOVERFLOW);break;}
        struct sony_bt_epoch_summary summary={s->epoch_frames,s->epoch_bytes};
        if(job.operation==SOURCE_DATA){unsigned first=EPOCH_POOL_BYTES-job.at;if(first>job.bytes)first=job.bytes;memcpy(s->packet,s->data+job.at,first);memcpy(s->packet+first,s->data,job.bytes-first);}
        else if(job.operation==SOURCE_END)__atomic_store_n(&s->phase,SONY_BT_EPOCH_FINISHING,__ATOMIC_RELEASE);
        else{fail(s,-EPROTO);break;}
        struct sony_bt_live_header h=message(s,job.operation==SOURCE_DATA?SONY_BT_EPOCH_PCM:SONY_BT_EPOCH_END,job.operation==SOURCE_DATA?job.bytes:16);
        int rc;uint64_t deadline=now()+2000;
        for(;;){rc=sony_bt_epoch_transport_send(s->fd,&h,job.operation==SOURCE_DATA?(void *)s->packet:(void *)&summary);if(rc!=-EAGAIN)break;if(__atomic_load_n(&s->error,__ATOMIC_ACQUIRE)){rc=__atomic_load_n(&s->error,__ATOMIC_ACQUIRE);break;}if(now()>=deadline){rc=-ETIMEDOUT;break;}struct pollfd p={s->fd,POLLOUT,0};if(poll(&p,1,20)<0&&errno!=EINTR){rc=-errno;break;}}
        if(rc){fail(s,rc);break;}
        ++s->sequence;
        if(job.operation==SOURCE_DATA){s->epoch_frames+=job.frames;s->epoch_bytes+=job.bytes;s->frames+=job.frames;s->bytes+=job.bytes;++s->packets;__atomic_sub_fetch(&s->used,job.frames,__ATOMIC_RELEASE);__atomic_sub_fetch(&s->used_bytes,job.bytes,__ATOMIC_RELEASE);__atomic_sub_fetch(&s->used_ns,job.duration_ns,__ATOMIC_RELEASE);}
        __atomic_store_n(&s->read,at+1,__ATOMIC_RELEASE);
        if(job.operation==SOURCE_END)__atomic_store_n(&s->end_sent,1,__ATOMIC_RELEASE);
    }
    __atomic_store_n(&s->sender_exited,1,__ATOMIC_RELEASE);return NULL;
}
int sony_bt_epoch_source_create(struct sony_bt_epoch_source **out,int fd,pid_t parent,uint64_t cookie){
    if(!out||*out||fd<0)return -EINVAL;unsigned size;int rc=sony_bt_epoch_transport_prepare(fd,&size);if(rc)return rc;
    struct sony_bt_epoch_source *s=calloc(1,sizeof(*s));if(!s)return -ENOMEM;
    s->fd=fd;s->parent=parent;s->uid=getuid();s->cookie=cookie;s->event=eventfd(0,EFD_CLOEXEC|EFD_NONBLOCK);if(s->event<0){rc=-errno;free(s);return rc;}
    for(unsigned at=0;at<sizeof(s->data);at+=4096)((volatile unsigned char *)s->data)[at]=0;
    for(unsigned at=0;at<sizeof(s->packet);at+=4096)((volatile unsigned char *)s->packet)[at]=0;
    rc=pthread_create(&s->thread,NULL,sender,s);if(rc){close(s->event);free(s);return -rc;}s->sender_started=1;*out=s;return 0;
}
int sony_bt_epoch_source_error(struct sony_bt_epoch_source *s,int rc){return !s||rc>=0||rc< -4095?-EINVAL:fail(s,rc);}
int sony_bt_epoch_source_peer(struct sony_bt_epoch_source *s,unsigned peer){
    if(!s||peer>1)return -EINVAL;unsigned old=__atomic_exchange_n(&s->peer,peer,__ATOMIC_ACQ_REL);
    if(!peer){__atomic_store_n(&s->cfg_valid,0,__ATOMIC_RELEASE);__atomic_store_n(&s->run,0,__ATOMIC_RELEASE);if(old){__atomic_store_n(&s->reconnect,1,__ATOMIC_RELEASE);__atomic_add_fetch(&s->pause_generation,1,__ATOMIC_RELEASE);}}return sync_producer(s,0);
}
int sony_bt_epoch_source_activity(struct sony_bt_epoch_source *s,unsigned playing){
    if(!s||playing>1)return -EINVAL;unsigned zero=0;if(!__atomic_compare_exchange_n(&s->activity_gate,&zero,1,0,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED))return fail(s,-EBUSY);
    unsigned previous=__atomic_exchange_n(&s->run,playing,__ATOMIC_ACQ_REL);if(!playing&&previous)__atomic_add_fetch(&s->pause_generation,1,__ATOMIC_RELEASE);
    int rc=sync_producer(s,1);__atomic_store_n(&s->activity_gate,0,__ATOMIC_RELEASE);return rc;
}
int sony_bt_epoch_source_format(struct sony_bt_epoch_source *s,const struct sony_bt_middleware_sound *raw){
    if(!s||!raw)return -EINVAL;struct sony_bt_middleware_format format;int rc=sony_bt_middleware_format(raw,&format);if(rc)return fail(s,rc);if(raw->state!=1&&raw->state!=2)return fail(s,-EPROTO);
    unsigned zero=0;if(!__atomic_compare_exchange_n(&s->cfg_busy,&zero,1,0,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED))return fail(s,-EBUSY);
    unsigned valid=__atomic_load_n(&s->cfg_valid,__ATOMIC_ACQUIRE),words[5]={1,raw->codec,raw->rate,raw->channel,raw->native_flag},changed=0;
    if(!valid&&raw->state!=1){__atomic_store_n(&s->cfg_busy,0,__ATOMIC_RELEASE);return fail(s,-EPROTO);}
    for(unsigned n=1;n<5;++n)changed|=__atomic_load_n(&s->cfg_words[n],__ATOMIC_RELAXED)!=words[n];
    /* Invalid until all words AND the boundary generation are published. */
    __atomic_add_fetch(&s->cfg_sequence,1,__ATOMIC_ACQ_REL);__atomic_store_n(&s->cfg_valid,0,__ATOMIC_RELEASE);
    for(unsigned n=0;n<5;++n)__atomic_store_n(&s->cfg_words[n],words[n],__ATOMIC_RELAXED);
    if(valid&&changed)__atomic_add_fetch(&s->pause_generation,1,__ATOMIC_RELEASE);
    __atomic_store_n(&s->cfg_valid,1,__ATOMIC_RELEASE);__atomic_add_fetch(&s->cfg_sequence,1,__ATOMIC_RELEASE);__atomic_store_n(&s->cfg_busy,0,__ATOMIC_RELEASE);return sync_producer(s,0);
}
static int boundary(struct sony_bt_epoch_source *s,unsigned bytes){__atomic_add_fetch(&s->boundary_packets,1,__ATOMIC_RELAXED);__atomic_add_fetch(&s->boundary_bytes,bytes,__ATOMIC_RELAXED);if(__atomic_load_n(&s->run,__ATOMIC_ACQUIRE)&&!__atomic_load_n(&s->stop_requested,__ATOMIC_ACQUIRE)){__atomic_add_fetch(&s->unready_running_packets,1,__ATOMIC_RELAXED);__atomic_add_fetch(&s->unready_running_bytes,bytes,__ATOMIC_RELAXED);}return 0;}
int sony_bt_epoch_source_pcm(struct sony_bt_epoch_source *s,unsigned bytes,const void *data){
    if(!s)return -EINVAL;int first_error=__atomic_load_n(&s->error,__ATOMIC_ACQUIRE);if(first_error)return first_error;if(!bytes)return 0;if(!data||bytes>SONY_BT_LIVE_MAX_BYTES)return fail(s,-EPROTO);
    unsigned zero=0;if(!__atomic_compare_exchange_n(&s->producer_busy,&zero,1,0,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED))return accepting(s)?fail(s,-EBUSY):boundary(s,bytes);
    int rc=__atomic_load_n(&s->error,__ATOMIC_ACQUIRE);if(!rc)rc=sync_producer_locked(s);
    if(!rc&&!accepting(s)){unsigned configured=__atomic_load_n(&s->configured,__ATOMIC_ACQUIRE);__atomic_store_n(&s->producer_busy,0,__ATOMIC_RELEASE);return configured?boundary(s,bytes):fail(s,-EPROTO);}
    unsigned frame_bytes=s->producer_frame_bytes,rate=s->producer_rate;
    if(!rc&&(!frame_bytes||bytes%frame_bytes||!rate))rc=-EPROTO;
    unsigned frames=rc?0:bytes/frame_bytes,duration=rc?0:(unsigned)(((uint64_t)frames*1000000000u+rate-1u)/rate);
    unsigned wr=__atomic_load_n(&s->write,__ATOMIC_RELAXED),rd=__atomic_load_n(&s->read,__ATOMIC_ACQUIRE),used=__atomic_load_n(&s->used_bytes,__ATOMIC_ACQUIRE),ns=__atomic_load_n(&s->used_ns,__ATOMIC_ACQUIRE);
    if(!rc&&(wr-rd>=EPOCH_JOBS-1u||used>EPOCH_POOL_BYTES||bytes>EPOCH_POOL_BYTES-used||ns>EPOCH_MAX_NS||duration>EPOCH_MAX_NS-ns))rc=-ENOBUFS;
    if(!rc&&(s->offered_frames>UINT64_MAX-frames||s->offered_bytes>UINT64_MAX-bytes||s->offered_packets==UINT64_MAX))rc=-EOVERFLOW;
    if(!rc){
        unsigned first=EPOCH_POOL_BYTES-s->write_at;if(first>bytes)first=bytes;memcpy(s->data+s->write_at,data,first);memcpy(s->data,(const char *)data+first,bytes-first);
        s->jobs[wr%EPOCH_JOBS]=(struct source_job){.operation=SOURCE_DATA,.at=s->write_at,.frames=frames,.bytes=bytes,.frame_bytes=frame_bytes,.duration_ns=duration,.epoch=s->producer_epoch};s->write_at=(s->write_at+bytes)%EPOCH_POOL_BYTES;
        unsigned queued=__atomic_add_fetch(&s->used,frames,__ATOMIC_RELEASE);if(queued>s->queued_peak_frames)s->queued_peak_frames=queued;
        queued=__atomic_add_fetch(&s->used_ns,duration,__ATOMIC_RELEASE);if(queued>s->queued_peak_ns)s->queued_peak_ns=queued;__atomic_add_fetch(&s->used_bytes,bytes,__ATOMIC_RELEASE);
        s->offered_frames+=frames;s->offered_bytes+=bytes;++s->offered_packets;
        uint64_t wire=__atomic_load_n(&s->epoch,__ATOMIC_ACQUIRE);if(wire!=s->producer_epoch||__atomic_load_n(&s->phase,__ATOMIC_ACQUIRE)!=SONY_BT_EPOCH_ACTIVE||!__atomic_load_n(&s->backend_ready,__ATOMIC_ACQUIRE)){++s->prefetched_packets;s->prefetched_bytes+=bytes;}
        if(wire&&wire<s->producer_epoch){++s->future_packets;s->future_bytes+=bytes;}
        __atomic_store_n(&s->write,wr+1,__ATOMIC_RELEASE);wake(s);
    }
    __atomic_store_n(&s->producer_busy,0,__ATOMIC_RELEASE);return rc?fail(s,rc):__atomic_load_n(&s->error,__ATOMIC_ACQUIRE);
}
int sony_bt_epoch_source_volume(struct sony_bt_epoch_source *s,int step){
    if(!s||(step!=1&&step!=-1))return -EINVAL;if(!__atomic_load_n(&s->peer,__ATOMIC_ACQUIRE))return -ENOTCONN;
    unsigned zero=0;if(!__atomic_compare_exchange_n(&s->volume_gate,&zero,1,0,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED))return zero&2u?-ESHUTDOWN:fail(s,-EBUSY);
    unsigned wr=__atomic_load_n(&s->volume_write,__ATOMIC_RELAXED),rd=__atomic_load_n(&s->volume_read,__ATOMIC_ACQUIRE);int rc=0;
    if(wr-rd>=64)rc=fail(s,-ENOBUFS);else{s->volume[wr%64]=step;++s->volume_offered;__atomic_store_n(&s->volume_write,wr+1,__ATOMIC_RELEASE);}
    __atomic_fetch_and(&s->volume_gate,~1u,__ATOMIC_RELEASE);return rc;
}
static int volume_poll(struct sony_bt_epoch_source *s){
    for(unsigned n=0;n<16;++n){unsigned at=__atomic_load_n(&s->volume_read,__ATOMIC_RELAXED);if(at==__atomic_load_n(&s->volume_write,__ATOMIC_ACQUIRE))break;struct sony_bt_live_header h={SONY_BT_LIVE_MAGIC,1,SONY_BT_LIVE_VOLUME_STEP,4,s->cookie,SONY_BT_LIVE_VOLUME_SOURCE,s->volume_sequence,0};int rc=sony_bt_epoch_transport_send(s->fd,&h,&s->volume[at%64]);if(rc==-EAGAIN){uint64_t time=now();if(!s->volume_blocked_at)s->volume_blocked_at=time;return time-s->volume_blocked_at<2000?0:fail(s,-ETIMEDOUT);}if(rc)return fail(s,rc);s->volume_blocked_at=0;++s->volume_sequence;++s->volume_sent;__atomic_store_n(&s->volume_read,at+1,__ATOMIC_RELEASE);}return 0;
}
int sony_bt_epoch_source_listening(struct sony_bt_epoch_source *s){
    if(!s||s->phase!=SONY_BT_EPOCH_NEW)return -EINVAL;struct sony_bt_live_header h=message(s,SONY_BT_EPOCH_LISTENING,0);int rc=sony_bt_epoch_transport_send(s->fd,&h,NULL);if(rc==-EAGAIN)return rc;if(rc)return fail(s,rc);++s->sequence;__atomic_store_n(&s->phase,SONY_BT_EPOCH_WAITING,__ATOMIC_RELEASE);return 0;
}
static int send_format_locked(struct sony_bt_epoch_source *s){
    unsigned phase=__atomic_load_n(&s->phase,__ATOMIC_ACQUIRE);if(phase!=SONY_BT_EPOCH_WAITING&&phase!=SONY_BT_EPOCH_PAUSED)return 0;
    unsigned at=__atomic_load_n(&s->read,__ATOMIC_ACQUIRE);if(at==__atomic_load_n(&s->write,__ATOMIC_ACQUIRE))return 0;
    struct source_job job=s->jobs[at%EPOCH_JOBS];if(job.operation!=SOURCE_FORMAT)return fail(s,-EPROTO);
    struct sony_bt_live_header h={SONY_BT_EPOCH_MAGIC,2,SONY_BT_EPOCH_FORMAT,20,s->cookie,job.epoch,s->sequence,0};
    int rc=sony_bt_epoch_transport_send(s->fd,&h,&job.raw);if(rc==-EAGAIN)return 0;if(rc)return fail(s,rc);
    ++s->sequence;s->epoch_frames=s->epoch_bytes=0;__atomic_store_n(&s->backend_ready,0,__ATOMIC_RELEASE);__atomic_store_n(&s->end_sent,0,__ATOMIC_RELEASE);__atomic_store_n(&s->epoch,job.epoch,__ATOMIC_RELEASE);__atomic_store_n(&s->phase,SONY_BT_EPOCH_OPENING,__ATOMIC_RELEASE);
    /* Sender may use this metadata only after observing the new head. */
    __atomic_store_n(&s->read,at+1,__ATOMIC_RELEASE);wake(s);return 0;
}
static int send_format(struct sony_bt_epoch_source *s){
    unsigned zero=0;if(!__atomic_compare_exchange_n(&s->consumer_busy,&zero,1,0,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED))return 0;
    int rc=send_format_locked(s);__atomic_store_n(&s->consumer_busy,0,__ATOMIC_RELEASE);return rc;
}
int sony_bt_epoch_source_stop_begin(struct sony_bt_epoch_source *s){
    if(!s)return -EINVAL;__atomic_store_n(&s->stop_requested,1,__ATOMIC_RELEASE);__atomic_fetch_or(&s->volume_gate,2u,__ATOMIC_RELEASE);return 0;
}
int sony_bt_epoch_source_poll(struct sony_bt_epoch_source *s){
    if(!s||s->sealed)return -EINVAL;int error=__atomic_load_n(&s->error,__ATOMIC_ACQUIRE);if(error)return error;
    if(volume_poll(s)||sync_producer(s,0)||send_format(s))return __atomic_load_n(&s->error,__ATOMIC_ACQUIRE);
    struct sony_bt_live_header h;unsigned char scratch[20];int rc,notification=0;
    if(s->has_pending_control){if(!__atomic_load_n(&s->end_sent,__ATOMIC_ACQUIRE))return 0;h=s->pending_control;s->has_pending_control=0;rc=0;}
    else rc=sony_bt_epoch_transport_receive(s->fd,s->parent,s->uid,s->cookie,&h,scratch,sizeof(scratch));
    if(rc&&rc!=-EAGAIN)return fail(s,rc);
    if(!rc){
        unsigned phase=__atomic_load_n(&s->phase,__ATOMIC_ACQUIRE);
        if(h.kind==SONY_BT_EPOCH_STOP){sony_bt_epoch_source_stop_begin(s);if(!s->stop_notified){s->stop_notified=1;notification=2;}}
        else if((h.kind==SONY_BT_EPOCH_READY||h.kind==SONY_BT_EPOCH_FLUSH_READY)&&(phase==SONY_BT_EPOCH_OPENING||phase==SONY_BT_EPOCH_FINISHING)){
            /* Sender can seal an empty END concurrently with this READY.
             * Never overwrite its FINISHING publication with stale ACTIVE. */
            if(phase==SONY_BT_EPOCH_OPENING){unsigned expected=SONY_BT_EPOCH_OPENING;if(!__atomic_compare_exchange_n(&s->phase,&expected,SONY_BT_EPOCH_ACTIVE,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE)&&expected!=SONY_BT_EPOCH_FINISHING)return fail(s,-EPROTO);}
            __atomic_store_n(&s->backend_ready,1,__ATOMIC_RELEASE);if(h.kind==SONY_BT_EPOCH_READY&&sony_bt_epoch_source_ready(s))notification=1;wake(s);
        }
        else if(h.kind==SONY_BT_EPOCH_CLOSED&&phase==SONY_BT_EPOCH_FINISHING){if(!__atomic_load_n(&s->end_sent,__ATOMIC_ACQUIRE)){s->pending_control=h;s->has_pending_control=1;return 0;}__atomic_store_n(&s->phase,SONY_BT_EPOCH_PAUSED,__ATOMIC_RELEASE);}
        else return fail(s,-EPROTO);
        ++s->control_sequence;
    }
    if(sync_producer(s,0)||send_format(s))return __atomic_load_n(&s->error,__ATOMIC_ACQUIRE);
    uint64_t ended=__atomic_load_n(&s->ended_epoch,__ATOMIC_ACQUIRE);if(!notification&&ended!=s->finish_notified_epoch){s->finish_notified_epoch=ended;notification=3;}
    error=__atomic_load_n(&s->error,__ATOMIC_ACQUIRE);return error?error:notification;
}
int sony_bt_epoch_source_finish(struct sony_bt_epoch_source *s){
    if(!s||s->sealed||!__atomic_load_n(&s->stop_requested,__ATOMIC_ACQUIRE))return -EINVAL;
    int error=__atomic_load_n(&s->error,__ATOMIC_ACQUIRE);if(!error){int rc=sony_bt_epoch_source_poll(s);if(rc<0)error=rc;}
    unsigned phase=__atomic_load_n(&s->phase,__ATOMIC_ACQUIRE);
    if(!error&&(phase==SONY_BT_EPOCH_OPENING||phase==SONY_BT_EPOCH_ACTIVE||phase==SONY_BT_EPOCH_FINISHING||__atomic_load_n(&s->producer_open,__ATOMIC_ACQUIRE)||__atomic_load_n(&s->read,__ATOMIC_ACQUIRE)!=__atomic_load_n(&s->write,__ATOMIC_ACQUIRE)))return -EAGAIN;
    if(__atomic_load_n(&s->producer_busy,__ATOMIC_ACQUIRE)||(__atomic_load_n(&s->volume_gate,__ATOMIC_ACQUIRE)&1u))return -EAGAIN;
    if(!error&&__atomic_load_n(&s->volume_read,__ATOMIC_ACQUIRE)!=__atomic_load_n(&s->volume_write,__ATOMIC_ACQUIRE))return -EAGAIN;
    __atomic_store_n(&s->sender_stop,1,__ATOMIC_RELEASE);wake(s);if(!__atomic_load_n(&s->sender_exited,__ATOMIC_ACQUIRE))return -EAGAIN;
    if(!s->sender_joined){int rc=pthread_join(s->thread,NULL);if(rc)return fail(s,-rc);s->sender_joined=1;}
    /* ERROR names only an actually published wire epoch and its sent prefix.
     * Future FORMAT/data remain in the failed receipt, without an invented
     * OPEN or a claim that an unsent accepted prefix was played. */
    if(s->finish_attempted)return s->error?s->error:-EALREADY;
    struct sony_bt_epoch_summary summary={s->frames,s->bytes};unsigned number=error?(unsigned)-error:0;struct sony_bt_live_header h=message(s,error?SONY_BT_EPOCH_ERROR:SONY_BT_EPOCH_QUIET,error?4:16);
    int rc=sony_bt_epoch_transport_send(s->fd,&h,error?(void *)&number:(void *)&summary);if(rc==-EAGAIN)return rc;s->finish_attempted=1;if(rc)return fail(s,rc);++s->sequence;s->sealed=1;__atomic_store_n(&s->phase,SONY_BT_EPOCH_QUIESCED,__ATOMIC_RELEASE);return 0;
}
int sony_bt_epoch_source_state(const struct sony_bt_epoch_source *s,struct sony_bt_epoch_source_state *out){
    if(!s||!out)return -EINVAL;if(!s->sender_joined)return -EAGAIN;
    *out=(struct sony_bt_epoch_source_state){.epochs=s->epoch,.frames=s->frames,.bytes=s->bytes,.offered_frames=s->offered_frames,.offered_bytes=s->offered_bytes,.packets=s->packets,.boundary_packets=__atomic_load_n(&s->boundary_packets,__ATOMIC_ACQUIRE),.boundary_bytes=__atomic_load_n(&s->boundary_bytes,__ATOMIC_ACQUIRE),.volume_offered=s->volume_offered,.volume_sent=s->volume_sent,.phase=s->phase,.queued_peak_frames=s->queued_peak_frames,.sealed=s->sealed,.sender_joined=s->sender_joined,.first_error=__atomic_load_n(&s->error,__ATOMIC_ACQUIRE),.offered_packets=s->offered_packets,.prefetched_packets=s->prefetched_packets,.prefetched_bytes=s->prefetched_bytes,.unready_running_packets=__atomic_load_n(&s->unready_running_packets,__ATOMIC_ACQUIRE),.unready_running_bytes=__atomic_load_n(&s->unready_running_bytes,__ATOMIC_ACQUIRE),.producer_epochs=s->producer_epoch,.future_packets=s->future_packets,.future_bytes=s->future_bytes,.queued_peak_ns=s->queued_peak_ns,.queued_bytes=s->used_bytes,.queued_frames=s->used,.queued_ns=s->used_ns};return 0;
}
int sony_bt_epoch_source_destroy(struct sony_bt_epoch_source **p,unsigned quiesced){
    if(!p||!*p||quiesced!=1)return -EINVAL;struct sony_bt_epoch_source *s=*p;if(!s->sealed||!s->sender_joined||__atomic_load_n(&s->producer_busy,__ATOMIC_ACQUIRE)||(__atomic_load_n(&s->volume_gate,__ATOMIC_ACQUIRE)&1u))return -EBUSY;close(s->event);free(s);*p=NULL;return 0;
}
