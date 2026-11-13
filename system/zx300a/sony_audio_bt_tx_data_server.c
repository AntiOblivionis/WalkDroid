#define _GNU_SOURCE
#include "sony_audio_bt_tx_data.h"
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
struct wbd_server {
    pid_t creator,peer;
    int fd,phase,owns_epoch;
    uint64_t route,nonce,epoch,native_epoch,accepted,submitted;
    unsigned next_request;
    struct wbl_client *link;
};
int wbd_server_open(struct wbd_server **out,int fd,pid_t pid,uint64_t route,uint64_t nonce,struct wbl_client*link){
    if(!out||*out||!link)return -EINVAL;
    int rc=0;
    struct wbd_server*s=calloc(1,sizeof(*s));if(!s)return -ENOMEM;
    s->fd=fcntl(fd,F_DUPFD_CLOEXEC,3);if(s->fd<0){rc=-errno;free(s);return rc;}
    s->creator=getpid();s->peer=pid;s->route=route;s->nonce=nonce;s->next_request=1;s->link=link;*out=s;return 0;
}
static int discard(struct wbd_server*s){
    if(!s->owns_epoch)return 0;s->owns_epoch=0;
    return wbl_client_drop(s->link);
}
int wbd_server_close(struct wbd_server**out){
    if(!out||!*out)return -EINVAL;struct wbd_server*s=*out;
    int rc=discard(s);close(s->fd);free(s);*out=NULL;return rc;
}
int wbd_server_phase(const struct wbd_server*s){return !s?-EINVAL:s->phase;}
int wbd_server_events(const struct wbd_server*s){return !s?-EINVAL:s->fd;}
static int fail(struct wbd_server*s,int rc){s->phase=WBD_FAILED;discard(s);return rc;}
int wbd_server_step(struct wbd_server*s){
    if(!s)return -EINVAL;
    int rc=0;
    struct wbd_request q={0};uint8_t pcm[WBT_MAX_PCM];
    struct iovec iov[2]={{&q,sizeof(q)},{pcm,sizeof(pcm)}};struct msghdr m={.msg_iov=iov,.msg_iovlen=2};
    ssize_t got=recvmsg(s->fd,&m,MSG_DONTWAIT|MSG_TRUNC);
    if(got<0){if(errno==EAGAIN||errno==EWOULDBLOCK||errno==EINTR)return -EAGAIN;return fail(s,-errno);}
    if(!got)return fail(s,-ECONNRESET);
    size_t available=got>(ssize_t)sizeof(q)?(size_t)got-sizeof(q):0;
    if(available>sizeof(pcm))available=sizeof(pcm);
    if(q.payload_bytes>available)q.payload_bytes=(unsigned)available;
    struct wbd_reply r={.magic=WBD_MAGIC,.version=1,.command=q.command,.request=q.request,.route=s->route,.session=s->nonce};
    if(q.command==WBD_ATTACH)s->phase=WBD_IDLE;
    else if(q.command==WBD_RESUME){
        rc=wbl_client_resume(s->link,q.rate);
        if(!rc){s->epoch=q.epoch;s->native_epoch=s->link->epoch;s->owns_epoch=1;s->phase=WBD_STREAMING;s->accepted=s->submitted=0;}
    }else if(q.command==WBD_DETACH){rc=discard(s);if(!rc)s->phase=WBD_DETACHED;}
    else if(q.command==WBD_WRITE||q.command==WBD_PAUSE||q.command==WBD_DROP){
        rc=q.command==WBD_WRITE?wbl_client_write(s->link,pcm,q.payload_bytes):q.command==WBD_PAUSE?wbl_client_pause(s->link):wbl_client_drop(s->link);
        s->accepted=s->link->accepted;s->submitted=s->link->submitted;
        if(!rc&&q.command!=WBD_WRITE){s->owns_epoch=0;s->phase=WBD_IDLE;}
    }else rc=-ENOSYS;
    if(rc)fail(s,rc);else s->next_request++;
    r.status=rc;r.phase=s->phase;r.epoch=s->epoch;r.accepted_frames=s->accepted;r.submitted_frames=s->submitted;r.flags=WBD_SUBMISSION_ONLY;
    if(s->link->phase==WBL_CONNECTED||s->link->phase==WBL_STREAMING)r.flags|=WBD_RF_CONNECTED;
    if(s->phase==WBD_DETACHED)r.flags|=WBD_ATTACHMENT_CLOSED;
    if(s->epoch&&s->link->epoch==s->native_epoch&&s->link->last_reply.flags&WBL_PROCESSING_STATS){
        r.flags|=WBD_PROCESSING_STATS;r.processing=s->link->last_reply.processing;r.queued_bytes=s->link->last_reply.queued_bytes;r.uncertain_frames=s->link->last_reply.uncertain_frames;
    }
    ssize_t sent=send(s->fd,&r,sizeof(r),MSG_NOSIGNAL|MSG_DONTWAIT);if(sent!=sizeof(r))return fail(s,sent<0?-errno:-EIO);
    return rc;
}
