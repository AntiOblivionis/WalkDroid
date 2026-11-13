/* Persistent original-TX owner, one native session per process. Not a tone
 * fixture: PCM comes only from an inherited root-controller socket. */
#define _GNU_SOURCE
#include "sony_audio_bt_tx_process.h"
#include "sony_audio_bt_native_guard.h"
#include "sony_audio_bt_tx_private_root.h"
#include "sony_audio_bt_supervisor_finish.h"
#include <poll.h>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <time.h>
static int64_t now_ms(void){struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t))return -1;return (int64_t)t.tv_sec*1000+t.tv_nsec/1000000;}
static int code(int status){return WIFEXITED(status)?WEXITSTATUS(status):128+WTERMSIG(status);}
static int report(int fd,uint64_t nonce,unsigned event,unsigned sequence,uint64_t audio,uint64_t radio,int status,int worker,int daemon,unsigned flags){
    struct wbt_process_report r={WBT_PROCESS_MAGIC,1,event,sequence,nonce,audio,radio,status,worker,daemon,flags,(unsigned)getpid(),0};
    return send(fd,&r,sizeof(r),MSG_NOSIGNAL)==sizeof(r)?0:-EPIPE;
}
static int wait_child(pid_t pid,int signal_fd,unsigned timeout,int *exit_code,int *forced){
    int64_t end=now_ms()+timeout;
    for(;;){int status;pid_t p=waitpid(pid,&status,WNOHANG);
        if(p==pid){*exit_code=code(status);return 0;}if(p<0&&errno!=EINTR)return -errno;
        int64_t left=end-now_ms();if(left<=0)break;
        struct pollfd f={signal_fd,POLLIN,0};int rc=poll(&f,1,(int)left);
        if(rc<0&&errno!=EINTR)return -errno;
        if(f.revents&POLLIN){struct signalfd_siginfo info;ssize_t n=read(signal_fd,&info,sizeof(info));(void)n;}
    }
    *forced=1;if(kill(pid,SIGKILL)&&errno!=ESRCH)return -errno;
    end=now_ms()+3000;
    for(;;){int status;pid_t p=waitpid(pid,&status,WNOHANG);if(p==pid){*exit_code=code(status);return 0;}
        if(p<0&&errno!=EINTR)return -errno;
        int64_t left=end-now_ms();if(left<=0)return -ETIMEDOUT;
        struct pollfd f={signal_fd,POLLIN,0};poll(&f,1,(int)left);
        if(f.revents&POLLIN){struct signalfd_siginfo info;ssize_t n=read(signal_fd,&info,sizeof(info));(void)n;}
    }
}
static int descriptor(const char *v){char *end;errno=0;long n=strtol(v,&end,10);return errno||!*v||*end||n<0||n>0x7fffffff?-1:(int)n;}
int main(int argc,char **argv){
#ifdef SONY_TX_VOLUME_CONTROL
    if(argc!=7&&argc!=8)return 64;
#else
    if(argc!=7)return 64;
#endif
    int volume=-1;
    int control=descriptor(argv[3]),updates=descriptor(argv[4]);char *end;
    errno=0;uint64_t nonce=strtoull(argv[5],&end,16);if(errno||!*argv[5]||*end||argv[5][0]=='-')return 64;
    int parent=descriptor(argv[6]);if(control<0||updates<0||parent<0)return 64;
#ifdef SONY_TX_VOLUME_CONTROL
    if(argc==8){volume=descriptor(argv[7]);if(volume<0)return 65;}
#endif
    signal(SIGPIPE,SIG_IGN);
    struct sigaction action;memset(&action,0,sizeof(action));action.sa_handler=SIG_DFL;sigemptyset(&action.sa_mask);
    const int reset_signals[]={SIGCHLD,SIGTERM,SIGINT,SIGHUP};
    for(unsigned i=0;i<4;++i)if(sigaction(reset_signals[i],&action,NULL))return 65;
    sigset_t mask;sigemptyset(&mask);sigaddset(&mask,SIGCHLD);sigaddset(&mask,SIGTERM);sigaddset(&mask,SIGINT);sigaddset(&mask,SIGHUP);
    if(sigprocmask(SIG_BLOCK,&mask,NULL))return 65;
#ifndef SONY_TX_PARENT_DEATH_SIGNAL
#define SONY_TX_PARENT_DEATH_SIGNAL SIGKILL
#endif
    /* Install the blocked stop signal before arming parent-death delivery. */
    (void)prctl(PR_SET_PDEATHSIG,SONY_TX_PARENT_DEATH_SIGNAL);
    if(sony_bt_guard_close_fds(control,updates,volume))return 65;
    int signals=signalfd(-1,&mask,SFD_CLOEXEC|SFD_NONBLOCK);if(signals<0)return 65;
    int rc=0;
    struct sony_audio_owner *audio=NULL;struct sony_bt_radio_owner *radio=NULL;uint64_t ag=0,rg=0;
    if(!rc){rc=sony_audio_owner_acquire(&audio,SONY_OWNER_BT_TX);if(!rc)ag=sony_audio_owner_generation(audio);}
    if(!rc){rc=sony_bt_radio_acquire(&radio,SONY_BT_RADIO_NATIVE_TX);if(!rc)rg=sony_bt_radio_generation(radio);}
    struct sony_tx_mounts mounts;memset(&mounts,0,sizeof(mounts));int root=-1;
    if(!rc)rc=sony_tx_prepare_root(&mounts,argv[1],argv[2],nonce,&root);
    int daemon=-1,worker=-1,de=-1,we=-1,df=0,wf=0,ready=0,daemon_reaped=1,worker_reaped=1;
    /* Continuous original diagnostic output is intentionally discarded; it
     * must not consume unbounded player RAM or flash during normal playback. */
    int null=-1;if(!rc){null=open("/dev/null",O_WRONLY|O_CLOEXEC);if(null<0)rc=-errno;}
    if(!rc){daemon=sony_bt_guard_spawn(root,null,-1,nonce);if(daemon<1)rc=-errno;else daemon_reaped=0;}
    if(!rc){int64_t deadline=now_ms()+3000;
        for(;;){struct stat st;if(!fstatat(root,"tmp/bt.int.adp",&st,0))break;
            int status;pid_t p=waitpid(daemon,&status,WNOHANG);if(p==daemon){de=code(status);daemon_reaped=1;rc=-ECHILD;break;}
            if(p<0){rc=-errno;break;}if(now_ms()>=deadline){rc=-ETIMEDOUT;break;}
            struct pollfd f={signals,POLLIN,0};poll(&f,1,10);
            if(f.revents&POLLIN){struct signalfd_siginfo info;if(read(signals,&info,sizeof(info))==sizeof(info)&&info.ssi_signo!=SIGCHLD){rc=-ECANCELED;break;}}
        }
    }
    if(!rc){worker=sony_bt_guard_spawn_volume(root,null,control,nonce,volume);if(worker<1)rc=-errno;else worker_reaped=0;}
    close(control);control=-1;if(null>=0)close(null);
    if(volume>=0){close(volume);volume=-1;}
    if(!rc){rc=report(updates,nonce,WBT_PROCESS_READY,1,ag,rg,0,-1,-1,0);if(!rc)ready=1;}
    /* No periodic wait/patrol during streaming: block on SIGCHLD/stop events.
     * A native child exits on FINISH, CANCEL, socket EOF or its own failure. */
    while(!rc&&!worker_reaped){
        int status;pid_t p=waitpid(worker,&status,WNOHANG);if(p==worker){we=code(status);worker_reaped=1;break;}if(p<0){rc=-errno;break;}
        p=waitpid(daemon,&status,WNOHANG);if(p==daemon){de=code(status);daemon_reaped=1;rc=-EPIPE;break;}if(p<0){rc=-errno;break;}
        struct pollfd f={signals,POLLIN,0};int n=poll(&f,1,-1);if(n<0){if(errno==EINTR)continue;rc=-errno;break;}
        struct signalfd_siginfo info;ssize_t got=read(signals,&info,sizeof(info));
        if(got<0&&(errno==EAGAIN||errno==EINTR))continue;
        if(got!=sizeof(info)){rc=-EIO;break;}
        if(info.ssi_signo!=SIGCHLD){rc=-ECANCELED;break;}
    }
    if(worker>0&&!worker_reaped){kill(worker,SIGTERM);int waited=wait_child(worker,signals,25000,&we,&wf);worker_reaped=!waited;if(waited&&!rc)rc=waited;}
    if(daemon>0&&!daemon_reaped){kill(daemon,SIGTERM);int waited=wait_child(daemon,signals,2000,&de,&df);daemon_reaped=!waited;if(waited&&!rc)rc=waited;}
    int cancelled=rc==-ECANCELED;if(cancelled)rc=0;
    if(worker>0&&worker_reaped&&we&&!rc&&!cancelled)rc=-EIO;
    if(root>=0)close(root);
    int unmounted=sony_tx_unmount(&mounts);
    rc=sony_bt_supervisor_finish(rc,unmounted,&radio,&audio);
    if(cancelled&&!rc)rc=-ECANCELED;
    unsigned flags=(worker_reaped&&daemon_reaped?WBT_PROCESS_CHILDREN_REAPED:0)|(wf||df?WBT_PROCESS_FORCED:0);
    int sent=report(updates,nonce,WBT_PROCESS_TERMINAL,ready?2:1,ag,rg,rc,we,de,flags);
    close(updates);close(signals);
#ifdef SONY_TX_CLEAN_ORPHAN_EXIT
    /* Original owner may have exited, so its report socket can be gone. */
    if(sent==-EPIPE)sent=0;
#endif
    return sony_bt_supervisor_exit_code(rc,sent);
}
