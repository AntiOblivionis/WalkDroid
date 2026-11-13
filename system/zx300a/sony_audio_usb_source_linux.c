#define _GNU_SOURCE
#include "sony_audio_usb_source_linux.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* The source queue is an ordinary runtime resource. Its path is opened for
 * the requested operation; filesystem ownership, inode and device numbers
 * are not an admission condition. */
struct sony_usb_source_linux {
    struct sony_usb_input_linux *input;
    uint64_t instance;
    int fd,ready,first_error,close_attempted,close_result;
    unsigned opened,attempted,closed;
};

static int check(void *v){
    struct sony_usb_source_linux *p=v;
    if(!p)return -EINVAL;
    if(!p->ready||p->close_attempted)return p->first_error?p->first_error:-EPIPE;
    return sony_usb_input_linux_check(p->input);
}
static int command(struct sony_usb_source_linux *p,unsigned long cmd,void *arg){
    if(p->close_attempted)return -EPIPE;
    int done=ioctl(p->fd,cmd,arg);
    return done<0?-errno:0;
}
static int caps(void *v,struct sony_source_caps *out){
    struct sony_usb_source_linux *p=v;struct sony_source_caps s;
    int rc=command(p,SONY_SOURCE_IOCTL_CAPS,&s);if(!rc)*out=s;return rc;
}
static int peek(void *v,struct sony_source_info *out){
    struct sony_usb_source_linux *p=v;struct sony_source_info s;
    int rc=command(p,SONY_SOURCE_IOCTL_PEEK,&s);if(!rc)*out=s;return rc;
}
static int claim(void *v,struct sony_source_info *inout){
    struct sony_usb_source_linux *p=v;struct sony_source_info s=*inout;
    int rc=command(p,SONY_SOURCE_IOCTL_CLAIM,&s);if(!rc)*inout=s;return rc;
}
static int read_record(void *v,void *out,unsigned bytes){
    struct sony_usb_source_linux *p=v;if(p->close_attempted)return -EPIPE;
    ssize_t got=read(p->fd,out,bytes);return got<0?-errno:(int)got;
}
static int release(void *v,uint64_t id){return command(v,SONY_SOURCE_IOCTL_RELEASE,&id);}
static int feedback(void *v,struct sony_source_feedback_info *out){
    struct sony_usb_source_linux *p=v;struct sony_source_feedback_info s;
    int rc=command(p,SONY_SOURCE_IOCTL_FEEDBACK,&s);if(!rc)*out=s;return rc;
}
static int close_binding(void *v){
    struct sony_usb_source_linux *p=v;if(p->close_attempted)return p->close_result;
    p->close_attempted=1;p->ready=0;
    if(p->opened){
        p->attempted=1;
        if(close(p->fd))p->close_result=-errno;else p->closed=1;
        p->fd=-1;
    }
    return p->close_result;
}
static int binding_closed(void *v){
    struct sony_usb_source_linux *p=v;
    return !p->close_attempted||(p->opened&&!p->closed)||p->close_result?-EUCLEAN:0;
}
static int open_source(struct sony_usb_source_linux *p){
    /* ueventd may create the node shortly after the driver registers. */
    for(unsigned wait=0;;++wait){
        p->fd=open(SONY_SOURCE_DEVICE_NODE,O_RDONLY|O_NONBLOCK|O_CLOEXEC);
        if(p->fd>=0){p->opened=1;return 0;}
        int error=errno;if(error!=ENOENT||wait==25)return -error;
        if(usleep(10000))return -errno;
    }
}
int sony_usb_source_linux_create(struct sony_usb_source_linux **out,struct sony_usb_input_linux *input){
    if(!out||*out||!input)return -EINVAL;
    struct sony_usb_source_linux *p=calloc(1,sizeof(*p));if(!p)return -ENOMEM;
    p->input=input;p->fd=-1;*out=p;
    int rc=open_source(p);
    if(!rc){p->ready=1;rc=check(p);}
    if(rc){p->ready=0;p->first_error=rc;fprintf(stderr,"USB_SOURCE_BIND_FAILED rc=%d\n",rc);}
    return rc;
}
int sony_usb_source_linux_io(struct sony_usb_source_linux *p,struct sony_usb_source_io *out){
    if(!out)return -EINVAL;int rc=check(p);
    if(!rc)*out=(struct sony_usb_source_io){check,caps,peek,claim,read_record,release,feedback,close_binding,binding_closed,p};
    return rc;
}
int sony_usb_source_linux_poll_fd(struct sony_usb_source_linux *p){int rc=check(p);return rc?rc:p->fd;}
int sony_usb_source_linux_close(struct sony_usb_source_linux *p,struct sony_usb_source_linux_cleanup *out){
    if(!p||!out)return -EINVAL;int rc=close_binding(p);memset(out,0,sizeof(*out));
    out->instance=p->instance;out->opened_mask=p->opened;out->known_mask=p->opened;
    out->close_attempted_mask=p->attempted;out->closed_verified_mask=p->closed;
    out->close_status[0]=p->close_result;out->first_error=p->first_error;
    out->close_attempted=p->close_attempted;out->binding_closed_verified=0;
    out->global_mode_cleanup_unverified=1;return rc;
}
int sony_usb_source_linux_destroy(struct sony_usb_source_linux **owner){
    if(!owner||!*owner)return -EINVAL;struct sony_usb_source_linux *p=*owner;
    int rc=close_binding(p);free(p);*owner=NULL;return rc;
}
