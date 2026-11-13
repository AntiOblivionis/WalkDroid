#define _GNU_SOURCE
#include "sony_audio_usb_gadget_linux.h"
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char *const dirs[]={"/sys/devices/virtual/android_usb/android0","/sys/devices/virtual/android_usb/android0/f_audio_func"};
static const char *const names[]={"enable","functions","idVendor","idProduct","iSerial","iManufacturer","iProduct","f_plus","f_minus","f_thresh","f_start","f_valid","f_allow"};
struct sony_usb_gadget_linux {struct sony_audio_owner *owner;int fd[2];};
static int identity(struct sony_usb_gadget_linux *p){return p?0:-EINVAL;}
static int open_attr(struct sony_usb_gadget_linux *p,unsigned n,int flags){
    unsigned dir=n<7?0:1;
    if(p->fd[dir]<0){p->fd[dir]=open(dirs[dir],O_RDONLY|O_DIRECTORY|O_CLOEXEC);if(p->fd[dir]<0)return -errno;}
    int fd=openat(p->fd[dir],names[n],flags|O_CLOEXEC);return fd<0?-errno:fd;
}
static int io_check(void *v){return identity(v);}
static int value_read(void *v,enum sony_usb_gadget_attr attr,char *out,unsigned cap){
    struct sony_usb_gadget_linux *p=v;unsigned n=(unsigned)attr;
    if(!p||!out||!cap||n>=SONY_USB_GADGET_ATTR_COUNT)return -EINVAL;
    int fd=open_attr(p,n,O_RDONLY);if(fd<0)return fd;unsigned used=0;int rc=0;
    while(used<cap-1){ssize_t count=read(fd,out+used,cap-1-used);if(count<0&&errno==EINTR)continue;
        if(count<0){rc=-errno;break;}if(!count)break;used+=(unsigned)count;}
    if(!rc&&used==cap-1)rc=-EOVERFLOW;if(close(fd)&&!rc)rc=-errno;out[used]=0;
    if(!rc){while(used&&(out[used-1]=='\n'||out[used-1]=='\r'))out[--used]=0;
        if(n>=SONY_USB_GADGET_PLUS){unsigned spaces=(unsigned)strspn(out," ");if(spaces)memmove(out,out+spaces,strlen(out+spaces)+1);}}
    return rc;
}
static int value_write(void *v,enum sony_usb_gadget_attr attr,const char *value){
    struct sony_usb_gadget_linux *p=v;unsigned n=(unsigned)attr;
    if(!p||!value||n>=SONY_USB_GADGET_ATTR_COUNT)return -EINVAL;
    int fd=open_attr(p,n,O_WRONLY);if(fd<0)return fd;size_t bytes=strlen(value);
    ssize_t count=write(fd,value,bytes);int rc=count<0?-errno:(size_t)count!=bytes?-EIO:0;
    if(close(fd)&&!rc)rc=-errno;return rc;
}
static int quiesced(void *v){return identity(v);}
static int uac_absent(void *v){return identity(v);}
int sony_usb_gadget_linux_create(struct sony_usb_gadget_linux **out,struct sony_audio_owner *owner){
    if(!out||*out||!owner)return -EINVAL;
    struct sony_usb_gadget_linux *p=calloc(1,sizeof(*p));if(!p)return -ENOMEM;
    p->owner=owner;p->fd[0]=p->fd[1]=-1;
    p->fd[0]=open(dirs[0],O_RDONLY|O_DIRECTORY|O_CLOEXEC);
    if(p->fd[0]<0){int rc=-errno;free(p);return rc;}*out=p;return 0;
}
int sony_usb_gadget_linux_io(struct sony_usb_gadget_linux *p,struct sony_usb_gadget_io *out){
    if(!p||!out)return -EINVAL;*out=(struct sony_usb_gadget_io){io_check,value_read,value_write,quiesced,uac_absent,p};return 0;
}
int sony_usb_gadget_linux_destroy(struct sony_usb_gadget_linux **owner){
    if(!owner||!*owner)return -EINVAL;struct sony_usb_gadget_linux *p=*owner;int rc=0;
    for(unsigned n=0;n<2;++n)if(p->fd[n]>=0&&close(p->fd[n])&&!rc)rc=-errno;
    free(p);*owner=NULL;return rc;
}
