/* WDF4 dedicated supervisor, derived from the audited PCM process owner.
 * Kept separate so unfinished DSF integration cannot change deployed PCM. */
#define _GNU_SOURCE
#include "sony_audio_dsf_process.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

struct sony_dsf_process {
    pid_t pid;
    struct sony_dsf_client *client;
    struct sony_dsf_process_exit result;
};
struct exec_error {int stage,error;};
static long long now_ms(void) {
    struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t))return -1;
    return (long long)t.tv_sec*1000+t.tv_nsec/1000000;
}
static void reset_result(struct sony_dsf_process_exit *r) {
    memset(r,0,sizeof(*r));r->exit_code=-1;
}
static int duplicate(int fd) {return fcntl(fd,F_DUPFD_CLOEXEC,10);}
static void close_fd(int *fd) {if(*fd>=0){close(*fd);*fd=-1;}}
static int make_pipe(int p[2]) {
    int raw[2];if(pipe2(raw,O_CLOEXEC))return -errno;
    p[0]=duplicate(raw[0]);p[1]=duplicate(raw[1]);
    int error=errno;close(raw[0]);close(raw[1]);
    if(p[0]<0||p[1]<0){close_fd(&p[0]);close_fd(&p[1]);return -error;}
    return 0;
}
static void child_error(int fd,int stage) {
    struct exec_error e={stage,errno};const unsigned char *p=(const void *)&e;
    unsigned int done=0;
    while(done<sizeof(e)) {
        ssize_t n=write(fd,p+done,sizeof(e)-done);
        if(n<0&&errno==EINTR)continue;
        if(n<=0)break;
        done+=(unsigned int)n;
    }
    _exit(127);
}
static void close_child_fds(int listing) {
    /* Opened before chroot, closed before exec. Enumerate actual descriptors,
     * including those above a subsequently lowered RLIMIT_NOFILE. Compatible
     * with 3.10; no close_range syscall, host /proc mount or allocation needed. */
    unsigned char buffer[4096];
    for(;;) {
        long size=syscall(__NR_getdents64,listing,buffer,sizeof(buffer));
        if(size<0&&errno==EINTR)continue;
        if(size<0)child_error(3,4);
        if(!size)break;
        unsigned int at=0;
        while(at<(unsigned int)size) {
            unsigned short step;
            if((unsigned int)size-at<20){errno=EIO;child_error(3,4);}
            memcpy(&step,buffer+at+16,2); /* linux_dirent64: name starts at 19. */
            if(step<20||step>(unsigned int)size-at){errno=EIO;child_error(3,4);}
            const char *name=(const char *)buffer+at+19;
            unsigned int n=0,value=0;
            while(n<(unsigned int)step-19&&name[n]>='0'&&name[n]<='9') {
                if(value>214748364u||(value==214748364u&&name[n]>'7')){errno=EIO;child_error(3,4);}
                value=value*10+(unsigned int)(name[n++]-'0');
            }
            if(n&&n<(unsigned int)step-19&&!name[n]) {
                if(value>3&&(int)value!=listing)close((int)value);
            }else if(!((step>=21&&name[0]=='.'&&name[1]==0)||(step>=22&&name[0]=='.'&&name[1]=='.'&&name[2]==0))) {
                errno=EIO;child_error(3,4);
            }
            at+=step;
        }
    }
    close(listing);
}
static void child_exec(pid_t parent,int root,int log_fd,int input,int output,int error) {
    (void)parent;if(prctl(PR_SET_PDEATHSIG,SIGKILL))child_error(error,1);
    int directory=open("/proc/self/fd",O_RDONLY|O_DIRECTORY|O_CLOEXEC);
    if(directory<0)child_error(error,4);
    int listing=duplicate(directory);int saved=errno;close(directory);
    if(listing<0){errno=saved;child_error(error,4);}
    if(fchdir(root)||chroot(".")||chdir("/"))child_error(error,2);
    if(dup2(input,0)<0||dup2(output,1)<0||dup2(log_fd,2)<0)child_error(error,3);
    if(dup2(error,3)<0)child_error(error,3);
    if(fcntl(3,F_SETFD,FD_CLOEXEC))child_error(3,3);
    close_child_fds(listing);
    /* Parent may block or ignore signals. Do not carry either into the worker. */
    struct sigaction action;memset(&action,0,sizeof(action));action.sa_handler=SIG_DFL;
    sigemptyset(&action.sa_mask);
    for(int n=1;n<NSIG;++n)if(n!=SIGKILL&&n!=SIGSTOP)sigaction(n,&action,NULL);
    sigset_t empty;sigemptyset(&empty);
    if(sigprocmask(SIG_SETMASK,&empty,NULL))child_error(3,4);
    char *argv[]={"/bin/sony-dsf-worker","--snapshot-dir","/data/config",NULL};
    char *env[8]={"LD_LIBRARY_PATH=/lib","LANG=C",NULL};
    const char *names[]={"WALKDROID_DSD_FADE_CONSTRUCT","WALKDROID_DSD_FADE_INITIALIZE",
        "WALKDROID_DSD_FADE_CONFIGURE","WALKDROID_DSD_FADE_PROCESS","WALKDROID_DSD_FADE_DESTROY"};
    char values[5][128];unsigned count=2;
    for(unsigned i=0;i<5;++i){
        const char *value=getenv(names[i]);if(!value)continue;
        snprintf(values[i],sizeof(values[i]),"%s=%s",names[i],value);env[count++]=values[i];
    }
    env[count]=NULL;
    execve(argv[0],argv,env);child_error(3,5);
}
static int await_exec(int fd,int timeout_ms,struct sony_dsf_process_exit *result) {
    long long start=now_ms();if(start<0)return (-EIO);
    long long end=start+timeout_ms;struct exec_error message;unsigned int bytes=0;
    while(bytes<sizeof(message)) {
        long long now=now_ms();if(now<0)return (-EIO);
        if(now>=end)return (-ETIMEDOUT);
        struct pollfd p={fd,POLLIN,0};int n=poll(&p,1,(int)(end-now));
        if(n<0&&errno==EINTR)continue;
        if(n<=0)return n==0?(-ETIMEDOUT):(-EIO);
        if(p.revents&(POLLERR|POLLNVAL))return (-EIO);
        ssize_t got=read(fd,(char *)&message+bytes,sizeof(message)-bytes);
        if(got<0&&errno==EINTR)continue;
        if(got<0)return (-EIO);
        if(!got)return bytes?(-EIO):0; /* CLOEXEC closed the error pipe. */
        bytes+=(unsigned int)got;
    }
    result->launch_stage=message.stage;result->launch_errno=message.error;
    return (-EIO);
}
static int reap(struct sony_dsf_process *p) {
    int status;pid_t got=waitpid(p->pid,&status,WNOHANG);
    if(got<0&&errno==EINTR)return 0; /* Outer loop rechecks its deadline. */
    if(!got)return 0;
    if(got==p->pid) {
        p->result.reaped=1;
        if(WIFEXITED(status))p->result.exit_code=WEXITSTATUS(status);
        if(WIFSIGNALED(status))p->result.signal_number=WTERMSIG(status);
        p->pid=0;return 1;
    }
    if(got<0&&errno==ECHILD){p->result.child_lost=1;p->pid=0;return SONY_DSF_PROCESS_CHILD_LOST;}
    return (-EIO);
}
int sony_dsf_process_stop(struct sony_dsf_process **owner,int timeout_ms,struct sony_dsf_process_exit *out) {
    if(!owner||!*owner||!out)return -22;
    struct sony_dsf_process *p=*owner;
    if(p->client){int rc=sony_dsf_client_destroy(p->client);if(rc){*out=p->result;return rc;}p->client=NULL;}
    long long start=now_ms();if(start<0){*out=p->result;return (-EIO);}
    long long end=start+timeout_ms,kill_at=start+timeout_ms/2;
    int status=0;
    while(p->pid) {
        status=reap(p);if(status)break;
        long long now=now_ms();if(now<0){status=(-EIO);break;}
        if(!p->result.kill_sent&&now>=kill_at) {
            /* PID has not been reaped; caller contract forbids other waiters. */
            if(!kill(p->pid,SIGKILL)||errno==ESRCH)p->result.kill_sent=1;
            else {status=(-EIO);break;}
        }
        if(now>=end)break;
        int delay=(int)(end-now);if(delay>5)delay=5;
        poll(NULL,0,delay);
    }
    *out=p->result;
    if(!p->pid) {
        int result=p->result.child_lost?SONY_DSF_PROCESS_CHILD_LOST:0;
        free(p);*owner=NULL;return result;
    }
    return status<0?status:SONY_DSF_PROCESS_CLEANUP_PENDING;
}
struct sony_dsf_client *sony_dsf_process_client(struct sony_dsf_process *p) {return p?p->client:NULL;}
int sony_dsf_process_start(struct sony_dsf_process **out,unsigned int activation,
    int root_fd,int log_fd,int timeout_ms,struct sony_dsf_process_exit *failure) {
    if(!out||!failure)return -22;
    if(*out)return -16; /* Never overwrite ownership of an existing child. */
    reset_result(failure);
    (void)activation;
    int status=0;
    int root=duplicate(root_fd),log=duplicate(log_fd),tx[2]={-1,-1},rx[2]={-1,-1},error[2]={-1,-1};
    if(root<0||log<0){status=-errno;goto done;}
    if((status=make_pipe(tx))||(status=make_pipe(rx))||(status=make_pipe(error)))goto done;
    struct sony_dsf_process *p=calloc(1,sizeof(*p));
    if(!p){status=-12;goto done;}
    reset_result(&p->result);pid_t parent=getpid();p->pid=fork();
    if(p->pid<0){status=-errno;free(p);goto done;}
    if(!p->pid)child_exec(parent,root,log,tx[0],rx[1],error[1]);
    close_fd(&tx[0]);close_fd(&rx[1]);close_fd(&error[1]);
    status=await_exec(error[0],timeout_ms,&p->result);
    if(!status) {
        status=0;
        if(!status&&(fcntl(tx[1],F_SETFL,O_NONBLOCK)||fcntl(rx[0],F_SETFL,O_NONBLOCK)))status=-errno;
        if(!status)status=sony_dsf_client_attach(&p->client,tx[1],rx[0],timeout_ms);
        if(!status){tx[1]=rx[0]=-1;status=sony_dsf_client_ready(p->client);}
    }
    if(!status)*out=p;
    else {
        /* Close all write ends BEFORE waiting for EOF cleanup. */
        close_fd(&tx[1]);close_fd(&rx[0]);
        *out=p;int cleaned=sony_dsf_process_stop(out,1000,failure);
        if(*out)status=cleaned;
    }
done:
    close_fd(&root);close_fd(&log);
    close_fd(&tx[0]);close_fd(&tx[1]);close_fd(&rx[0]);close_fd(&rx[1]);close_fd(&error[0]);close_fd(&error[1]);
    return status;
}
