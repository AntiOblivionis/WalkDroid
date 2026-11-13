/* Fixed root operator and bounded LAB acceptance. Production supports only
 * status/disarm; it cannot choose an arbitrary peer/path or start test RF. */
#define _GNU_SOURCE
#include "sony_audio_bt_link_control.h"
#include "sony_audio_bt_catalog.h"
#include "sony_audio_bt_tx_data.h"
#include "sony_audio_bt_tx_route_gate.h"
#include "sony_audio_bt_link_service_config.h"
#if WLC_LAB
#include "sony_audio_bt_peer_config.h"
#endif
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/prctl.h>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/vfs.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
static int64_t milliseconds(void){struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t))return -1;return (int64_t)t.tv_sec*1000+t.tv_nsec/1000000;}
static int zero(const void*p,size_t n){const unsigned char*v=p;while(n--)if(*v++)return 0;return 1;}
#if WLC_LAB
#if WLC_HAL_SUPPORT
int sony_bt_link_hal_produce(int,uint64_t);
#endif
static int random_(void*p,size_t n){int fd=open("/dev/urandom",O_RDONLY|O_CLOEXEC);if(fd<0)return -errno;ssize_t got=read(fd,p,n);close(fd);return got==(ssize_t)n?0:-EIO;}
#endif
static int connect_(const char*name){int fd=socket(AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC,0);if(fd<0)return -errno;struct sockaddr_un a={.sun_family=AF_UNIX};size_t n=strlen(name);if(n>=sizeof(a.sun_path)-1){close(fd);return -EINVAL;}memcpy(a.sun_path+1,name,n);if(connect(fd,(void*)&a,offsetof(struct sockaddr_un,sun_path)+1+n)){int rc=-errno;close(fd);return rc;}return fd;}
static int receive_(int fd,void*p,size_t bytes,int timeout){struct pollfd f={fd,POLLIN,0};int n;do{n=poll(&f,1,timeout);}while(n<0&&errno==EINTR);if(n<=0)return n?-errno:-ETIMEDOUT;struct iovec v={p,bytes};struct msghdr m={.msg_iov=&v,.msg_iovlen=1};ssize_t got=recvmsg(fd,&m,MSG_DONTWAIT|MSG_TRUNC);return got==(ssize_t)bytes&&!(m.msg_flags&(MSG_TRUNC|MSG_CTRUNC))?0:got<0?-errno:-EPROTO;}
struct ctl {int fd;unsigned request;struct wlc_reply state;};
static void ctl_close(struct ctl*c){if(c->fd>=0)close(c->fd);c->fd=-1;}
static int ctl_open(struct ctl*c){memset(c,0,sizeof(*c));c->fd=connect_(WLC_CONTROL_SOCKET);if(c->fd<0)return c->fd;c->request=1;int rc=receive_(c->fd,&c->state,sizeof(c->state),5000);if(rc){ctl_close(c);return rc;}return 0;}
static int exchange(struct ctl*c,struct wlc_request*q){
    q->magic=WLC_MAGIC;q->version=WLC_VERSION;q->request=c->request++;if(send(c->fd,q,sizeof(*q),MSG_NOSIGNAL)!=sizeof(*q))return -EPIPE;
    int64_t end=milliseconds()+5000;
    for(;;){struct wlc_reply r;int64_t left=end-milliseconds();if(left<=0)return -ETIMEDOUT;int rc=receive_(c->fd,&r,sizeof(r),(int)left);if(rc)return rc;
        c->state=r;if(!r.operation&&!r.request)continue;return r.status;}
}
static struct wlc_request command(const struct ctl*c,unsigned op){struct wlc_request q={.operation=op,.generation=c->state.generation,.route=c->state.route};memcpy(q.instance,c->state.instance,16);memcpy(q.token,c->state.token,16);return q;}
static int wait_state(struct ctl*c,unsigned phase,unsigned flags,int reaped,int timeout){
    int64_t end=milliseconds()+timeout;
    for(;;){if(c->state.phase==phase&&(c->state.flags&flags)==flags&&(!reaped||!c->state.broker_pid))return 0;
        int64_t left=end-milliseconds();if(left<=0)return -ETIMEDOUT;struct wlc_reply r;int rc=receive_(c->fd,&r,sizeof(r),(int)left);if(rc)return rc;c->state=r;
        if(r.phase==WLC_FAILED||r.phase==WLC_UNKNOWN)return r.last_error?r.last_error:-EIO;
    }
}
#if WLC_TEST_MODEL
static int volume_exercise(struct ctl*c,unsigned*checks){
    int64_t end=milliseconds()+5000;
    while((c->state.volume.flags&15)!=15){
        int64_t left=end-milliseconds();if(left<=0)return -ETIMEDOUT;
        struct wlc_reply r;int rc=receive_(c->fd,&r,sizeof(r),(int)left);
        if(rc)return rc;c->state=r;
    }
    if(c->state.volume.value!=64||c->state.volume.command)return -EPROTO;
    struct ctl other={.fd=-1};int rc=ctl_open(&other);if(rc)return rc;
    struct wlc_request q=command(&other,WLC_VOLUME_STEP);q.volume_revision=other.state.volume.revision;q.volume_value=-1;
    rc=exchange(&other,&q);ctl_close(&other);if(rc!=-ESTALE)return -EPROTO;++*checks;
    q=command(c,WLC_VOLUME_STEP);q.volume_revision=c->state.volume.revision+1;q.volume_value=-1;
    if(exchange(c,&q)!=-ESTALE||c->state.volume.command)return -EPROTO;++*checks;
    q=command(c,WLC_VOLUME_STEP);q.volume_revision=c->state.volume.revision;q.volume_value=2;
    if(exchange(c,&q)!=-EPROTO||c->state.volume.command)return -EPROTO;++*checks;
    q=command(c,WLC_VOLUME_STEP);q.volume_revision=c->state.volume.revision;q.volume_value=-1;
    if(exchange(c,&q)||c->state.volume.command!=1||c->state.volume.confirmed!=1||c->state.volume.value!=60||(c->state.volume.flags&~15u))return -EPROTO;++*checks;
    // Deliberate duplicate packet: never turn an uncertain command into a
    // second press. Bypass exchange's monotonically allocated request ID.
    if(send(c->fd,&q,sizeof(q),MSG_NOSIGNAL)!=sizeof(q))return -EPIPE;
    for(;;){struct wlc_reply r;rc=receive_(c->fd,&r,sizeof(r),5000);if(rc)return rc;c->state=r;if(!r.operation)continue;
        if(r.request!=q.request||r.operation!=q.operation||r.status!=-ESTALE||r.volume.command!=1)return -EPROTO;break;}
    ++*checks;
    q=command(c,WLC_VOLUME_MODE);q.volume_revision=c->state.volume.revision;q.volume_value=0;
    if(exchange(c,&q)||c->state.volume.command!=2||c->state.volume.confirmed!=2||c->state.volume.mode||!(c->state.volume.flags&WVA_MODE_KNOWN))return -EPROTO;++*checks;
    q=command(c,WLC_VOLUME_STEP);q.volume_revision=c->state.volume.revision;q.volume_value=-1;
    if(exchange(c,&q)||c->state.volume.command!=3||c->state.volume.confirmed!=2||c->state.volume.value||c->state.volume.flags&WVA_VALUE_KNOWN)return -EPROTO;++*checks;
    return 0;
}
#endif
#if WLC_LAB
static int metadata(struct sony_bt_catalog_reply*r,int select,uint64_t generation,const unsigned char instance[16],int choose){
    int fd=connect_(WLC_META_SOCKET);if(fd<0)return fd;struct sony_bt_catalog_request q={.magic=SONY_BT_CATALOG_MAGIC,.version=1,.operation=select?2:1,.request=1,.expected_generation=generation};
    if(select){memcpy(q.instance,instance,16);if(random_(q.nonce,16)){close(fd);return -EIO;}if(choose&&sony_bt_configured_peer(q.peer)){close(fd);return -EINVAL;}}
    int rc=0;if(send(fd,&q,sizeof(q),MSG_NOSIGNAL)!=sizeof(q))rc=-EPIPE;
    if(!rc){struct pollfd f={fd,POLLIN,0};if(poll(&f,1,5000)<=0)rc=-ETIMEDOUT;else{ssize_t n=recv(fd,r,sizeof(*r),MSG_TRUNC);if(n<80||n>(ssize_t)sizeof(*r)||r->magic!=SONY_BT_CATALOG_MAGIC||r->version!=1||r->request!=1||r->operation!=q.operation||r->bytes!=(uint32_t)n||r->count>40||n!=(ssize_t)(80+r->count*sizeof(r->peers[0])))rc=-EPROTO;}}
    close(fd);return rc?rc:r->status;
}
static int producer(int control,int pcm,uint64_t route,struct wbt_route_guard*inherited){
    close(control);wbt_route_guard_release(&inherited);int fd=connect_(WLC_DATA_SOCKET);if(fd<0)return 71;
    struct wbd_client c;int rc=wbd_client_init(&c,fd,-1,route,-1);
    if(!rc)rc=wbd_client_attach(&c);if(!rc)rc=wbd_client_resume(&c,44100);
    if(!rc&&lseek(pcm,0,SEEK_SET))rc=-EIO;unsigned char samples[1024];
    for(unsigned at=0;at<196608&&!rc;at+=sizeof(samples)){if(read(pcm,samples,sizeof(samples))!=sizeof(samples)){rc=-EIO;break;}rc=wbd_client_write(&c,samples,sizeof(samples));}
    if(!rc)rc=wbd_client_pause(&c);if(!rc&&(c.accepted!=49152||c.submitted!=49152||c.last_reply.processing.codec_submitted!=49152||c.last_reply.processing.codec_padding))rc=-EPROTO;
    if(!rc)rc=wbd_client_detach(&c);close(fd);return rc?72:0;
}
struct fixture_group {int parent,group;pid_t target;char name[32];struct stat identity;};
static int fixture_group_create(struct fixture_group*g,pid_t child){
    memset(g,0,sizeof(*g));g->parent=g->group=-1;g->target=child;
#if WLC_GUARD_CGROUP
    g->parent=open("/acct/uid_0",O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);struct statfs fs;if(g->parent<0||fstatfs(g->parent,&fs)||fs.f_type!=0x27e0eb)return -EPERM;
    snprintf(g->name,sizeof(g->name),"pid_%d",(int)child);if(mkdirat(g->parent,g->name,0700))return -errno;
    g->group=openat(g->parent,g->name,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);if(g->group<0||fstat(g->group,&g->identity))return -errno;
    int fd=openat(g->group,"cgroup.procs",O_WRONLY|O_NOFOLLOW|O_CLOEXEC);if(fd<0)return -errno;char text[32];int n=snprintf(text,sizeof(text),"%d\n",(int)child);int rc=write(fd,text,(size_t)n)==n?0:-EIO;close(fd);return rc;
#else
    return 0;
#endif
}
static int fixture_group_check(struct fixture_group*g,int empty){
#if WLC_GUARD_CGROUP
    struct stat a,b;if(fstat(g->group,&a)||fstatat(g->parent,g->name,&b,AT_SYMLINK_NOFOLLOW)||a.st_ino!=b.st_ino||a.st_dev!=b.st_dev||a.st_ino!=g->identity.st_ino)return -ESTALE;
    char text[4096];int fd=openat(g->group,"cgroup.procs",O_RDONLY|O_NOFOLLOW|O_CLOEXEC);if(fd<0)return -errno;ssize_t n=read(fd,text,sizeof(text)-1);close(fd);if(n<0||n==sizeof(text)-1)return -EIO;text[n]=0;
    if(empty)return n?-EBUSY:0;char*end;long pid=strtol(text,&end,10);while(*end=='\n'||*end==' ')end++;return pid==g->target&&!*end?0:-EPROTO;
#else
    (void)g;(void)empty;return 0;
#endif
}
static int fixture_group_retire(struct fixture_group*g){int rc=fixture_group_check(g,1);if(rc)return rc;
#if WLC_GUARD_CGROUP
    if(unlinkat(g->parent,g->name,AT_REMOVEDIR))return -errno;close(g->group);close(g->parent);
#endif
    return 0;
}
struct lease {uint32_t magic,version,active,pid;uint64_t generation;uint32_t kind,reserved;uint64_t complement;};
static int lease_read(int fd,struct lease*r){struct lease a;if(pread(fd,&a,sizeof(a),0)!=sizeof(a)||pread(fd,r,sizeof(*r),0)!=sizeof(*r)||memcmp(&a,r,sizeof(a))||r->magic!=UINT32_C(0x4f534457)||r->version!=1||r->active>1||r->reserved||r->complement!=(r->generation^UINT64_MAX))return -EPROTO;return 0;}
static int wait_native_off(int lease,int watch,uint64_t generation){int64_t end=milliseconds()+35000;for(;;){struct lease r;int rc=lease_read(lease,&r);if(rc)return rc;if(r.generation!=generation)return -ESTALE;if(!r.active)return r.pid||r.kind?-EPROTO:0;int64_t left=end-milliseconds();if(left<=0)return -ETIMEDOUT;struct pollfd p={watch,POLLIN,0};if(poll(&p,1,(int)left)<=0)return -ETIMEDOUT;unsigned char events[2048];while(read(watch,events,sizeof(events))>0){}}}
static int wait_child(pid_t child,int signals,int expected_signal,int timeout){int64_t end=milliseconds()+timeout;for(;;){int s=0;pid_t p=waitpid(child,&s,WNOHANG);if(p==child)return expected_signal?(WIFSIGNALED(s)&&WTERMSIG(s)==expected_signal?0:-ECHILD):(WIFEXITED(s)&&!WEXITSTATUS(s)?0:-ECHILD);if(p<0)return -errno;int64_t left=end-milliseconds();if(left<=0)return -ETIMEDOUT;struct pollfd f={signals,POLLIN,0};if(poll(&f,1,(int)left)<=0)return -ETIMEDOUT;struct signalfd_siginfo info;while(read(signals,&info,sizeof(info))==sizeof(info)){} }}
static int exercise(const char*which,const char*pcm_path){
    if(prctl(PR_SET_CHILD_SUBREAPER,1))return 65;sigset_t mask;sigemptyset(&mask);sigaddset(&mask,SIGCHLD);if(sigprocmask(SIG_BLOCK,&mask,NULL))return 65;int signals=signalfd(-1,&mask,SFD_NONBLOCK|SFD_CLOEXEC);if(signals<0)return 65;
    int go[2],ready[2];if(pipe2(go,O_CLOEXEC)||pipe2(ready,O_CLOEXEC))return 65;pid_t service=fork();if(service<0)return 65;
    if(!service){close(go[1]);close(ready[0]);(void)prctl(PR_SET_PDEATHSIG,SIGTERM);if(dup2(ready[1],STDOUT_FILENO)<0)_exit(126);char b;if(read(go[0],&b,1)!=1)_exit(126);execl(WLC_SERVICE_EXE,WLC_SERVICE_EXE,"--service",(char*)NULL);_exit(127);}
    close(go[0]);close(ready[1]);struct fixture_group group;int rc=fixture_group_create(&group,service);if(!rc&&write(go[1],"x",1)!=1)rc=-EIO;close(go[1]);
    if(!rc){struct pollfd f={ready[0],POLLIN,0};char line[256]={0};ssize_t n=poll(&f,1,10000)>0?read(ready[0],line,sizeof(line)-1):-1;if(n<=0||!strstr(line,"SONY_BT_LINK_READY"))rc=-EIO;}close(ready[0]);
    struct ctl control={.fd=-1},observer={.fd=-1};struct wbt_route_guard*guard=NULL;struct wbt_hal_route route;int root=open(WLC_RAM_PARENT,O_RDONLY|O_DIRECTORY|O_CLOEXEC);unsigned played=0;pid_t broker=0;int killed=0,lease=-1,watch=-1;uint64_t generation=0;
    if(!rc)rc=ctl_open(&control);struct sony_bt_catalog_reply*catalog=calloc(1,sizeof(*catalog));if(!catalog)rc=-ENOMEM;
    if(!rc)rc=metadata(catalog,0,0,NULL,0);if(!rc)rc=metadata(catalog,1,catalog->generation,catalog->instance,1);
    uint64_t selected=catalog?catalog->generation:0;
    if(!rc){struct wlc_request q={.operation=WLC_ARM,.generation=selected};memcpy(q.instance,control.state.instance,16);rc=random_(q.token,16);if(!rc)rc=exchange(&control,&q);}
    int64_t query_ms=0;int frozen=0,commit_checked=0,retired=0,native_off=0;unsigned volume_checks=0;
    if(!rc){int64_t before=milliseconds();rc=metadata(catalog,0,0,NULL,0);query_ms=milliseconds()-before;if(!rc&&query_ms>250)rc=-ETIMEDOUT;if(!rc){if(metadata(catalog,1,selected,catalog->instance,0)!=-EBUSY)rc=-EPROTO;else frozen=1;}}
    if(!rc)rc=wait_state(&control,WLC_READY,WLC_RF_CONNECTED,0,65000);if(!rc){broker=(pid_t)control.state.broker_pid;if(broker<1)rc=-EPROTO;}
    if(!rc&&wbt_route_guard_acquire_at(&guard,root,WLC_RAM_NAME,&route)!=-EBUSY)rc=-EPROTO;
    if(!rc){struct wlc_request bad=command(&control,WLC_COMMIT);bad.route++;if(exchange(&control,&bad)!=-ESTALE)rc=-EPROTO;else commit_checked=1;}
    if(!rc){struct wlc_request q=command(&control,WLC_COMMIT);rc=exchange(&control,&q);}if(!rc)rc=wait_state(&control,WLC_ACTIVE,WLC_ROUTE_PUBLISHED|WLC_RF_CONNECTED,0,5000);
    if(!rc)rc=wbt_route_guard_acquire_at(&guard,root,WLC_RAM_NAME,&route);
#if WLC_TEST_MODEL
    if(!rc&&!strcmp(which,"volume"))rc=volume_exercise(&control,&volume_checks);
#endif
    if(!rc&&!strcmp(which,"normal")){
        int pcm=open(pcm_path,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);struct stat s;if(pcm<0||fstat(pcm,&s)||!S_ISREG(s.st_mode)||s.st_size!=196608)rc=-EINVAL;
        for(unsigned e=1;e<=2&&!rc;e++){pid_t child=fork();if(child<0){rc=-errno;break;}if(!child)_exit(producer(control.fd,pcm,route.generation,guard));rc=wait_child(child,signals,0,65000);if(!rc){played=e;printf("{\"event\":\"pause\",\"epoch\":%u,\"input_frames\":49152,\"codec_frames\":49152,\"codec_padding\":0,\"service_controller\":true}\n",e);poll(NULL,0,3000);}}
        if(pcm>=0)close(pcm);
    }
#if WLC_HAL_SUPPORT
    if(!rc&&!strcmp(which,"hal")){
        int pcm=open(pcm_path,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);struct stat s;if(pcm<0||fstat(pcm,&s)||!S_ISREG(s.st_mode)||s.st_size!=196608)rc=-EINVAL;
        if(!rc){pid_t child=fork();if(child<0)rc=-errno;else if(!child){close(control.fd);wbt_route_guard_release(&guard);_exit(sony_bt_link_hal_produce(pcm,route.generation));}else rc=wait_child(child,signals,0,65000);}
        if(!rc)played=2;if(pcm>=0)close(pcm);
    }
#endif
    if(!rc&&!strcmp(which,"front-death")){
        rc=fixture_group_check(&group,0);
        const char*path=WLC_TEST_MODEL?WLC_MODEL_LEASE:"/dev/walkdroid-bt-radio-owner/lease";
        if(!rc){lease=open(path,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);watch=inotify_init1(IN_NONBLOCK|IN_CLOEXEC);if(lease<0||watch<0||inotify_add_watch(watch,path,IN_MODIFY|IN_CLOSE_WRITE)<0)rc=-errno;}
        struct lease active;if(!rc){rc=lease_read(lease,&active);if(!rc&&(!active.active||active.kind!=5||!active.pid))rc=-EPROTO;generation=active.generation;}
        if(!rc&&kill(service,SIGKILL))rc=-errno;if(!rc){killed=1;rc=wait_child(service,signals,SIGKILL,5000);}
        if(!rc){rc=wait_native_off(lease,watch,generation);if(!rc)native_off=1;}
        if(!rc){struct stat s;if(lstat(WLC_RAM_PATH,&s)||!S_ISDIR(s.st_mode)||kill(broker,0))rc=-EPROTO;}
    }else if(!rc){
        struct ctl*waiter=&control;
        if(!strcmp(which,"owner-loss")){rc=ctl_open(&observer);if(!rc){ctl_close(&control);waiter=&observer;}}
        else{struct wlc_request q=command(&control,WLC_DISARM);rc=exchange(&control,&q);}
        if(!rc){rc=wait_state(waiter,WLC_STOPPING,WLC_NATIVE_QUIESCENT|WLC_RAM_OWNED,0,35000);if(!rc)native_off=1;}
    }
    if(!rc&&wbt_route_guard_check(guard)>=0)rc=-EPROTO;wbt_route_guard_release(&guard);
    if(!rc){if(killed)rc=wait_child(broker,signals,0,10000);else rc=wait_state(control.fd>=0?&control:&observer,WLC_IDLE,WLC_NATIVE_QUIESCENT|WLC_GUARD_QUIESCENT,1,10000);}
    if(!rc){struct stat s;if(!lstat(WLC_RAM_PATH,&s)||errno!=ENOENT)rc=-EPROTO;else retired=1;}
    ctl_close(&control);ctl_close(&observer);if(lease>=0)close(lease);if(watch>=0)close(watch);if(root>=0)close(root);free(catalog);
    if(!killed){kill(service,SIGTERM);int stopped=wait_child(service,signals,0,10000);if(!rc)rc=stopped;}
    int group_rc=fixture_group_retire(&group);if(!rc)rc=group_rc;close(signals);
    printf("{\"passed\":%s,\"service_controller\":true,\"scenario\":\"%s\",\"status\":%d,\"played_epochs\":%u,\"volume_checks\":%u,\"metadata_query_ms\":%lld,\"metadata_selection_frozen\":%s,\"commit_generation_checked\":%s,\"service_pid\":%d,\"broker_pid\":%d,\"frontend_killed_by_test\":%s,\"native_off_verified\":%s,\"original_ram_retired\":%s,\"fixture_cgroup_retired\":%s,\"real_native_backend\":%s,\"framework_route_tested\":false}\n",rc?"false":"true",which,rc,played,volume_checks,(long long)query_ms,frozen?"true":"false",commit_checked?"true":"false",(int)service,(int)broker,killed?"true":"false",native_off?"true":"false",retired?"true":"false",group_rc?"false":"true",WLC_TEST_MODEL?"false":"true");
    return rc?70:0;
}
#endif
int main(int argc,char**argv){
    signal(SIGPIPE,SIG_IGN);setvbuf(stdout,NULL,_IONBF,0);alarm(180);
    if(argc==2&&(!strcmp(argv[1],"--status")||!strcmp(argv[1],"--disarm"))){struct ctl c;int rc=ctl_open(&c);if(rc)return 70;if(!strcmp(argv[1],"--disarm")&&c.state.phase!=WLC_IDLE){struct wlc_request q=command(&c,WLC_DISARM);rc=exchange(&c,&q);if(!rc)rc=wait_state(&c,WLC_IDLE,WLC_GUARD_QUIESCENT,1,45000);}printf("{\"status\":%d,\"phase\":%u,\"flags\":%u,\"native_phase\":%u,\"broker_pid\":%u}\n",rc,c.state.phase,c.state.flags,c.state.native_phase,c.state.broker_pid);ctl_close(&c);return rc?70:0;}
#if WLC_LAB
#if WLC_TEST_MODEL
    if(argc==2&&!strcmp(argv[1],"--volume"))return exercise("volume",NULL);
#endif
    if(argc==3&&!strcmp(argv[1],"--normal"))return exercise("normal",argv[2]);
#if WLC_HAL_SUPPORT
    if(argc==3&&!strcmp(argv[1],"--hal"))return exercise("hal",argv[2]);
#endif
    if(argc==2&&!strcmp(argv[1],"--owner-loss"))return exercise("owner-loss",NULL);
    if(argc==2&&!strcmp(argv[1],"--front-death"))return exercise("front-death",NULL);
#endif
    return 64;
}
