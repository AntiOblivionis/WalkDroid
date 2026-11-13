#define _GNU_SOURCE
#include "sony_audio_bt_tx_link_client.h"
#include <errno.h>
#include <poll.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
static int64_t milliseconds(void){struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t))return -1;return (int64_t)t.tv_sec*1000+t.tv_nsec/1000000;}
static int fail(struct wbl_client*c,int rc){c->phase=WBL_FAILED;shutdown(c->fd,SHUT_RDWR);return rc;}
static int ready(struct wbl_client*c){return !c?-EINVAL:c->phase==WBL_FAILED||c->phase==WBL_CLOSED?-ESHUTDOWN:0;}
static int io(struct wbl_client*c,void*buffer,size_t bytes,int output,int64_t deadline){
    uint8_t*p=buffer;
    while(bytes){int64_t left=deadline-milliseconds();if(left<=0)return -ETIMEDOUT;
        struct pollfd f[2]={{c->fd,output?POLLOUT:POLLIN,0},{c->cancel_fd,POLLIN,0}};int n=poll(f,c->cancel_fd>=0?2:1,(int)left);
        if(n<0){if(errno==EINTR)continue;return -errno;}if(f[1].revents)return -ECANCELED;
        if(f[0].revents&(POLLERR|POLLNVAL))return -EPIPE;
        if(!(f[0].revents&(output?(POLLOUT|POLLHUP):(POLLIN|POLLHUP))))continue;
        ssize_t got=output?send(c->fd,p,bytes,MSG_NOSIGNAL|MSG_DONTWAIT):recv(c->fd,p,bytes,MSG_DONTWAIT);
        if(got<0){if(errno==EINTR||errno==EAGAIN)continue;return -errno;}if(!got)return -EPIPE;p+=got;bytes-=got;
    }return 0;
}
int wbl_client_init(struct wbl_client*c,int fd,int cancel_fd,uint64_t nonce){
    if(!c||fd<0)return -EINVAL;
    memset(c,0,sizeof(*c));c->creator=getpid();c->fd=fd;c->cancel_fd=cancel_fd;c->nonce=nonce;c->next_request=1;return 0;
}
static int transact(struct wbl_client*c,struct wbl_request*q,const void*pcm,int timeout){
    int rc=ready(c);if(rc)return rc;
    uint64_t base=q->command==WBL_RESUME?0:c->accepted;
    q->magic=WBL_MAGIC;q->version=3;q->session=c->nonce;q->request=c->next_request;
    q->epoch=c->epoch+(q->command==WBL_RESUME);q->frame_position=base;
    int64_t deadline=milliseconds()+timeout;rc=io(c,q,sizeof(*q),1,deadline);
    if(!rc&&q->payload_bytes)rc=io(c,(void*)pcm,q->payload_bytes,1,deadline);
    struct wbl_reply r={0};if(!rc)rc=io(c,&r,sizeof(r),0,deadline);if(rc)return fail(c,rc);
    c->last_reply=r;if(r.status)return fail(c,r.status<0?r.status:-EIO);
    if(q->command==WBL_RESUME)++c->epoch;
    if(q->command==WBL_RESUME)c->accepted_tail=0;
    uint64_t bytes=(uint64_t)c->accepted_tail+q->payload_bytes;
    c->accepted=base+bytes/4;c->accepted_tail=(unsigned)(bytes%4);c->submitted=r.submitted_frames;
    c->phase=q->command==WBL_CONNECT||q->command==WBL_PAUSE||q->command==WBL_DROP?WBL_CONNECTED:
        q->command==WBL_CLOSE||q->command==WBL_CANCEL?WBL_CLOSED:WBL_STREAMING;
    ++c->next_request;
    if(q->command==WBL_RESUME&&!r.status){c->input_rate=q->rate;c->codec_rate=r.processing.codec_rate;}
    return r.status?fail(c,r.status):0;
}
int wbl_client_connect(struct wbl_client*c,const uint8_t peer[6]){int rc=ready(c);if(rc)return rc;if(c->phase!=WBL_NEW||!peer)return -EINVAL;struct wbl_request q={.command=WBL_CONNECT};memcpy(q.peer,peer,6);return transact(c,&q,NULL,60000);}
int wbl_client_resume(struct wbl_client*c,unsigned rate){int rc=ready(c);if(rc)return rc;if(c->phase!=WBL_CONNECTED)return -EINVAL;struct wbl_request q={.command=WBL_RESUME,.rate=rate,.channels=2,.bits=16};return transact(c,&q,NULL,60000);}
int wbl_client_write(struct wbl_client*c,const void*p,unsigned n){
    int rc=ready(c);if(rc)return rc;if(!p&&n)return -EINVAL;
    const uint8_t *data=p;unsigned done=0;
    do{unsigned part=n-done;if(part>WBT_MAX_PCM)part=WBT_MAX_PCM;
        struct wbl_request q={.command=WBL_WRITE,.payload_bytes=part};
        rc=transact(c,&q,part?data+done:NULL,15000);if(rc)return rc;done+=part;
    }while(done<n);return 0;
}
int wbl_client_pause(struct wbl_client*c){int rc=ready(c);if(rc)return rc;if(c->phase!=WBL_STREAMING)return -EINVAL;struct wbl_request q={.command=WBL_PAUSE};return transact(c,&q,NULL,30000);}
int wbl_client_drop(struct wbl_client*c){int rc=ready(c);if(rc)return rc;if(c->phase!=WBL_STREAMING)return -EINVAL;struct wbl_request q={.command=WBL_DROP};return transact(c,&q,NULL,30000);}
int wbl_client_close(struct wbl_client*c){int rc=ready(c);if(rc)return rc;if(c->phase!=WBL_CONNECTED)return -EINVAL;struct wbl_request q={.command=WBL_CLOSE};return transact(c,&q,NULL,30000);}
int wbl_client_cancel(struct wbl_client*c){int rc=ready(c);if(rc)return rc;struct wbl_request q={.command=WBL_CANCEL};return transact(c,&q,NULL,30000);}
