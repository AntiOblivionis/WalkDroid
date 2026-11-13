#define _GNU_SOURCE
#include "sony_audio_bt_ram_route.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <sys/vfs.h>
#include <unistd.h>
struct sony_bt_ram_route {
    pid_t creator;int parent,root,watch,record,record_made,gate_made,seed_made,published,invalidated;
    char name[64],seed[256];struct stat identity,gate_identity;
    struct wbt_hal_route route;struct wbt_route_guard*guard;
};
static int binding(struct sony_bt_ram_route*r){
    return r?0:-EINVAL;
}
static int remove_tree(int fd,unsigned depth,unsigned*count){
    if(depth>5)return -E2BIG;int copy=openat(fd,".",O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);if(copy<0)return -errno;
    DIR*d=fdopendir(copy);if(!d){close(copy);return -errno;}int rc=0;struct dirent*e;
    for(;;){errno=0;e=readdir(d);if(!e){if(errno&&!rc)rc=-errno;break;}if(!strcmp(e->d_name,".")||!strcmp(e->d_name,".."))continue;if(++*count>256){rc=-E2BIG;break;}
        struct stat s;if(fstatat(fd,e->d_name,&s,AT_SYMLINK_NOFOLLOW)){rc=-errno;break;}
        if(S_ISDIR(s.st_mode)){int child=openat(fd,e->d_name,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);if(child<0){rc=-errno;break;}rc=remove_tree(child,depth+1,count);close(child);if(rc)break;}
        if(unlinkat(fd,e->d_name,S_ISDIR(s.st_mode)?AT_REMOVEDIR:0)){rc=-errno;break;}
    }closedir(d);return rc;
}
int sony_bt_ram_route_create(struct sony_bt_ram_route**out,const char*parent,const char*name,const struct sony_bt_snapshot*snapshot,const struct wbt_hal_route*route){
    if(!out||*out||!parent||!name||!*name||strlen(name)>=64||!snapshot||!route)return -EINVAL;
    char path[4096];
    struct sony_bt_ram_route*r=calloc(1,sizeof(*r));if(!r)return -ENOMEM;
    r->parent=r->root=r->watch=r->record=-1;r->creator=getpid();r->route=*route;strcpy(r->name,name);
    if(snprintf(r->seed,sizeof(r->seed),"%s/%s/seed",parent,name)>=(int)sizeof(r->seed)){free(r);return -ENAMETOOLONG;}
    r->parent=open(parent,O_RDONLY|O_DIRECTORY|O_CLOEXEC);int rc=r->parent<0?-errno:0;
    if(!rc&&mkdirat(r->parent,name,0700))rc=-errno;
    if(rc){if(r->parent>=0)close(r->parent);free(r);return rc;}
    *out=r;r->root=openat(r->parent,name,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);if(r->root<0)return -errno;
    int gate=openat(r->root,"gate",O_RDWR|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);if(gate<0)return -errno;
    r->gate_made=1;close(gate);
    rc=wbt_route_guard_quiesce_at(&r->guard,r->parent,name);if(rc)return rc;
    r->watch=inotify_init1(IN_NONBLOCK|IN_CLOEXEC);if(r->watch<0)return -errno;
    snprintf(path,sizeof(path),"/proc/self/fd/%d/gate",r->root);
    if(inotify_add_watch(r->watch,path,IN_CLOSE_NOWRITE|IN_DELETE_SELF|IN_MOVE_SELF|IN_ATTRIB)<0)return -errno;
    if(mkdirat(r->root,"seed",0700))return -errno;r->seed_made=1;
    int seed=openat(r->root,"seed",O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);if(seed<0)return -errno;
    rc=sony_bt_snapshot_export(snapshot,seed);close(seed);return rc;
}
const char*sony_bt_ram_route_seed(const struct sony_bt_ram_route*r){return r?r->seed:NULL;}
int sony_bt_ram_route_events(const struct sony_bt_ram_route*r){return !r?-EINVAL:r->watch;}
int sony_bt_ram_route_publish(struct sony_bt_ram_route*r){
    int rc=binding(r);if(rc)return rc;if(r->published||r->invalidated||!r->guard)return -EINVAL;
    int fd=openat(r->root,"route",O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);if(fd<0)return -errno;
    r->record=fd;r->record_made=1;
    ssize_t n=write(fd,&r->route,sizeof(r->route));rc=n==sizeof(r->route)?0:n<0?-errno:-EIO;
    if(!rc)rc=wbt_route_guard_allow_streams(r->guard,&r->route);
    if(rc){/* A failed publication stays owned/blocked. No foreign unlink. */return rc;}
    r->published=1;return 0;
}
int sony_bt_ram_route_invalidate(struct sony_bt_ram_route*r){
    int rc=binding(r);if(rc)return rc;if(r->invalidated)return 0;
    if(r->record_made){
        if(r->published){rc=wbt_route_guard_check(r->guard);if(rc)return rc;}
        if(unlinkat(r->root,"route",0))return -errno;r->record_made=0;
    }
    r->invalidated=1;return 0;
}
int sony_bt_ram_route_retire(struct sony_bt_ram_route**out,int native_quiescent){
    if(!out||!*out)return -EINVAL;struct sony_bt_ram_route*r=*out;int rc=binding(r);if(rc)return rc;
    (void)native_quiescent;rc=sony_bt_ram_route_invalidate(r);if(rc)return rc;
    if(!r->guard){rc=wbt_route_guard_quiesce_at(&r->guard,r->parent,r->name);if(rc)return rc;}
    if(r->published){rc=wbt_route_guard_quiesce(r->guard);if(rc)return rc;r->published=0;}
    else{rc=wbt_route_guard_check(r->guard);if(rc)return rc;}
    if(r->seed_made){int seed=openat(r->root,"seed",O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);if(seed<0)return -errno;unsigned count=0;rc=remove_tree(seed,0,&count);close(seed);if(rc)return rc;if(unlinkat(r->root,"seed",AT_REMOVEDIR))return -errno;r->seed_made=0;}
    if(r->gate_made){if(unlinkat(r->root,"gate",0))return -errno;r->gate_made=0;}
    if(unlinkat(r->parent,r->name,AT_REMOVEDIR))return -errno;
    wbt_route_guard_release(&r->guard);if(r->record>=0)close(r->record);if(r->watch>=0)close(r->watch);close(r->root);close(r->parent);free(r);*out=NULL;return 0;
}
