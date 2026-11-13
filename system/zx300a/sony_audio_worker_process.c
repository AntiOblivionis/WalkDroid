#define _GNU_SOURCE
#include "sony_audio_worker_process.h"
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
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

struct sony_worker_process {
    pid_t pid;
    struct wdc_client *client;
    struct sony_worker_exit result;
};
struct exec_error {int stage,error;};
static long long now_ms(void) {
    struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t))return -1;
    return (long long)t.tv_sec*1000+t.tv_nsec/1000000;
}
static void reset_result(struct sony_worker_exit *r) {
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
static void close_child_fds(int listing,int ai) {
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
                if(value>(ai?4u:3u)&&(int)value!=listing)close((int)value);
            }else if(!((step>=21&&name[0]=='.'&&name[1]==0)||(step>=22&&name[0]=='.'&&name[1]=='.'&&name[2]==0))) {
                errno=EIO;child_error(3,4);
            }
            at+=step;
        }
    }
    close(listing);
}
static void child_exec(pid_t parent,int root,int log_fd,int input,int output,int error,
    int feature,int profile,int ai,const char *lo,const char *hi,int guarded,int signature,
    const char *profile_text,const char *signature_text,const char *ship_text) {
    (void)parent;if(prctl(PR_SET_PDEATHSIG,SIGKILL))child_error(error,1);
    int directory=open("/proc/self/fd",O_RDONLY|O_DIRECTORY|O_CLOEXEC);
    if(directory<0)child_error(error,4);
    int listing=duplicate(directory);int saved=errno;close(directory);
    if(listing<0){errno=saved;child_error(error,4);}
    if(fchdir(root)||chroot(".")||chdir("/"))child_error(error,2);
    if(dup2(input,0)<0||dup2(output,1)<0||dup2(log_fd,2)<0)child_error(error,3);
    if(dup2(error,3)<0)child_error(error,3);
    if(fcntl(3,F_SETFD,FD_CLOEXEC))child_error(3,3);
    if(ai>=0&&(dup2(ai,4)<0||fcntl(4,F_SETFD,0)))child_error(3,3);
    close_child_fds(listing,ai>=0);
    /* Parent may block or ignore signals. Do not carry either into the worker. */
    struct sigaction action;memset(&action,0,sizeof(action));action.sa_handler=SIG_DFL;
    sigemptyset(&action.sa_mask);
    for(int n=1;n<NSIG;++n)if(n!=SIGKILL&&n!=SIGSTOP)sigaction(n,&action,NULL);
    sigset_t empty;sigemptyset(&empty);
    if(sigprocmask(SIG_SETMASK,&empty,NULL))child_error(3,4);
    char *argv[16]={"/bin/sony-output-worker","--snapshot-dir","/data/config",feature?"1":"0",
        profile>=0?(char *)profile_text:NULL,NULL};
    char *ai_argv[16]={"/bin/sony-output-ai-worker","--snapshot-dir","/data/config",feature?"1":"0",
        (char *)profile_text,"--ai-fd","4",(char *)lo,(char *)hi,NULL};
    char *guarded_argv[16]={"/bin/sony-output-ai-guarded-worker","--snapshot-dir","/data/config",feature?"1":"0",
        (char *)profile_text,"--ai-fd","4",(char *)lo,(char *)hi,"--parent-paused-resources",NULL};
    char *env[]={"LD_LIBRARY_PATH=/lib","LD_PRELOAD=/lib/libwalkdroid-sony-io-guard-v2.so","LANG=C",NULL};
    char **selected=guarded?guarded_argv:ai>=0?ai_argv:argv;
    if(signature>=0){
        unsigned at=0;while(selected[at])++at;
        selected[at++]="--cfw-context";selected[at++]=(char *)signature_text;selected[at++]=(char *)ship_text;selected[at]=NULL;
    }
    execve(selected[0],selected,env);child_error(3,5);
}
static int await_exec(int fd,int timeout_ms,struct sony_worker_exit *result) {
    long long start=now_ms();if(start<0)return WDA_CLIENT_IO;
    long long end=start+timeout_ms;struct exec_error message;unsigned int bytes=0;
    while(bytes<sizeof(message)) {
        long long now=now_ms();if(now<0)return WDA_CLIENT_IO;
        if(now>=end)return WDA_CLIENT_TIMEOUT;
        struct pollfd p={fd,POLLIN,0};int n=poll(&p,1,(int)(end-now));
        if(n<0&&errno==EINTR)continue;
        if(n<=0)return n==0?WDA_CLIENT_TIMEOUT:WDA_CLIENT_IO;
        if(p.revents&(POLLERR|POLLNVAL))return WDA_CLIENT_IO;
        ssize_t got=read(fd,(char *)&message+bytes,sizeof(message)-bytes);
        if(got<0&&errno==EINTR)continue;
        if(got<0)return WDA_CLIENT_IO;
        if(!got)return bytes?WDA_CLIENT_IO:0; /* CLOEXEC closed the error pipe. */
        bytes+=(unsigned int)got;
    }
    result->launch_stage=message.stage;result->launch_errno=message.error;
    return WDA_CLIENT_IO;
}
static int reap(struct sony_worker_process *p) {
    int status;pid_t got=waitpid(p->pid,&status,WNOHANG);
    if(got<0&&errno==EINTR)return 0; /* Outer loop rechecks its deadline. */
    if(!got)return 0;
    if(got==p->pid) {
        p->result.reaped=1;
        if(WIFEXITED(status))p->result.exit_code=WEXITSTATUS(status);
        if(WIFSIGNALED(status))p->result.signal_number=WTERMSIG(status);
        p->pid=0;return 1;
    }
    if(got<0&&errno==ECHILD){p->result.child_lost=1;p->pid=0;return SONY_PROCESS_CHILD_LOST;}
    return WDA_CLIENT_IO;
}
int sony_worker_process_stop(struct sony_worker_process **owner,int timeout_ms,struct sony_worker_exit *out) {
    if(!owner||!*owner||!out)return -22;
    struct sony_worker_process *p=*owner;
    if(p->client){wdc_client_destroy(p->client);p->client=NULL;}
    long long start=now_ms();if(start<0){*out=p->result;return WDA_CLIENT_IO;}
    long long end=start+timeout_ms,kill_at=start+timeout_ms/2;
    int status=0;
    while(p->pid) {
        status=reap(p);if(status)break;
        long long now=now_ms();if(now<0){status=WDA_CLIENT_IO;break;}
        if(!p->result.kill_sent&&now>=kill_at) {
            /* PID has not been reaped; caller contract forbids other waiters. */
            if(!kill(p->pid,SIGKILL)||errno==ESRCH)p->result.kill_sent=1;
            else {status=WDA_CLIENT_IO;break;}
        }
        if(now>=end)break;
        int delay=(int)(end-now);if(delay>5)delay=5;
        poll(NULL,0,delay);
    }
    *out=p->result;
    if(!p->pid) {
        int result=p->result.child_lost?SONY_PROCESS_CHILD_LOST:0;
        free(p);*owner=NULL;return result;
    }
    return status<0?status:SONY_PROCESS_CLEANUP_PENDING;
}
struct wdc_client *sony_worker_process_client(struct sony_worker_process *p) {return p?p->client:NULL;}
static int process_start(struct sony_worker_process **out,unsigned int activation,
    int root_fd,int log_fd,int feature,int profile,int ai_fd,uint64_t nonce,int timeout_ms,struct sony_worker_exit *failure,int ai_requested,int guarded,int signature,int ship) {
    if(!out||!failure)return -22;
    if(*out)return -16; /* Never overwrite ownership of an existing child. */
    reset_result(failure);
    (void)activation;
    int status=0;
    int root=duplicate(root_fd),log=duplicate(log_fd),ai=-1,tx[2]={-1,-1},rx[2]={-1,-1},error[2]={-1,-1};
    if(root<0||log<0){status=-errno;goto done;}
    if(ai_requested){
        ai=duplicate(ai_fd);if(ai<0){status=-errno;goto done;}
    }
    if((status=make_pipe(tx))||(status=make_pipe(rx))||(status=make_pipe(error)))goto done;
    struct sony_worker_process *p=calloc(1,sizeof(*p));
    if(!p){status=-12;goto done;}
    char lo[16],hi[16],profile_text[16],signature_text[16],ship_text[16];
    snprintf(lo,sizeof(lo),"%u",(unsigned)nonce);snprintf(hi,sizeof(hi),"%u",(unsigned)(nonce>>32));
    snprintf(profile_text,sizeof(profile_text),"%d",profile);
    snprintf(signature_text,sizeof(signature_text),"%d",signature);
    snprintf(ship_text,sizeof(ship_text),"%d",ship);
    reset_result(&p->result);pid_t parent=getpid();p->pid=fork();
    if(p->pid<0){status=-errno;free(p);goto done;}
    if(!p->pid)child_exec(parent,root,log,tx[0],rx[1],error[1],feature,profile,ai,lo,hi,guarded,signature,profile_text,signature_text,ship_text);
    close_fd(&ai);
    close_fd(&tx[0]);close_fd(&rx[1]);close_fd(&error[1]);
    status=await_exec(error[0],timeout_ms,&p->result);
    if(!status) {
        status=wdc_client_attach_output(&p->client,tx[1],rx[0],timeout_ms);
        if(!status){tx[1]=rx[0]=-1;status=wdc_client_output_ready(p->client);}
    }
    if(!status)*out=p;
    else {
        /* Close all write ends BEFORE waiting for EOF cleanup. */
        close_fd(&tx[1]);close_fd(&rx[0]);
        *out=p;int cleaned=sony_worker_process_stop(out,1000,failure);
        if(*out)status=cleaned;
    }
done:
    close_fd(&root);close_fd(&log);
    close_fd(&ai);
    close_fd(&tx[0]);close_fd(&tx[1]);close_fd(&rx[0]);close_fd(&rx[1]);close_fd(&error[0]);close_fd(&error[1]);
    return status;
}
int sony_worker_process_start(struct sony_worker_process **out,unsigned int activation,
    int root_fd,int log_fd,int feature,int timeout_ms,struct sony_worker_exit *failure) {
    return process_start(out,activation,root_fd,log_fd,feature,-1,-1,0,timeout_ms,failure,0,0,-1,0);
}
int sony_worker_process_start_profile(struct sony_worker_process **out,unsigned int activation,
    int root_fd,int log_fd,int feature,int profile,int timeout_ms,struct sony_worker_exit *failure) {
    return process_start(out,activation,root_fd,log_fd,feature,profile,-1,0,timeout_ms,failure,0,0,-1,0);
}
int sony_worker_process_start_profile_ai(struct sony_worker_process **out,unsigned int activation,
    int root_fd,int log_fd,int feature,int profile,int ai_fd,uint64_t nonce,int timeout_ms,struct sony_worker_exit *failure) {
    return process_start(out,activation,root_fd,log_fd,feature,profile,ai_fd,nonce,timeout_ms,failure,1,0,-1,0);
}
int sony_worker_process_start_profile_ai_guarded(struct sony_worker_process **out,unsigned int activation,
    int root_fd,int log_fd,int feature,int profile,int ai_fd,uint64_t nonce,int timeout_ms,struct sony_worker_exit *failure) {
    return process_start(out,activation,root_fd,log_fd,feature,profile,ai_fd,nonce,timeout_ms,failure,1,1,-1,0);
}

int sony_worker_process_start_profile_context(struct sony_worker_process **out,unsigned int activation,
    int root_fd,int log_fd,int feature,int profile,int signature,int ship,int timeout_ms,struct sony_worker_exit *failure){
    return process_start(out,activation,root_fd,log_fd,feature,profile,-1,0,timeout_ms,failure,0,0,signature,ship);
}
int sony_worker_process_start_profile_ai_guarded_context(struct sony_worker_process **out,unsigned int activation,
    int root_fd,int log_fd,int feature,int profile,int ai_fd,uint64_t nonce,int signature,int ship,int timeout_ms,struct sony_worker_exit *failure){
    return process_start(out,activation,root_fd,log_fd,feature,profile,ai_fd,nonce,timeout_ms,failure,1,1,signature,ship);
}
