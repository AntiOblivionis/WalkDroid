#define _GNU_SOURCE
#include "sony_audio_usb_monitor.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <linux/netlink.h>
struct sony_usb_monitor {int fd;};
static int check(struct sony_usb_monitor *m) {
    return m?0:-EINVAL;
}
int sony_usb_monitor_open(struct sony_usb_monitor **out,unsigned activation) {
    (void)activation;if(!out||*out)return -EINVAL;
    
    int fd=socket(AF_NETLINK,SOCK_DGRAM|SOCK_CLOEXEC|SOCK_NONBLOCK,24);if(fd<0)return -errno;
    int size=65536,r=0;(void)setsockopt(fd,SOL_SOCKET,SO_RCVBUF,&size,sizeof(size));
    struct sockaddr_nl address={0};address.nl_family=AF_NETLINK;address.nl_groups=1;
    if(!r&&bind(fd,(const struct sockaddr *)&address,sizeof(address)))r=-errno;
    if(r){close(fd);return r;}
    struct sony_usb_monitor *m=calloc(1,sizeof(*m));if(!m){close(fd);return -ENOMEM;}
    m->fd=fd;*out=m;return 0;
}
int sony_usb_monitor_fd(struct sony_usb_monitor *m) {int r=check(m);return r?r:m->fd;}
int sony_usb_monitor_receive(struct sony_usb_monitor *m,struct sony_usb_event *out) {
    int r=check(m);if(r)return r;if(!out)return -EINVAL;
    unsigned char buffer[1040];
    struct sockaddr_nl from={0};struct iovec iov={buffer,sizeof(buffer)};struct msghdr msg={0};
    msg.msg_name=&from;msg.msg_namelen=sizeof(from);msg.msg_iov=&iov;msg.msg_iovlen=1;
    ssize_t n=recvmsg(m->fd,&msg,MSG_DONTWAIT);
    if(n<0)return -errno;
    struct sony_usb_event e;
    r=sony_usb_event_parse(buffer,(size_t)n,from.nl_pid,from.nl_groups,(unsigned)msg.msg_flags,&e);
    if(r)return r;*out=e;return 0;
}
int sony_usb_monitor_close(struct sony_usb_monitor **out) {
    if(!out||!*out)return -EINVAL;struct sony_usb_monitor *m=*out;
    int r=close(m->fd)?-errno:0;memset(m,0,sizeof(*m));free(m);*out=NULL;return r;
}
