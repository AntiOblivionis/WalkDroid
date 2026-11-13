#define _GNU_SOURCE
#include "sony_audio_bt_tx_client.h"
#include <errno.h>
#include <poll.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
static int64_t milliseconds(void) {
    struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t))return -1;
    return (int64_t)t.tv_sec*1000+t.tv_nsec/1000000;
}
static int fail(struct wbt_client *c,int rc) {
    c->phase=WBT_FAILED;shutdown(c->fd,SHUT_RDWR);return rc;
}
static int io(struct wbt_client *c,void *buffer,size_t bytes,int output,int64_t deadline) {
    uint8_t *p=buffer;
    while(bytes) {
        int64_t left=deadline-milliseconds();if(left<=0)return -ETIMEDOUT;
        struct pollfd fds[2]={{c->fd,output?POLLOUT:POLLIN,0},{c->cancel_fd,POLLIN,0}};
        int n=poll(fds,c->cancel_fd>=0?2:1,(int)left);
        if(n<0){if(errno==EINTR)continue;return -errno;}
        if(fds[1].revents)return -ECANCELED;
        if(fds[0].revents&(POLLERR|POLLNVAL))return -EPIPE;
        if(!(fds[0].revents&(output?(POLLOUT|POLLHUP):(POLLIN|POLLHUP))))continue;
        ssize_t count=output?send(c->fd,p,bytes,MSG_NOSIGNAL|MSG_DONTWAIT):recv(c->fd,p,bytes,MSG_DONTWAIT);
        if(count<0){if(errno==EINTR||errno==EAGAIN)continue;return -errno;}
        if(!count)return -EPIPE;
        p+=count;bytes-=count;
    }
    return 0;
}
int wbt_client_init(struct wbt_client *c,int fd,int cancel_fd,uint64_t nonce) {
    if(!c||fd<0)return -EINVAL;
    memset(c,0,sizeof(*c));c->fd=fd;c->cancel_fd=cancel_fd;c->nonce=nonce;c->next_request=1;return 0;
}
static int transact(struct wbt_client *c,struct wbt_request *q,const void *pcm,int timeout) {
    if(!c||!q||(q->payload_bytes&&!pcm))return -EINVAL;
    q->magic=WBT_MAGIC;q->version=WBT_VERSION;q->request=c->next_request;
    q->session=c->nonce;q->frame_position=c->accepted;
    int64_t deadline=milliseconds()+timeout;
    int rc=io(c,q,sizeof(*q),1,deadline);
    if(!rc&&q->payload_bytes)rc=io(c,(void*)pcm,q->payload_bytes,1,deadline);
    struct wbt_reply r;memset(&r,0,sizeof(r));if(!rc)rc=io(c,&r,sizeof(r),0,deadline);
    if(rc)return fail(c,rc);
    if(q->command==WBT_OPEN&&!r.status){c->input_rate=q->rate;c->codec_rate=r.processing.codec_rate;c->processing_stats=!!(r.flags&WBT_PROCESSING_STATS);}
    c->last_reply=r;c->accepted=r.accepted_frames;c->submitted=r.submitted_frames;
    c->phase=r.phase;++c->next_request;
    return r.status?fail(c,r.status):0;
}
int wbt_client_open(struct wbt_client *c,const uint8_t peer[6],unsigned rate) {
    if(!c||!peer)return -EINVAL;
    struct wbt_request q={.command=WBT_OPEN,.rate=rate,.channels=2,.bits=16};memcpy(q.peer,peer,6);
    return transact(c,&q,NULL,80000);
}
int wbt_client_write(struct wbt_client *c,const void *pcm,unsigned bytes) {
    if(!c||(!pcm&&bytes))return -EINVAL;
    const uint8_t *data=pcm;unsigned offset=0;
    do{
        unsigned chunk=bytes-offset;if(chunk>WBT_MAX_PCM)chunk=WBT_MAX_PCM;
        struct wbt_request q={.command=WBT_WRITE,.payload_bytes=chunk};
        int rc=transact(c,&q,data?data+offset:NULL,15000);if(rc)return rc;
        offset+=chunk;
    }while(offset<bytes);
    return 0;
}
int wbt_client_finish(struct wbt_client *c) {
    if(!c)return -EINVAL;
    struct wbt_request q={.command=WBT_FINISH};return transact(c,&q,NULL,30000);
}
int wbt_client_cancel(struct wbt_client *c) {
    if(!c)return -EINVAL;
    struct wbt_request q={.command=WBT_CANCEL};return transact(c,&q,NULL,30000);
}
