/* Bluetooth metadata/selection daemon. No RF, audio or arbitrary app paths.
 * One-time migration copies existing native RX data, never a PC backup. */
#define _GNU_SOURCE
#include "sony_audio_bt_catalog.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/mount.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>
#ifndef SONY_BT_CATALOG_STORE
#define SONY_BT_CATALOG_STORE "/data/sony-bt-pair-store-v1"
#endif
#ifndef SONY_BT_CATALOG_SOURCE
#define SONY_BT_CATALOG_SOURCE "/data/walkdroid/bt-receiver/peer-template"
#endif
#ifndef SONY_BT_CATALOG_SOCKET
#define SONY_BT_CATALOG_SOCKET "walkdroid.sony.bt.catalog.v1"
#endif
#define STORE SONY_BT_CATALOG_STORE
#define SOURCE SONY_BT_CATALOG_SOURCE
#define SOCKET SONY_BT_CATALOG_SOCKET
static volatile sig_atomic_t stopping;
static void stop(int signal){(void)signal;stopping=1;}
static int64_t now(void){struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t))return -1;return (int64_t)t.tv_sec*1000+t.tv_nsec/1000000;}
static int exact_dir(const char *path){int fd=open(path,O_RDONLY|O_DIRECTORY|O_CLOEXEC);return fd<0?-errno:fd;}
static int random_bytes(unsigned char *out,unsigned bytes){int fd=open("/dev/urandom",O_RDONLY|O_NOFOLLOW|O_CLOEXEC);if(fd<0)return -errno;unsigned at=0;int rc=0;
    while(at<bytes){ssize_t n=read(fd,out+at,bytes-at);if(n<0&&errno==EINTR)continue;if(n<=0){rc=n?-errno:-EIO;break;}at+=(unsigned)n;}close(fd);return rc;}
static int factory(unsigned char out[64]){int fd=open("/dev/walkdroid-BT_Addr",O_RDONLY|O_CLOEXEC);if(fd<0)return -errno;
    int rc=read(fd,out,64)==64?0:-EIO;close(fd);return rc;}
