/* Native DSD bytes only; bounded WDN1 waits service the separate power RPC. */
#define _POSIX_C_SOURCE 200809L
#include "sony_audio_dsd_sink_client.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
struct sony_dsd_sink_client {int tx,rx,timeout,dead,faulted,sealed,poisoned;uint32_t request,session,last_session,bit_rate;int opened_attempted;struct sony_dsd_sink_progress progress;uint64_t accepted;struct wdn_state state;pthread_mutex_t mutex;};
static int lock(struct sony_dsd_sink_client *c){return !c?-EINVAL:pthread_mutex_trylock(&c->mutex)?-EBUSY:0;}
static int broken(struct sony_dsd_sink_client *c,int error){c->dead=1;int tx=c->tx,rx=c->rx;c->tx=c->rx=-1;if(tx>=0)close(tx);if(rx>=0&&rx!=tx)close(rx);return error;}
static int64_t now_ms(void){struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t))return -1;return (int64_t)t.tv_sec*1000+t.tv_nsec/1000000;}
static ssize_t pipe_write(int fd,const void *p,unsigned size){
    sigset_t blocked,old,pending;sigemptyset(&blocked);sigaddset(&blocked,SIGPIPE);
    if(pthread_sigmask(SIG_BLOCK,&blocked,&old)){errno=EIO;return -1;}
    if(sigpending(&pending)){pthread_sigmask(SIG_SETMASK,&old,NULL);errno=EIO;return -1;}
    int prior=sigismember(&pending,SIGPIPE);ssize_t n=write(fd,p,size);int saved=errno;
    if(n<0&&saved==EPIPE&&!prior&&!sigismember(&old,SIGPIPE)){struct timespec zero={0,0};while(sigtimedwait(&blocked,NULL,&zero)<0&&errno==EINTR){}}
    pthread_sigmask(SIG_SETMASK,&old,NULL);errno=saved;return n;
}
static int transfer(struct sony_dsd_sink_client *c,int fd,void *p,unsigned n,int sending,int64_t end){
    unsigned at=0;while(at<n){int progress=c->progress.pump(c->progress.context);if(progress)return progress<0?progress:-EPROTO;int64_t now=now_ms();if(now<0)return -EIO;if(now>=end)return -ETIMEDOUT;
        struct pollfd f={fd,sending?POLLOUT:POLLIN,0};int delay=(int)(end-now);if(delay>5)delay=5;int rc=poll(&f,1,delay);
        if(rc<0&&errno==EINTR)continue;if(!rc)continue;if(rc<0||f.revents&(POLLERR|POLLNVAL))return -EIO;
        unsigned amount=n-at;if(amount>4096)amount=4096;
        ssize_t got=sending?pipe_write(fd,(char *)p+at,amount):read(fd,(char *)p+at,amount);
        if(got<0&&(errno==EINTR||errno==EAGAIN||errno==EWOULDBLOCK))continue;if(got<=0)return -EIO;at+=(unsigned)got;
    }return 0;
}
static int no_progress(void *context){(void)context;return 0;}
int sony_dsd_sink_attach(struct sony_dsd_sink_client **out,int tx,int rx,int timeout,const struct sony_dsd_sink_progress *progress){
    if(!out||*out)return -EINVAL;
    int tf=fcntl(tx,F_GETFL),rf=fcntl(rx,F_GETFL);
    if(tf<0||rf<0||fcntl(tx,F_SETFL,tf|O_NONBLOCK)||fcntl(rx,F_SETFL,rf|O_NONBLOCK))return -errno;
    struct sony_dsd_sink_client *c=calloc(1,sizeof(*c));if(!c)return -ENOMEM;
    if(pthread_mutex_init(&c->mutex,NULL)){free(c);return -ENOMEM;}c->tx=tx;c->rx=rx;c->timeout=timeout;
    if(progress)c->progress=*progress;if(!c->progress.pump)c->progress.pump=no_progress;*out=c;return 0;
}
static int exchange(struct sony_dsd_sink_client *c,unsigned cmd,const void *data,unsigned bytes,unsigned bit_rate,struct wdn_state *out){
    if(c->dead)return -EPIPE;
    int64_t now=now_ms();if(now<0)return broken(c,-EIO);int64_t end=now+c->timeout;
    struct wdn_header h={WDN_MAGIC,WDN_VERSION,cmd,++c->request,c->session,bytes,0,bit_rate,bytes,0},r;
    int rc=transfer(c,c->tx,&h,sizeof(h),1,end);if(!rc&&h.payload_bytes)rc=transfer(c,c->tx,(void *)data,h.payload_bytes,1,end);
    if(!rc)rc=transfer(c,c->rx,&r,sizeof(r),0,end);if(rc)return broken(c,rc);
    int shaped=cmd==WDN_OPEN||cmd==WDN_WRITE||cmd==WDN_DRAIN||cmd==WDN_STATE;
    struct wdn_state s={0};unsigned copied=!r.status&&shaped&&r.payload_bytes<sizeof(s)?r.payload_bytes:!r.status&&shaped?sizeof(s):0;
    if(copied)rc=transfer(c,c->rx,&s,copied,0,end);
    unsigned remaining=r.payload_bytes-copied;
    while(!rc&&remaining){unsigned char discard[1024];unsigned n=remaining<sizeof(discard)?remaining:sizeof(discard);rc=transfer(c,c->rx,discard,n,0,end);remaining-=n;}
    if(rc)return broken(c,rc);
    if(cmd==WDN_CLOSE&&!r.status){c->session=0;c->bit_rate=0;c->accepted=0;c->sealed=c->faulted=0;}
    if(cmd==WDN_OPEN){c->session=r.session;c->last_session=r.session;c->bit_rate=bit_rate;}
    if(r.status){
        if(r.status==WDN_BACKEND||r.status==WDN_CLEANUP_UNCERTAIN){c->poisoned=c->faulted=1;if(cmd==WDN_DRAIN)c->sealed=1;}
        return r.status;
    }
    if(shaped){
        uint64_t ack=s.accepted_low|((uint64_t)s.accepted_high<<32);
        c->accepted=ack;c->state=s;if(out)*out=s;if(cmd==WDN_DRAIN)c->sealed=1;
    }
    if(cmd==WDN_SHUTDOWN)broken(c,0);
    return 0;
}
static int command(struct sony_dsd_sink_client *c,unsigned cmd,const void *data,unsigned bytes,unsigned bit_rate,struct wdn_state *out){
    if(cmd==WDN_WRITE&&bytes&&!data)return -EINVAL;
    int rc=lock(c);if(rc)return rc;
    if(cmd==WDN_OPEN)c->opened_attempted=1;
    rc=exchange(c,cmd,data,bytes,cmd==WDN_WRITE?c->bit_rate:bit_rate,out);
    pthread_mutex_unlock(&c->mutex);return rc;
}
int sony_dsd_sink_ready(struct sony_dsd_sink_client *c){return command(c,WDN_READY,NULL,0,0,NULL);}
int sony_dsd_sink_open(struct sony_dsd_sink_client *c,unsigned bit_rate,struct wdn_state *s){return command(c,WDN_OPEN,NULL,0,bit_rate,s);}
int sony_dsd_sink_write(struct sony_dsd_sink_client *c,const void *data,unsigned bytes){
    if(!data&&bytes)return -EINVAL;const unsigned char *p=data;unsigned at=0;
    do{unsigned n=bytes-at;if(n>WDN_MAX_BYTES)n=WDN_MAX_BYTES;int rc=command(c,WDN_WRITE,n?p+at:NULL,n,0,NULL);if(rc)return rc;at+=n;}while(at<bytes);return 0;
}
int sony_dsd_sink_drain(struct sony_dsd_sink_client *c,struct wdn_state *s){return command(c,WDN_DRAIN,NULL,0,0,s);}
int sony_dsd_sink_state(struct sony_dsd_sink_client *c,struct wdn_state *s){return command(c,WDN_STATE,NULL,0,0,s);}
int sony_dsd_sink_close(struct sony_dsd_sink_client *c){return command(c,WDN_CLOSE,NULL,0,0,NULL);}
int sony_dsd_sink_shutdown(struct sony_dsd_sink_client *c){return command(c,WDN_SHUTDOWN,NULL,0,0,NULL);}
int sony_dsd_sink_destroy(struct sony_dsd_sink_client *c){if(!c)return 0;int rc=lock(c);if(rc)return rc;broken(c,0);pthread_mutex_unlock(&c->mutex);pthread_mutex_destroy(&c->mutex);free(c);return 0;}
