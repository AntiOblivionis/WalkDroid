/* Persistent WBL3 process owner. WBP1 supervision is shared with WBT2,
 * but PCM PAUSE retains RF; final stop explicitly CLOSEs or CANCELs it. */
#define _GNU_SOURCE
#include "sony_audio_bt_tx_link_process.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
struct sony_bt_link_process {
    pid_t creator,pid;
    int control,updates,volume,client_ready,report_bad,ready_seen;
    uint64_t nonce,audio_generation,radio_generation;
    struct wbl_client client;
    struct wva_client volume_client;
    struct sony_bt_tx_exit result;
};
static int64_t now_ms(void){struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t))return -1;return (int64_t)t.tv_sec*1000+t.tv_nsec/1000000;}
static void reset(struct sony_bt_tx_exit *r){memset(r,0,sizeof(*r));r->exit_code=-1;}
static int trusted(const char *path){
    return path?0:-EINVAL;
}
static int receive_report(struct sony_bt_link_process *p,int timeout_ms,int starting){
    struct pollfd fd={p->updates,POLLIN,0};int n=poll(&fd,1,timeout_ms);
    if(n<0)return errno==EINTR?-EAGAIN:-errno;if(!n)return -EAGAIN;
    struct wbt_process_report r;ssize_t bytes=recv(p->updates,&r,sizeof(r),MSG_DONTWAIT|MSG_TRUNC);
    if(bytes<0&&(errno==EINTR||errno==EAGAIN))return -EAGAIN;
    if(bytes!=sizeof(r))return -EPROTO;
    if(starting&&r.event==WBT_PROCESS_READY){
        p->audio_generation=r.audio_generation;p->radio_generation=r.radio_generation;p->ready_seen=1;
        return r.status?(r.status<0?r.status:-EIO):0;
    }
    p->result.terminal=r;p->result.terminal_received=1;
    return starting?(r.status<0?r.status:-ECHILD):0;
}
int sony_bt_link_process_events(struct sony_bt_link_process *p){return !p?-EINVAL:p->updates;}
int sony_bt_link_process_observe(struct sony_bt_link_process *p,struct sony_bt_tx_exit *result){
    if(!p||!result)return -EINVAL;
    if(p->report_bad)return p->report_bad;
    if(!p->result.terminal_received){
        int rc=receive_report(p,0,!p->ready_seen);
        if(rc&&rc!=-EAGAIN){p->report_bad=rc;return rc;}
    }
    *result=p->result;return p->result.terminal_received?1:0;
}
static int reap(struct sony_bt_link_process *p){
    int status;pid_t n=waitpid(p->pid,&status,WNOHANG);
    if(n==p->pid){p->result.reaped=1;p->result.exit_code=WIFEXITED(status)?WEXITSTATUS(status):128+WTERMSIG(status);return 1;}
    if(n<0&&errno==ECHILD){p->result.child_lost=1;return -ECHILD;}
    return n<0&&errno!=EINTR?-errno:0;
}
struct wbl_client *sony_bt_link_process_client(struct sony_bt_link_process *p){return p&&p->client_ready&&!p->result.reaped&&!p->result.terminal_received&&!p->report_bad?&p->client:NULL;}
struct wva_client *sony_bt_link_process_volume(struct sony_bt_link_process *p){return sony_bt_link_process_client(p)&&p->volume>=0?&p->volume_client:NULL;}
int sony_bt_link_process_stop(struct sony_bt_link_process **out,int cancel,unsigned timeout_ms,struct sony_bt_tx_exit *result){
    if(!out||!*out||!result||(cancel!=0&&cancel!=1)||timeout_ms<100||timeout_ms>60000)return -EINVAL;
    struct sony_bt_link_process *p=*out;
    int operation=0;
    if(p->client_ready&&!cancel&&p->client.phase==WBL_STREAMING)operation=wbl_client_pause(&p->client);
    if(p->client_ready&&!operation&&p->client.phase!=WBL_CLOSED&&p->client.phase!=WBL_FAILED){
        operation=cancel?wbl_client_cancel(&p->client):wbl_client_close(&p->client);
    }
    p->client_ready=0;
    if(p->control>=0){shutdown(p->control,SHUT_RDWR);close(p->control);p->control=-1;}
    if(p->volume>=0){shutdown(p->volume,SHUT_RDWR);close(p->volume);p->volume=-1;}
    int64_t start=now_ms(),end=start+timeout_ms,kill_at=end-100;int term_sent=0;
    while(!p->result.reaped&&!p->result.child_lost){
        if(!p->result.terminal_received&&!p->report_bad){
            int rc=receive_report(p,0,!p->ready_seen);
            if(rc&&rc!=-EAGAIN&&!p->result.terminal_received)p->report_bad=rc;
        }
        int rc=reap(p);if(rc)break;
        int64_t now=now_ms();
        if(!term_sent&&now-start>=1000){if(!kill(p->pid,SIGTERM)||errno==ESRCH)term_sent=1;}
        if(now>=kill_at&&!p->result.kill_sent){
            if(!kill(p->pid,SIGKILL)||errno==ESRCH)p->result.kill_sent=1;
            else {if(!operation)operation=-errno;break;}
        }
        if(now>=end)break;
        /* Child reap is bounded. Descriptor readiness handles normal exit;
         * after terminal EOF, a short reap wait avoids a busy HUP loop. */
        int delay=(int)(end-now);if(delay>10)delay=10;
        struct pollfd fd={p->updates,POLLIN,0};poll(p->result.terminal_received||p->report_bad?NULL:&fd,p->result.terminal_received||p->report_bad?0:1,delay);
    }
    if(p->result.reaped&&!p->result.terminal_received&&!p->report_bad){
        int rc=receive_report(p,0,!p->ready_seen);if(rc&&!p->result.terminal_received)p->report_bad=rc;
    }
    *result=p->result;
    if(!p->result.reaped&&!p->result.child_lost)return operation?operation:-EINPROGRESS;
    int child_error=p->result.child_lost?-ECHILD:p->result.exit_code?-EIO:0;
    close(p->updates);free(p);*out=NULL;
    return operation?operation:child_error;
}
static int connect_owned(struct sony_bt_link_process **out,unsigned activation,const char *runtime,const char *seed,int log,
                            const uint8_t peer[6],int cancel_fd,int with_volume,struct sony_bt_tx_exit *failure){
    if(!out||!failure)return -EINVAL;if(*out)return -EBUSY;reset(failure);
    (void)activation;if(!peer)return -EINVAL;
    int rc=0;if(!rc)rc=trusted(runtime);if(!rc)rc=trusted(seed);if(rc)return rc;
    char executable[1280],library[1536];
    snprintf(executable,sizeof(executable),"%s/sony-bt-tx-supervisor",runtime);
    snprintf(library,sizeof(library),"LD_LIBRARY_PATH=%s/lib:/vendor/lib:/system/lib",runtime);
    struct sony_bt_link_process *p=calloc(1,sizeof(*p));if(!p)return -ENOMEM;p->control=p->updates=p->volume=-1;reset(&p->result);p->creator=getpid();
    int random=open("/dev/urandom",O_RDONLY|O_CLOEXEC);
    if(random>=0){(void)read(random,&p->nonce,sizeof(p->nonce));close(random);}
    int data[2]={-1,-1},updates[2]={-1,-1},volume[2]={-1,-1};
    if(socketpair(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0,data)||socketpair(AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC,0,updates)||
       (with_volume&&socketpair(AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC,0,volume))){
        rc=-errno;for(unsigned i=0;i<2;++i){if(data[i]>=0)close(data[i]);if(updates[i]>=0)close(updates[i]);if(volume[i]>=0)close(volume[i]);}free(p);return rc;}
    char control_arg[24],update_arg[24],nonce[24],parent[24],volume_arg[24];
    snprintf(control_arg,sizeof(control_arg),"%d",data[1]);snprintf(update_arg,sizeof(update_arg),"%d",updates[1]);
    snprintf(nonce,sizeof(nonce),"%llx",(unsigned long long)p->nonce);snprintf(parent,sizeof(parent),"%d",p->creator);
    snprintf(volume_arg,sizeof(volume_arg),"%d",volume[1]);
    char *args[]={executable,(char*)runtime,(char*)seed,control_arg,update_arg,nonce,parent,with_volume?volume_arg:NULL,NULL};
    char *env[]={library,"PATH=/system/bin",NULL};p->pid=fork();
    if(!p->pid){
        close(data[0]);close(updates[0]);if(volume[0]>=0)close(volume[0]);
        (void)prctl(PR_SET_PDEATHSIG,SIGKILL);
        if(dup2(log,1)<0||dup2(log,2)<0||fcntl(data[1],F_SETFD,0)||fcntl(updates[1],F_SETFD,0))_exit(126);
        if(with_volume&&fcntl(volume[1],F_SETFD,0))_exit(126);
        execve(executable,args,env);_exit(127);
    }
    close(data[1]);close(updates[1]);if(volume[1]>=0)close(volume[1]);
    if(p->pid<0){rc=-errno;close(data[0]);close(updates[0]);if(volume[0]>=0)close(volume[0]);free(p);return rc;}
    p->control=data[0];p->updates=updates[0];p->volume=volume[0];*out=p;
    int64_t deadline=now_ms()+8000;
    do{int64_t left=deadline-now_ms();
        if(left<=0)rc=-ETIMEDOUT;
        else if(cancel_fd<0)rc=receive_report(p,(int)left,1);
        else {struct pollfd f[2]={{p->updates,POLLIN,0},{cancel_fd,POLLIN,0}};int n=poll(f,2,(int)left);
            rc=n<0?(errno==EINTR?-EAGAIN:-errno):!n?-ETIMEDOUT:f[1].revents?-ECANCELED:receive_report(p,0,1);}
    }while(rc==-EAGAIN);
    if(!rc&&with_volume)rc=wva_client_init(&p->volume_client,p->volume,p->nonce);
    if(!rc){rc=wbl_client_init(&p->client,p->control,cancel_fd,p->nonce);if(!rc){p->client_ready=1;rc=wbl_client_connect(&p->client,peer);}}
    if(rc){int original=rc;int stopped=sony_bt_link_process_stop(out,1,35000,failure);return stopped?stopped:original;}
    return 0;
}
int sony_bt_link_process_connect_cancel(struct sony_bt_link_process **out,unsigned activation,const char *runtime,const char *seed,int log,
                            const uint8_t peer[6],int cancel_fd,struct sony_bt_tx_exit *failure){
    return connect_owned(out,activation,runtime,seed,log,peer,cancel_fd,0,failure);
}
int sony_bt_link_process_connect_volume_cancel(struct sony_bt_link_process **out,unsigned activation,const char *runtime,const char *seed,int log,
                            const uint8_t peer[6],int cancel_fd,struct sony_bt_tx_exit *failure){
    return connect_owned(out,activation,runtime,seed,log,peer,cancel_fd,1,failure);
}
int sony_bt_link_process_connect(struct sony_bt_link_process **out,unsigned activation,const char *runtime,const char *seed,int log,
                            const uint8_t peer[6],struct sony_bt_tx_exit *failure){
    return sony_bt_link_process_connect_cancel(out,activation,runtime,seed,log,peer,-1,failure);
}