static int copy_tree(int source,int dest,unsigned depth,unsigned *files,unsigned *bytes){
    if(depth>4)return -E2BIG;
    int copy=openat(source,".",O_RDONLY|O_DIRECTORY|O_CLOEXEC);if(copy<0)return -errno;DIR *stream=fdopendir(copy);if(!stream){close(copy);return -errno;}int rc=0;struct dirent *e;
    for(;;){errno=0;e=readdir(stream);if(!e){if(errno&&!rc)rc=-errno;break;}if(rc||stopping){if(!rc)rc=-ECANCELED;break;}if(!strcmp(e->d_name,".")||!strcmp(e->d_name,".."))continue;
        struct stat path;if(fstatat(source,e->d_name,&path,0)){rc=-errno;break;}
        int is_dir=S_ISDIR(path.st_mode);if(!is_dir&&!S_ISREG(path.st_mode))continue;
        int from=openat(source,e->d_name,O_RDONLY|O_NONBLOCK|O_CLOEXEC|(is_dir?O_DIRECTORY:0)),to=-1;if(from<0){rc=-errno;break;}
        if(!rc&&is_dir){if(mkdirat(dest,e->d_name,0700))rc=-errno;else{to=openat(dest,e->d_name,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);if(to<0)rc=-errno;else rc=copy_tree(from,to,depth+1,files,bytes);}}
        else if(!rc){if(path.st_size<0||path.st_size>65536||++*files>128||(unsigned)path.st_size>524288-*bytes)rc=-EFBIG;
            if(!rc){to=openat(dest,e->d_name,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);if(to<0)rc=-errno;}
            unsigned total=0;unsigned char buffer[4096];
            while(!rc&&total<(unsigned)path.st_size){size_t wanted=(unsigned)path.st_size-total;if(wanted>sizeof(buffer))wanted=sizeof(buffer);ssize_t n=read(from,buffer,wanted);if(n<0&&errno==EINTR)continue;if(n<=0){rc=n?-errno:-ESTALE;break;}
                size_t at=0;while(at<(size_t)n){ssize_t done=write(to,buffer+at,(size_t)n-at);if(done<0&&errno==EINTR)continue;if(done<=0){rc=done?-errno:-EIO;break;}at+=(size_t)done;}total+=(unsigned)n;}
            volatile unsigned char *v=buffer;for(unsigned i=0;i<sizeof(buffer);++i)v[i]=0;*bytes+=total;}
        if(to>=0)close(to);close(from);
    }
    closedir(stream);return rc;
}
static int migrate(struct sony_bt_store *store,const unsigned char nonce[16]){
    struct sony_bt_snapshot *snapshot=NULL;int rc=sony_bt_store_reconcile(store,&snapshot);uint64_t generation=sony_bt_snapshot_generation(snapshot);sony_bt_snapshot_close(&snapshot);
    if(rc||generation)return rc; /* Never replace any existing/damaged store. */
    int source=open(SOURCE,O_RDONLY|O_DIRECTORY|O_CLOEXEC);if(source<0)return -errno;
    char ram[128];snprintf(ram,sizeof(ram),"/dev/sony-bt-store-init-%d",(int)getpid());int made=0,mounted=0,dest=-1;
    if(mkdir(ram,0700))rc=-errno;else made=1;
    if(!rc){if(mount("sony-bt-store-init",ram,"tmpfs",MS_NOSUID|MS_NODEV|MS_NOEXEC,"size=2097152,mode=0700"))rc=-errno;else mounted=1;}
    if(!rc){dest=exact_dir(ram);if(dest<0)rc=dest;}
    unsigned files=0,bytes=0;if(!rc)rc=copy_tree(source,dest,0,&files,&bytes);close(source);
    if(!rc){enum sony_bt_store_publication publication;rc=sony_bt_store_commit(store,dest,0,nonce,&publication,&snapshot);sony_bt_snapshot_close(&snapshot);}
    if(dest>=0)close(dest);if(mounted&&umount(ram)&&!rc)rc=-errno;if(made&&rmdir(ram)&&!rc)rc=-errno;
    return rc;
}
int main(int argc,char **argv){
    if(argc!=2||strcmp(argv[1],"--catalog-only"))return 64;
    struct rlimit core={0,0};(void)setrlimit(RLIMIT_CORE,&core);
    umask(077);signal(SIGTERM,stop);signal(SIGINT,stop);signal(SIGHUP,stop);signal(SIGPIPE,SIG_IGN);
    if(mkdir(STORE,0700)&&errno!=EEXIST)return 66;int root=exact_dir(STORE);if(root<0)return 66;
    int gate=openat(root,"service",O_RDWR|O_CREAT|O_CLOEXEC,0600);
    if(gate<0||flock(gate,LOCK_EX|LOCK_NB)){if(gate>=0)close(gate);close(root);return 67;}
    const char *phase="factory";unsigned char config[64],instance[16];int rc=factory(config);if(!rc){phase="random";rc=random_bytes(instance,16);}struct sony_bt_store *store=NULL;
    if(!rc){phase="store-open";rc=sony_bt_store_open(&store,root,config);}if(!rc){phase="migration";rc=migrate(store,instance);}memset(config,0,sizeof(config));
    int server=-1;if(!rc){phase="socket";server=socket(AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC|SOCK_NONBLOCK,0);if(server<0)rc=-errno;}
    struct sockaddr_un address={.sun_family=AF_UNIX};memcpy(address.sun_path+1,SOCKET,sizeof(SOCKET)-1);
    if(!rc){phase="listen";if(bind(server,(void*)&address,offsetof(struct sockaddr_un,sun_path)+sizeof(SOCKET))||listen(server,16))rc=-errno;}
    if(!rc){struct sony_bt_snapshot *s=NULL;rc=sony_bt_store_read(store,&s);if(!rc)printf("SONY_BT_CATALOG_READY pid=%d generation=%llu rf_requested=0\n",(int)getpid(),(unsigned long long)sony_bt_snapshot_generation(s));sony_bt_snapshot_close(&s);fflush(stdout);}
    struct pollfd fds[17];int64_t deadlines[17]={0};for(unsigned n=0;n<17;++n)fds[n]=(struct pollfd){.fd=-1,.events=POLLIN};fds[0].fd=server;
    while(!rc&&!stopping){
        int timeout=-1;int64_t time=now();for(unsigned n=1;n<17;++n)if(fds[n].fd>=0){int64_t left=deadlines[n]-time;if(left<0)left=0;if(timeout<0||left<timeout)timeout=(int)left;}
        int ready=poll(fds,17,timeout);if(ready<0){if(errno==EINTR)continue;rc=-errno;break;}
        for(unsigned n=1;n<17;++n)if(fds[n].fd>=0&&((fds[n].revents&(POLLIN|POLLHUP|POLLERR|POLLNVAL))||now()>=deadlines[n])){
            if(fds[n].revents&POLLIN){int status=sony_bt_catalog_serve_once(store,instance,fds[n].fd);if(status==-EAGAIN||status==-EWOULDBLOCK)continue;}
            close(fds[n].fd);fds[n].fd=-1;
        }
        if(fds[0].revents&POLLIN)for(;;){int client=accept4(server,NULL,NULL,SOCK_CLOEXEC|SOCK_NONBLOCK);if(client<0){if(errno!=EAGAIN&&errno!=EWOULDBLOCK&&errno!=EINTR)rc=-errno;break;}
            unsigned n;for(n=1;n<17;++n)if(fds[n].fd<0){fds[n].fd=client;deadlines[n]=now()+5000;break;}if(n==17)close(client);}
    }
    for(unsigned n=1;n<17;++n)if(fds[n].fd>=0)close(fds[n].fd);if(server>=0)close(server);sony_bt_store_close(&store);close(gate);close(root);
    if(rc)fprintf(stderr,"SONY_BT_CATALOG_ERROR phase=%s result=%d\n",phase,rc);
    printf("SONY_BT_CATALOG_EXIT result=%d rf_requested=0\n",rc);fflush(stdout);return rc?70:0;
}
