/* Source-specific receiver WDP5 + checked WKP7 owner; no route activation.
 * Exact-child/FD/reap discipline derived from the native DSD supervisor. */
#define _GNU_SOURCE
#include "sony_audio_receiver_sink_process.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

struct sony_receiver_sink_process {
    pid_t pid;
    struct sony_pcm_sink_client *client;
    struct sony_receiver_sink_process_exit result;
    struct sony_receiver_sink_process_scope scope;
    struct sony_dsd_power_wire_server *power;
    int power_fd,closing,busy;
};
struct exec_error {int stage,error;};
static long long now_ms(void) {
    struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t))return -1;
    return (long long)t.tv_sec*1000+t.tv_nsec/1000000;
}
static void reset_result(struct sony_receiver_sink_process_exit *r) {
    memset(r,0,sizeof(*r));r->exit_code=-1;r->output_cleanup_unverified=1;
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
        if(size<0)child_error(4,4);
        if(!size)break;
        unsigned int at=0;
        while(at<(unsigned int)size) {
            unsigned short step;
            if((unsigned int)size-at<20){errno=EIO;child_error(4,4);}
            memcpy(&step,buffer+at+16,2); /* linux_dirent64: name starts at 19. */
            if(step<20||step>(unsigned int)size-at){errno=EIO;child_error(4,4);}
            const char *name=(const char *)buffer+at+19;
            unsigned int n=0,value=0;
            while(n<(unsigned int)step-19&&name[n]>='0'&&name[n]<='9') {
                if(value>214748364u||(value==214748364u&&name[n]>'7')){errno=EIO;child_error(4,4);}
                value=value*10+(unsigned int)(name[n++]-'0');
            }
            if(n&&n<(unsigned int)step-19&&!name[n]) {
                if(value>4&&(int)value!=listing)close((int)value);
            }else if(!((step>=21&&name[0]=='.'&&name[1]==0)||(step>=22&&name[0]=='.'&&name[1]=='.'&&name[2]==0))) {
                errno=EIO;child_error(4,4);
            }
            at+=step;
        }
    }
    close(listing);
}
static void child_exec(pid_t parent,int root,int log_fd,int input,int output,int error,int power,char arguments[6][24]) {
    (void)parent;if(prctl(PR_SET_PDEATHSIG,SIGKILL))child_error(error,1);
    int directory=open("/proc/self/fd",O_RDONLY|O_DIRECTORY|O_CLOEXEC);
    if(directory<0)child_error(error,4);
    int listing=duplicate(directory);int saved=errno;close(directory);
    if(listing<0){errno=saved;child_error(error,4);}
    if(fchdir(root)||chroot(".")||chdir("/"))child_error(error,2);
    if(dup2(input,0)<0||dup2(output,1)<0||dup2(log_fd,2)<0)child_error(error,3);
    if(dup2(power,3)<0||fcntl(3,F_SETFD,0))child_error(error,3);
    if(dup2(error,4)<0)child_error(error,3);
    if(fcntl(4,F_SETFD,FD_CLOEXEC))child_error(4,3);
    close_child_fds(listing);
    /* Parent may block or ignore signals. Do not carry either into the worker. */
    struct sigaction action;memset(&action,0,sizeof(action));action.sa_handler=SIG_DFL;
    sigemptyset(&action.sa_mask);
    for(int n=1;n<NSIG;++n)if(n!=SIGKILL&&n!=SIGSTOP)sigaction(n,&action,NULL);
    sigset_t empty;sigemptyset(&empty);
    if(sigprocmask(SIG_SETMASK,&empty,NULL))child_error(4,4);
    char *argv[]={"/bin/sony-receiver-legacy-worker","--receiver-pcm",arguments[0],arguments[1],arguments[2],arguments[3],arguments[4],arguments[5],NULL};
    char *env[]={"LD_LIBRARY_PATH=/lib","LD_PRELOAD=/lib/libsony-receiver-legacy-io.so:/lib/libwalkdroid-sony-receiver-legacy-output.so","LANG=C",NULL};
    execve(argv[0],argv,env);child_error(4,5);
}
static int await_exec(int fd,int timeout_ms,struct sony_receiver_sink_process_exit *result) {
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
static int reap(struct sony_receiver_sink_process *p) {
    int status;pid_t got=waitpid(p->pid,&status,WNOHANG);
    if(got<0&&errno==EINTR)return 0; /* Outer loop rechecks its deadline. */
    if(!got)return 0;
    if(got==p->pid) {
        p->result.reaped=1;
        if(WIFEXITED(status))p->result.exit_code=WEXITSTATUS(status);
        if(WIFSIGNALED(status))p->result.signal_number=WTERMSIG(status);
        p->pid=0;return 1;
    }
    if(got<0&&errno==ECHILD){p->result.child_lost=1;p->pid=0;return SONY_RECEIVER_SINK_PROCESS_CHILD_LOST;}
    return (-EIO);
}
static int owner_check(void *v){
    struct sony_receiver_sink_process *p=v;int rc=p->scope.owner_check(p->scope.context);return rc>0?-EPROTO:rc;
}
static int resource_assure(void *v,unsigned operation){
    struct sony_receiver_sink_process *p=v;int rc=owner_check(p);if(!rc)rc=p->scope.assure(p->scope.context,operation);return rc>0?-EPROTO:rc;
}
static int peer_reaped(void *v){
    struct sony_receiver_sink_process *p=v;if(p->result.child_lost)return -ECHILD;
    if(p->result.reaped)return 1;int rc=reap(p);return rc<0?rc:p->result.reaped;
}
static int progress(void *v){
    struct sony_receiver_sink_process *p=v;
    struct sony_dsd_power_wire_state state;int rc=sony_dsd_power_wire_server_state(p->power,&state);if(rc)return rc;
    if(state.released)return 0;if(state.dead)return state.first_error?state.first_error:-EPIPE;
    struct pollfd pending={p->power_fd,POLLIN,0};rc=poll(&pending,1,0);
    if(rc<0)return -errno;if(!rc)return 0;if(!(pending.revents&POLLIN))return -EPIPE;
    /* No work on an empty socket. A real request still enters server_step,
     * which checks exact lease/peer authority before receive, dispatch and
     * acknowledgement. Its resource policy gates every actual PCM write. */
    rc=sony_dsd_power_wire_server_step(p->power);return rc<0?rc:0;
}
static int progress_fd(void *v,int *out){
    /* Pure descriptor/state observation, not an ownership or output grant.
     * Client pins and rechecks the socket inode/type/flags around poll. */
    struct sony_receiver_sink_process *p=v;
    struct sony_dsd_power_wire_state state;int rc=sony_dsd_power_wire_server_state(p->power,&state);if(rc)return rc;
    if(state.released){*out=-1;return 0;}if(state.dead)return state.first_error?state.first_error:-EPIPE;*out=p->power_fd;return 0;
}
int sony_receiver_sink_process_state(struct sony_receiver_sink_process *p,struct sony_receiver_sink_process_exit *out){
    if(!p||!out)return -EINVAL;if(__atomic_exchange_n(&p->busy,1,__ATOMIC_ACQUIRE))return -EBUSY;
    int rc=p->power?sony_dsd_power_wire_server_state(p->power,&p->result.power):0;
    if(!rc)*out=p->result;__atomic_store_n(&p->busy,0,__ATOMIC_RELEASE);return rc;
}
static int retained(struct sony_receiver_sink_process *p,struct sony_receiver_sink_process_exit *out,int rc){
    *out=p->result;__atomic_store_n(&p->busy,0,__ATOMIC_RELEASE);return rc;
}
int sony_receiver_sink_process_stop(struct sony_receiver_sink_process **owner,int timeout_ms,struct sony_receiver_sink_process_exit *out){
    if(!owner||!*owner||!out||timeout_ms<1||timeout_ms>30000)return -EINVAL;
    struct sony_receiver_sink_process *p=*owner;
    if(__atomic_exchange_n(&p->busy,1,__ATOMIC_ACQUIRE))return -EBUSY;
    if(p->client){int rc=sony_pcm_sink_destroy(p->client);if(rc)return retained(p,out,rc);p->client=NULL;}
    p->closing=1;long long start=now_ms();if(start<0)return retained(p,out,-EIO);
    long long end=start+timeout_ms,kill_at=start+timeout_ms/2;int status=0;
    while(p->pid){
        status=reap(p);if(status)break;
        /* Closing pipes can make the original child request Close/Release.
         * Service those exact messages while awaiting its natural exit. */
        if(p->power){int serviced=progress(p);(void)serviced;}
        if(!p->pid)break;
        long long now=now_ms();if(now<0){status=-EIO;break;}
        if(!p->result.kill_sent&&now>=kill_at){
            if(!kill(p->pid,SIGKILL)||errno==ESRCH)p->result.kill_sent=1;else{status=-errno;break;}
        }
        if(now>=end)break;int delay=(int)(end-now);if(delay>5)delay=5;poll(NULL,0,delay);
    }
    if(p->pid)return retained(p,out,status<0?status:SONY_RECEIVER_SINK_PROCESS_CLEANUP_PENDING);
    if(p->result.child_lost)return retained(p,out,SONY_RECEIVER_SINK_PROCESS_CHILD_LOST);
    int cleaned=owner_check(p);
    if(!cleaned)cleaned=p->power?sony_dsd_power_wire_server_finish_peer(p->power):p->scope.backend.output_closed(p->scope.backend.context);
    if(cleaned>0)cleaned=-EPROTO;p->result.power_cleanup_status=cleaned;p->result.power_cleanup_confirmed=!cleaned;
    if(p->power)sony_dsd_power_wire_server_state(p->power,&p->result.power);
    if(cleaned)return retained(p,out,cleaned);
    if(p->power){cleaned=sony_dsd_power_wire_server_destroy(&p->power);if(cleaned)return retained(p,out,cleaned);}
    *out=p->result;free(p);*owner=NULL;return 0;
}
int sony_receiver_sink_process_pid(struct sony_receiver_sink_process *p){return p&&!p->closing?(int)p->pid:0;}
struct sony_pcm_sink_client *sony_receiver_sink_process_client(struct sony_receiver_sink_process *p){return p&&!p->closing?p->client:NULL;}
int sony_receiver_sink_process_start(struct sony_receiver_sink_process **out,unsigned int activation,
    int root_fd,int log_fd,const struct sony_receiver_sink_process_scope *scope,int timeout_ms,struct sony_receiver_sink_process_exit *failure) {
    if(!out||!failure)return -22;
    if(*out)return -16; /* Never overwrite ownership of an existing child. */
    reset_result(failure);
    if(activation!=SONY_RECEIVER_SINK_PROCESS_EXPLICIT_START||root_fd<0||log_fd<0||
       !scope||!scope->assure||!scope->owner_check||!scope->backend.acquire||!scope->backend.output_closed||!scope->backend.release||timeout_ms<1)return -22;
    int status=0;if(status)return status;
    status=scope->owner_check(scope->context);if(status)return status>0?-EPROTO:status;
    int root=duplicate(root_fd),log=duplicate(log_fd),tx[2]={-1,-1},rx[2]={-1,-1},error[2]={-1,-1},power[2]={-1,-1};
    if(root<0||log<0){status=-errno;goto done;}
    if((status=make_pipe(tx))||(status=make_pipe(rx))||(status=make_pipe(error)))goto done;
    {int raw[2];status=sony_dsd_power_wire_pair(raw);if(status)goto done;
        power[0]=duplicate(raw[0]);power[1]=duplicate(raw[1]);int saved=errno;close(raw[0]);close(raw[1]);
        if(power[0]<0||power[1]<0){status=-saved;goto done;}}
    struct sony_receiver_sink_process *p=calloc(1,sizeof(*p));
    if(!p){status=-12;goto done;}
    reset_result(&p->result);p->scope=*scope;p->power_fd=-1;pid_t parent=getpid();
    char arguments[6][24];snprintf(arguments[0],24,"%ld",(long)parent);
    snprintf(arguments[1],24,"%llu",(unsigned long long)scope->session);snprintf(arguments[2],24,"%llu",(unsigned long long)scope->generation);
    snprintf(arguments[3],24,"%llu",(unsigned long long)scope->receipt);snprintf(arguments[4],24,"%u",scope->cfw_profile);snprintf(arguments[5],24,"%u",scope->mode);
    p->pid=fork();
    if(p->pid<0){status=-errno;free(p);goto done;}
    if(!p->pid)child_exec(parent,root,log,tx[0],rx[1],error[1],power[1],arguments);
    p->result.spawned_pid=(int)p->pid;close_fd(&tx[0]);close_fd(&rx[1]);close_fd(&error[1]);close_fd(&power[1]);
    struct sony_dsd_power_wire_binding binding={scope->session,scope->generation,scope->receipt,p->pid,0,0};
    struct sony_dsd_power_wire_authority auth={owner_check,peer_reaped,p};p->power_fd=power[0];
    struct sony_dsd_power_wire_policy policy={resource_assure,p};
    status=sony_dsd_power_wire_server_attach_policy(&p->power,power[0],&binding,&scope->backend,&auth,50,&policy);
    if(!status){power[0]=-1;status=await_exec(error[0],timeout_ms,&p->result);}
    if(!status) {
        status=0;if(!status)status=owner_check(p);
        if(!status&&(fcntl(tx[1],F_SETFL,O_NONBLOCK)||fcntl(rx[0],F_SETFL,O_NONBLOCK)))status=-errno;
        if(!status){struct sony_pcm_sink_events cb={progress,progress_fd,p};status=sony_pcm_sink_attach_events(&p->client,tx[1],rx[0],timeout_ms,&cb);}
        if(!status){tx[1]=rx[0]=-1;status=sony_pcm_sink_ready(p->client);}
    }
    if(!status)*out=p;
    else {
        /* Close all write ends BEFORE waiting for EOF cleanup. */
        close_fd(&tx[1]);close_fd(&rx[0]);
        *out=p;int cleaned=sony_receiver_sink_process_stop(out,1000,failure);
        if(*out)status=cleaned;
    }
done:
    close_fd(&root);close_fd(&log);close_fd(&power[0]);close_fd(&power[1]);
    close_fd(&tx[0]);close_fd(&tx[1]);close_fd(&rx[0]);close_fd(&rx[1]);close_fd(&error[0]);close_fd(&error[1]);
    return status;
}
