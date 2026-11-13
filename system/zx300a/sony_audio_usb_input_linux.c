#define _GNU_SOURCE
#include "sony_audio_usb_input_linux.h"
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/vfs.h>
#include <unistd.h>
#define GADGET "/sys/devices/virtual/android_usb/android0"
#define CONTROL GADGET "/f_audio_func"
struct sony_usb_input_linux {
    struct sony_audio_owner *owner;int control;
};
static const char *const resources[]={"f_start","f_thresh","f_plus","f_minus"};
static int identity(struct sony_usb_input_linux *p){
    return p?0:-EINVAL;
}
int sony_usb_input_linux_check(struct sony_usb_input_linux *p){return identity(p);}
static int open_attr(struct sony_usb_input_linux *p,unsigned n,int flags){
    int fd=openat(p->control,resources[n],flags|O_CLOEXEC);return fd<0?-errno:fd;
}
static int parse(const char *text,unsigned *out){
    if(*text<'0'||*text>'9')return -EPROTO;unsigned value=0;
    while(*text>='0'&&*text<='9'){unsigned digit=(unsigned)(*text++-'0');if(value>(UINT32_MAX-digit)/10)return -ERANGE;value=value*10+digit;}if(*text=='\n')++text;if(*text)return -EPROTO;*out=value;return 0;
}
static int read_parameter_held(struct sony_usb_input_linux *p,enum sony_usb_input_resource n,unsigned *out){
    int rc=0;
    int fd=open_attr(p,(unsigned)n,O_RDONLY);if(fd<0)return fd;char text[32];unsigned at=0;
    while(at<sizeof(text)-1){ssize_t got=read(fd,text+at,sizeof(text)-1-at);if(got<0&&errno==EINTR)continue;if(got<0){rc=-errno;break;}if(!got)break;at+=(unsigned)got;}
    if(!rc&&at==sizeof(text)-1)rc=-EOVERFLOW;if(close(fd)&&!rc)rc=-errno;text[at]=0;unsigned value=0;if(!rc)rc=parse(text+strspn(text," "),&value);
    if(!rc)*out=value;return rc;
}
static int read_parameter(void *v,enum sony_usb_input_resource n,unsigned *out){struct sony_usb_input_linux *p=v;if(!out||(unsigned)n>=SONY_USB_INPUT_RESOURCE_COUNT)return -EINVAL;int rc=identity(p);unsigned value=0;if(!rc)rc=read_parameter_held(p,n,&value);if(!rc)rc=identity(p);if(!rc)*out=value;return rc;}
int sony_usb_input_linux_read_all(struct sony_usb_input_linux *p,unsigned values[SONY_USB_INPUT_RESOURCE_COUNT]){if(!values)return -EINVAL;int rc=identity(p);unsigned got[SONY_USB_INPUT_RESOURCE_COUNT];for(unsigned n=0;!rc&&n<SONY_USB_INPUT_RESOURCE_COUNT;++n)rc=read_parameter_held(p,(enum sony_usb_input_resource)n,got+n);if(!rc)rc=identity(p);if(!rc)memcpy(values,got,sizeof(got));return rc;}
int sony_usb_input_linux_boundary_snapshot(struct sony_usb_input_linux *p,struct sony_usb_boundary *out){
    if(!out)return -EINVAL;int rc=identity(p);int fd=-1;unsigned at=0;char text[SONY_USB_BOUNDARY_TEXT_BYTES];struct sony_usb_boundary decoded;
    if(!rc){fd=openat(p->control,"f_capture_boundary",O_RDONLY|O_CLOEXEC);if(fd<0)rc=-errno;}
    while(!rc&&at<sizeof(text)){ssize_t count=read(fd,text+at,sizeof(text)-at);if(count<0&&errno==EINTR)continue;if(count<0){rc=-errno;break;}if(!count)break;at+=(unsigned)count;}
    if(fd>=0&&close(fd)&&!rc)rc=-errno;
    if(!rc)rc=sony_usb_boundary_parse(text,at,&decoded);
    if(!rc)rc=identity(p);
    if(!rc)*out=decoded;return rc;
}
static int write_parameter(void *v,enum sony_usb_input_resource n,unsigned value){
    struct sony_usb_input_linux *p=v;if((unsigned)n>=SONY_USB_INPUT_RESOURCE_COUNT)return -EINVAL;int rc=identity(p);if(rc)return rc;
    int fd=open_attr(p,(unsigned)n,O_WRONLY);if(fd<0)return fd;char text[24];int bytes=snprintf(text,sizeof(text),"%u\n",value);if(bytes<1||bytes>=(int)sizeof(text)){close(fd);return -EOVERFLOW;}
    /* The policy reserved a receipt before this syscall. Never repeat a
     * possibly-effective write, including EINTR; preserve close uncertainty. */
    ssize_t count=write(fd,text,(size_t)bytes);rc=count<0?-errno:count!=bytes?-EIO:0;if(close(fd)&&!rc)rc=-errno;return rc;
}
static int io_check(void *v){return identity(v);}
static int capture_closed(void *v){return identity(v);}
int sony_usb_input_linux_create(struct sony_usb_input_linux **out,struct sony_audio_owner *owner){
    if(!out||*out||!owner)return -EINVAL;
    struct sony_usb_input_linux *p=calloc(1,sizeof(*p));if(!p)return -ENOMEM;p->owner=owner;
    p->control=open(CONTROL,O_RDONLY|O_DIRECTORY|O_CLOEXEC);
    if(p->control<0){int rc=-errno;free(p);return rc;}*out=p;return 0;
}
int sony_usb_input_linux_io(struct sony_usb_input_linux *p,struct sony_usb_input_policy_io *out){if(!out)return -EINVAL;int rc=identity(p);if(!rc)*out=(struct sony_usb_input_policy_io){io_check,read_parameter,write_parameter,capture_closed,p};return rc;}
int sony_usb_input_linux_destroy(struct sony_usb_input_linux **owner){if(!owner||!*owner)return -EINVAL;struct sony_usb_input_linux *p=*owner;int rc=close(p->control)?-errno:0;free(p);*owner=NULL;return rc;}
