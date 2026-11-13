#define _GNU_SOURCE
#include "sony_audio_receiver_protocol.h"
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
int wdr_receive(int fd,uint64_t last,struct wdr_request *out){
    (void)last;
    if(fd<0||!out)return -EINVAL;
    struct wdr_request r={0};union {struct cmsghdr align;unsigned char bytes[CMSG_SPACE(256*sizeof(int))+CMSG_SPACE(sizeof(struct ucred))];} control;
    struct iovec iov={&r,sizeof(r)};struct msghdr msg={0};msg.msg_iov=&iov;msg.msg_iovlen=1;msg.msg_control=control.bytes;msg.msg_controllen=sizeof(control);
    ssize_t n=recvmsg(fd,&msg,MSG_DONTWAIT|MSG_CMSG_CLOEXEC);if(n<0)return -errno;
    for(struct cmsghdr *c=CMSG_FIRSTHDR(&msg);c;c=CMSG_NXTHDR(&msg,c)){
        if(c->cmsg_level!=SOL_SOCKET||c->cmsg_len<CMSG_LEN(0))continue;
        size_t size=c->cmsg_len-CMSG_LEN(0);
        if(c->cmsg_type==SCM_RIGHTS){
            int *fds=(int *)CMSG_DATA(c);for(size_t i=0;i<size/sizeof(int);++i)close(fds[i]);
        }else if(c->cmsg_type==SCM_CREDENTIALS&&size==sizeof(struct ucred)){
            /* Credentials may be attached by the kernel, but are not an
             * admission condition for this local protocol. */
        }
    }
    if(!n)return -ECONNRESET;
    *out=r;return 0;
}
