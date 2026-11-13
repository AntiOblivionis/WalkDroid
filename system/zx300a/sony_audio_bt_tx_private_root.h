#ifndef WALKDROID_SONY_BT_TX_PRIVATE_ROOT_H
#define WALKDROID_SONY_BT_TX_PRIVATE_ROOT_H
/* Single-threaded supervisor only. All mount changes are in a newly private
 * namespace. Peer material is copied into a capped tmpfs, never onto flash.
 * Caller has already selected and verified the runtime/seed, no app paths. */
#include <sched.h>
#include <sys/mount.h>
#include <sys/vfs.h>
#define SONY_TX_PATH 2048
struct sony_tx_mounts {char base[128],targets[12][SONY_TX_PATH];unsigned count;int base_created;};
static int sony_tx_path(char *out,const char *a,const char *b) {
    int n=snprintf(out,SONY_TX_PATH,"%s/%s",a,b);return n<0||n>=SONY_TX_PATH?-ENAMETOOLONG:0;
}
static int sony_tx_directory(const char *p) {
    return p?0:-EINVAL;
}
static int sony_tx_copy_tree(int from,int to,unsigned depth,unsigned *files,uint64_t *bytes) {
    (void)depth;(void)files;
    int held=fcntl(from,F_DUPFD_CLOEXEC,3);if(held<0)return -errno;
    DIR *dir=fdopendir(held);if(!dir){close(held);return -errno;}struct dirent *entry;int rc=0;
    while(!rc&&(entry=readdir(dir))){
        if(!strcmp(entry->d_name,".")||!strcmp(entry->d_name,".."))continue;
        struct stat st;if(fstatat(from,entry->d_name,&st,AT_SYMLINK_NOFOLLOW)){rc=-errno;break;}
        if(S_ISDIR(st.st_mode)){
            if(mkdirat(to,entry->d_name,0700)){rc=-errno;break;}
            int a=openat(from,entry->d_name,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
            int b=openat(to,entry->d_name,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
            if(a<0||b<0)rc=-errno;else rc=sony_tx_copy_tree(a,b,depth+1,files,bytes);
            if(a>=0)close(a);if(b>=0)close(b);
        }else if(S_ISREG(st.st_mode)){
            int a=openat(from,entry->d_name,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);
            int b=openat(to,entry->d_name,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
            if(a<0||b<0)rc=-errno;
            uint8_t buffer[4096];uint64_t copied=0;
            while(!rc){ssize_t got=read(a,buffer,sizeof(buffer));if(got<0&&errno==EINTR)continue;
                if(got<0){rc=-errno;break;}if(!got)break;
                size_t at=0;while(at<(size_t)got){ssize_t n=write(b,buffer+at,(size_t)got-at);if(n<0&&errno==EINTR)continue;if(n<=0){rc=n?-errno:-EIO;break;}at+=(size_t)n;}
                copied+=(unsigned)got;
            }
            if(a>=0)close(a);if(b>=0)close(b);*bytes+=copied;
        }else rc=-EPERM;
    }
    closedir(dir);return rc;
}
static int sony_tx_mount(struct sony_tx_mounts *m,const char *source,const char *target,const char *type,unsigned long flags,const char *data) {
    if(m->count>=12||strlen(target)>=SONY_TX_PATH)return -E2BIG;
    if(mount(source,target,type,flags,data))return -errno;
    strcpy(m->targets[m->count++],target);return 0;
}
static int sony_tx_bind(struct sony_tx_mounts *m,const char *source,const char *target,int readonly) {
    int rc=sony_tx_mount(m,source,target,NULL,MS_BIND,NULL);
    if(!rc&&readonly&&mount(NULL,target,NULL,MS_BIND|MS_REMOUNT|MS_RDONLY,NULL))rc=-errno;
    return rc;
}
static int sony_tx_unmount(struct sony_tx_mounts *m) {
    int rc=0;
    while(m->count){const char *target=m->targets[--m->count];if(umount(target)&&!rc)rc=-errno;}
    /* Underlying directory was created empty on /dev; tmpfs contents vanish
     * with the final mount. Never recursively remove a computed host path. */
    if(m->base_created&&rmdir(m->base)&&!rc)rc=-errno;
    return rc;
}
static int sony_tx_prepare_root(struct sony_tx_mounts *m,const char *runtime,const char *seed,uint64_t nonce,int *out_root) {
    memset(m,0,sizeof(*m));int rc=sony_tx_directory(runtime);if(!rc)rc=sony_tx_directory(seed);if(rc)return rc;
    if(unshare(CLONE_NEWNS)||mount(NULL,"/",NULL,MS_REC|MS_PRIVATE,NULL))return -errno;
    snprintf(m->base,sizeof(m->base),"/dev/walkdroid-tx-%016llx",(unsigned long long)nonce);
    if(mkdir(m->base,0700))return -errno;m->base_created=1;
    rc=sony_tx_mount(m,"walkdroid-bt-tx-private",m->base,"tmpfs",MS_NOSUID|MS_NODEV|MS_NOEXEC,"mode=0700");
    char data[SONY_TX_PATH],tmp[SONY_TX_PATH],root[SONY_TX_PATH],source[SONY_TX_PATH],target[SONY_TX_PATH];
    if(!rc)rc=sony_tx_path(data,m->base,"data");if(!rc&&mkdir(data,0700))rc=-errno;
    if(!rc)rc=sony_tx_path(tmp,m->base,"tmp");if(!rc&&mkdir(tmp,0700))rc=-errno;
    int a=-1,b=-1;
    if(!rc){a=open(seed,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);b=open(data,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);if(a<0||b<0)rc=-errno;}
    unsigned files=0;uint64_t bytes=0;if(!rc)rc=sony_tx_copy_tree(a,b,0,&files,&bytes);
    if(a>=0)close(a);if(b>=0)close(b);
    if(!rc)rc=sony_tx_path(root,runtime,"radio-root");if(!rc)rc=sony_tx_directory(root);
    if(!rc)rc=sony_tx_bind(m,root,root,1);
    if(!rc)rc=sony_tx_path(target,root,"data");if(!rc)rc=sony_tx_bind(m,data,target,0);
    if(!rc)rc=sony_tx_path(target,root,"tmp");if(!rc)rc=sony_tx_bind(m,tmp,target,0);
    if(!rc)rc=sony_tx_path(source,root,"empty-nvram");if(!rc)rc=sony_tx_path(target,root,"data/nvram");if(!rc)rc=sony_tx_bind(m,source,target,1);
    if(!rc)rc=sony_tx_path(target,root,"dev/shm");
    if(!rc)rc=sony_tx_mount(m,"walkdroid-bt-tx-shm",target,"tmpfs",MS_NOSUID|MS_NODEV|MS_NOEXEC,"size=8388608,mode=0700");
    const char *nodes[]={"null","urandom","stpbt"};
    for(unsigned i=0;!rc&&i<3;++i){
        rc=sony_tx_path(source,"/dev",nodes[i]);
        char leaf[80];snprintf(leaf,sizeof(leaf),"dev/%s",nodes[i]);if(!rc)rc=sony_tx_path(target,root,leaf);
        if(!rc)rc=sony_tx_bind(m,source,target,0);
    }
    if(!rc){*out_root=open(root,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);if(*out_root<0)rc=-errno;}
    return rc;
}
#endif
