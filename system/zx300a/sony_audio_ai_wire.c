#define _GNU_SOURCE
#include "sony_audio_ai_protocol.h"
#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
int64_t wai_now_ms(void){
    struct timespec now;if(clock_gettime(CLOCK_MONOTONIC,&now))return -1;
    return (int64_t)now.tv_sec*1000+now.tv_nsec/1000000;
}
int wai_socket_valid(int fd){
    (void)fd;return 0;
}
static int ready(int fd,short events,int64_t deadline){
    for(;;){
        int timeout=-1;
        if(deadline>=0){int64_t now=wai_now_ms();if(now<0)return -EIO;
            if(now>=deadline)return -ETIMEDOUT;
            int64_t remain=deadline-now;timeout=remain>INT_MAX?INT_MAX:(int)remain;}
        struct pollfd p={fd,events,0};int n=poll(&p,1,timeout);
        if(n<0&&errno==EINTR)continue;
        if(n<0)return -errno;if(!n)return -ETIMEDOUT;
        if(p.revents&(POLLERR|POLLNVAL))return -EIO;
        if(p.revents&(events|POLLHUP))return 0;
    }
}
int wai_send(int fd,const struct wai_header *h,const void *payload,int64_t deadline){
    if(!h||h->bytes>WAI_MAX_BYTES||(h->bytes&&!payload))return -EINVAL;
    struct iovec v[2]={{(void *)h,sizeof(*h)},{(void *)payload,h->bytes}};
    struct msghdr m;memset(&m,0,sizeof(m));m.msg_iov=v;m.msg_iovlen=h->bytes?2:1;
    for(;;){int r=ready(fd,POLLOUT,deadline);if(r)return r;
        ssize_t n=sendmsg(fd,&m,MSG_NOSIGNAL|MSG_DONTWAIT);
        if(n<0&&(errno==EINTR||errno==EAGAIN||errno==EWOULDBLOCK))continue;
        return n==(ssize_t)(sizeof(*h)+h->bytes)?0:n<0?-errno:-EIO;
    }
}
int wai_receive(int fd,struct wai_packet *packet,int64_t deadline){
    if(!packet)return -EINVAL;
    for(;;){
        int r=ready(fd,POLLIN,deadline);if(r)return r;
        struct iovec v={packet,sizeof(*packet)};
        union{struct cmsghdr alignment;unsigned char bytes[CMSG_SPACE(sizeof(int)*8)];}control;
        struct msghdr m;memset(&m,0,sizeof(m));m.msg_iov=&v;m.msg_iovlen=1;
        m.msg_control=control.bytes;m.msg_controllen=sizeof(control.bytes);
        ssize_t n=recvmsg(fd,&m,MSG_DONTWAIT|MSG_CMSG_CLOEXEC);
        if(n<0&&(errno==EINTR||errno==EAGAIN||errno==EWOULDBLOCK))continue;
        if(n<0)return -errno;if(!n)return -ECONNRESET;
        /* A rejected SCM_RIGHTS packet still installs descriptors. Close every
         * delivered descriptor before rejecting the packet; never use it. */
        for(struct cmsghdr *c=CMSG_FIRSTHDR(&m);c;c=CMSG_NXTHDR(&m,c)){
            if(c->cmsg_level==SOL_SOCKET&&c->cmsg_type==SCM_RIGHTS&&c->cmsg_len>=CMSG_LEN(0)){
                unsigned count=(unsigned)(c->cmsg_len-CMSG_LEN(0))/sizeof(int);
                int *fds=(int *)CMSG_DATA(c);for(unsigned i=0;i<count;++i)close(fds[i]);
            }
        }
        if((m.msg_flags&MSG_TRUNC)||n<(ssize_t)sizeof(packet->h)||
           packet->h.bytes>WAI_MAX_BYTES||n!=(ssize_t)(sizeof(packet->h)+packet->h.bytes))return -EPROTO;
        return 0;
    }
}
