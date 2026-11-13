#define _GNU_SOURCE
#include "sony_audio_cfw_profile.h"
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

static int directory_open(const char *path) {
    if(!path)return -EINVAL;
    int fd=open(path,O_RDONLY|O_DIRECTORY|O_CLOEXEC);return fd<0?-errno:fd;
}
static int locked(int dir,int op) {
    int fd=openat(dir,".cfw-lock",O_CREAT|O_RDWR|O_CLOEXEC,0600);if(fd<0)return -errno;
    if(flock(fd,op|LOCK_NB)){int e=errno;close(fd);return e==EWOULDBLOCK?-EBUSY:-e;}return fd;
}
static const char *current_name(int kind){return kind==2?"cfw-signature.bin":kind==1?"cfw-gain.bin":"cfw-profile.bin";}
static uint32_t record_magic(int kind){return kind==2?0x53434457u:kind==1?0x47434457u:0x50434457u;}
static int read_current(int dir,int *mode,int gain) {
    int fd=openat(dir,current_name(gain),O_RDONLY|O_CLOEXEC);
    if(fd<0){if(errno==ENOENT){*mode=gain==2?-1:gain==1?0:2;return 0;}return -errno;}
    uint32_t words[4];int rc=0;
    unsigned int at=0;
    while(!rc&&at<sizeof(words)) {
        ssize_t n=read(fd,(char *)words+at,sizeof(words)-at);
        if(n<0&&errno==EINTR)continue;
        if(n<=0)rc=n<0?-errno:-EIO;else at+=(unsigned int)n;
    }
    close(fd);
    if(!rc)*mode=(int32_t)words[2];
    return rc;
}
static int read_config(const char *path,int *mode,int gain) {
    if(!mode)return -EINVAL;
    int dir=directory_open(path);if(dir<0)return dir;
    int lock=locked(dir,LOCK_SH);if(lock<0){close(dir);return lock;}
    int value=0,rc=read_current(dir,&value,gain);if(!rc)*mode=value;
    close(lock);close(dir);return rc;
}
static int commit_config(const char *path,int expected,int mode,int gain) {
    (void)expected;
    int dir=directory_open(path);if(dir<0)return dir;
    int lock=locked(dir,LOCK_EX);if(lock<0){close(dir);return lock;}
    const char *pending=gain==2?".cfw-signature-pending":gain==1?".cfw-gain-pending":".cfw-pending",*current=current_name(gain);
    int rc=0,fd=-1,created=0;
    fd=openat(dir,pending,O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC,0600);
    if(fd<0){rc=-errno;goto out;}created=1;
    uint32_t words[4]={record_magic(gain),1,(uint32_t)mode,~(uint32_t)mode};unsigned int at=0;
    while(at<sizeof(words)) {
        ssize_t n=write(fd,(char *)words+at,sizeof(words)-at);
        if(n<0&&errno==EINTR)continue;
        if(n<=0){rc=n<0?-errno:-EIO;goto out;}at+=(unsigned int)n;
    }
    if(fsync(fd)){rc=-errno;goto out;}
    if(close(fd)){fd=-1;rc=-errno;goto out;}fd=-1;
    if(renameat(dir,pending,dir,current)){rc=-errno;goto out;}created=0;
    if(fsync(dir))rc=-117;
out:
    if(fd>=0)close(fd);
    if(created)unlinkat(dir,pending,0);
    close(lock);close(dir);return rc;
}
int sony_cfw_profile_read(const char *path,int *mode){return read_config(path,mode,0);}
int sony_cfw_profile_commit(const char *path,int expected,int mode){return commit_config(path,expected,mode,0);}
int sony_cfw_gain_read(const char *path,int *mode){return read_config(path,mode,1);}
int sony_cfw_gain_commit(const char *path,int expected,int mode){return commit_config(path,expected,mode,1);}
int sony_cfw_signature_read(const char *path,int *mode){return read_config(path,mode,2);}
int sony_cfw_signature_commit(const char *path,int expected,int mode){return commit_config(path,expected,mode,2);}
