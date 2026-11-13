#define _POSIX_C_SOURCE 200809L
#include "sony_audio_dsf_client.h"
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
struct sony_dsf_client {
    int tx,rx,timeout,dead,faulted,eos_acked,cancelled;
    uint32_t request,session,last_session,sequence;
    unsigned terminal,latency,tail_started,tail_packets;
    uint64_t file_bytes,output_acked,packets_acked;
    struct sony_dsf_client_callbacks cb;
    struct wdf_state state;
    struct wdf_controls controls;
    struct sony_dsd_live_controls target;
    struct sony_chain_settings opening_settings;
    struct sony_dsd_controls opening_dsd;
    unsigned control_base,control_min_packets;
    uint64_t control_packet_base;
    pthread_mutex_t mutex;
    uint8_t buffer[WDF_MAX_SAMPLES];
};
static int lock(struct sony_dsf_client *c) {
    if(!c)return -EINVAL;
    return pthread_mutex_trylock(&c->mutex)?-EBUSY:0;
}
static int break_connection(struct sony_dsf_client *c,int error) {
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
static ssize_t pipe_write(int fd,const void *data,size_t size) {
    sigset_t blocked,old,pending;
    sigemptyset(&blocked);sigaddset(&blocked,SIGPIPE);
    if(pthread_sigmask(SIG_BLOCK,&blocked,&old)){errno=EIO;return -1;}
    if(sigpending(&pending)){pthread_sigmask(SIG_SETMASK,&old,NULL);errno=EIO;return -1;}
    int prior=sigismember(&pending,SIGPIPE);ssize_t n=write(fd,data,size);int saved=errno;
    if(n<0&&saved==EPIPE&&!prior&&!sigismember(&old,SIGPIPE)) {
        struct timespec zero={0,0};while(sigtimedwait(&blocked,NULL,&zero)<0&&errno==EINTR){}
    }
    pthread_sigmask(SIG_SETMASK,&old,NULL);errno=saved;return n;
}
static int transfer(int fd,void *data,unsigned size,int sending,int64_t deadline) {
    unsigned done=0;
    while(done<size) {
        int64_t now=now_ms();if(now<0)return -EIO;if(now>=deadline)return -ETIMEDOUT;
        struct pollfd p={fd,sending?POLLOUT:POLLIN,0};int ready=poll(&p,1,(int)(deadline-now));
        if(ready<0&&errno==EINTR)continue;
        if(!ready)return -ETIMEDOUT;
        if(ready<0||p.revents&(POLLERR|POLLNVAL))return -EIO;
        unsigned n=size-done;if(n>4096)n=4096;
        ssize_t count=sending?pipe_write(fd,(uint8_t *)data+done,n):read(fd,(uint8_t *)data+done,n);
        if(count<0&&(errno==EINTR||errno==EAGAIN||errno==EWOULDBLOCK))continue;
        if(count<=0)return -EIO;done+=(unsigned)count;
    }
    return 0;
}
int sony_dsf_client_attach(struct sony_dsf_client **out,int tx,int rx,int timeout) {
    if(!out)return -EINVAL;
    int tf=fcntl(tx,F_GETFL),rf=fcntl(rx,F_GETFL);
    if(tf<0||rf<0||fcntl(tx,F_SETFL,tf|O_NONBLOCK)||fcntl(rx,F_SETFL,rf|O_NONBLOCK))return -errno;
    struct sony_dsf_client *c=calloc(1,sizeof(*c));if(!c)return -ENOMEM;
    if(pthread_mutex_init(&c->mutex,NULL)){free(c);return -ENOMEM;}
    c->tx=tx;c->rx=rx;c->timeout=timeout;*out=c;return 0;
}
static uint64_t pair(uint32_t lo,uint32_t hi){return lo|((uint64_t)hi<<32);}
static int discard(int fd,unsigned bytes,int64_t deadline) {
    unsigned char buffer[1024];
    while(bytes){unsigned n=bytes<sizeof(buffer)?bytes:sizeof(buffer);int rc=transfer(fd,buffer,n,0,deadline);if(rc)return rc;bytes-=n;}
    return 0;
}
static int exchange(struct sony_dsf_client *c,unsigned command,void *payload,unsigned bytes,struct wdf_state *out) {
    if(c->dead)return -EPIPE;
    int64_t now=now_ms();if(now<0)return break_connection(c,-EIO);int64_t deadline=now+c->timeout;
    struct wdf_header h={WDF_MAGIC,WDF_VERSION,command,++c->request,c->session,0,bytes,0,0,0,0,0};
    int rc=transfer(c->tx,&h,sizeof(h),1,deadline);
    if(!rc&&bytes)rc=transfer(c->tx,payload,bytes,1,deadline);
    if(rc)return break_connection(c,rc);
    int opening=command==WDF_OPEN||command==WDF_OPEN_TERMINAL;
    for(;;) {
        struct wdf_header r;rc=transfer(c->rx,&r,sizeof(r),0,deadline);
        if(rc)return break_connection(c,rc);
        if(r.command&WDF_REPLY) {
            int shaped=(opening||command==WDF_STEP||command==WDF_STATE);
            struct wdf_state s={0};unsigned copied=!r.status&&shaped&&r.payload_bytes<sizeof(s)?r.payload_bytes:!r.status&&shaped?sizeof(s):0;
            if(copied)rc=transfer(c->rx,&s,copied,0,deadline);
            if(!rc)rc=discard(c->rx,r.payload_bytes-copied,deadline);
            if(rc)return break_connection(c,rc);
            if(opening)c->last_session=r.session;
            if(command==WDF_CLOSE){c->session=0;c->faulted=c->cancelled=c->eos_acked=0;c->terminal=c->tail_started=c->tail_packets=0;memset(&c->cb,0,sizeof(c->cb));memset(&c->controls,0,sizeof(c->controls));}
            if(r.status) {
                if(command==WDF_STEP)c->faulted=1;
                /* A shaped failed OPEN reply has session=0: the worker already
                 * released the attempted session. Keep last_session/sequence to
                 * reject stale traffic, but retire its callbacks/cancel flags so
                 * idle READY or SHUTDOWN is not misread as a missing CANCEL. */
                if(opening){c->faulted=c->cancelled=c->eos_acked=0;c->terminal=c->tail_started=c->tail_packets=0;memset(&c->cb,0,sizeof(c->cb));}
                return r.status;
            }
            if(shaped) {
                c->state=s;if(out)*out=s;
            }
            if(command!=WDF_CLOSE)c->session=r.session;return 0;
        }
        c->sequence=r.sequence;int callback=0;unsigned response=0;
        if(r.command==WDF_READ) {
            uint64_t offset=pair(r.arg0,r.arg1);
            rc=discard(c->rx,r.payload_bytes,deadline);if(rc)return break_connection(c,rc);
            unsigned requested=r.flags<sizeof(c->buffer)?r.flags:sizeof(c->buffer);size_t actual=0;
            callback=c->cb.read_at?c->cb.read_at(c->cb.context,offset,c->buffer,requested,&actual):-ENOSYS;
            if(actual>requested)actual=requested;if(!callback)response=(unsigned)actual;
        }else if(r.command==WDF_SAMPLES) {
            unsigned copied=r.payload_bytes<sizeof(c->buffer)?r.payload_bytes:sizeof(c->buffer);
            rc=transfer(c->rx,c->buffer,copied,0,deadline);if(!rc)rc=discard(c->rx,r.payload_bytes-copied,deadline);if(rc)return break_connection(c,rc);
            if(r.flags)c->tail_started=1;
            struct sony_dsd_result sample={copied,r.arg0,r.arg1};callback=c->cb.samples?c->cb.samples(c->cb.context,c->buffer,&sample):-ENOSYS;
        }else if(r.command==WDF_EOS) {
            rc=discard(c->rx,r.payload_bytes,deadline);if(rc)return break_connection(c,rc);
            callback=c->cb.input_eos?c->cb.input_eos(c->cb.context):-ENOSYS;
        }else{rc=discard(c->rx,r.payload_bytes,deadline);if(rc)return break_connection(c,rc);callback=-ENOSYS;}
        struct wdf_header ack=r;ack.command|=WDF_REPLY;ack.payload_bytes=response;ack.status=callback?WDF_CANCELLED:0;
        rc=transfer(c->tx,&ack,sizeof(ack),1,deadline);
        if(!rc&&response)rc=transfer(c->tx,c->buffer,response,1,deadline);
        if(rc)return break_connection(c,rc);
        if(callback){c->cancelled=1;c->faulted=1;}
        else if(r.command==WDF_SAMPLES){c->state.output_rate=r.arg0;c->state.output_format=r.arg1;c->output_acked+=r.payload_bytes<sizeof(c->buffer)?r.payload_bytes:sizeof(c->buffer);++c->packets_acked;if(r.flags)++c->tail_packets;}
        else if(r.command==WDF_EOS)c->eos_acked=1;
    }
}
static int open_session(struct sony_dsf_client *c,uint64_t file_bytes,const struct sony_chain_settings *settings,
    const struct sony_dsd_controls *dsd,const struct sony_output_controls *output,
    unsigned terminal,unsigned latency,const struct sony_dsf_client_callbacks *cb,struct wdf_state *state_out) {
    if(!settings||!dsd||!output||!cb||!state_out)return -EINVAL;
    int rc=lock(c);if(rc)return rc;
    c->cb=*cb;c->file_bytes=file_bytes;c->output_acked=c->packets_acked=0;c->eos_acked=c->cancelled=c->faulted=0;memset(&c->state,0,sizeof(c->state));
    memset(&c->controls,0,sizeof(c->controls));c->control_base=c->control_min_packets=0;c->control_packet_base=0;
    c->opening_settings=*settings;c->opening_dsd=*dsd;
    c->terminal=terminal;c->latency=latency;c->tail_started=c->tail_packets=0;
    uint32_t payload[WDF_OPEN_TERMINAL_WORDS];memcpy(payload,settings,68);memcpy(payload+17,dsd,12);memcpy(payload+20,output,20);
    payload[25]=(uint32_t)file_bytes;payload[26]=(uint32_t)(file_bytes>>32);payload[27]=latency;
    rc=exchange(c,terminal?WDF_OPEN_TERMINAL:WDF_OPEN,payload,4*(terminal?WDF_OPEN_TERMINAL_WORDS:WDF_OPEN_WORDS),state_out);
    pthread_mutex_unlock(&c->mutex);return rc;
}
int sony_dsf_client_open(struct sony_dsf_client *c,uint64_t bytes,const struct sony_chain_settings *s,
    const struct sony_dsd_controls *d,const struct sony_output_controls *o,const struct sony_dsf_client_callbacks *cb,struct wdf_state *st) {
    return open_session(c,bytes,s,d,o,0,0,cb,st);
}
int sony_dsf_client_open_terminal(struct sony_dsf_client *c,uint64_t bytes,const struct sony_chain_settings *s,
    const struct sony_dsd_controls *d,const struct sony_output_controls *o,unsigned latency,const struct sony_dsf_client_callbacks *cb,struct wdf_state *st) {
    return open_session(c,bytes,s,d,o,1,latency,cb,st);
}
static int simple(struct sony_dsf_client *c,unsigned command,struct wdf_state *out) {
    if((command==WDF_STATE||command==WDF_STEP)&&!out)return -EINVAL;
    int rc=lock(c);if(rc)return rc;
    if(c->dead)rc=-EPIPE;
    else rc=exchange(c,command,NULL,0,out);
    if(!rc&&command==WDF_SHUTDOWN)break_connection(c,0);
    pthread_mutex_unlock(&c->mutex);return rc;
}
int sony_dsf_client_step(struct sony_dsf_client *c,struct wdf_state *s){return simple(c,WDF_STEP,s);}
int sony_dsf_client_state(struct sony_dsf_client *c,struct wdf_state *s){return simple(c,WDF_STATE,s);}
static int control_receipt(struct sony_dsf_client *c,const struct wdf_controls *r) {
    (void)c;(void)r;return 0;
}
/* Control replies cannot contain events. An uncertain request seals transport;
 * there is no automatic retry on the same or a replacement worker. */
static int control_exchange(struct sony_dsf_client *c,unsigned command,const void *payload,unsigned bytes,
                            struct wdf_controls *out,const struct sony_dsd_live_controls *target) {
    int64_t now=now_ms();if(now<0)return break_connection(c,-EIO);int64_t deadline=now+c->timeout;
    struct wdf_header h={WDF_MAGIC,WDF_VERSION,command,++c->request,c->session,0,bytes,0,0,0,0,0},r;
    int rc=transfer(c->tx,&h,sizeof(h),1,deadline);
    if(!rc&&bytes)rc=transfer(c->tx,(void *)payload,bytes,1,deadline);
    if(!rc)rc=transfer(c->rx,&r,sizeof(r),0,deadline);
    if(rc)return break_connection(c,rc);
    struct wdf_controls result={0};
    unsigned copied=!r.status&&r.payload_bytes<sizeof(result)?r.payload_bytes:!r.status?sizeof(result):0;
    if(copied)rc=transfer(c->rx,&result,copied,0,deadline);
    unsigned remaining=r.payload_bytes-copied;
    while(!rc&&remaining){
        unsigned char discard[1024];unsigned count=remaining<sizeof(discard)?remaining:sizeof(discard);
        rc=transfer(c->rx,discard,count,0,deadline);remaining-=count;
    }
    if(rc)return break_connection(c,rc);
    if(r.status){if(r.status==WDF_BACKEND)c->faulted=1;return r.status;}
    if(command==WDF_CONTROLS_STAGE&&memcmp(target,&c->controls.controls,sizeof(*target))) {
        c->control_base=c->controls.revision;c->target=*target;
        c->control_min_packets=target->dsd.filter!=c->controls.controls.dsd.filter||target->dsd.gain!=c->controls.controls.dsd.gain?2:1;
        c->control_packet_base=c->packets_acked-c->tail_packets;
    }else if(control_receipt(c,&result))return break_connection(c,-EPROTO);
    c->controls=result;*out=result;
    return command==WDF_CONTROLS_COMMIT&&result.pending?1:0;
}
static int control_ready(struct sony_dsf_client *c) {
    if(c->dead||c->faulted||c->cancelled)return -EPIPE;
    return 0;
}
int sony_dsf_client_get_controls(struct sony_dsf_client *c,struct wdf_controls *out) {
    if(!out)return -EINVAL;int rc=lock(c);if(rc)return rc;
    rc=control_ready(c);if(!rc)rc=control_exchange(c,WDF_CONTROLS_GET,NULL,0,out,NULL);
    pthread_mutex_unlock(&c->mutex);return rc;
}
int sony_dsf_client_stage_controls(struct sony_dsf_client *c,unsigned revision,const struct sony_dsd_live_controls *target,struct wdf_controls *out) {
    if(!target||!out)return -EINVAL;int rc=lock(c);if(rc)return rc;
    rc=control_ready(c);
    if(!rc&&c->eos_acked)rc=-EINVAL;
    if(!rc&&c->controls.pending)rc=WDF_BUSY;
    struct wdf_controls_stage stage={revision,*target};
    if(!rc) {
        if(stage.controls.selector==2)stage.controls.tone=c->controls.controls.tone;
        rc=control_exchange(c,WDF_CONTROLS_STAGE,&stage,sizeof(stage),out,&stage.controls);
    }
    pthread_mutex_unlock(&c->mutex);return rc;
}
int sony_dsf_client_commit_controls(struct sony_dsf_client *c,unsigned revision,unsigned request,struct wdf_controls *out) {
    if(!out)return -EINVAL;int rc=lock(c);if(rc)return rc;
    rc=control_ready(c);
    struct wdf_controls_commit commit={revision,request};
    if(!rc)rc=control_exchange(c,WDF_CONTROLS_COMMIT,&commit,sizeof(commit),out,NULL);
    pthread_mutex_unlock(&c->mutex);return rc;
}
int sony_dsf_client_close_session(struct sony_dsf_client *c){return simple(c,WDF_CLOSE,NULL);}
int sony_dsf_client_shutdown(struct sony_dsf_client *c){return simple(c,WDF_SHUTDOWN,NULL);}
int sony_dsf_client_ready(struct sony_dsf_client *c){return simple(c,WDF_READY,NULL);}
int sony_dsf_client_destroy(struct sony_dsf_client *c) {
    if(!c)return 0;int rc=lock(c);if(rc)return rc;
    break_connection(c,0);pthread_mutex_unlock(&c->mutex);pthread_mutex_destroy(&c->mutex);free(c);return 0;
}
