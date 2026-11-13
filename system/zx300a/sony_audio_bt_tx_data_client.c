/* HAL-side WBD1 attachment client. Detach closes this data attachment only;
 * RF lifetime belongs to the connection service. */
#define _GNU_SOURCE
#include "sony_audio_bt_tx_data.h"
#include <errno.h>
#include <poll.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
static int64_t milliseconds(void){struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t))return -1;return (int64_t)t.tv_sec*1000+t.tv_nsec/1000000;}
static int fail(struct wbd_client*c,int rc){c->phase=WBD_FAILED;shutdown(c->fd,SHUT_RDWR);return rc;}
static int ready(struct wbd_client*c){return c?0:-EINVAL;}
static int wait_ready(struct wbd_client*c,int output,int64_t deadline){
    for(;;){int64_t left=deadline-milliseconds();if(left<=0)return -ETIMEDOUT;
        struct pollfd f[2]={{c->fd,output?POLLOUT:POLLIN,0},{c->cancel_fd,POLLIN,0}};int n=poll(f,c->cancel_fd>=0?2:1,(int)left);
        if(n<0){if(errno==EINTR)continue;return -errno;}if(f[1].revents)return -ECANCELED;
        if(f[0].revents&(POLLERR|POLLNVAL))return -EPIPE;
        if(f[0].revents&(output?(POLLOUT|POLLHUP):(POLLIN|POLLHUP)))return 0;
    }
}
int wbd_client_init(struct wbd_client*c,int fd,int cancel_fd,uint64_t route,pid_t server){
    if(!c)return -EINVAL;(void)server;
    memset(c,0,sizeof(*c));c->creator=getpid();c->fd=fd;c->cancel_fd=cancel_fd;c->route=route;c->next_request=1;return 0;
}
static int transact(struct wbd_client*c,struct wbd_request*q,const void*pcm,int timeout){
    int rc=ready(c);if(rc)return rc;
    uint64_t base=q->command==WBD_RESUME?0:c->accepted;
    q->magic=WBD_MAGIC;q->version=1;q->route=c->route;q->session=c->nonce;q->request=c->next_request;
    q->epoch=c->epoch+(q->command==WBD_RESUME);q->frame_position=base;
    struct iovec vectors[2]={{q,sizeof(*q)},{(void*)pcm,q->payload_bytes}};
    struct msghdr message={.msg_iov=vectors,.msg_iovlen=q->payload_bytes?2:1};
    int64_t deadline=milliseconds()+timeout;rc=wait_ready(c,1,deadline);
    if(!rc){ssize_t sent=sendmsg(c->fd,&message,MSG_NOSIGNAL|MSG_DONTWAIT);if(sent!=(ssize_t)(sizeof(*q)+q->payload_bytes))rc=sent<0?-errno:-EIO;}
    struct wbd_reply r={0};if(!rc)rc=wait_ready(c,0,deadline);
    if(!rc){struct iovec v={&r,sizeof(r)};struct msghdr reply={.msg_iov=&v,.msg_iovlen=1};ssize_t n=recvmsg(c->fd,&reply,MSG_DONTWAIT|MSG_TRUNC);if(n<=0)rc=n<0?-errno:-ECONNRESET;}
    if(rc)return fail(c,rc);
    c->last_reply=r;c->nonce=r.session;c->epoch=r.epoch;c->accepted=r.accepted_frames;c->submitted=r.submitted_frames;c->phase=r.phase;++c->next_request;
    if(q->command==WBD_RESUME&&!r.status){c->input_rate=q->rate;c->codec_rate=r.processing.codec_rate;}
    return r.status?fail(c,r.status):0;
}
int wbd_client_attach(struct wbd_client*c){struct wbd_request q={.command=WBD_ATTACH};return transact(c,&q,NULL,5000);}
int wbd_client_resume(struct wbd_client*c,unsigned rate){struct wbd_request q={.command=WBD_RESUME,.rate=rate,.channels=2,.bits=16};return transact(c,&q,NULL,60000);}
int wbd_client_write(struct wbd_client*c,const void*p,unsigned n){
    if(!p&&n)return -EINVAL;const uint8_t *data=p;unsigned done=0;
    do{unsigned part=n-done;if(part>WBT_MAX_PCM)part=WBT_MAX_PCM;
        struct wbd_request q={.command=WBD_WRITE,.payload_bytes=part};int rc=transact(c,&q,part?data+done:NULL,15000);
        if(rc)return rc;done+=part;
    }while(done<n);return 0;
}
int wbd_client_pause(struct wbd_client*c){struct wbd_request q={.command=WBD_PAUSE};return transact(c,&q,NULL,30000);}
int wbd_client_drop(struct wbd_client*c){struct wbd_request q={.command=WBD_DROP};return transact(c,&q,NULL,30000);}
int wbd_client_detach(struct wbd_client*c){struct wbd_request q={.command=WBD_DETACH};return transact(c,&q,NULL,30000);}
