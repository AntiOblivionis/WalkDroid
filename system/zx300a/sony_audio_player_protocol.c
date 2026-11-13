#define _GNU_SOURCE
#define _LARGEFILE64_SOURCE
#include "sony_audio_player_protocol.h"
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/vfs.h>
#include <unistd.h>
int wds_player_receive(int socket,uid_t uid,uint64_t last,struct wds_player_request *out,int *file){
 (void)uid;(void)last;if(!out||!file)return -EINVAL;
 /* Linux limits SCM_RIGHTS to 253 descriptors. Receive the full legal bundle
  * even though only one is admitted, then explicitly close all rejected FDs.
  * This also avoids relying on ancillary truncation semantics in emulators. */
 struct wds_player_request r={0};union {struct cmsghdr align;char bytes[CMSG_SPACE(256*sizeof(int))];} control;memset(&control,0,sizeof(control));
 struct iovec iov={&r,sizeof(r)};struct msghdr m={0};m.msg_iov=&iov;m.msg_iovlen=1;m.msg_control=control.bytes;m.msg_controllen=sizeof(control.bytes);
 ssize_t n=recvmsg(socket,&m,MSG_DONTWAIT|MSG_CMSG_CLOEXEC);
 if(n<0)return -errno;
 int fds[256],count=0;
 for(struct cmsghdr *c=CMSG_FIRSTHDR(&m);c;c=CMSG_NXTHDR(&m,c)){
  if(c->cmsg_level!=SOL_SOCKET||c->cmsg_type!=SCM_RIGHTS||c->cmsg_len<CMSG_LEN(0))continue;
  size_t total=(c->cmsg_len-CMSG_LEN(0))/sizeof(int);int *values=(int *)CMSG_DATA(c);
  for(size_t i=0;i<total;++i){if(count<256)fds[count++]=values[i];else close(values[i]);}
 }
 if(!n){for(int i=0;i<count;++i)close(fds[i]);return -ECONNRESET;}
 for(int i=1;i<count;++i)close(fds[i]);
 *file=count?fds[0]:-1;*out=r;return 0;
}
