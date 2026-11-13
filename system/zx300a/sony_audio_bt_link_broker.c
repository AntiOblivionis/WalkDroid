#define _GNU_SOURCE
#include "sony_audio_bt_link_control.h"
#include "sony_audio_bt_broker_group.h"
#include "sony_audio_bt_ram_route.h"
#include "sony_audio_bt_tx_data.h"
#include "sony_audio_bt_tx_link_process.h"
#include "sony_audio_bt_link_service_config.h" /* Fixed audited build profile. */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/prctl.h>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>
struct broker {
    int ipc,signals,cancel,listener,log,stopping,stop_started,front_gone,native_done;
    struct sony_bt_ram_route*ram;
    struct sony_bt_link_process*link;
    struct sony_bt_tx_exit exit;
    struct wbd_server*data;
    struct wlc_reply state;
    struct wlc_request volume_request;
    int volume_pending,volume_closed;
    int64_t volume_deadline;
};
static int64_t milliseconds(void){struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t))return -1;return (int64_t)t.tv_sec*1000+t.tv_nsec/1000000;}
static void volume_state(struct broker*b){struct wva_client*c=sony_bt_link_process_volume(b->link);if(c)b->state.volume=c->state;}
static int send_front(struct broker*b,const struct wlc_reply*r){
    if(b->front_gone)return 0;
    ssize_t n=send(b->ipc,r,sizeof(*r),MSG_NOSIGNAL|MSG_DONTWAIT);
    if(n==sizeof(*r))return 0;
    b->front_gone=b->stopping=1;return n<0?-errno:-EIO;
}
static int publish_state(struct broker*b){
    b->state.broker_pid=(uint32_t)getpid();b->state.flags=0;
    if(b->ram)b->state.flags|=WLC_RAM_OWNED;
    if(b->state.phase==WLC_ACTIVE)b->state.flags|=WLC_ROUTE_PUBLISHED;
    struct wbl_client*c=sony_bt_link_process_client(b->link);
    if(c){b->state.native_phase=c->phase;b->state.native_epoch=c->epoch;if(c->phase==WBL_CONNECTED||c->phase==WBL_STREAMING)b->state.flags|=WLC_RF_CONNECTED;}
    else b->state.native_phase=WBL_NEW;
    if(!b->ram)b->state.flags|=WLC_GUARD_QUIESCENT;
    volume_state(b);
    /* State changes are sparse. Losing the only completion report is not
     * permission to continue an unobservable active route. */
    return send_front(b,&b->state);
}
static int complete_volume(struct broker*b,const struct wlc_request*q,int status){
    volume_state(b);struct wlc_reply r=b->state;r.operation=q->operation;r.request=q->request;r.status=status;
    return send_front(b,&r);
}
static void cancel_volume(struct broker*b,int status){
    if(!b->volume_pending)return;struct wlc_request q=b->volume_request;
    b->volume_pending=0;memset(&b->volume_request,0,sizeof(b->volume_request));
    /* A cancelled wait is not evidence that the remote command was undone. */
    complete_volume(b,&q,status);
}
static int submit_volume(struct broker*b,const struct wlc_request*q){
    struct wva_client*c=sony_bt_link_process_volume(b->link);
    int rc=b->volume_closed||b->stopping||b->state.phase!=WLC_ACTIVE?-ESHUTDOWN:
        !c?-ENOTCONN:b->volume_pending?-EBUSY:0;
    if(!rc)rc=wva_client_submit(c,q->operation==WLC_VOLUME_STEP?WVA_STEP:WVA_MODE,q->volume_revision,q->volume_value);
    if(rc)return complete_volume(b,q,rc);
    b->volume_request=*q;b->volume_pending=1;b->volume_deadline=milliseconds()+5000;return 0;
}
static int observe_volume(struct broker*b){
    struct wva_client*c=sony_bt_link_process_volume(b->link);if(!c)return -ENOTCONN;
    for(unsigned n=0;n<64;n++){
        struct wva_reply r;int rc=wva_client_observe(c,&r);if(rc==-EAGAIN)return 0;if(rc<0)return rc;
        if(rc){
            if(b->volume_pending){struct wlc_request q=b->volume_request;b->volume_pending=0;memset(&b->volume_request,0,sizeof(b->volume_request));
                if(complete_volume(b,&q,r.status))return -EPIPE;}
            else if(publish_state(b))return -EPIPE;
        }else if(publish_state(b))return -EPIPE;
    }
    return 0;
}
static int listener(void){
    int fd=socket(AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC|SOCK_NONBLOCK,0);if(fd<0)return -errno;
    struct sockaddr_un a={.sun_family=AF_UNIX};memcpy(a.sun_path+1,WLC_DATA_SOCKET,sizeof(WLC_DATA_SOCKET)-1);
    if(bind(fd,(void*)&a,offsetof(struct sockaddr_un,sun_path)+sizeof(WLC_DATA_SOCKET))||listen(fd,4)){int rc=-errno;close(fd);return rc;}return fd;
}
static void discard_data(struct broker*b){
    if(b->data){int rc=wbd_server_close(&b->data);if(rc&&!b->state.last_error)b->state.last_error=rc;}
}
static int try_retire(struct broker*b){
    if(!b->ram)return 0;
    int rc=sony_bt_ram_route_retire(&b->ram,0);
    if(rc&&rc!=-EBUSY){b->state.last_error=rc;b->state.phase=WLC_FAILED;publish_state(b);}
    return rc;
}
static int stop_owned(struct broker*b){
    b->volume_closed=1;b->stopping=b->stop_started=1;b->state.phase=WLC_STOPPING;
    cancel_volume(b,-ECANCELED);publish_state(b);
    if(b->listener>=0){close(b->listener);b->listener=-1;}
    if(b->ram){int rc=sony_bt_ram_route_invalidate(b->ram);if(rc){b->state.last_error=rc;b->state.phase=WLC_FAILED;}}
    /* Once shutdown is chosen, cancellation events must not prevent the
     * CLOSE/CANCEL ACK itself. Socket EOF still handles a broken native link. */
    struct wbl_client*c=sony_bt_link_process_client(b->link);if(c)c->cancel_fd=-1;
    discard_data(b);
    if(b->link){int rc=sony_bt_link_process_stop(&b->link,1,35000,&b->exit);if(rc&&rc!=-EINPROGRESS&&rc!=-EAGAIN&&!b->state.last_error)b->state.last_error=rc;}
    b->native_done=!b->link;
    if(!b->native_done){b->state.phase=WLC_STOPPING;publish_state(b);return -EINPROGRESS;}
    int rc=try_retire(b);if(!rc){b->state.phase=WLC_IDLE;publish_state(b);}else if(rc==-EBUSY)publish_state(b);return rc;
}
static int exact_command(const struct broker*b,const struct wlc_request*q){
    (void)b;return q!=NULL;
}
static int accept_data(struct broker*b){
    int fd=accept4(b->listener,NULL,NULL,SOCK_CLOEXEC|SOCK_NONBLOCK);if(fd<0)return errno==EAGAIN||errno==EWOULDBLOCK?0:-errno;
    int rc=b->state.phase!=WLC_ACTIVE||b->data?-EBUSY:0;
    uint64_t nonce=0;int random=-1;
    if(!rc){random=open("/dev/urandom",O_RDONLY|O_CLOEXEC);if(random>=0)(void)read(random,&nonce,8);}
    if(random>=0)close(random);
    if(!rc){struct wbl_client*c=sony_bt_link_process_client(b->link);if(!c)rc=-ENOTCONN;else rc=wbd_server_open(&b->data,fd,-1,b->state.route,nonce,c);}
    close(fd);
    return 0; /* A rejected external client cannot terminate the owned link. */
}
static void close_except(int keep){
    DIR*d=opendir("/proc/self/fd");if(!d)_exit(126);int own=dirfd(d);struct dirent*e;
    while((e=readdir(d))){char*end;long n=strtol(e->d_name,&end,10);if(!*end&&n>2&&n!=keep&&n!=own)close((int)n);}closedir(d);
}
int sony_bt_link_broker(int ipc,pid_t parent,struct sony_bt_snapshot*snapshot,const struct wlc_reply*initial){
    struct broker b={.ipc=ipc,.signals=-1,.cancel=-1,.listener=-1,.log=-1,.native_done=1,.state=*initial};
    close_except(ipc);signal(SIGPIPE,SIG_IGN);
    struct sony_bt_broker_group*group=NULL;
    sigset_t mask;sigemptyset(&mask);sigaddset(&mask,SIGTERM);sigaddset(&mask,SIGINT);sigaddset(&mask,SIGCHLD);
    struct sigaction action={.sa_handler=SIG_DFL};sigemptyset(&action.sa_mask);
    sigaction(SIGTERM,&action,NULL);sigaction(SIGINT,&action,NULL);sigaction(SIGCHLD,&action,NULL);
    int rc=sigprocmask(SIG_BLOCK,&mask,NULL)?-errno:0;
    if(!rc){b.signals=signalfd(-1,&mask,SFD_NONBLOCK|SFD_CLOEXEC);if(b.signals<0)rc=-errno;}
    (void)parent;if(!rc&&prctl(PR_SET_PDEATHSIG,SIGTERM)){b.front_gone=1;rc=-ECANCELED;}
    if(!rc&&setsid()<0)rc=-errno;
#if WLC_GUARD_CGROUP
    if(!rc)rc=sony_bt_broker_group_enter(&group);
#endif
    if(!rc){b.cancel=epoll_create1(EPOLL_CLOEXEC);if(b.cancel<0)rc=-errno;}
    struct epoll_event event={.events=EPOLLIN|EPOLLHUP|EPOLLERR};
    if(!rc&&(epoll_ctl(b.cancel,EPOLL_CTL_ADD,ipc,&event)||epoll_ctl(b.cancel,EPOLL_CTL_ADD,b.signals,&event)))rc=-errno;
    struct wbt_hal_route route={.magic=WBT_HAL_ROUTE_MAGIC,.version=1,.generation=initial->route};memcpy(route.peer,initial->peer,6);
    if(!rc)rc=sony_bt_ram_route_create(&b.ram,WLC_RAM_PARENT,WLC_RAM_NAME,snapshot,&route);
    sony_bt_snapshot_close(&snapshot);
    if(!rc){b.log=open("/dev/null",O_WRONLY|O_CLOEXEC);if(b.log<0)rc=-errno;}
    if(!rc){b.native_done=0;rc=sony_bt_link_process_connect_volume_cancel(&b.link,WBL_PROCESS_EXPLICIT_CONNECT,wbt_hal_runtime_path(),sony_bt_ram_route_seed(b.ram),b.log,route.peer,b.cancel,&b.exit);
        if(rc&&!b.link)b.native_done=1;}
    if(!rc){b.listener=listener();if(b.listener<0)rc=b.listener;}
    if(rc){b.state.last_error=rc;stop_owned(&b);}else{b.state.phase=WLC_READY;publish_state(&b);}
    for(;;){
        if(b.stopping&&!b.stop_started)stop_owned(&b);
        if(b.stopping&&b.native_done&&!b.ram)break;
        struct wva_client*volume=sony_bt_link_process_volume(b.link);
        struct pollfd f[6]={{b.front_gone?-1:ipc,POLLIN,0},{b.signals,POLLIN,0},{b.listener,POLLIN,0},
            {b.data?wbd_server_events(b.data):-1,POLLIN,0},{b.link?sony_bt_link_process_events(b.link):b.ram?sony_bt_ram_route_events(b.ram):-1,POLLIN,0},
            {volume&&!b.volume_closed?volume->fd:-1,POLLIN,0}};
        int timeout=-1;if(b.volume_pending){int64_t left=b.volume_deadline-milliseconds();timeout=left<=0?0:left>5000?5000:(int)left;}
        int n=poll(f,6,timeout);if(n<0){if(errno==EINTR)continue;b.state.last_error=-errno;stop_owned(&b);continue;}
        if(b.volume_pending&&milliseconds()>=b.volume_deadline){cancel_volume(&b,-ETIMEDOUT);b.state.last_error=-ETIMEDOUT;stop_owned(&b);continue;}
        if(f[1].revents&POLLIN){struct signalfd_siginfo info;while(read(b.signals,&info,sizeof(info))==sizeof(info))if(info.ssi_signo==SIGTERM||info.ssi_signo==SIGINT){b.front_gone=1;stop_owned(&b);}}
        if(f[0].revents&(POLLIN|POLLHUP|POLLERR)){
            struct wlc_request q;struct iovec v={&q,sizeof(q)};struct msghdr message={.msg_iov=&v,.msg_iovlen=1};ssize_t got=recvmsg(ipc,&message,MSG_DONTWAIT|MSG_TRUNC);
            if(got<=0){if(got<0&&(errno==EAGAIN||errno==EINTR)){}else{b.front_gone=1;stop_owned(&b);}}
            else if(got!=sizeof(q)||message.msg_flags&(MSG_TRUNC|MSG_CTRUNC)||!exact_command(&b,&q)){b.state.last_error=-EPROTO;stop_owned(&b);}
            else if(q.operation==WLC_DISARM)stop_owned(&b);
            else if(wlc_is_volume(q.operation))submit_volume(&b,&q);
            else if(q.operation==WLC_COMMIT&&b.state.phase==WLC_READY){int status=sony_bt_ram_route_publish(b.ram);if(status){b.state.last_error=status;stop_owned(&b);}else{b.state.phase=WLC_ACTIVE;publish_state(&b);}}
            else{b.state.last_error=-EINVAL;publish_state(&b);}
        }
        struct epoll_event ignored[4];if(b.cancel>=0)epoll_wait(b.cancel,ignored,4,0);
        if(!b.stopping&&f[5].revents){int status=observe_volume(&b);if(status){b.state.last_error=status;cancel_volume(&b,status);stop_owned(&b);}}
        if(f[4].revents){
            if(b.link){struct sony_bt_tx_exit observed;int terminal=sony_bt_link_process_observe(b.link,&observed);if(terminal){if(!b.state.last_error)b.state.last_error=terminal<0?terminal:-ENOTCONN;stop_owned(&b);}}
            else if(b.ram){unsigned char events[2048];int fd=sony_bt_ram_route_events(b.ram);while(fd>=0&&read(fd,events,sizeof(events))>0){}if(b.stopping&&b.native_done&&!try_retire(&b)){b.state.phase=WLC_IDLE;publish_state(&b);}}
        }
        if(!b.stopping&&f[2].revents&POLLIN)accept_data(&b);
        if(!b.stopping&&b.data&&f[3].revents){unsigned before=b.state.native_phase;int status=wbd_server_step(b.data);
            if(status==-EAGAIN){}else if(status||wbd_server_phase(b.data)==WBD_DETACHED){discard_data(&b);struct wbl_client*c=sony_bt_link_process_client(b.link);if(!c||c->phase==WBL_FAILED){b.state.last_error=status?status:-ENOTCONN;stop_owned(&b);}}
            struct wbl_client*c=sony_bt_link_process_client(b.link);if(c&&c->phase!=before)publish_state(&b);
        }
    }
    discard_data(&b);if(b.listener>=0)close(b.listener);if(b.log>=0)close(b.log);if(b.cancel>=0)close(b.cancel);if(b.signals>=0)close(b.signals);close(ipc);
    int group_rc=group?sony_bt_broker_group_leave(&group):0;
    return b.native_done&&!b.ram&&!group_rc?0:70;
}
