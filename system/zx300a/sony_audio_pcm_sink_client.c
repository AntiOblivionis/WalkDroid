#define _POSIX_C_SOURCE 200809L
#include "sony_audio_pcm_sink_client.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
struct sony_pcm_sink_client {int tx,rx,timeout,dead,faulted,sealed,poisoned;uint32_t request,session,last_session,rate;uint64_t accepted;struct wdp_state state;struct sony_pcm_sink_progress progress;int (*poll_fd)(void *,int *);pthread_mutex_t mutex;};
static int lock(struct sony_pcm_sink_client *c){return !c?-EINVAL:pthread_mutex_trylock(&c->mutex)?-EBUSY:0;}
static int broken(struct sony_pcm_sink_client *c,int error){c->dead=1;int tx=c->tx,rx=c->rx;c->tx=c->rx=-1;if(tx>=0)close(tx);if(rx>=0&&rx!=tx)close(rx);return error;}
static int64_t now_ms(void){struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t))return -1;return (int64_t)t.tv_sec*1000+t.tv_nsec/1000000;}
static ssize_t pipe_write(int fd,const void *p,unsigned size){
    sigset_t blocked,old,pending;sigemptyset(&blocked);sigaddset(&blocked,SIGPIPE);
    if(pthread_sigmask(SIG_BLOCK,&blocked,&old)){errno=EIO;return -1;}
    if(sigpending(&pending)){pthread_sigmask(SIG_SETMASK,&old,NULL);errno=EIO;return -1;}
    int prior=sigismember(&pending,SIGPIPE);ssize_t n=write(fd,p,size);int saved=errno;
    if(n<0&&saved==EPIPE&&!prior&&!sigismember(&old,SIGPIPE)){struct timespec zero={0,0};while(sigtimedwait(&blocked,NULL,&zero)<0&&errno==EINTR){}}
    pthread_sigmask(SIG_SETMASK,&old,NULL);errno=saved;return n;
}
static int event_check(struct sony_pcm_sink_client *c,int *out){
    *out=-1;if(!c->poll_fd)return 0;int rc=c->poll_fd(c->progress.context,out);
    return rc>0?-EPROTO:rc;
}
static int terminal_peer_end(int rc){return rc==-EPIPE||rc==-ECHILD;}
static int transfer(struct sony_pcm_sink_client *c,int fd,void *p,unsigned n,int sending,int terminal_reply,int64_t end){
    /* A worker may send SHUTDOWN's WDP reply then close/reap its separate
     * resource channel before we consume that reply. Only this terminal read
     * may stop pumping an ended auxiliary peer. The exact WDP frame and the
     * original absolute deadline remain mandatory. This is not a cleanup
     * receipt: the supervisor still verifies exact child exit and resources. */
    unsigned at=0;int auxiliary_ended=0;while(at<n){
        int auxiliary=-1,rc=0;
        if(!auxiliary_ended){
            rc=event_check(c,&auxiliary);
            if(rc){if(terminal_reply&&terminal_peer_end(rc))auxiliary_ended=1;else return rc;}
            /* Event mode pumps only a ready resource socket. Legacy callers
             * without a watch retain their bounded eager-progress callback.
             * An empty resource poll cannot authorize PCM or resource work. */
            if(!auxiliary_ended&&!c->poll_fd&&c->progress.pump){int progress=c->progress.pump(c->progress.context);
                if(progress){if(terminal_reply&&terminal_peer_end(progress))auxiliary_ended=1;else return progress<0?progress:-EPROTO;}}
        }
        if(auxiliary_ended)auxiliary=-1;
        int64_t now=now_ms();if(now<0)return -EIO;if(now>=end)return -ETIMEDOUT;
        int delay=(int)(end-now);if(c->progress.pump&&!c->poll_fd&&delay>5)delay=5;
        struct pollfd f[2]={{fd,sending?POLLOUT:POLLIN,0},{auxiliary,POLLIN,0}};int count=auxiliary>=0?2:1;rc=poll(f,count,delay);
        if(rc<0&&errno==EINTR)continue;if(!rc){if(c->progress.pump&&!c->poll_fd)continue;return -ETIMEDOUT;}if(rc<0||f[0].revents&(POLLERR|POLLNVAL))return -EIO;
        if(count==2){
            if(f[1].revents&POLLNVAL)return -ESTALE;if(f[1].revents&POLLIN){
                int progress=c->progress.pump(c->progress.context);
                if(progress){if(terminal_reply&&terminal_peer_end(progress)){auxiliary_ended=1;continue;}return progress<0?progress:-EPROTO;}
                continue;
            }
            if(f[1].revents&(POLLERR|POLLHUP)){if(terminal_reply){auxiliary_ended=1;continue;}return -EPIPE;}}
        if(!(f[0].revents&(sending?POLLOUT:POLLIN|POLLHUP)))continue;
        unsigned amount=n-at;if(amount>4096)amount=4096;
        ssize_t got=sending?pipe_write(fd,(char *)p+at,amount):read(fd,(char *)p+at,amount);
        if(got<0&&(errno==EINTR||errno==EAGAIN||errno==EWOULDBLOCK))continue;if(got<=0)return -EIO;at+=(unsigned)got;
    }return 0;
}
int sony_pcm_sink_attach_progress(struct sony_pcm_sink_client **out,int tx,int rx,int timeout,const struct sony_pcm_sink_progress *progress){
    if(!out||*out||(progress&&!progress->pump))return -EINVAL;
    int tf=fcntl(tx,F_GETFL),rf=fcntl(rx,F_GETFL);
    if(tf<0||rf<0||fcntl(tx,F_SETFL,tf|O_NONBLOCK)||fcntl(rx,F_SETFL,rf|O_NONBLOCK))return -errno;
    struct sony_pcm_sink_client *c=calloc(1,sizeof(*c));if(!c)return -ENOMEM;
    if(pthread_mutex_init(&c->mutex,NULL)){free(c);return -ENOMEM;}c->tx=tx;c->rx=rx;c->timeout=timeout;if(progress)c->progress=*progress;*out=c;return 0;
}
int sony_pcm_sink_attach(struct sony_pcm_sink_client **out,int tx,int rx,int timeout){return sony_pcm_sink_attach_progress(out,tx,rx,timeout,NULL);}
int sony_pcm_sink_attach_events(struct sony_pcm_sink_client **out,int tx,int rx,int timeout,const struct sony_pcm_sink_events *events){
    if(!out||*out||!events||!events->pump||!events->poll_fd)return -EINVAL;
    int fd=-1;int rc=events->poll_fd(events->context,&fd);if(rc)return rc<0?rc:-EPROTO;
    struct sony_pcm_sink_progress progress={events->pump,events->context};rc=sony_pcm_sink_attach_progress(out,tx,rx,timeout,&progress);
    if(!rc)(*out)->poll_fd=events->poll_fd;return rc;
}
static int exchange(struct sony_pcm_sink_client *c,unsigned cmd,const void *pcm,unsigned frames,unsigned rate,struct wdp_state *out){
    if(c->dead)return -EPIPE;
    int64_t now=now_ms();if(now<0)return broken(c,-EIO);int64_t end=now+c->timeout;
    struct wdp_header h={WDP_MAGIC,WDP_VERSION,cmd,++c->request,c->session,frames*8,0,rate,frames,0},r;
    int rc=transfer(c,c->tx,&h,sizeof(h),1,0,end);if(!rc&&h.payload_bytes)rc=transfer(c,c->tx,(void *)pcm,h.payload_bytes,1,0,end);
    if(!rc)rc=transfer(c,c->rx,&r,sizeof(r),0,cmd==WDP_SHUTDOWN,end);if(rc)return broken(c,rc);
    int shaped=cmd==WDP_OPEN||cmd==WDP_WRITE||cmd==WDP_DRAIN||cmd==WDP_STATE;
    int released=cmd==WDP_CLOSE&&!r.status;
    int new_session=cmd==WDP_OPEN&&(!r.status||r.session);
    /* The stream must remain parseable, but reply identifiers and state
     * metadata do not authenticate the worker or gate its actual result. */
    if(r.payload_bytes!=(r.status?0u:shaped?sizeof(struct wdp_state):0u))return broken(c,-EPROTO);
    if(cmd==WDP_CLOSE&&released){c->session=0;c->rate=0;c->accepted=0;c->sealed=c->faulted=0;}
    if(cmd==WDP_OPEN&&new_session){c->session=r.session;c->last_session=r.session;c->rate=rate;}
    if(r.status){
        if(r.status==WDP_BACKEND||r.status==WDP_CLEANUP_UNCERTAIN){c->poisoned=c->faulted=1;if(cmd==WDP_DRAIN)c->sealed=1;}
        return r.status;
    }
    if(shaped){
        struct wdp_state s;rc=transfer(c,c->rx,&s,sizeof(s),0,0,end);if(rc)return broken(c,rc);
        uint64_t ack=s.accepted_low|((uint64_t)s.accepted_high<<32);
        c->accepted=ack;c->state=s;if(out)*out=s;if(cmd==WDP_DRAIN)c->sealed=1;
    }
    if(cmd==WDP_SHUTDOWN)broken(c,0);
    return 0;
}
static int command(struct sony_pcm_sink_client *c,unsigned cmd,const int *pcm,unsigned frames,unsigned rate,struct wdp_state *out){
    if((cmd==WDP_OPEN||cmd==WDP_DRAIN||cmd==WDP_STATE)&&!out)return -EINVAL;
    if(cmd==WDP_WRITE&&(!pcm||!frames||frames>WDP_MAX_FRAMES))return -EINVAL;
    int rc=lock(c);if(rc)return rc;
    if(c->dead)rc=-EPIPE;
    else rc=exchange(c,cmd,pcm,frames,cmd==WDP_WRITE?c->rate:rate,out);
    pthread_mutex_unlock(&c->mutex);return rc;
}
int sony_pcm_sink_ready(struct sony_pcm_sink_client *c){return command(c,WDP_READY,NULL,0,0,NULL);}
int sony_pcm_sink_open(struct sony_pcm_sink_client *c,unsigned rate,struct wdp_state *s){return command(c,WDP_OPEN,NULL,0,rate,s);}
int sony_pcm_sink_write(struct sony_pcm_sink_client *c,const int *pcm,unsigned frames){return command(c,WDP_WRITE,pcm,frames,0,NULL);}
int sony_pcm_sink_drain(struct sony_pcm_sink_client *c,struct wdp_state *s){return command(c,WDP_DRAIN,NULL,0,0,s);}
int sony_pcm_sink_state(struct sony_pcm_sink_client *c,struct wdp_state *s){return command(c,WDP_STATE,NULL,0,0,s);}
int sony_pcm_sink_close(struct sony_pcm_sink_client *c){return command(c,WDP_CLOSE,NULL,0,0,NULL);}
int sony_pcm_sink_shutdown(struct sony_pcm_sink_client *c){return command(c,WDP_SHUTDOWN,NULL,0,0,NULL);}
int sony_pcm_sink_destroy(struct sony_pcm_sink_client *c){if(!c)return 0;int rc=lock(c);if(rc)return rc;broken(c,0);pthread_mutex_unlock(&c->mutex);pthread_mutex_destroy(&c->mutex);free(c);return 0;}
