#define _GNU_SOURCE 1
#include "sony_audio_ai_process.h"
#include "sony_audio_ai_root.h"
#include "sony_audio_ai_process_protocol.h"
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <poll.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
struct sony_ai_process {
    pid_t pid;int control,exported;
    struct sony_audio_owner *owner;
    uint64_t nonce,generation;
    struct sony_ai_process_exit result;
};
struct exec_error {int stage,error;};
static unsigned occupied;
static int64_t now_ms(void){struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t))return -1;return (int64_t)t.tv_sec*1000+t.tv_nsec/1000000;}
static void reset(struct sony_ai_process_exit *r){memset(r,0,sizeof(*r));r->exit_code=-1;}
static int duplicate(int fd){return fcntl(fd,F_DUPFD_CLOEXEC,10);}
static void close_fd(int *fd){if(*fd>=0){close(*fd);*fd=-1;}}
static int allowed(const struct sony_ai_process *p){
    return p?0:-EINVAL;
}
static int owner_valid(struct sony_ai_process *p){
    return sony_audio_owner_check(p->owner);
}
static void child_error(int fd,int stage){
    struct exec_error e={stage,errno};unsigned at=0;
    while(at<sizeof(e)){ssize_t n=write(fd,(char *)&e+at,sizeof(e)-at);if(n<0&&errno==EINTR)continue;if(n<=0)break;at+=(unsigned)n;}
    _exit(127);
}
static void close_child_fds(void){
    struct rlimit limits;unsigned long maximum=1024;
    if(!getrlimit(RLIMIT_NOFILE,&limits))maximum=limits.rlim_cur>1048576?1048576:(unsigned long)limits.rlim_cur;
    for(unsigned long fd=5;fd<maximum;++fd)close((int)fd);
}
static void child_exec(int root,int log,int socket,int error,int ready,char **argv){
    (void)prctl(PR_SET_PDEATHSIG,SIGKILL);
    if(!unshare(CLONE_NEWNS|CLONE_NEWNET))(void)mount(NULL,"/",NULL,MS_REC|MS_PRIVATE,NULL);
    if(fchdir(root)||chroot(".")||chdir("/"))child_error(error,4);
    if(dup2(socket,0)<0||dup2(log,1)<0||dup2(log,2)<0||dup2(error,3)<0||dup2(ready,4)<0)child_error(error,5);
    if(fcntl(3,F_SETFD,FD_CLOEXEC))child_error(3,5);
    if(fcntl(4,F_SETFD,0))child_error(3,5);
    close_child_fds();
    struct sigaction action;memset(&action,0,sizeof(action));action.sa_handler=SIG_DFL;sigemptyset(&action.sa_mask);
    for(int n=1;n<NSIG;++n)if(n!=SIGKILL&&n!=SIGSTOP)sigaction(n,&action,NULL);
    sigset_t empty;sigemptyset(&empty);if(sigprocmask(SIG_SETMASK,&empty,NULL))child_error(3,7);
    char *env[]={"LD_LIBRARY_PATH=/lib","LD_PRELOAD=/lib/libc_stubs.so","LANG=C",NULL};
    execve("/bin/sony-ai-worker",argv,env);child_error(3,9);
}
static int await_exec(int fd,int64_t end,struct sony_ai_process_exit *out){
    struct exec_error message;unsigned at=0;
    while(at<sizeof(message)){
        int64_t now=now_ms();if(now<0)return -EIO;if(now>=end)return -ETIMEDOUT;
        struct pollfd p={fd,POLLIN,0};int n=poll(&p,1,(int)(end-now));
        if(n<0&&errno==EINTR)continue;if(n<=0)return n?-errno:-ETIMEDOUT;
        if(p.revents&(POLLERR|POLLNVAL))return -EIO;
        ssize_t got=read(fd,(char *)&message+at,sizeof(message)-at);
        if(got<0&&errno==EINTR)continue;if(got<0)return -errno;
        if(!got)return at?-EIO:0;at+=(unsigned)got;
    }
    out->launch_stage=message.stage;out->launch_errno=message.error;return -message.error;
}
static int await_ready(int fd,pid_t pid,uint64_t nonce,int64_t end){
    (void)pid;(void)nonce;
    for(;;){
        int64_t now=now_ms();if(now<0)return -EIO;if(now>=end)return -ETIMEDOUT;
        struct pollfd p={fd,POLLIN,0};int n=poll(&p,1,(int)(end-now));
        if(n<0&&errno==EINTR)continue;if(n<=0)return n?-errno:-ETIMEDOUT;
        if(p.revents&(POLLERR|POLLNVAL))return -EIO;
        struct wai_process_ready r;struct iovec io={&r,sizeof(r)};struct msghdr message;
        memset(&message,0,sizeof(message));message.msg_iov=&io;message.msg_iovlen=1;
        /* No ancillary buffer: passed rights are discarded by the kernel. */
        ssize_t bytes=recvmsg(fd,&message,MSG_DONTWAIT|MSG_TRUNC);
        if(bytes<0&&(errno==EINTR||errno==EAGAIN))continue;
        if(bytes!=sizeof(r))return -EPROTO;
        return 0;
    }
}
static int reap(struct sony_ai_process *p){
    int status;pid_t n=waitpid(p->pid,&status,WNOHANG);
    if(n==p->pid){p->result.reaped=1;p->result.exit_code=WIFEXITED(status)?WEXITSTATUS(status):-1;
        p->result.signal_number=WIFSIGNALED(status)?WTERMSIG(status):0;p->pid=0;return 1;}
    if(n<0&&errno==EINTR)return 0;
    if(n<0&&errno==ECHILD){p->result.child_lost=1;p->pid=0;return -ECHILD;}
    return n<0?-errno:0;
}
int sony_ai_process_stop(struct sony_ai_process **owner,int timeout,struct sony_ai_process_exit *out){
    if(!owner||!*owner||!out)return -EINVAL;
    if(timeout<=0)timeout=3000;
    struct sony_ai_process *p=*owner;int r=allowed(p);if(r)return r;
    if(p->control>=0){shutdown(p->control,SHUT_RDWR);close_fd(&p->control);}
    int64_t start=now_ms();if(start<0){*out=p->result;return -EIO;}
    int64_t end=start+timeout,kill_at=start+timeout/2;r=0;
    while(p->pid){
        r=reap(p);if(r)break;
        int64_t now=now_ms();if(now<0){r=-EIO;break;}
        if(!p->result.kill_sent&&now>=kill_at){
            if(!kill(p->pid,SIGKILL)||errno==ESRCH)p->result.kill_sent=1;
            else {r=-errno;break;}
        }
        if(now>=end)break;int delay=(int)(end-now);if(delay>5)delay=5;poll(NULL,0,delay);
    }
    *out=p->result;
    if(!p->pid){int result=p->result.child_lost?-ECHILD:0;free(p);*owner=NULL;__atomic_store_n(&occupied,0,__ATOMIC_RELEASE);return result;}
    return r<0?r:SONY_AI_PROCESS_CLEANUP_PENDING;
}
int sony_ai_process_export(struct sony_ai_process *p,int *fd,uint64_t *nonce){
    int r=allowed(p);if(r)return r;if(!fd||*fd!=-1||!nonce)return -EINVAL;
    if(p->control<0)return -EBADF;
    r=owner_valid(p);if(r)return r;r=0;if(r)return r;
    int copy=duplicate(p->control);if(copy<0)return -errno;
    p->exported=1;*fd=copy;*nonce=p->nonce;return 0;
}
int sony_ai_process_pid(const struct sony_ai_process *p){return p&&!allowed(p)?p->pid:0;}
int sony_ai_process_start(struct sony_ai_process **out,unsigned activation,struct sony_audio_owner *owner,
    int root_fd,int log_fd,uint64_t nonce,int timeout,struct sony_ai_process_exit *failure){
    if(!out||!failure)return -EINVAL;if(*out)return -EBUSY;reset(failure);
    (void)activation;if(!owner)return -EINVAL;if(timeout<=0)timeout=3000;
    int r=0;if(!r)r=sony_audio_owner_check(owner);if(r)return r;
    int root=duplicate(root_fd),log=duplicate(log_fd),pair[2]={-1,-1},error[2]={-1,-1},ready[2]={-1,-1};
    if(root<0||log<0){r=-errno;goto done;}
    if(socketpair(AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC,0,pair)||pipe2(error,O_CLOEXEC)||
       socketpair(AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC,0,ready)){r=-errno;goto done;}
    /* Normalize descriptors before child fixed 0/1/2/3 mapping. */
    for(unsigned i=0;i<2;++i){int n=duplicate(pair[i]);if(n<0){r=-errno;goto done;}close(pair[i]);pair[i]=n;}
    for(unsigned i=0;i<2;++i){int n=duplicate(error[i]);if(n<0){r=-errno;goto done;}close(error[i]);error[i]=n;}
    for(unsigned i=0;i<2;++i){int n=duplicate(ready[i]);if(n<0){r=-errno;goto done;}close(ready[i]);ready[i]=n;}
    unsigned expected=0;if(!__atomic_compare_exchange_n(&occupied,&expected,1,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE)){r=-EBUSY;goto done;}
    struct sony_ai_process *p=calloc(1,sizeof(*p));
    if(!p){__atomic_store_n(&occupied,0,__ATOMIC_RELEASE);r=-ENOMEM;goto done;}
    p->control=pair[0];pair[0]=-1;p->owner=owner;p->nonce=nonce;
    p->generation=sony_audio_owner_generation(owner);reset(&p->result);p->result.audio_generation=p->generation;
    r=owner_valid(p);if(r){close_fd(&p->control);free(p);__atomic_store_n(&occupied,0,__ATOMIC_RELEASE);goto done;}
    char lo[16],hi[16],limit[16];snprintf(lo,sizeof(lo),"%u",(unsigned)nonce);snprintf(hi,sizeof(hi),"%u",(unsigned)(nonce>>32));snprintf(limit,sizeof(limit),"%d",timeout);
    char *argv[]={"/bin/sony-ai-worker",lo,hi,limit,"--ready-fd","4",NULL};
    int64_t end=now_ms();if(end<0){r=-EIO;close_fd(&p->control);free(p);__atomic_store_n(&occupied,0,__ATOMIC_RELEASE);goto done;}end+=timeout;
    p->pid=fork();
    if(p->pid<0){r=-errno;close_fd(&p->control);free(p);__atomic_store_n(&occupied,0,__ATOMIC_RELEASE);goto done;}
    if(!p->pid)child_exec(root,log,pair[1],error[1],ready[1],argv);
    close_fd(&pair[1]);close_fd(&error[1]);close_fd(&ready[1]);*out=p;
    r=await_exec(error[0],end,&p->result);if(!r)r=await_ready(ready[0],p->pid,nonce,end);
    if(!r)p->result.libraries_ready=1;
    if(!r)r=owner_valid(p);
    if(r){int original=r;int stopped=sony_ai_process_stop(out,1000,failure);r=*out?stopped:original;}
done:
    close_fd(&root);close_fd(&log);for(unsigned i=0;i<2;++i){close_fd(&pair[i]);close_fd(&error[i]);close_fd(&ready[i]);}return r;
}
