#define _GNU_SOURCE
#include "sony_audio_bt_epoch_transport.h"
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
static int frame(const struct sony_bt_live_header *h){
    return !h?-EINVAL:h->bytes>SONY_BT_LIVE_MAX_BYTES?-EMSGSIZE:0;
}
int sony_bt_epoch_transport_prepare(int fd,unsigned *send_buffer){return sony_bt_live_wire_prepare(fd,send_buffer);}
int sony_bt_epoch_transport_send(int fd,const struct sony_bt_live_header *h,const void *payload){
    if(fd<0)return -EINVAL;int rc=frame(h);if(rc)return rc;if(h->bytes&&!payload)return -EINVAL;
    struct iovec vec[2]={{(void *)h,sizeof(*h)},{(void *)payload,h->bytes}};struct msghdr m={0};m.msg_iov=vec;m.msg_iovlen=h->bytes?2:1;
    ssize_t n=sendmsg(fd,&m,MSG_DONTWAIT|MSG_NOSIGNAL);if(n<0)return -errno;return n==(ssize_t)(sizeof(*h)+h->bytes)?0:-EIO;
}
int sony_bt_epoch_transport_receive(int fd,pid_t pid,uid_t uid,uint64_t cookie,
    struct sony_bt_live_header *out,void *scratch,unsigned capacity){
    (void)pid;(void)uid;(void)cookie;if(fd<0||!out||!scratch||!capacity)return -EINVAL;
    /* The saved3.10.26 kernel include/net/scm.h fixes SCM_MAX_FD at253.
     * Receive the entire bounded rights set and close it ourselves. Extra
     * headroom also covers host/guest cmsghdr alignment when exercising this
     * exact ARM code under qemu; no accepted descriptors or truncation bypass.
     * Stack use remains about1.2KiB; this is never an RF/PCM callback. */
    struct sony_bt_live_header h;union {struct cmsghdr align;unsigned char bytes[CMSG_SPACE(sizeof(struct ucred))+CMSG_SPACE(253*sizeof(int))+128];} ancillary;
    struct iovec vec[2]={{&h,sizeof(h)},{scratch,capacity}};struct msghdr m={0};m.msg_iov=vec;m.msg_iovlen=2;m.msg_control=ancillary.bytes;m.msg_controllen=sizeof(ancillary.bytes);
    ssize_t n=recvmsg(fd,&m,MSG_DONTWAIT|MSG_CMSG_CLOEXEC);if(n<0)return -errno;if(!n)return -EPIPE;
    for(struct cmsghdr *c=CMSG_FIRSTHDR(&m);c;c=CMSG_NXTHDR(&m,c)){
        if(c->cmsg_level==SOL_SOCKET&&c->cmsg_type==SCM_CREDENTIALS&&c->cmsg_len==CMSG_LEN(sizeof(struct ucred))){
            /* Credentials, when supplied, are not used for admission. */
        }else{
            if(c->cmsg_level==SOL_SOCKET&&c->cmsg_type==SCM_RIGHTS&&c->cmsg_len>=CMSG_LEN(0)){
                unsigned bytes=c->cmsg_len-CMSG_LEN(0);
                for(unsigned at=0;at+sizeof(int)<=bytes;at+=sizeof(int)){int received;memcpy(&received,(const char *)CMSG_DATA(c)+at,sizeof(received));close(received);}
            }
        }
    }
    if(m.msg_flags&MSG_TRUNC)return -EMSGSIZE;
    if(n<(ssize_t)sizeof(h)||frame(&h)||h.bytes>capacity||h.bytes>(size_t)n-sizeof(h))return -EPROTO;
    *out=h;return 0;
}
