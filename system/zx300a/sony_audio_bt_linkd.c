/* Root metadata/control front end. Slow RF and all route resources belong to
 * one independent original-handle broker; this loop never waits for RF. */
#define _GNU_SOURCE
#include "sony_audio_bt_link_service_config.h"
#define SONY_BT_CATALOG_STORE WLC_STORE
#define SONY_BT_CATALOG_SOURCE WLC_TEMPLATE
#define SONY_BT_CATALOG_SOCKET WLC_META_SOCKET
#define main wlc_legacy_catalog_main
#include "sony_audio_bt_catalogd.c"
#undef main
#include "sony_audio_bt_link_control.h"
#include "sony_audio_bt_tx_hal_config.h"
#include <sys/signalfd.h>
#include <sys/wait.h>
#define CLIENTS 16
struct control_client {int fd,metadata;uint32_t last_request;int64_t deadline;};
struct frontend {
    struct sony_bt_store*store;int gate,root,metadata,control,signals,broker,owner,commit_pending;
    pid_t broker_pid;struct wlc_reply state;struct control_client clients[CLIENTS];
    int volume_client;struct wlc_request volume_request;
};
static void close_client(struct frontend*,unsigned);
static void notify_owner(struct frontend*f){
    for(unsigned n=0;n<CLIENTS;n++){struct control_client*c=&f->clients[n];if(c->fd<0||c->metadata)continue;
        struct wlc_reply r=f->state;r.operation=WLC_EVENT;r.request=0;r.status=0;
        if(send(c->fd,&r,sizeof(r),MSG_NOSIGNAL|MSG_DONTWAIT)!=sizeof(r))close_client(f,n);
    }
}
static int bind_listener(const char*name){
    size_t len=strlen(name);if(!len||len>=sizeof(((struct sockaddr_un*)0)->sun_path)-1)return -EINVAL;
    int fd=socket(AF_UNIX,SOCK_SEQPACKET|SOCK_NONBLOCK|SOCK_CLOEXEC,0);if(fd<0)return -errno;
    struct sockaddr_un a={.sun_family=AF_UNIX};memcpy(a.sun_path+1,name,len);
    if(bind(fd,(void*)&a,offsetof(struct sockaddr_un,sun_path)+1+len)||listen(fd,16)){int rc=-errno;close(fd);return rc;}return fd;
}
static int queue_command(struct frontend*f,const struct wlc_request*q){
    if(f->broker<0)return -ENOTCONN;ssize_t n=send(f->broker,q,sizeof(*q),MSG_NOSIGNAL|MSG_DONTWAIT);
    if(n==sizeof(*q))return 0;int rc=n<0?-errno:-EIO;
    /* A lost DISARM must still deliver owner loss; never blindly replay it. */
    shutdown(f->broker,SHUT_WR);f->state.phase=WLC_STOPPING;f->state.last_error=rc;return rc;
}
static void disarm_owner(struct frontend*f){
    if(f->broker<0||f->state.phase==WLC_IDLE)return;struct wlc_request q={.magic=WLC_MAGIC,.version=WLC_VERSION,.operation=WLC_DISARM,.request=1,.generation=f->state.generation,.route=f->state.route};
    memcpy(q.instance,f->state.instance,16);memcpy(q.token,f->state.token,16);queue_command(f,&q);f->state.phase=WLC_STOPPING;
}
static void close_client(struct frontend*f,unsigned slot){
    struct control_client*c=&f->clients[slot];if(c->fd<0)return;
    if(f->volume_client==c->fd){f->volume_client=-1;memset(&f->volume_request,0,sizeof(f->volume_request));}
    if(f->owner==c->fd){disarm_owner(f);f->owner=-1;}
    close(c->fd);c->fd=-1;
}
static void reconcile_absence(struct frontend*f){
    if(f->broker_pid||f->state.phase!=WLC_UNKNOWN)return;
    struct stat s;if(lstat(WLC_RAM_PATH,&s)&&errno==ENOENT){
        unsigned char instance[16];memcpy(instance,f->state.instance,16);memset(&f->state,0,sizeof(f->state));
        f->state.magic=WLC_MAGIC;f->state.version=WLC_VERSION;memcpy(f->state.instance,instance,16);f->state.phase=WLC_IDLE;
    }
}
static int start_broker(struct frontend*f,int client,const struct wlc_request*q){
    reconcile_absence(f);if(f->broker_pid||f->state.phase!=WLC_IDLE)return -EBUSY;
    struct sony_bt_snapshot*snapshot=NULL;int rc=sony_bt_store_read(f->store,&snapshot);
    unsigned char peer[6]={0};if(!rc)sony_bt_snapshot_selection(snapshot,peer);
    int pair[2]={-1,-1};if(!rc&&socketpair(AF_UNIX,SOCK_SEQPACKET|SOCK_NONBLOCK|SOCK_CLOEXEC,0,pair))rc=-errno;
    uint64_t route=0;if(!rc){rc=random_bytes((void*)&route,sizeof(route));route&=UINT64_C(0x7fffffffffffffff);if(!route&&!rc)rc=-EIO;}
    if(rc){sony_bt_snapshot_close(&snapshot);if(pair[0]>=0)close(pair[0]);if(pair[1]>=0)close(pair[1]);return rc;}
    unsigned char instance[16];memcpy(instance,f->state.instance,16);memset(&f->state,0,sizeof(f->state));
    f->state.magic=WLC_MAGIC;f->state.version=WLC_VERSION;f->state.phase=WLC_ARMING;f->state.generation=sony_bt_snapshot_generation(snapshot);f->state.route=route;
    memcpy(f->state.instance,instance,16);memcpy(f->state.token,q->token,16);memcpy(f->state.peer,peer,6);
    pid_t parent=getpid(),pid=fork();
    if(!pid){close(pair[0]);sony_bt_store_close(&f->store);close(f->gate);close(f->root);int result=sony_bt_link_broker(pair[1],parent,snapshot,&f->state);_exit(result);}
    close(pair[1]);sony_bt_snapshot_close(&snapshot);
    if(pid<0){rc=-errno;close(pair[0]);f->state.phase=WLC_IDLE;f->state.last_error=rc;return rc;}
    f->broker=pair[0];f->broker_pid=pid;f->state.broker_pid=(uint32_t)pid;f->owner=client;f->commit_pending=0;
    f->volume_client=-1;memset(&f->volume_request,0,sizeof(f->volume_request));return 0;
}
static void read_broker(struct frontend*f){
    while(f->broker>=0){struct wlc_reply r;ssize_t n=recv(f->broker,&r,sizeof(r),MSG_DONTWAIT|MSG_TRUNC);
        if(n<0&&(errno==EAGAIN||errno==EINTR))break;
        if(n==0){close(f->broker);f->broker=-1;break;}
        if(n!=sizeof(r)){
            f->state.last_error=-EPROTO;disarm_owner(f);break;
        }
        if(r.operation){
            /* An owner may have disappeared while native was executing. Its
             * late reply updates observation only; no FD reuse can adopt it. */
            if(f->volume_client>=0){
                for(unsigned i=0;i<CLIENTS;i++){struct control_client*c=&f->clients[i];if(c->fd!=f->volume_client)continue;
                    if(send(c->fd,&r,sizeof(r),MSG_NOSIGNAL|MSG_DONTWAIT)!=sizeof(r))close_client(f,i);break;}
                f->volume_client=-1;memset(&f->volume_request,0,sizeof(f->volume_request));
            }
            r.operation=r.request=0;r.status=0;
        }
        f->state=r;if(r.phase!=WLC_READY)f->commit_pending=0;notify_owner(f);
    }
}
static void reap_broker(struct frontend*f){
    if(!f->broker_pid)return;read_broker(f);int s=0;pid_t p=waitpid(f->broker_pid,&s,WNOHANG);if(p==0)return;
    if(p<0&&errno==EINTR)return;
    if(p!=f->broker_pid||!WIFEXITED(s)||WEXITSTATUS(s)||f->state.phase!=WLC_IDLE){f->state.phase=WLC_UNKNOWN;f->state.last_error=-ECHILD;}
    f->broker_pid=0;f->state.broker_pid=0;if(f->broker>=0){close(f->broker);f->broker=-1;}
    f->commit_pending=0;notify_owner(f);f->owner=-1;
}
static int serve_control(struct frontend*f,unsigned slot){
    struct control_client*c=&f->clients[slot];int rc=0;
    struct wlc_request q={0};struct iovec v={&q,sizeof(q)};struct msghdr m={.msg_iov=&v,.msg_iovlen=1};ssize_t n=recvmsg(c->fd,&m,MSG_DONTWAIT|MSG_TRUNC);
    if(n<0)return -errno;if(!n)return -ECONNRESET;if(n!=sizeof(q)||m.msg_flags&(MSG_TRUNC|MSG_CTRUNC))return -EMSGSIZE;
    if(q.operation==WLC_STATUS){
        reap_broker(f);reconcile_absence(f);
    }
    else if(q.operation==WLC_ARM)rc=start_broker(f,c->fd,&q);
    else if(q.operation==WLC_COMMIT||q.operation==WLC_DISARM){
        if(!f->broker_pid)rc=-ESTALE;
        else if(q.operation==WLC_COMMIT&&(f->state.phase!=WLC_READY||f->commit_pending))rc=-EALREADY;
        else{rc=queue_command(f,&q);if(!rc){if(q.operation==WLC_COMMIT)f->commit_pending=1;else f->state.phase=WLC_STOPPING;}}
    }else if(wlc_is_volume(q.operation)){
        if(!f->broker_pid)rc=-ESTALE;
        else if(f->state.phase!=WLC_ACTIVE)rc=-ESHUTDOWN;
        else if(f->volume_client>=0)rc=-EBUSY;
        else{
            rc=queue_command(f,&q);
            if(!rc){f->volume_client=c->fd;f->volume_request=q;c->last_request=q.request;return 0;}
        }
    }else rc=-ENOSYS;
    if(q.request>c->last_request)c->last_request=q.request;
    struct wlc_reply r=f->state;r.operation=q.operation;r.request=q.request;r.status=rc;
    n=send(c->fd,&r,sizeof(r),MSG_NOSIGNAL|MSG_DONTWAIT);if(n!=sizeof(r))return n<0?-errno:-EIO;
    c->deadline=now()+5000;return 0;
}
static int accept_client(struct frontend*f,int listener_,int metadata){
    int fd=accept4(listener_,NULL,NULL,SOCK_CLOEXEC|SOCK_NONBLOCK);if(fd<0)return errno==EAGAIN||errno==EWOULDBLOCK?0:-errno;
    unsigned n;for(n=0;n<CLIENTS;n++)if(f->clients[n].fd<0)break;if(n==CLIENTS){close(fd);return 0;}
    struct control_client*c=&f->clients[n];
    c->fd=fd;c->metadata=metadata;c->last_request=0;c->deadline=now()+5000;
    if(!metadata){struct wlc_reply state=f->state;state.operation=WLC_EVENT;state.request=0;state.status=0;if(send(fd,&state,sizeof(state),MSG_NOSIGNAL|MSG_DONTWAIT)!=sizeof(state))close_client(f,n);}
    return 0;
}
int main(int argc,char**argv){
    if(argc!=2||strcmp(argv[1],"--service"))return 64;
    struct rlimit limit={0,0};(void)setrlimit(RLIMIT_CORE,&limit);
    umask(077);signal(SIGPIPE,SIG_IGN);
    struct sigaction action={.sa_handler=SIG_DFL};sigemptyset(&action.sa_mask);sigaction(SIGTERM,&action,NULL);sigaction(SIGINT,&action,NULL);sigaction(SIGCHLD,&action,NULL);
    sigset_t mask;sigemptyset(&mask);sigaddset(&mask,SIGTERM);sigaddset(&mask,SIGINT);sigaddset(&mask,SIGCHLD);if(sigprocmask(SIG_BLOCK,&mask,NULL))return 65;
    struct frontend f={.gate=-1,.root=-1,.metadata=-1,.control=-1,.signals=-1,.broker=-1,.owner=-1,.volume_client=-1};for(unsigned n=0;n<CLIENTS;n++)f.clients[n].fd=-1;
    int rc=0;if(mkdir(STORE,0700)&&errno!=EEXIST)rc=-errno;
    if(!rc){f.root=exact_dir(STORE);if(f.root<0)rc=f.root;}
    if(!rc){f.gate=openat(f.root,"service",O_RDWR|O_CREAT|O_CLOEXEC,0600);if(f.gate<0||flock(f.gate,LOCK_EX|LOCK_NB))rc=-EBUSY;}
    unsigned char config[64]={0},instance[16]={0};if(!rc)rc=factory(config);if(!rc)rc=random_bytes(instance,16);
    if(!rc)rc=sony_bt_store_open(&f.store,f.root,config);memset(config,0,sizeof(config));if(!rc)rc=migrate(f.store,instance);
    f.state.magic=WLC_MAGIC;f.state.version=WLC_VERSION;memcpy(f.state.instance,instance,16);
    if(!rc){f.metadata=bind_listener(WLC_META_SOCKET);if(f.metadata<0)rc=f.metadata;}
    if(!rc){f.control=bind_listener(WLC_CONTROL_SOCKET);if(f.control<0)rc=f.control;}
    if(!rc){f.signals=signalfd(-1,&mask,SFD_NONBLOCK|SFD_CLOEXEC);if(f.signals<0)rc=-errno;}
    if(!rc){printf("SONY_BT_LINK_READY pid=%d profile=%s rf_requested=0\n",(int)getpid(),WLC_PROFILE);fflush(stdout);}
    int stop_=0;
    while(!rc&&!stop_){
        struct pollfd p[4+CLIENTS];p[0]=(struct pollfd){f.metadata,POLLIN,0};p[1]=(struct pollfd){f.control,POLLIN,0};p[2]=(struct pollfd){f.signals,POLLIN,0};p[3]=(struct pollfd){f.broker,POLLIN,0};int timeout=-1;int64_t time=now();
        for(unsigned n=0;n<CLIENTS;n++){struct control_client*c=&f.clients[n];p[n+4]=(struct pollfd){c->fd,POLLIN,0};if(c->fd>=0&&c->metadata){int64_t left=c->deadline-time;if(left<0)left=0;if(timeout<0||left<timeout)timeout=(int)left;}}
        int ready=poll(p,4+CLIENTS,timeout);if(ready<0){if(errno==EINTR)continue;rc=-errno;break;}
        if(p[3].revents)read_broker(&f);
        if(p[2].revents&POLLIN){struct signalfd_siginfo info;while(read(f.signals,&info,sizeof(info))==sizeof(info)){if(info.ssi_signo==SIGCHLD)reap_broker(&f);else stop_=1;}}
        for(unsigned n=0;n<CLIENTS;n++){struct control_client*c=&f.clients[n];if(c->fd<0)continue;
            if(p[n+4].revents&POLLIN){int status=c->metadata?sony_bt_catalog_serve_once_guarded(f.store,f.state.instance,c->fd,!f.broker_pid&&f.state.phase==WLC_IDLE):serve_control(&f,n);if(status==-EAGAIN||status==-EWOULDBLOCK)continue;if(status||c->metadata){close_client(&f,n);continue;}}
            if((p[n+4].revents&(POLLHUP|POLLERR|POLLNVAL))||(c->fd>=0&&c->metadata&&now()>=c->deadline))close_client(&f,n);
        }
        if(!stop_&&p[0].revents&POLLIN)accept_client(&f,f.metadata,1);
        if(!stop_&&p[1].revents&POLLIN)accept_client(&f,f.control,0);
    }
    disarm_owner(&f);for(unsigned n=0;n<CLIENTS;n++)close_client(&f,n);
    if(f.broker>=0){shutdown(f.broker,SHUT_WR);close(f.broker);}if(f.metadata>=0)close(f.metadata);if(f.control>=0)close(f.control);if(f.signals>=0)close(f.signals);
    sony_bt_store_close(&f.store);if(f.gate>=0)close(f.gate);if(f.root>=0)close(f.root);return rc?70:0;
}
