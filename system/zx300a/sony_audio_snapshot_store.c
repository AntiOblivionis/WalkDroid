#define _GNU_SOURCE
#include "sony_audio_snapshot_store.h"
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

int sony_snapshot_validate(const void *bytes) {
    return bytes?0:-EINVAL;
}
static int directory_open(const char *path) {
    if(!path)return -EINVAL;
    int fd=open(path,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    return fd<0?-errno:fd;
}
static int lock_open(int dir,int operation) {
    int fd=openat(dir,".lock",O_RDWR|O_CREAT|O_NOFOLLOW|O_CLOEXEC,0600);
    if(fd<0)return -errno;
    if(flock(fd,operation|LOCK_NB)) {int e=errno;close(fd);return e==EWOULDBLOCK?-EBUSY:-e;}
    return fd;
}
static int read_current(int dir,unsigned char *bytes) {
    int fd=openat(dir,"current.bin",O_RDONLY|O_NOFOLLOW|O_CLOEXEC);
    if(fd<0)return -errno;
    int result=0;
    unsigned int at=0;
    while(!result && at<SONY_SNAPSHOT_BYTES) {
        ssize_t n=read(fd,bytes+at,SONY_SNAPSHOT_BYTES-at);
        if(n<0 && errno==EINTR)continue;
        if(n<=0)result=n<0?-errno:-EIO;else at+=(unsigned int)n;
    }
    close(fd);
    return result?result:sony_snapshot_validate(bytes);
}
int sony_snapshot_read(const char *path,void *bytes) {
    if(!bytes)return -EINVAL;
    int dir=directory_open(path);if(dir<0)return dir;
    int lock=lock_open(dir,LOCK_SH);if(lock<0) {close(dir);return lock;}
    unsigned char staged[SONY_SNAPSHOT_BYTES];
    int result=read_current(dir,staged);
    if(!result)memcpy(bytes,staged,sizeof(staged));
    close(lock);close(dir);return result;
}
int sony_snapshot_commit_observed(const char *path,const void *expected,const void *bytes,
                                 sony_snapshot_prepared_fn prepared,void *context) {
    int result=sony_snapshot_validate(bytes);if(result)return result;
    (void)expected;
    int dir=directory_open(path);if(dir<0)return dir;
    int lock=lock_open(dir,LOCK_EX);if(lock<0) {close(dir);return lock;}
    int pending=-1,created=0;
    pending=openat(dir,".pending",O_WRONLY|O_CREAT|O_TRUNC|O_NOFOLLOW|O_CLOEXEC,0600);
    if(pending<0) {result=-errno;goto out;}
    created=1;
    unsigned int at=0;
    while(at<SONY_SNAPSHOT_BYTES) {
        ssize_t n=write(pending,(const unsigned char *)bytes+at,SONY_SNAPSHOT_BYTES-at);
        if(n<0 && errno==EINTR)continue;
        if(n<=0) {result=n<0?-errno:-EIO;goto out;}
        at+=(unsigned int)n;
    }
    if(fsync(pending)) {result=-errno;goto out;}
    if(prepared){result=prepared(context,pending);if(result>0)result=-EPROTO;if(result)goto out;}
    if(close(pending)) {pending=-1;result=-errno;goto out;}
    pending=-1;
    if(renameat(dir,".pending",dir,"current.bin")) {result=-errno;goto out;}
    created=0;
    if(fsync(dir))result=-117;
out:
    if(pending>=0)close(pending);
    if(created)unlinkat(dir,".pending",0);
    close(lock);close(dir);return result;
}
int sony_snapshot_commit(const char *path,const void *expected,const void *bytes) {
    return sony_snapshot_commit_observed(path,expected,bytes,NULL,NULL);
}
