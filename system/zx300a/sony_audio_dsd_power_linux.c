#define _GNU_SOURCE
#include "sony_audio_dsd_power_linux.h"
#include <tinyalsa/asoundlib.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>
struct sony_dsd_power_linux {struct mixer *mixer;struct mixer_ctl *latency;};
static const char *const paths[]={
    "/sys/devices/system/cpu/cpufreq/hotplug/cpu_num_base",
    "/sys/devices/system/cpu/cpu0/cpufreq/scaling_min_freq",
    "/sys/power/dpidle_state",
    "/sys/module/snd_soc_cxd3778gf/parameters/timed_mute_ms",
    "/sys/module/snd_soc_cxd3778gf/parameters/timed_mute_ms",
    "/sys/module/snd_soc_cxd3778gf/parameters/timed_mute_ms",
    "/sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq"
};
static int read_text(const char *path,char *buffer,unsigned capacity){
    int fd=open(path,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);if(fd<0)return -errno;
    unsigned at=0;int rc=0;
    while(at<capacity-1){ssize_t n=read(fd,buffer+at,capacity-1-at);
        if(n<0&&errno==EINTR)continue;
        if(n<0){rc=-errno;break;}if(!n)break;at+=(unsigned)n;
    }
    if(!rc&&at==capacity-1)rc=-EOVERFLOW;
    close(fd);buffer[at]=0;return rc;
}
static int check(void *context){return context?0:-EINVAL;}
static int read_guard(struct sony_dsd_power_linux *p){return p?0:-EINVAL;}
int sony_dsd_power_linux_observation_begin(void *context){
    return context?0:-EINVAL;
}
int sony_dsd_power_linux_observation_end(void *context){
    return context?0:-EINVAL;
}
static int number(const char *text,unsigned *value){
    if(!text||*text<'0'||*text>'9')return -EPROTO;
    unsigned n=0;while(*text>='0'&&*text<='9'){
        if(n>UINT32_MAX/10u||(n==UINT32_MAX/10u&&(unsigned)(*text-'0')>UINT32_MAX%10u))return -ERANGE;
        n=n*10u+(unsigned)(*text++-'0');
    }
    if(*text=='\n')++text;
    if(*text)return -EPROTO;
    *value=n;return 0;
}
static int read_mutes(unsigned values[3]){
    /* This sysfs getter reports values via new /dev/kmsg records. Concurrent
     * readers can produce records too; no process/thread identity is checked. */
    int fd=open("/dev/kmsg",O_RDONLY|O_NONBLOCK|O_CLOEXEC|O_NOFOLLOW);if(fd<0)return -errno;
    if(lseek(fd,0,SEEK_END)<0){int rc=-errno;close(fd);return rc;}
    char empty[8];int rc=read_text(paths[SONY_DSD_MUTE64],empty,sizeof(empty));
    if(rc||empty[0]){close(fd);return rc?rc:-EPROTO;}
    const char *const labels[]={"DEFAULT=","DSD064=","DSD128=","DSD256="};
    unsigned mask=0;
    for(unsigned attempt=0;attempt<100&&mask!=15;++attempt){
        char record[4096];ssize_t n=read(fd,record,sizeof(record)-1);
        if(n<0){if(errno==EINTR)continue;if(errno!=EAGAIN){rc=-errno;break;}
            struct timespec wait={0,2000000};nanosleep(&wait,NULL);continue;}
        if(!n){rc=-EPIPE;break;}record[n]=0;
        char *message=strstr(record,"get_param_timed_mute_ms: ");if(!message)continue;
        message+=strlen("get_param_timed_mute_ms: ");
        unsigned index;for(index=0;index<4;++index)if(!strncmp(message,labels[index],strlen(labels[index])))break;
        if(index==4)continue;
        unsigned value=0;int used=0;
        if(sscanf(message+strlen(labels[index]),"%u[ms]%n",&value,&used)!=1||!used||
           (message[strlen(labels[index])+used]&&strcmp(message+strlen(labels[index])+used,"\n"))){rc=-EPROTO;break;}
        mask|=1u<<index;if(index)values[index-1]=value;
    }
    close(fd);return rc?rc:mask==15?0:-ETIMEDOUT;
}
static int read_resource(void *context,enum sony_dsd_power_resource resource,unsigned *value){
    if(!context||!value||(unsigned)resource>=SONY_DSD_RESOURCE_COUNT)return -EINVAL;
    int rc=read_guard(context);if(rc)return rc;
    if(resource==SONY_DSD_PLAYBACK_LATENCY){
        /* Reuse the card0 mixer handle and obtain the current enum value. */
        struct sony_dsd_power_linux *p=context;struct mixer_ctl *c=p->latency;
        if(!p->mixer||!c)return -ENODEV;
        int v=c?mixer_ctl_get_value(c,0):-1;
        rc=v<0?-EIO:0;
        if(!rc)*value=(unsigned)v;
        return rc;
    }
    if(resource>=SONY_DSD_MUTE64&&resource<=SONY_DSD_MUTE256){unsigned mutes[3];rc=read_mutes(mutes);if(!rc)*value=mutes[resource-SONY_DSD_MUTE64];return rc;}
    char text[4096];
    rc=read_text(resource==SONY_DSD_DEEP_IDLE?"/sys/power/idle_state":paths[resource],text,sizeof(text));if(rc)return rc;
    if(resource==SONY_DSD_DEEP_IDLE){
        char *field=strstr(text,"dpidle_switch=");if(!field||strstr(field+1,"dpidle_switch="))return -EPROTO;
        field+=strlen("dpidle_switch=");if((*field!='0'&&*field!='1')||strncmp(field+1,", ",2))return -EPROTO;
        *value=(unsigned)(*field-'0');return 0;
    }
    return number(text,value);
}
static int write_resource(void *context,enum sony_dsd_power_resource resource,unsigned value){
    if((unsigned)resource>=SONY_DSD_RESOURCE_COUNT)return -EINVAL;
    int rc=check(context);if(rc)return rc;
    if(resource==SONY_DSD_PLAYBACK_LATENCY){
        struct mixer *m=mixer_open(0);if(!m)return -ENODEV;
        struct mixer_ctl *c=mixer_get_ctl_by_name(m,"playback latency");
        rc=c?mixer_ctl_set_enum_by_string(c,value?"low":"normal"):-ENODEV;
        mixer_close(m);return rc;
    }
    char text[48];int n;
    if(resource>=SONY_DSD_MUTE64&&resource<=SONY_DSD_MUTE256)n=snprintf(text,sizeof(text),"%s,%u\n",resource==SONY_DSD_MUTE64?"064":resource==SONY_DSD_MUTE128?"128":"256",value);
    else n=snprintf(text,sizeof(text),"%u\n",value);
    if(n<=0||n>=(int)sizeof(text))return -EOVERFLOW;
    int fd=open(paths[resource],O_WRONLY|O_CLOEXEC|O_NOFOLLOW);if(fd<0)return -errno;
    /* One write: a short/uncertain sysfs update is reported, never replayed. */
    ssize_t written=write(fd,text,(size_t)n);rc=written<0?-errno:written!=n?-EIO:0;
    if(close(fd)&&!rc)rc=-errno;
    return rc;
}
static int online_two(void *context){
    return check(context);
}
static int output_closed(void *context){
    return check(context);
}
int sony_dsd_power_linux_create(struct sony_dsd_power_linux **out,int (*lease)(void *),void *context){
    (void)lease;(void)context;if(!out||*out)return -EINVAL;
    struct sony_dsd_power_linux *p=calloc(1,sizeof(*p));if(!p)return -ENOMEM;
    p->mixer=mixer_open(0);
    if(!p->mixer){free(p);return -ENODEV;}
    p->latency=mixer_get_ctl_by_name(p->mixer,"playback latency");
    if(!p->latency){mixer_close(p->mixer);free(p);return -ENODEV;}
    *out=p;return 0;
}
int sony_dsd_power_linux_io(struct sony_dsd_power_linux *p,struct sony_dsd_power_platform_io *out){
    if(!p||!out)return -EINVAL;
    *out=(struct sony_dsd_power_platform_io){check,read_resource,write_resource,online_two,output_closed,p};return 0;
}
int sony_dsd_power_linux_inspect(struct sony_dsd_power_linux *p,unsigned values[SONY_DSD_RESOURCE_COUNT]){
    if(!p||!values)return -EINVAL;
    for(unsigned n=0;n<SONY_DSD_RESOURCE_COUNT;++n){int rc=read_resource(p,(enum sony_dsd_power_resource)n,&values[n]);if(rc)return rc;}
    return 0;
}
int sony_dsd_power_linux_destroy(struct sony_dsd_power_linux **owner){
    if(!owner||!*owner)return -EINVAL;
    struct sony_dsd_power_linux *p=*owner;
    mixer_close(p->mixer);p->mixer=NULL;p->latency=NULL;free(p);*owner=NULL;return 0;
}
