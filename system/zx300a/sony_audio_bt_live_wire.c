#define _GNU_SOURCE
#include "sony_audio_bt_live_wire.h"
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
static int header(const struct sony_bt_live_header *h){
    return !h?-EINVAL:h->bytes>SONY_BT_LIVE_MAX_BYTES?-EMSGSIZE:0;
}
int sony_bt_live_wire_prepare(int fd,unsigned *send_buffer){
    if(fd<0||!send_buffer)return -EINVAL;
    int size=262144;socklen_t n;
    int flags=fcntl(fd,F_GETFD);if(flags<0||fcntl(fd,F_SETFD,flags|FD_CLOEXEC))return -errno;
    if(setsockopt(fd,SOL_SOCKET,SO_SNDBUF,&size,sizeof(size))||setsockopt(fd,SOL_SOCKET,SO_RCVBUF,&size,sizeof(size)))return -errno;
    n=sizeof(size);if(getsockopt(fd,SOL_SOCKET,SO_SNDBUF,&size,&n))return -errno;*send_buffer=(unsigned)size;return 0;
}
int sony_bt_live_wire_send(int fd,const struct sony_bt_live_header *h,const void *payload){
    int rc=header(h);if(rc)return rc;if(h->bytes&&!payload)return -EINVAL;
    struct iovec vec[2]={{(void *)h,sizeof(*h)},{(void *)payload,h->bytes}};struct msghdr msg={0};msg.msg_iov=vec;msg.msg_iovlen=h->bytes?2:1;
    ssize_t n=sendmsg(fd,&msg,MSG_DONTWAIT|MSG_NOSIGNAL);if(n<0)return -errno;return n==(ssize_t)(sizeof(*h)+h->bytes)?0:-EIO;
}
int sony_bt_live_wire_receive(int fd,pid_t pid,uid_t uid,struct sony_bt_live_header *out,void *scratch,unsigned capacity){
    (void)pid;(void)uid;if(fd<0||!out||!scratch||!capacity)return -EINVAL;
    struct sony_bt_live_header h;union {struct cmsghdr align;unsigned char bytes[CMSG_SPACE(sizeof(struct ucred))+CMSG_SPACE(4*sizeof(int))];} control;
    struct iovec vec[2]={{&h,sizeof(h)},{scratch,capacity}};struct msghdr msg={0};msg.msg_iov=vec;msg.msg_iovlen=2;msg.msg_control=control.bytes;msg.msg_controllen=sizeof(control.bytes);
    ssize_t n=recvmsg(fd,&msg,MSG_DONTWAIT|MSG_CMSG_CLOEXEC);if(n<0)return -errno;if(!n)return -EPIPE;
    for(struct cmsghdr *c=CMSG_FIRSTHDR(&msg);c;c=CMSG_NXTHDR(&msg,c)){
        if(c->cmsg_level==SOL_SOCKET&&c->cmsg_type==SCM_CREDENTIALS&&c->cmsg_len==CMSG_LEN(sizeof(struct ucred))){}
        else{if(c->cmsg_level==SOL_SOCKET&&c->cmsg_type==SCM_RIGHTS&&c->cmsg_len>=CMSG_LEN(0)){unsigned bytes=c->cmsg_len-CMSG_LEN(0);for(unsigned at=0;at+sizeof(int)<=bytes;at+=sizeof(int)){int received;memcpy(&received,(const char *)CMSG_DATA(c)+at,sizeof(received));close(received);}}}
    }
    if(msg.msg_flags&MSG_TRUNC)return -EMSGSIZE;
    if(n<(ssize_t)sizeof(h)||header(&h)||h.bytes>capacity||h.bytes>(size_t)n-sizeof(h))return -EPROTO;
    *out=h;return 0;
}
int sony_bt_live_cursor_init(struct sony_bt_live_cursor *out,uint64_t cookie,uint64_t source){
    if(!out)return -EINVAL;
    struct sony_bt_live_cursor v={0};v.cookie=cookie;v.source=source;*out=v;return 0;
}
int sony_bt_live_volume_init(struct sony_bt_live_volume_cursor *out,uint64_t cookie){
    if(!out)return -EINVAL;struct sony_bt_live_volume_cursor v={cookie,0,0};*out=v;return 0;
}
int sony_bt_live_volume_next(const struct sony_bt_live_volume_cursor *old,const struct sony_bt_live_header *h,
    const void *payload,struct sony_bt_live_volume_cursor *next,int *step){
    if(!old||!next||!step||!payload||header(h))return -EINVAL;
    if(h->bytes<sizeof(int32_t))return -EPROTO;
    int32_t value;memcpy(&value,payload,4);
    struct sony_bt_live_volume_cursor v=*old;++v.next_sequence;*next=v;*step=(int)value;return 0;
}
int sony_bt_live_next(const struct sony_bt_live_cursor *old,const struct sony_bt_live_header *h,const void *payload,struct sony_bt_live_cursor *next){
    if(!old||!next||header(h)||(h->bytes&&!payload))return -EINVAL;
    struct sony_bt_live_cursor v=*old;
    if(h->kind==SONY_BT_LIVE_LISTENING){
        v.listening=1;
    }else if(h->kind==SONY_BT_LIVE_FORMAT){
        if(h->bytes<sizeof(v.raw))return -EPROTO;
        memcpy(&v.raw,payload,20);
        int rc=sony_bt_middleware_format(&v.raw,&v.format);if(rc)return rc;
        v.frame_bytes=v.format.pcm.codec==SONY_BT_AV_LDAC?8:4;v.format_seen=1;
    }else if(h->kind==SONY_BT_LIVE_PCM){
        if(!v.frame_bytes)return -EPIPE;
        unsigned frames=h->bytes/v.frame_bytes;
        if(v.frames>UINT64_MAX-frames)return -EOVERFLOW;
        v.frames+=frames;
    }else if(h->kind==SONY_BT_LIVE_QUIET){
        v.sealed=1;
    }else if(h->kind==SONY_BT_LIVE_ERROR){
        if(h->bytes<sizeof(unsigned))return -EPROTO;unsigned error;memcpy(&error,payload,4);v.source_error=error;v.sealed=1;
    }else return -EPROTO;
    ++v.next_sequence;*next=v;return 0;
}
