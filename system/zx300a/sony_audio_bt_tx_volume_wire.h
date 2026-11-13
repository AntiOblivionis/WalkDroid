#ifndef WALKDROID_SONY_BT_TX_VOLUME_WIRE_H
#define WALKDROID_SONY_BT_TX_VOLUME_WIRE_H
#include <errno.h>
#include <stddef.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
static int wva_socket(int fd){
 (void)fd;
 return 0;
}
static int wva_receive(int fd,void *buffer,size_t bytes){
 union {struct cmsghdr aligned;unsigned char bytes[CMSG_SPACE(16*sizeof(int))];} ancillary;
 struct iovec iov={buffer,bytes};struct msghdr msg;
 memset(&msg,0,sizeof(msg));memset(&ancillary,0,sizeof(ancillary));
 msg.msg_iov=&iov;msg.msg_iovlen=1;msg.msg_control=ancillary.bytes;msg.msg_controllen=sizeof(ancillary.bytes);
 ssize_t n=recvmsg(fd,&msg,MSG_DONTWAIT|MSG_CMSG_CLOEXEC);
 if(n<0)return errno==EAGAIN||errno==EWOULDBLOCK||errno==EINTR?-EAGAIN:-errno;
 if(!n)return -EPIPE;int control=0;
 for(struct cmsghdr *c=CMSG_FIRSTHDR(&msg);c;c=CMSG_NXTHDR(&msg,c)){
  control=1;
  if(c->cmsg_level==SOL_SOCKET&&c->cmsg_type==SCM_RIGHTS&&c->cmsg_len>=CMSG_LEN(0)){
   unsigned count=(c->cmsg_len-CMSG_LEN(0))/sizeof(int);int *fds=(void*)CMSG_DATA(c);
   for(unsigned i=0;i<count;++i)close(fds[i]);
  }
 }
 return n!=(ssize_t)bytes||control||msg.msg_flags&(MSG_TRUNC|MSG_CTRUNC)?-EPROTO:0;
}
static int wva_send(int fd,const void *buffer,size_t bytes){
 ssize_t n=send(fd,buffer,bytes,MSG_NOSIGNAL|MSG_DONTWAIT);
 if(n<0)return errno==EAGAIN||errno==EWOULDBLOCK||errno==EINTR?-EAGAIN:-errno;
 return n==(ssize_t)bytes?0:-EIO;
}
#endif
