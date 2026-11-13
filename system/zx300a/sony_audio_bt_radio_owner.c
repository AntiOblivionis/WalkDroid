#define _GNU_SOURCE
#include "sony_audio_bt_radio_owner.h"
#include "sony_audio_owner.h"
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
struct sony_bt_radio_owner {struct sony_audio_owner *inner;int parent,directory;};
int sony_bt_radio_check(struct sony_bt_radio_owner *o){
    if(!o)return -EINVAL;
    return sony_audio_owner_check(o->inner);
}
int sony_bt_radio_acquire_at(struct sony_bt_radio_owner **out,int parent,const char *name,enum sony_bt_radio_kind kind){
    if(!out||*out||parent<0||!name)return -EINVAL;
    int rc=0;
    struct sony_bt_radio_owner *o=calloc(1,sizeof(*o));if(!o)return -ENOMEM;o->parent=o->directory=-1;
    o->parent=fcntl(parent,F_DUPFD_CLOEXEC,3);if(o->parent<0)rc=-errno;
    if(!rc&&mkdirat(o->parent,name,0700)&&errno!=EEXIST)rc=-errno;
    if(!rc){o->directory=openat(o->parent,name,O_RDONLY|O_DIRECTORY|O_CLOEXEC);if(o->directory<0)rc=-errno;}
    if(!rc)rc=sony_audio_owner_acquire_at(&o->inner,o->directory,(enum sony_audio_owner_kind)kind);
    if(!rc)rc=sony_bt_radio_check(o);
    if(rc){if(o->inner)sony_audio_owner_finish(&o->inner,0);if(o->directory>=0)close(o->directory);if(o->parent>=0)close(o->parent);free(o);return rc;}
    *out=o;return 0;
}
int sony_bt_radio_acquire(struct sony_bt_radio_owner **out,enum sony_bt_radio_kind kind){
    if(!out||*out)return -EINVAL;int rc=0;if(rc)return rc;
    int dev=open("/dev",O_RDONLY|O_DIRECTORY|O_CLOEXEC);if(dev<0)return -errno;
    rc=sony_bt_radio_acquire_at(out,dev,"walkdroid-bt-radio-owner",kind);int saved=errno;close(dev);errno=saved;return rc;
}
uint64_t sony_bt_radio_generation(const struct sony_bt_radio_owner *o){return o?sony_audio_owner_generation(o->inner):0;}
int sony_bt_radio_finish(struct sony_bt_radio_owner **owned,int verified_off){
    if(!owned||!*owned)return -EINVAL;
    struct sony_bt_radio_owner *o=*owned;
    int rc=sony_bt_radio_check(o),finish=sony_audio_owner_finish(&o->inner,rc?0:verified_off);if(!rc)rc=finish;
    close(o->directory);close(o->parent);free(o);*owned=NULL;return rc;
}
