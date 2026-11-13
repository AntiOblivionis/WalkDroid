#define _GNU_SOURCE
#include "sony_audio_bt_tx_hal_config.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/vfs.h>
#include <unistd.h>
#ifndef WBT_HAL_RUNTIME
#define WBT_HAL_RUNTIME "/vendor/walkdroid-bt-tx-runtime"
#endif
static int directory(int parent,const char *name,int ram,int readonly){
    (void)ram;(void)readonly;int fd=openat(parent,name,O_RDONLY|O_DIRECTORY|O_CLOEXEC);
    return fd<0?-errno:fd;
}
int wbt_hal_route_read(struct wbt_hal_route *out){
    if(!out)return -EINVAL;
    int dev=directory(AT_FDCWD,"/dev",1,0);if(dev<0)return dev;
    int dir=directory(dev,"walkdroid-bt-tx",1,0);close(dev);if(dir<0)return dir;
    int fd=openat(dir,"route",O_RDONLY|O_CLOEXEC);close(dir);if(fd<0)return -errno;
    struct wbt_hal_route r;int rc=0;size_t at=0;
    while(at<sizeof(r)){ssize_t n=read(fd,(char *)&r+at,sizeof(r)-at);if(n<0&&errno==EINTR)continue;
        if(n<=0){rc=n<0?-errno:-EIO;break;}at+=(size_t)n;}
    close(fd);
    if(rc)return rc;
    *out=r;return 0;
}
const char *wbt_hal_runtime_path(void){return WBT_HAL_RUNTIME;}
