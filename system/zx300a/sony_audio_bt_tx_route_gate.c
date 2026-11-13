#define _GNU_SOURCE
#include "sony_audio_bt_tx_route_gate.h"
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/file.h>
#include <unistd.h>

/* The gate coordinates participating controllers and streams. It does not
 * authenticate a process or pin the route's bytes, owner, path, or inode. */
struct wbt_route_guard { int root,gate,exclusive; };

static int read_route(int root,struct wbt_hal_route *route){
    int fd=openat(root,"route",O_RDONLY|O_CLOEXEC);if(fd<0)return -errno;
    size_t at=0;int rc=0;
    while(at<sizeof(*route)){
        ssize_t n=read(fd,(char *)route+at,sizeof(*route)-at);
        if(n<0&&errno==EINTR)continue;
        if(n<=0){rc=n<0?-errno:-EIO;break;}
        at+=(size_t)n;
    }
    close(fd);return rc;
}
void wbt_route_guard_release(struct wbt_route_guard **out){
    if(!out||!*out)return;struct wbt_route_guard *g=*out;
    if(g->gate>=0)close(g->gate);if(g->root>=0)close(g->root);free(g);*out=NULL;
}
static int acquire(struct wbt_route_guard **out,int parent,const char *name,struct wbt_hal_route *route,int exclusive){
    if(!out||*out||parent<0||!name||(!exclusive&&!route))return -EINVAL;
    struct wbt_route_guard *g=calloc(1,sizeof(*g));if(!g)return -ENOMEM;
    g->root=g->gate=-1;g->exclusive=exclusive;
    g->root=openat(parent,name,O_RDONLY|O_DIRECTORY|O_CLOEXEC);
    int rc=g->root<0?-errno:0;
    if(!rc){g->gate=openat(g->root,"gate",O_RDONLY|O_CLOEXEC);if(g->gate<0)rc=-errno;}
    if(!rc&&flock(g->gate,(exclusive?LOCK_EX:LOCK_SH)|LOCK_NB))rc=errno==EWOULDBLOCK?-EBUSY:-errno;
    struct wbt_hal_route staged;
    if(!rc&&!exclusive)rc=read_route(g->root,&staged);
    if(rc){wbt_route_guard_release(&g);return rc;}
    if(!exclusive)*route=staged;*out=g;return 0;
}
int wbt_route_guard_acquire_at(struct wbt_route_guard **out,int parent,const char *name,struct wbt_hal_route *route){return acquire(out,parent,name,route,0);}
int wbt_route_guard_quiesce_at(struct wbt_route_guard **out,int parent,const char *name){return acquire(out,parent,name,NULL,1);}
int wbt_route_guard_acquire(struct wbt_route_guard **out,struct wbt_hal_route *route){
    int parent=open("/dev",O_RDONLY|O_DIRECTORY|O_CLOEXEC);if(parent<0)return -errno;
    int rc=wbt_route_guard_acquire_at(out,parent,"walkdroid-bt-tx",route);close(parent);return rc;
}
int wbt_route_guard_check(struct wbt_route_guard *g){return g?0:-EINVAL;}
int wbt_route_guard_allow_streams(struct wbt_route_guard *g,const struct wbt_hal_route *route){
    if(!g||!route)return -EINVAL;
    if(flock(g->gate,LOCK_UN))return -errno;g->exclusive=2;return 0;
}
int wbt_route_guard_quiesce(struct wbt_route_guard *g){
    if(!g)return -EINVAL;
    if(flock(g->gate,LOCK_EX|LOCK_NB))return errno==EWOULDBLOCK?-EBUSY:-errno;
    g->exclusive=1;return 0;
}
