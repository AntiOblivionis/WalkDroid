#define _GNU_SOURCE
#include "sony_audio_bt_broker_group.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
struct sony_bt_broker_group {int base,parent,group,moved;char name[64];};
static int directory(int at,const char*name){int fd=openat(at,name,O_RDONLY|O_DIRECTORY|O_CLOEXEC);return fd<0?-errno:fd;}
static int move_self(int group){int fd=openat(group,"cgroup.procs",O_WRONLY|O_CLOEXEC);if(fd<0)return -errno;char text[32];int n=snprintf(text,sizeof(text),"%d\n",(int)getpid());ssize_t written=write(fd,text,(size_t)n);int rc=written==n?0:written<0?-errno:-EIO;close(fd);return rc;}
int sony_bt_broker_group_enter(struct sony_bt_broker_group**out){
    if(!out||*out)return -EINVAL;struct sony_bt_broker_group*g=calloc(1,sizeof(*g));if(!g)return -ENOMEM;
    g->base=g->parent=g->group=-1;snprintf(g->name,sizeof(g->name),"walkdroid_bt_link_%d",(int)getpid());
    int rc=directory(AT_FDCWD,"/acct");if(rc<0){free(g);return rc;}g->base=rc;
    g->parent=fcntl(g->base,F_DUPFD_CLOEXEC,3);if(g->parent<0){rc=-errno;close(g->base);free(g);return rc;}
    if(mkdirat(g->parent,g->name,0700)&&errno!=EEXIST){rc=-errno;close(g->parent);close(g->base);free(g);return rc;}
    *out=g;g->group=directory(g->parent,g->name);if(g->group<0)return g->group;
    rc=move_self(g->group);if(!rc)g->moved=1;return rc;
}
int sony_bt_broker_group_leave(struct sony_bt_broker_group**out){
    if(!out||!*out)return -EINVAL;struct sony_bt_broker_group*g=*out;
    int rc=g->moved?move_self(g->base):0;if(rc)return rc;g->moved=0;
    if(unlinkat(g->parent,g->name,AT_REMOVEDIR))return -errno;
    close(g->group);close(g->parent);close(g->base);free(g);*out=NULL;return 0;
}
