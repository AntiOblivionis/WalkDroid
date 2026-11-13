#define _GNU_SOURCE
#include "sony_audio_cpu.h"
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#define DIRECTORY "walkdroid-sony-cpu"
#define GOV "/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor"
#define LCD "/sys/class/leds/lcd-backlight/brightness"
#define CPU1 "/sys/devices/system/cpu/cpu1/online"
enum {HOTPLUG=1,PERFORMANCE=2};
/* The record remains available to old readers as status, not as a gate. */
struct record {uint32_t magic,version,active,pid;uint64_t generation,inverse;uint32_t previous,reserved;};
struct files {int parent,dir,gate,lease;};
struct sony_audio_cpu {struct files f;struct record record;};

static void close_files(struct files *f){
    if(f->lease>=0)close(f->lease);if(f->gate>=0)close(f->gate);
    if(f->dir>=0)close(f->dir);if(f->parent>=0)close(f->parent);
    *f=(struct files){-1,-1,-1,-1};
}
static int open_files(struct files *f,int writing){
    *f=(struct files){-1,-1,-1,-1};
    f->parent=open("/dev",O_RDONLY|O_DIRECTORY|O_CLOEXEC);if(f->parent<0)return -errno;
    f->dir=openat(f->parent,DIRECTORY,O_RDONLY|O_DIRECTORY|O_CLOEXEC);
    if(f->dir<0){int rc=-errno;close_files(f);return rc;}
    f->gate=openat(f->dir,"gate",O_RDONLY|O_CLOEXEC);
    if(f->gate<0){int rc=-errno;close_files(f);return rc;}
    f->lease=openat(f->dir,"lease",(writing?O_RDWR:O_RDONLY)|O_CLOEXEC);
    if(f->lease<0){int rc=-errno;close_files(f);return rc;}
    return 0;
}
static int store(struct files *f,const struct record *r){
    size_t pos=0;
    while(pos<sizeof(*r)){
        ssize_t n=pwrite(f->lease,(const char *)r+pos,sizeof(*r)-pos,(off_t)pos);
        if(n<0&&errno==EINTR)continue;if(n<=0)return n<0?-errno:-EIO;pos+=(size_t)n;
    }
    return fsync(f->lease)?-errno:0;
}
static int text(const char *path,char *b,size_t capacity){
    int fd=open(path,O_RDONLY|O_CLOEXEC);if(fd<0)return -errno;
    ssize_t n;do{n=read(fd,b,capacity-1);}while(n<0&&errno==EINTR);
    int rc=n<0?-errno:0;if(close(fd)&&!rc)rc=-errno;if(!rc)b[n]=0;return rc;
}
static int write_text(const char *path,const char *b){
    int fd=open(path,O_WRONLY|O_CLOEXEC);if(fd<0)return -errno;
    size_t size=strlen(b);ssize_t n;do{n=write(fd,b,size);}while(n<0&&errno==EINTR);
    int rc=n<0?-errno:(size_t)n!=size?-EIO:0;if(close(fd)&&!rc)rc=-errno;return rc;
}
static int apply(unsigned wanted){
    const char *name=wanted==PERFORMANCE?"performance":"hotplug";
    char current[64]={0};int observed=text(GOV,current,sizeof(current));
    int rc=0;if(observed||strncmp(current,name,strlen(name))||
       (current[strlen(name)]&&current[strlen(name)]!='\n'))rc=write_text(GOV,name);
    if(!rc&&wanted==PERFORMANCE){
        char online[16]={0};int state=text(CPU1,online,sizeof(online));
        if(!state&&online[0]!='1')rc=write_text(CPU1,"1");
        else if(state&&state!=-ENOENT)rc=state;
    }
    return rc;
}
static int brightness(unsigned *value){
    char b[32];int rc=text(LCD,b,sizeof(b));if(rc)return rc;
    *value=(unsigned)strtoul(b,NULL,10);return 0;
}
int sony_audio_cpu_prepare(void){
    int dev=open("/dev",O_RDONLY|O_DIRECTORY|O_CLOEXEC);if(dev<0)return -errno;
    int created=!mkdirat(dev,DIRECTORY,0750);
    if(!created&&errno!=EEXIST){int rc=-errno;close(dev);return rc;}
    int dir=openat(dev,DIRECTORY,O_RDONLY|O_DIRECTORY|O_CLOEXEC);int rc=dir<0?-errno:0;
    if(!rc&&created)(void)fchown(dir,(uid_t)-1,1000);
    const char *names[]={"gate","lease"};
    for(unsigned i=0;!rc&&i<2;++i){
        int fd=openat(dir,names[i],O_RDWR|O_CREAT|O_EXCL|O_CLOEXEC,0640),created=fd>=0;
        if(fd<0&&errno==EEXIST)fd=openat(dir,names[i],O_RDWR|O_CLOEXEC);
        if(fd<0){rc=-errno;break;}
        if(created){
            (void)fchown(fd,(uid_t)-1,1000);
            if(i){struct record clean={.magic=0x43534457,.version=1,.inverse=UINT64_MAX};
                struct files f={.lease=fd};rc=store(&f,&clean);}
        }
        if(close(fd)&&!rc)rc=-errno;
    }
    if(dir>=0)close(dir);close(dev);return rc;
}
int sony_audio_cpu_acquire(struct sony_audio_cpu **out,uint64_t generation,
                           int (*check)(void *),int (*closed)(void *),void *context){
    (void)check;(void)closed;(void)context;
    if(!out||*out)return -EINVAL;
    struct sony_audio_cpu *c=calloc(1,sizeof(*c));if(!c)return -ENOMEM;
    int rc=open_files(&c->f,1);if(rc){free(c);return rc;}
    if(flock(c->f.lease,LOCK_EX|LOCK_NB)){rc=errno==EWOULDBLOCK?-EBUSY:-errno;close_files(&c->f);free(c);return rc;}
    if(flock(c->f.gate,LOCK_EX|LOCK_NB)){rc=errno==EWOULDBLOCK?-EBUSY:-errno;close_files(&c->f);free(c);return rc;}
    c->record=(struct record){0x43534457,1,1,(uint32_t)getpid(),generation,~generation,0,0};
    *out=c;rc=store(&c->f,&c->record);if(!rc)rc=apply(HOTPLUG);
    flock(c->f.gate,LOCK_UN);return rc;
}
int sony_audio_cpu_check(struct sony_audio_cpu *c){return c?0:-EINVAL;}
int sony_audio_cpu_release(struct sony_audio_cpu **owner){
    if(!owner||!*owner)return -EINVAL;struct sony_audio_cpu *c=*owner;
    if(flock(c->f.gate,LOCK_EX|LOCK_NB))return errno==EWOULDBLOCK?-EBUSY:-errno;
    unsigned level=0;int observed=brightness(&level);
    if(observed)fprintf(stderr,"CPU_BACKLIGHT_READ status=%d; using hotplug\n",observed);
    int rc=apply(level?PERFORMANCE:HOTPLUG);
    if(!rc){struct record clean=c->record;clean.active=clean.pid=clean.previous=0;rc=store(&c->f,&clean);}
    if(!rc){if(close(c->f.lease))rc=-errno;c->f.lease=-1;}
    flock(c->f.gate,LOCK_UN);
    if(!rc){close_files(&c->f);free(c);*owner=NULL;}return rc;
}
int sony_audio_cpu_backlight(unsigned level){
    struct files f;int rc=open_files(&f,0);char value[32];snprintf(value,sizeof(value),"%u",level);
    if(rc)return write_text(LCD,value);
    if(flock(f.gate,LOCK_EX)){rc=-errno;close_files(&f);return rc;}
    rc=write_text(LCD,value);
    if(!rc){
        if(flock(f.lease,LOCK_EX|LOCK_NB)){if(errno!=EWOULDBLOCK)rc=-errno;}
        else rc=apply(level?PERFORMANCE:HOTPLUG);
    }
    flock(f.gate,LOCK_UN);close_files(&f);return rc;
}
