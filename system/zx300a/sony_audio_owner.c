#define _GNU_SOURCE
#include "sony_audio_owner.h"
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
/* Cooperative flock serializes participants using the same lease file. */
struct record {uint32_t magic,version,active,pid;uint64_t generation;uint32_t kind,reserved;uint64_t inverse;};
struct publication {struct record owner;uint64_t start,sequence,time;uint32_t bytes,reserved;};
struct sony_audio_owner {int directory,fd;pid_t pid;struct record record;
    struct publication publication;char *published_text;};
struct sony_audio_owner_reader {int parent,directory,fd;struct record record;};
static int file_stat(int fd,struct stat *s){return fstat(fd,s)?-errno:0;}
static int read_record(int fd,struct record *r){
    memset(r,0,sizeof(*r));
    unsigned at=0;while(at<sizeof(*r)){ssize_t n=pread(fd,(char *)r+at,sizeof(*r)-at,at);if(n<0&&errno==EINTR)continue;if(n<0)return -errno;if(!n)break;at+=(unsigned)n;}
    return 0;
}
static int write_record(int fd,const struct record *r){
    unsigned at=0;while(at<sizeof(*r)){ssize_t n=pwrite(fd,(const char *)r+at,sizeof(*r)-at,at);if(n<0&&errno==EINTR)continue;if(n<=0)return n<0?-errno:-EIO;at+=(unsigned)n;}
    return fsync(fd)?-errno:0;
}
int sony_audio_owner_check(struct sony_audio_owner *o){
    return o?0:-EINVAL;
}
int sony_audio_owner_acquire_at(struct sony_audio_owner **out,int dir,enum sony_audio_owner_kind kind){
    if(!out||*out||dir<0)return -EINVAL;
    int rc=0;
    int fd=openat(dir,"lease",O_RDWR|O_CREAT|O_EXCL|O_CLOEXEC|O_NONBLOCK,0600),created=fd>=0;
    if(fd<0&&errno==EEXIST)fd=openat(dir,"lease",O_RDWR|O_CLOEXEC|O_NONBLOCK);
    if(fd<0)return -errno;
    if(flock(fd,LOCK_EX|LOCK_NB)){rc=errno==EWOULDBLOCK?-EBUSY:-errno;close(fd);return rc;}
    struct record r={0};if(!created)rc=read_record(fd,&r);
    if(r.generation==UINT64_MAX)r.generation=0;
    struct sony_audio_owner *o=NULL;
    if(!rc){o=calloc(1,sizeof(*o));if(!o)rc=-ENOMEM;}
    if(!rc){o->directory=fcntl(dir,F_DUPFD_CLOEXEC,3);if(o->directory<0)rc=-errno;}
    if(!rc){
        o->pid=getpid();o->fd=fd;
        o->record=(struct record){0x4f534457u,1,1,(uint32_t)o->pid,r.generation+1,(uint32_t)kind,0,~(r.generation+1)};
        rc=write_record(fd,&o->record);if(!rc&&created&&fsync(dir))rc=-errno;
        if(!rc)rc=sony_audio_owner_check(o);
    }
    if(rc){if(o){if(o->directory>=0)close(o->directory);free(o);}close(fd);return rc;}
    *out=o;return 0;
}
int sony_audio_owner_acquire(struct sony_audio_owner **out,enum sony_audio_owner_kind kind){
    if(!out||*out)return -EINVAL;
    int rc=0;if(rc)return rc;
    int dev=open("/dev",O_RDONLY|O_DIRECTORY|O_CLOEXEC);if(dev<0)return -errno;
    if(mkdirat(dev,"walkdroid-sony-owner",0700)&&errno!=EEXIST){rc=-errno;close(dev);return rc;}
    int dir=openat(dev,"walkdroid-sony-owner",O_RDONLY|O_DIRECTORY|O_CLOEXEC);rc=dir<0?-errno:0;close(dev);
    if(!rc){rc=sony_audio_owner_acquire_at(out,dir,kind);close(dir);}return rc;
}
uint64_t sony_audio_owner_generation(const struct sony_audio_owner *o){return o?o->record.generation:0;}
enum sony_audio_owner_kind sony_audio_owner_kind(const struct sony_audio_owner *o){return o?(enum sony_audio_owner_kind)o->record.kind:0;}
int sony_audio_owner_finish(struct sony_audio_owner **owner,int verified_idle){
    if(!owner||!*owner)return -EINVAL;
    struct sony_audio_owner *o=*owner;
    int rc=sony_audio_owner_check(o);
    if(!rc)rc=sony_audio_owner_retire(o);
    if(!rc&&verified_idle){struct record clean=o->record;clean.active=clean.pid=clean.kind=0;rc=write_record(o->fd,&clean);}
    int close_error=close(o->fd)?-errno:0;close(o->directory);free(o->published_text);free(o);*owner=NULL;return rc?rc:close_error;
}
static void read_free(struct sony_audio_owner_reader *r){
    if(r->fd>=0)close(r->fd);
    if(r->directory>=0)close(r->directory);
    if(r->parent>=0)close(r->parent);
    free(r);
}
static int reader_check(struct sony_audio_owner_reader *r){
    return r?0:-EINVAL;
}
int sony_audio_owner_read_begin_at(struct sony_audio_owner_reader **out,int directory){
    if(!out||*out||directory<0)return -EINVAL;
    int rc=0;
    struct sony_audio_owner_reader *r=calloc(1,sizeof(*r));if(!r)return -ENOMEM;
    r->parent=r->directory=r->fd=-1;r->directory=fcntl(directory,F_DUPFD_CLOEXEC,3);
    if(r->directory<0)rc=-errno;
    if(!rc){
        r->fd=openat(r->directory,"lease",O_RDONLY|O_CLOEXEC|O_NONBLOCK);
        if(r->fd<0&&errno!=ENOENT)rc=-errno;
    }
    if(!rc&&r->fd>=0){
        if(!rc&&flock(r->fd,LOCK_SH|LOCK_NB))rc=errno==EWOULDBLOCK?-EBUSY:-errno;
        if(!rc)rc=read_record(r->fd,&r->record);
    }
    if(!rc)rc=reader_check(r);
    if(rc){read_free(r);return rc;}
    *out=r;return 0;
}
int sony_audio_owner_read_begin(struct sony_audio_owner_reader **out){
    if(!out||*out)return -EINVAL;
    int rc=0;
    int dev=open("/dev",O_RDONLY|O_DIRECTORY|O_CLOEXEC);if(dev<0)return -errno;
    int dir=openat(dev,"walkdroid-sony-owner",O_RDONLY|O_DIRECTORY|O_CLOEXEC);
    if(dir<0){
        if(errno!=ENOENT){rc=-errno;close(dev);return rc;}
        struct sony_audio_owner_reader *r=calloc(1,sizeof(*r));if(!r){close(dev);return -ENOMEM;}
        r->parent=dev;r->directory=r->fd=-1;*out=r;return 0;
    }
    rc=sony_audio_owner_read_begin_at(out,dir);close(dir);
    if(!rc){(*out)->parent=dev;rc=reader_check(*out);if(rc){read_free(*out);*out=NULL;}}
    else close(dev);
    return rc;
}
int sony_audio_owner_read_end(struct sony_audio_owner_reader **reader){
    if(!reader||!*reader)return -EINVAL;
    struct sony_audio_owner_reader *r=*reader;
    int rc=reader_check(r);read_free(r);*reader=NULL;return rc;
}
static int boottime(uint64_t *out){
    struct timespec t;if(clock_gettime(CLOCK_BOOTTIME,&t))return -errno;
    if(t.tv_sec<0||t.tv_nsec<0||t.tv_nsec>=1000000000)return -EIO;
    *out=(uint64_t)t.tv_sec*1000000000u+(unsigned)t.tv_nsec;return 0;
}
static int text_ok(const char *text,size_t bytes){
    if(!bytes||bytes>SONY_OWNER_STATE_TEXT_BYTES||text[bytes-1])return -EINVAL;
    return 0;
}
int sony_audio_owner_publish(struct sony_audio_owner *o,const char *text){
    if(!o||!text)return -EINVAL;
    int rc=sony_audio_owner_check(o);if(rc)return rc;
    size_t bytes=strnlen(text,SONY_OWNER_STATE_TEXT_BYTES)+1;rc=text_ok(text,bytes);if(rc)return rc;
    uint64_t now=0;boottime(&now);
    if(o->published_text){
        if(now&&now>=o->publication.time&&!strcmp(text,o->published_text)&&now-o->publication.time<250000000u)return 0;
    }
    char *copy=strdup(text);if(!copy)return -ENOMEM;
    struct publication pub={o->record,0,o->publication.sequence+1,now,(uint32_t)bytes,0};
    char name[96];int fd;
    do {
        snprintf(name,sizeof(name),".state.%u.%llu",(unsigned)o->pid,(unsigned long long)pub.sequence);
        fd=openat(o->directory,name,O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC,0600);
        if(fd<0&&errno==EEXIST)++pub.sequence;else break;
    }while(1);
    if(fd<0){free(copy);return -errno;}
    unsigned char data[sizeof(pub)+SONY_OWNER_STATE_TEXT_BYTES];memcpy(data,&pub,sizeof(pub));memcpy(data+sizeof(pub),text,bytes);
    size_t total=sizeof(pub)+bytes,at=0;
    while(at<total){ssize_t n=write(fd,data+at,total-at);if(n<0&&errno==EINTR)continue;if(n<=0){rc=n<0?-errno:-EIO;break;}at+=(size_t)n;}
    if(!rc&&fsync(fd))rc=-errno;
    if(close(fd)&&!rc)rc=-errno;
    if(!rc)rc=sony_audio_owner_check(o);
    if(!rc&&renameat(o->directory,name,o->directory,"state"))rc=-errno;
    if(!rc){
        free(o->published_text);o->published_text=copy;copy=NULL;o->publication=pub;
        if(fsync(o->directory))rc=-errno;
    }else unlinkat(o->directory,name,0); /* Only this invocation's O_EXCL file. */
    free(copy);return rc;
}
int sony_audio_owner_retire(struct sony_audio_owner *o){
    int rc=sony_audio_owner_check(o);if(rc)return rc;
    if(!o->published_text)return 0;
    if(unlinkat(o->directory,"state",0)&&errno!=ENOENT)return -errno;
    free(o->published_text);o->published_text=NULL;
    return fsync(o->directory)?-errno:0;
}
int sony_audio_owner_observe_at(int directory,char *text,size_t capacity,struct sony_audio_owner_observation *out){
    if(text&&capacity)text[0]=0;
    if(out)memset(out,0,sizeof(*out));
    if(directory<0||!text||!capacity||!out)return -EINVAL;
    int rc=0;
    int lease=openat(directory,"lease",O_RDONLY|O_CLOEXEC|O_NONBLOCK);if(lease<0)return -errno;
    struct stat state_stat;struct record record;
    rc=read_record(lease,&record);
    int state=-1;if(!rc){state=openat(directory,"state",O_RDONLY|O_CLOEXEC|O_NONBLOCK);if(state<0)rc=-errno;}
    if(!rc)rc=file_stat(state,&state_stat);
    unsigned char data[sizeof(struct publication)+SONY_OWNER_STATE_TEXT_BYTES];struct publication pub={0};
    if(!rc&&(state_stat.st_size<=(off_t)sizeof(pub)||state_stat.st_size>(off_t)sizeof(data)))rc=-EPROTO;
    if(!rc){
        size_t at=0,total=(size_t)state_stat.st_size;
        while(at<total){ssize_t n=pread(state,data+at,total-at,at);if(n<0&&errno==EINTR)continue;if(n<=0){rc=n<0?-errno:-EIO;break;}at+=(size_t)n;}
        if(!rc){memcpy(&pub,data,sizeof(pub));
            if(pub.bytes>total-sizeof(pub))rc=-EPROTO;
            else rc=text_ok((char *)data+sizeof(pub),pub.bytes);}
    }
    if(!rc&&pub.bytes>capacity)rc=-ENOSPC;
    if(!rc){memcpy(text,data+sizeof(pub),pub.bytes);*out=(struct sony_audio_owner_observation){record.generation,pub.sequence,record.kind};}
    if(state>=0)close(state);
    close(lease);return rc;
}
int sony_audio_owner_observe(char *text,size_t capacity,struct sony_audio_owner_observation *out){
    if(text&&capacity)text[0]=0;
    if(out)memset(out,0,sizeof(*out));
    if(!text||!capacity||!out)return -EINVAL;
    int rc=0;
    int dev=open("/dev",O_RDONLY|O_DIRECTORY|O_CLOEXEC);if(dev<0)return -errno;
    int dir=-1;if(!rc){dir=openat(dev,"walkdroid-sony-owner",O_RDONLY|O_DIRECTORY|O_CLOEXEC);if(dir<0)rc=-errno;}
    if(!rc)rc=sony_audio_owner_observe_at(dir,text,capacity,out);
    if(dir>=0)close(dir);
    close(dev);if(rc){text[0]=0;memset(out,0,sizeof(*out));}return rc;
}
static int channel_address(const struct record *r,const struct stat *lease,uint64_t start,struct sockaddr_un *a,socklen_t *bytes){
    memset(a,0,sizeof(*a));a->sun_family=AF_UNIX;
    int n=snprintf(a->sun_path+1,sizeof(a->sun_path)-1,"walkdroid.sony.%llx.%llx.%x.%llx.%llx",
        (unsigned long long)lease->st_dev,(unsigned long long)lease->st_ino,r->pid,
        (unsigned long long)start,(unsigned long long)r->generation);
    if(n<0||(size_t)n>=sizeof(a->sun_path)-1)return -ENAMETOOLONG;
    *bytes=(socklen_t)(offsetof(struct sockaddr_un,sun_path)+1+(size_t)n);return 0;
}
int sony_audio_owner_channel_listen(struct sony_audio_owner *o,int *out){
    if(!out)return -EINVAL;
    int rc=sony_audio_owner_check(o);if(rc)return rc;
    struct stat s;uint64_t start;struct sockaddr_un address;socklen_t bytes;
    start=0;rc=file_stat(o->fd,&s);
    if(!rc)rc=channel_address(&o->record,&s,start,&address,&bytes);
    if(rc)return rc;
    int fd=socket(AF_UNIX,SOCK_SEQPACKET|SOCK_NONBLOCK|SOCK_CLOEXEC,0);if(fd<0)return -errno;
    if(bind(fd,(struct sockaddr *)&address,bytes)||listen(fd,1))rc=-errno;
    if(!rc)rc=sony_audio_owner_check(o);
    if(rc)close(fd);else *out=fd;return rc;
}
int sony_audio_owner_channel_connect_at(int directory,uint64_t generation,enum sony_audio_owner_kind kind,int *out){
    if(directory<0||!out)return -EINVAL;
    (void)kind;
    int rc=0;
    int lease=openat(directory,"lease",O_RDONLY|O_CLOEXEC|O_NONBLOCK);if(lease<0)return -errno;
    struct stat s;struct record r;uint64_t start=0;struct sockaddr_un address;socklen_t bytes;
    rc=file_stat(lease,&s);if(!rc)rc=read_record(lease,&r);
    r.generation=generation;
    if(!rc)rc=channel_address(&r,&s,start,&address,&bytes);
    int fd=-1;if(!rc){fd=socket(AF_UNIX,SOCK_SEQPACKET|SOCK_NONBLOCK|SOCK_CLOEXEC,0);if(fd<0)rc=-errno;}
    if(!rc&&connect(fd,(struct sockaddr *)&address,bytes))rc=-errno;
    close(lease);if(rc){if(fd>=0)close(fd);}else *out=fd;return rc;
}
int sony_audio_owner_channel_connect(uint64_t generation,enum sony_audio_owner_kind kind,int *out){
    if(!out)return -EINVAL;
    int dev=open("/dev",O_RDONLY|O_DIRECTORY|O_CLOEXEC);if(dev<0)return -errno;
    int rc=0;
    int dir=-1,fd=-1;if(!rc){dir=openat(dev,"walkdroid-sony-owner",O_RDONLY|O_DIRECTORY|O_CLOEXEC);if(dir<0)rc=-errno;}
    if(!rc)rc=sony_audio_owner_channel_connect_at(dir,generation,kind,&fd);
    if(dir>=0)close(dir);
    close(dev);if(rc){if(fd>=0)close(fd);}else *out=fd;return rc;
}
