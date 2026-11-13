#define _POSIX_C_SOURCE 200809L
#include "sony_audio_receiver_client.h"
#include "sony_audio_receiver_pcm.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
struct sony_receiver_client {
    int tx,rx,timeout,dead,faulted,cancelled;
    uint32_t request,session,last_session,sequence,may_flags,padding_ms;
    uint64_t input_min,input_max,output_acked,packets_acked,tail_acked;
    struct wur_state state;
    struct wur_open opening;
    sony_receiver_client_sink sink;
    void *context;
    pthread_mutex_t mutex;
    uint8_t buffer[WUR_MAX_OUTPUT];
};
static int lock(struct sony_receiver_client *c) {
    if(!c)return -EINVAL;return pthread_mutex_trylock(&c->mutex)?-EBUSY:0;
}
static int broken(struct sony_receiver_client *c,int error) {
    c->dead=1;
    int tx=c->tx,rx=c->rx;c->tx=c->rx=-1;
    if(tx>=0)close(tx);
    if(rx>=0&&rx!=tx)close(rx);
    return error;
}
static int64_t now_ms(void) {
    struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t))return -1;
    return (int64_t)t.tv_sec*1000+t.tv_nsec/1000000;
}
static ssize_t pipe_write(int fd,const void *p,size_t n) {
    sigset_t block,old,pending;sigemptyset(&block);sigaddset(&block,SIGPIPE);
    if(pthread_sigmask(SIG_BLOCK,&block,&old)){errno=EIO;return -1;}
    if(sigpending(&pending)){pthread_sigmask(SIG_SETMASK,&old,NULL);errno=EIO;return -1;}
    int prior=sigismember(&pending,SIGPIPE);ssize_t r=write(fd,p,n);int saved=errno;
    if(r<0&&saved==EPIPE&&!prior&&!sigismember(&old,SIGPIPE)){
        struct timespec zero={0,0};while(sigtimedwait(&block,NULL,&zero)<0&&errno==EINTR){}
    }
    pthread_sigmask(SIG_SETMASK,&old,NULL);errno=saved;return r;
}
static int transfer(int fd,void *p,unsigned bytes,int sending,int64_t deadline) {
    unsigned done=0;
    while(done<bytes){
        int64_t now=now_ms();if(now<0)return -EIO;if(now>=deadline)return -ETIMEDOUT;
        struct pollfd f={fd,sending?POLLOUT:POLLIN,0};int r=poll(&f,1,(int)(deadline-now));
        if(r<0&&errno==EINTR)continue;if(!r)return -ETIMEDOUT;if(r<0||(f.revents&(POLLERR|POLLNVAL)))return -EIO;
        unsigned part=bytes-done;if(part>4096)part=4096;
        ssize_t count=sending?pipe_write(fd,(uint8_t *)p+done,part):read(fd,(uint8_t *)p+done,part);
        if(count<0&&(errno==EINTR||errno==EAGAIN||errno==EWOULDBLOCK))continue;
        if(count<=0)return -EIO;done+=(unsigned)count;
    }
    return 0;
}
/* Consume the whole framed body, retaining only what fits the destination. */
static int receive_body(int fd,void *p,unsigned capacity,unsigned bytes,int64_t deadline) {
    unsigned retained=bytes<capacity?bytes:capacity;
    int rc=retained?transfer(fd,p,retained,0,deadline):0;
    unsigned remaining=bytes-retained;unsigned char discarded[4096];
    while(!rc&&remaining){
        unsigned part=remaining<sizeof(discarded)?remaining:sizeof(discarded);
        rc=transfer(fd,discarded,part,0,deadline);remaining-=part;
    }
    return rc;
}
int sony_receiver_client_attach(struct sony_receiver_client **out,int tx,int rx,int timeout) {
    if(!out||*out)return -EINVAL;
    int tf=fcntl(tx,F_GETFL),rf=fcntl(rx,F_GETFL);
    if(tf<0||rf<0)return -errno;
    if(fcntl(tx,F_SETFL,tf|O_NONBLOCK))return -errno;
    if(fcntl(rx,F_SETFL,rf|O_NONBLOCK)){int saved=errno;fcntl(tx,F_SETFL,tf);return -saved;}
    struct sony_receiver_client *c=calloc(1,sizeof(*c));if(!c)return -ENOMEM;
    if(pthread_mutex_init(&c->mutex,NULL)){free(c);return -ENOMEM;}
    c->tx=tx;c->rx=rx;c->timeout=timeout;*out=c;return 0;
}
static uint64_t pair(uint32_t lo,uint32_t hi){return lo|((uint64_t)hi<<32);}
static void reset_session(struct sony_receiver_client *c) {
    c->session=0;c->faulted=c->cancelled=0;c->may_flags=c->padding_ms=0;
    c->input_min=c->input_max=c->output_acked=c->packets_acked=c->tail_acked=0;
    c->sink=NULL;c->context=NULL;memset(&c->state,0,sizeof(c->state));memset(&c->opening,0,sizeof(c->opening));
}
static int exchange(struct sony_receiver_client *c,unsigned command,const void *payload,unsigned bytes,
                    unsigned frames,struct wur_state *out) {
    if(c->dead)return -EPIPE;
    int64_t now=now_ms();if(now<0)return broken(c,-EIO);int64_t deadline=now+c->timeout;
    int service_data=command==WUR_SERVICE_PROCESS||command==WUR_SERVICE_FINAL;
    int finish=command==WUR_SERVICE_FINISH;
    int data=command==WUR_PROCESS||command==WUR_PROCESS_FINAL||service_data;
    uint64_t source=c->input_min,after=source+frames;
    struct wur_header h={WUR_MAGIC,WUR_VERSION,command,++c->request,c->session,0,bytes,0,
        data?(uint32_t)source:0,data?(uint32_t)(source>>32):0,0,0};
    if(data)c->input_max=after;
    if(command==WUR_PROCESS_FINAL||command==WUR_SERVICE_FINAL)c->may_flags|=WUR_FLAG_INPUT_SEALED;
    if(command==WUR_SILENCE)c->may_flags|=WUR_FLAG_INPUT_SEALED|WUR_FLAG_PADDING_USED;
    if(command==WUR_SONY_STOP)c->may_flags|=WUR_FLAG_INPUT_SEALED|WUR_FLAG_STOPPED;
    if(finish){
        c->may_flags|=WUR_FLAG_INPUT_SEALED|WUR_FLAG_STOPPED|
            (*(const unsigned *)payload==WUR_SERVICE_STOP?WUR_FLAG_SERVICE_STOP:WUR_FLAG_SERVICE_DRAIN);
        if(source&&!(c->may_flags&WUR_FLAG_BT_SERVICE)){c->padding_ms=100;c->may_flags|=WUR_FLAG_PADDING_USED;}
    }
    int rc=transfer(c->tx,&h,sizeof(h),1,deadline);if(!rc&&bytes)rc=transfer(c->tx,(void *)payload,bytes,1,deadline);
    if(rc)return broken(c,rc);
    for(;;){
        struct wur_header r;rc=transfer(c->rx,&r,sizeof(r),0,deadline);if(rc)return broken(c,rc);
        if(r.command&WUR_REPLY){
            int opening=command==WUR_OPEN||command==WUR_OPEN_SERVICE||command==WUR_OPEN_BT_SERVICE;
            int shaped=opening||data||finish||command==WUR_SILENCE||command==WUR_SONY_STOP||command==WUR_STATE||command==WUR_VOLUME;
            struct wur_state s={0};
            rc=receive_body(c->rx,&s,shaped?sizeof(s):0,r.payload_bytes,deadline);
            if(rc)return broken(c,rc);
            if(r.status){
                if(data||finish||command==WUR_SILENCE||command==WUR_SONY_STOP||(command==WUR_VOLUME&&r.status==WUR_BACKEND))c->faulted=1;
                if(command==WUR_CLOSE){c->session=0;c->faulted=1;c->cancelled=0;c->sink=NULL;c->context=NULL;}
                return r.status;
            }
            if(shaped){
                if(data)c->input_min=c->input_max=after;
                c->input_min=c->input_max=pair(s.input_processed_low,s.input_processed_high);c->state=s;
                if(s.flags&WUR_FLAG_FAULTED)c->faulted=1;
                *out=s;
            }
            if(opening){c->session=r.session;c->last_session=r.session;}
            else if(command==WUR_CLOSE)reset_session(c);
            else if(command==WUR_SHUTDOWN)broken(c,0);
            return 0;
        }
        if(r.command!=WUR_SAMPLES){
            rc=receive_body(c->rx,NULL,0,r.payload_bytes,deadline);if(rc)return broken(c,rc);
            struct wur_header ack=r;ack.command|=WUR_REPLY;ack.payload_bytes=0;ack.status=WUR_CANCELLED;
            rc=transfer(c->tx,&ack,sizeof(ack),1,deadline);if(rc)return broken(c,rc);
            continue;
        }
        struct sony_receiver_chain_result sample={0};
        unsigned meta=r.payload_bytes<sizeof(sample)?r.payload_bytes:sizeof(sample);
        rc=receive_body(c->rx,&sample,sizeof(sample),meta,deadline);if(rc)return broken(c,rc);
        unsigned available=r.payload_bytes-meta;
        unsigned retained=available<sizeof(c->buffer)?available:sizeof(c->buffer);
        rc=receive_body(c->rx,c->buffer,sizeof(c->buffer),available,deadline);if(rc)return broken(c,rc);
        sample.bytes=retained;
        c->sequence=r.sequence;if(data)c->input_min=c->input_max=after;
        int callback=c->sink?c->sink(c->context,c->buffer,&sample,r.flags):-1;
        struct wur_header ack=r;ack.command|=WUR_REPLY;ack.payload_bytes=0;ack.status=callback?WUR_CANCELLED:0;
        rc=transfer(c->tx,&ack,sizeof(ack),1,deadline);if(rc)return broken(c,rc);
        if(callback){c->cancelled=c->faulted=1;}
        else{c->output_acked+=sample.bytes;++c->packets_acked;if(r.flags)c->tail_acked+=sample.bytes;}
    }
}
static int open_mode(struct sony_receiver_client *c,const struct wur_open *o,sony_receiver_client_sink sink,void *ctx,struct wur_state *out,int service) {
    if(!out||!sink||!o)return -EINVAL;int rc=lock(c);if(rc)return rc;
    if(c->dead)rc=-EPIPE;
    else{
        struct wur_open previous=c->opening;sony_receiver_client_sink old_sink=c->sink;
        void *old_context=c->context;uint32_t old_flags=c->may_flags;
        int old_faulted=c->faulted,old_cancelled=c->cancelled;
        c->opening=*o;c->sink=sink;c->context=ctx;c->faulted=c->cancelled=0;
        c->may_flags=WUR_FLAG_OPEN|(service?WUR_FLAG_SERVICE:0)|(service==2?WUR_FLAG_BT_SERVICE:0);
        rc=exchange(c,service==2?WUR_OPEN_BT_SERVICE:service?WUR_OPEN_SERVICE:WUR_OPEN,o,sizeof(*o),0,out);
        if(rc){c->opening=previous;c->sink=old_sink;c->context=old_context;c->may_flags=old_flags;c->faulted=old_faulted;c->cancelled=old_cancelled;}
    }
    pthread_mutex_unlock(&c->mutex);return rc;
}
int sony_receiver_client_open(struct sony_receiver_client *c,const struct wur_open *o,sony_receiver_client_sink sink,void *ctx,struct wur_state *out){return open_mode(c,o,sink,ctx,out,0);}
int sony_receiver_client_open_service(struct sony_receiver_client *c,const struct wur_open *o,sony_receiver_client_sink sink,void *ctx,struct wur_state *out){return open_mode(c,o,sink,ctx,out,1);}
int sony_receiver_client_open_bt_service(struct sony_receiver_client *c,const struct wur_open *o,sony_receiver_client_sink sink,void *ctx,struct wur_state *out){return open_mode(c,o,sink,ctx,out,2);}
static int process_mode(struct sony_receiver_client *c,const void *data,unsigned frames,int final,struct wur_state *out,int service) {
    if(!data||!out)return -EINVAL;
    int rc=lock(c);if(rc)return rc;
    if(c->dead)rc=-EPIPE;
    else{
        uint64_t width=(uint64_t)c->opening.track[11]*2;
        unsigned command=service?(final?WUR_SERVICE_FINAL:WUR_SERVICE_PROCESS):(final?WUR_PROCESS_FINAL:WUR_PROCESS);
        if(width&&frames>UINT32_MAX/width)rc=-EOVERFLOW;
        else rc=exchange(c,command,data,(unsigned)(frames*width),frames,out);
    }
    pthread_mutex_unlock(&c->mutex);return rc;
}
int sony_receiver_client_process(struct sony_receiver_client *c,const void *data,unsigned frames,int final,struct wur_state *out){return process_mode(c,data,frames,final,out,0);}
int sony_receiver_client_process_service(struct sony_receiver_client *c,const void *data,unsigned frames,int final,struct wur_state *out){return process_mode(c,data,frames,final,out,1);}
int sony_receiver_client_volume(struct sony_receiver_client *c,unsigned expected,unsigned volume,struct wur_state *out){
    if(!out)return -EINVAL;
    int rc=lock(c);if(rc)return rc;
    if(c->dead)rc=-EPIPE;
    else{
        unsigned payload[2]={expected,volume};
        rc=exchange(c,WUR_VOLUME,payload,sizeof(payload),0,out);
        if(!rc)c->opening.settings.volume=(int)volume;
    }
    pthread_mutex_unlock(&c->mutex);return rc;
}
int sony_receiver_client_finish_service(struct sony_receiver_client *c,unsigned mode,struct wur_state *out){
    if(!out)return -EINVAL;int rc=lock(c);if(rc)return rc;
    if(c->dead)rc=-EPIPE;
    else rc=exchange(c,WUR_SERVICE_FINISH,&mode,4,0,out);
    pthread_mutex_unlock(&c->mutex);return rc;
}
int sony_receiver_client_silence(struct sony_receiver_client *c,unsigned ms,struct wur_state *out) {
    if(!out)return -EINVAL;int rc=lock(c);if(rc)return rc;
    if(c->dead)rc=-EPIPE;
    else{c->padding_ms=ms;rc=exchange(c,WUR_SILENCE,&ms,4,0,out);}
    pthread_mutex_unlock(&c->mutex);return rc;
}
static int simple(struct sony_receiver_client *c,unsigned command,struct wur_state *out) {
    if((command==WUR_STATE||command==WUR_SONY_STOP)&&!out)return -EINVAL;int rc=lock(c);if(rc)return rc;
    if(c->dead)rc=-EPIPE;
    else rc=exchange(c,command,NULL,0,0,out);
    pthread_mutex_unlock(&c->mutex);return rc;
}
int sony_receiver_client_ready(struct sony_receiver_client *c){return simple(c,WUR_READY,NULL);}
int sony_receiver_client_shutdown(struct sony_receiver_client *c){return simple(c,WUR_SHUTDOWN,NULL);}
int sony_receiver_client_state(struct sony_receiver_client *c,struct wur_state *out){return simple(c,WUR_STATE,out);}
int sony_receiver_client_stop(struct sony_receiver_client *c,struct wur_state *out){return simple(c,WUR_SONY_STOP,out);}
int sony_receiver_client_close(struct sony_receiver_client *c){return simple(c,WUR_CLOSE,NULL);}
int sony_receiver_client_status(struct sony_receiver_client *c,int *dead,int *faulted) {
    if(!dead||!faulted)return -EINVAL;int rc=lock(c);if(rc)return rc;*dead=c->dead;*faulted=c->faulted;pthread_mutex_unlock(&c->mutex);return 0;
}
int sony_receiver_client_destroy(struct sony_receiver_client *c) {
    if(!c)return 0;int rc=lock(c);if(rc)return rc;broken(c,0);pthread_mutex_unlock(&c->mutex);pthread_mutex_destroy(&c->mutex);free(c);return 0;
}
