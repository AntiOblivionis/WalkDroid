#define _GNU_SOURCE
#include "sony_audio_receiver_idle_guard_linux.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
static int check(void *v){struct sony_receiver_idle_guard_linux *l=v;int rc=l->verified_base.check(l->verified_base.context);return rc>0?-EPROTO:rc;}
static int text(char *buffer,unsigned cap){
    int fd=open("/sys/power/dpidle_state",O_RDONLY|O_CLOEXEC|O_NOFOLLOW);if(fd<0)return -errno;unsigned at=0;int rc=0;
    while(at<cap-1){ssize_t n=read(fd,buffer+at,cap-1-at);if(n<0&&errno==EINTR)continue;if(n<0){rc=-errno;break;}if(!n)break;at+=(unsigned)n;}
    if(!rc&&at==cap-1)rc=-EOVERFLOW;close(fd);buffer[at]=0;return rc;
}
static int inspect(void *v,struct sony_receiver_idle_observation *o){
    if(!o)return -EINVAL;struct sony_receiver_idle_guard_linux *l=v;int rc=check(v);char buffer[4096];if(!rc)rc=text(buffer,sizeof(buffer));
    unsigned timer=0;if(!rc){const char *key="dpidle_time_critera=",*p=strstr(buffer,key);if(!p||(p!=buffer&&p[-1]!='\n')||strstr(p+1,key))return -EPROTO;p+=strlen(key);
        if(*p<'0'||*p>'9')return -EPROTO;while(*p>='0'&&*p<='9'){unsigned d=(unsigned)(*p++-'0');if(timer>(UINT32_MAX-d)/10)return -ERANGE;timer=timer*10+d;}if(*p!='\n')return -EPROTO;}
    unsigned raw=0;if(!rc)rc=l->verified_base.read(l->verified_base.context,SONY_DSD_DEEP_IDLE,&raw);if(!rc&&raw>1)rc=-EPROTO;
    if(!rc){o->timer=timer;o->raw_switch=raw;}return rc;
}
static int closed(void *v){struct sony_receiver_idle_guard_linux *l=v;int rc=check(v);return rc?rc:l->verified_base.output_closed(l->verified_base.context);}
static int write_timer(void *v,unsigned timer){
    int rc=check(v);if(rc)return rc;if(timer!=UINT32_MAX&&timer>INT32_MAX)return -EINVAL;
    char command[40];int n=timer==UINT32_MAX?snprintf(command,sizeof(command),"time -1\n"):snprintf(command,sizeof(command),"time %u\n",timer);
    if(n<=0||n>=(int)sizeof(command))return -EOVERFLOW;
    int fd=open("/sys/power/dpidle_state",O_WRONLY|O_CLOEXEC|O_NOFOLLOW);if(fd<0)return -errno;
    ssize_t wrote=write(fd,command,(size_t)n);rc=wrote<0?-errno:wrote!=n?-EIO:0;if(close(fd)&&!rc)rc=-errno;return rc;
}
int sony_receiver_idle_guard_linux_io(struct sony_receiver_idle_guard_linux *l,const struct sony_dsd_power_platform_io *base,struct sony_receiver_idle_guard_io *out){
    if(!l||!base||!base->check||!base->read||!base->output_closed||!out)return -EINVAL;l->verified_base=*base;*out=(struct sony_receiver_idle_guard_io){check,closed,inspect,write_timer,l};return 0;
}
