/* Persistent broker; one dedicated private-namespace USB
 * controller child per explicit ON. EOF requests OFF. No shell/app paths,
 * automatic ON, stale-owner reset or global ADB/device operations. */
#define _GNU_SOURCE
#include "sony_audio_receiver_protocol.h"
#include "sony_audio_usb_mode.h"
#include "sony_audio_jack.h"
#include "sony_audio_route.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sched.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
static volatile sig_atomic_t quitting;
static int journal=-1,audit_enabled,verify_muted;
#ifndef SONY_BT_RECEIVER_BROKER
struct audit {int directory,fd;unsigned files,sequence;uint64_t bytes;};
static int audit_close(struct audit *a){int rc=0;if(a->fd>=0){if(fsync(a->fd))rc=-errno;if(close(a->fd)&&!rc)rc=-errno;a->fd=-1;}return rc;}
static int audit_input(void *v,const struct sony_usb_event *event,const void *data,unsigned frames){
    struct audit *a=v;if(!event||!data||!frames||frames>SONY_USB_DUPLEX_MAX_FRAMES||event->stream.format!=SONY_USB_PCM)return -EINVAL;
    size_t bytes=(size_t)frames*8;if(a->bytes+bytes>UINT64_C(33554432))return -EFBIG;
    if(a->fd<0||a->sequence!=event->sequence){int rc=audit_close(a);if(rc)return rc;if(a->files>=8)return -EOVERFLOW;
        char name[64];snprintf(name,sizeof(name),"source_%04u.s32",++a->files);a->fd=openat(a->directory,name,O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600);if(a->fd<0)return -errno;a->sequence=event->sequence;
        if(journal>=0)dprintf(journal,"AUDIT file=%s sequence=%u rate=%u bits=%u\n",name,event->sequence,event->stream.rate,event->stream.bit_width);
    }
    size_t at=0;while(at<bytes){ssize_t n=write(a->fd,(const char *)data+at,bytes-at);if(n<0&&errno==EINTR)continue;if(n<=0)return n<0?-errno:-EIO;at+=(size_t)n;}
    a->bytes+=bytes;return 0;
}
#endif
static void interrupted(int n){(void)n;quitting=1;}
static void child_changed(int n){(void)n;}
static uint64_t now(void){struct timespec t;if(clock_gettime(CLOCK_BOOTTIME,&t))return 0;return (uint64_t)t.tv_sec*1000000000u+(unsigned)t.tv_nsec;}
static int path(char out[2048],const char *a,const char *b){int n=snprintf(out,2048,"%s/%s",a,b);return n<0||n>=2048?-ENAMETOOLONG:0;}
static int directory(const char *p){return open(p,O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);}
static int bind_mount(const char *source,const char *target,int readonly){
    int rc=0;if(!rc&&mount(source,target,NULL,MS_BIND,NULL))rc=-errno;
    if(!rc&&readonly&&mount(NULL,target,NULL,MS_BIND|MS_REMOUNT|MS_RDONLY,NULL))rc=-errno;
    if(journal>=0)dprintf(journal,"BIND %s -> %s ro=%d rc=%d\n",source,target,readonly,rc);return rc;
}
static int mounted_roots(const char *runtime,const char *session){
    int rc=unshare(CLONE_NEWNS)?-errno:0;if(!rc&&mount(NULL,"/",NULL,MS_REC|MS_PRIVATE,NULL))rc=-errno;
    char source[2048],target[2048];
    const char *kinds[]={"receiver","sink"};
    for(unsigned i=0;!rc&&i<2;++i){char name[64];snprintf(name,sizeof(name),"%s-root",kinds[i]);rc=path(target,runtime,name);if(!rc)rc=bind_mount(target,target,1);
        snprintf(name,sizeof(name),"%s-root/data/config",kinds[i]);if(!rc)rc=path(target,runtime,name);if(!rc)rc=path(source,session,"private");if(!rc)rc=bind_mount(source,target,0);
        snprintf(name,sizeof(name),"%s-root/tmp",kinds[i]);if(!rc)rc=path(target,runtime,name);snprintf(name,sizeof(name),"tmp-%s",kinds[i]);if(!rc)rc=path(source,session,name);if(!rc)rc=bind_mount(source,target,0);
    }
    const char *nodes[]={"null","urandom","snd/controlC0","snd/pcmC0D0p","snd/pcmC0D4p","snd/timer"};
    for(unsigned i=0;!rc&&i<6;++i){char name[128];rc=path(source,"/dev",nodes[i]);
        snprintf(name,sizeof(name),"sink-root/dev/%s",nodes[i]);if(!rc)rc=path(target,runtime,name);if(!rc)rc=bind_mount(source,target,0);
    }
    return rc;
}
static int close_others(int stop,int updates){
    DIR *dir=opendir("/proc/self/fd");if(!dir)return -errno;struct dirent *entry;int held=dirfd(dir),rc=0;
    while((entry=readdir(dir))){char *end;long fd=strtol(entry->d_name,&end,10);if(*end||fd<=2||fd>0x7fffffff||fd==held||fd==stop||fd==updates)continue;if(close((int)fd)&&!rc)rc=-errno;}closedir(dir);return rc;
}
static int stop_requested(int fd){
    unsigned char byte;ssize_t n=read(fd,&byte,1);return quitting||n==1||n==0||(n<0&&errno!=EAGAIN&&errno!=EINTR);
}
static int await_idle(int stop){
    uint64_t begin=now();if(!begin)return -EIO;
    for(;;){
        if(stop_requested(stop))return -ECANCELED;
        struct sony_audio_owner_reader *reader=NULL;int rc=sony_audio_owner_read_begin(&reader);
        if(!rc){rc=sony_audio_route_idle();int end=sony_audio_owner_read_end(&reader);if(end)return end;if(!rc)return 0;}
        /* Only a live exclusive holder/active PCM is a bounded handover wait.
         * Dirty/stale ownership is never cleared, retried as ON, or ignored. */
        /* AudioFlinger's own standby delay is 3 seconds, before the Sony HAL
         * has closed its output. Allow that complete handover, bounded and
         * cancelable. Dirty/stale errors still fail immediately; no owner is
         * acquired, cleared or taken over while waiting. */
        if(rc!=-EBUSY)return rc;uint64_t time=now();if(!time||time-begin>=UINT64_C(6000000000))return -EBUSY;
        struct pollfd p={stop,POLLIN,0};if(poll(&p,1,20)<0&&errno!=EINTR)return -errno;
    }
}
static int update(int fd,const struct wdr_reply *r,int terminal){
    uint64_t deadline=now()+UINT64_C(2000000000);
    for(;;){ssize_t n=write(fd,r,sizeof(*r));if(n==sizeof(*r))return 0;
        if(n<0&&errno==EINTR)continue;if(!terminal)return n<0&&(errno==EAGAIN||errno==EWOULDBLOCK)?0:-EPIPE;
        if(n>=0||!(errno==EAGAIN||errno==EWOULDBLOCK))return -EIO;
        uint64_t time=now();if(!time||time>=deadline)return -ETIMEDOUT;
        struct pollfd p={fd,POLLOUT,0};int rc=poll(&p,1,100);if(rc<0&&errno!=EINTR)return -errno;
    }
}
#ifndef SONY_BT_RECEIVER_BROKER
static void snapshot(struct wdr_reply *r,const struct sony_usb_mode_state *s){
    r->phase=(unsigned)s->phase;r->error=s->first_error;r->generation=s->generation;
    r->flags=s->enabled?WDR_ENABLED:0;r->source_frames=s->duplex.successful_push_frames;
    r->output_acked_bytes=s->duplex.output.playback.sink_acked_bytes;r->rate=s->duplex.output.output_rate;
    r->cleanup_error=s->cleanup_error;r->retained=s->phase==SONY_USB_MODE_RETAINED;
}
static void receiver_child(const char *runtime,const char *session,int stop,int updates,uint64_t id){
    int rc=close_others(stop,updates);signal(SIGINT,interrupted);signal(SIGTERM,interrupted);signal(SIGHUP,interrupted);signal(SIGPIPE,SIG_IGN);
    char p[2048],drp[2048],srp[2048],config[2048];if(!rc)rc=path(p,session,"controller.log");
    if(!rc){journal=open(p,O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600);if(journal<0)rc=-errno;}
    struct wdr_reply r={.magic=WDR_MAGIC,.version=1,.phase=WDR_STARTING,.session=id};
    if(!rc&&!stop_requested(stop))rc=mounted_roots(runtime,session);else if(!rc)rc=-ECANCELED;
    int dr=-1,sr=-1,dl=-1,sl=-1;
    if(!rc)rc=path(drp,runtime,"receiver-root");if(!rc)rc=path(srp,runtime,"sink-root");if(!rc)rc=path(config,session,"private");
    if(!rc){dr=directory(drp);sr=directory(srp);if(dr<0||sr<0)rc=-errno;}
    if(!rc)rc=path(p,session,"dsp.log");if(!rc){dl=open(p,O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600);if(dl<0)rc=-errno;}
    if(!rc)rc=path(p,session,"sink.log");if(!rc){sl=open(p,O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600);if(sl<0)rc=-errno;}
    struct sony_audio_jack_state jack={0};if(!rc)rc=sony_audio_jack_read(&jack);
    /* Prefer balanced output when reported; otherwise request single-ended. */
    struct audit audit={.directory=-1,.fd=-1};if(!rc&&audit_enabled){audit.directory=directory(session);if(audit.directory<0)rc=-errno;}
    if(journal>=0&&verify_muted)dprintf(journal,"DIAGNOSTIC_VERIFY_MUTED\n");
    struct sony_usb_duplex_launch launch={.downstream={dr,sr,dl,sl,config,0,verify_muted?SONY_RECEIVER_SESSION_VERIFY_MUTED:SONY_RECEIVER_SESSION_LIVE_VOLUME,5000},.expected_output=jack.btl?SONY_OUTPUT_BTL:SONY_OUTPUT_SE,.observe=audit_enabled?audit_input:NULL,.context=&audit};
    struct sony_usb_mode *mode=NULL;struct sony_usb_mode_state state={0};struct sony_usb_mode_cleanup cleanup={0};
    if(!rc)rc=await_idle(stop);
    if(!rc&&!stop_requested(stop))rc=sony_usb_mode_start(&mode,SONY_USB_MODE_EXPLICIT_START,&launch);else if(!rc)rc=-ECANCELED;
    if(mode){int got=sony_usb_mode_state(mode,&state);if(got&&!rc)rc=got;snapshot(&r,&state);}
    else {r.phase=WDR_OFFLINE;r.error=rc;r.flags=WDR_CLEAN|WDR_REFUSED;}
    int reporting_failed=update(updates,&r,1);uint64_t last=now();unsigned last_phase=r.phase;int stopping=!!reporting_failed||(mode&&state.phase==SONY_USB_MODE_FAULT);
    while(mode){
        uint64_t before_frames=state.duplex.successful_push_frames;int progressed=0;
        if(stop_requested(stop))stopping=1;
        if(stopping){int s=sony_usb_mode_stop(&mode,5000,&cleanup);state=cleanup.state;snapshot(&r,&state);
            if(!mode&&!s){r.phase=WDR_OFFLINE;r.flags=WDR_CLEAN;r.retained=0;break;}
        }else if(state.phase==SONY_USB_MODE_READY||state.phase==SONY_USB_MODE_STREAMING){
            (void)sony_usb_mode_step(mode,SONY_USB_DUPLEX_MAX_FRAMES);(void)sony_usb_mode_state(mode,&state);snapshot(&r,&state);progressed=state.duplex.successful_push_frames>before_frames;
            /* Known admission/data failure must enter the normal receipt-based
             * teardown without waiting for an ADB-dependent OFF. Uncertain
             * cleanup still retains the owner; never clear it or reopen. */
            if(state.phase==SONY_USB_MODE_FAULT)stopping=1;
        }
        uint64_t time=now();if(r.phase!=last_phase||!time||time-last>=UINT64_C(1000000000)){
            if(journal>=0&&r.phase!=last_phase)dprintf(journal,"PHASE %u error=%d cleanup=%d retained=%u\n",r.phase,r.error,r.cleanup_error,r.retained);
            if(update(updates,&r,r.phase!=last_phase))stopping=1;last=time;last_phase=r.phase;
        }
        int event=-1,timeout=-1;
        if(!stopping){if(sony_usb_mode_wait(mode,&event,&timeout)){event=-1;timeout=-1;}}
        else if(state.phase!=SONY_USB_MODE_RETAINED)timeout=state.phase==SONY_USB_MODE_STOPPING?20:0;
        /* One-second status/lease observation while idle; progress does not
         * incur a fixed sleep. RETAINED waits without further mutations. */
        if(!stopping&&(state.phase==SONY_USB_MODE_READY||state.phase==SONY_USB_MODE_STREAMING)&&timeout<0)timeout=1000;
        if(!stopping&&progressed)timeout=0;
        struct pollfd wait[2]={{stopping?-1:stop,POLLIN,0},{event,POLLIN,0}};
        if(stopping&&state.phase==SONY_USB_MODE_RETAINED){(void)update(updates,&r,0);timeout=-1;}
        int ready=poll(wait,2,timeout);if(ready<0&&errno!=EINTR)stopping=1;
    }
    int audit_result=audit_close(&audit);if(audit.directory>=0)close(audit.directory);if(audit_result&&!r.error)r.error=audit_result;
    r.flags|=WDR_CHILD_TERMINAL;if(journal>=0){if(audit_enabled)dprintf(journal,"AUDIT_END bytes=%llu files=%u result=%d\n",(unsigned long long)audit.bytes,audit.files,audit_result);dprintf(journal,"TERMINAL phase=%u error=%d cleanup=%d\n",r.phase,r.error,r.cleanup_error);close(journal);}
    if(dr>=0)close(dr);if(sr>=0)close(sr);if(dl>=0)close(dl);if(sl>=0)close(sl);
    int sent=update(updates,&r,1);close(stop);close(updates);_exit(sent?1:0);
}
#else
#include "sony_audio_bt_receiver_broker.inc"
#endif
static int session_directory(const char *base,uint64_t id,char out[2048]){
    int rc;char leaf[32];snprintf(leaf,sizeof(leaf),"%016llx",(unsigned long long)id);rc=path(out,base,leaf);if(rc)return rc;
    if(mkdir(out,0700))return -errno;const char *names[]={"private","tmp-receiver","tmp-sink"
#ifdef SONY_BT_RECEIVER_BROKER
        ,"radio-data","tmp-radio"
#endif
    };char p[2048];
    for(unsigned i=0;i<sizeof(names)/sizeof(names[0]);++i){rc=path(p,out,names[i]);if(rc)return rc;if(mkdir(p,0700))return -errno;}return 0;
}
static void request_stop(int *fd){if(*fd<0)return;unsigned char byte=1;ssize_t sent=write(*fd,&byte,1);(void)sent;close(*fd);*fd=-1;}
static int consume_updates(int fd,uint64_t session,struct wdr_reply *state,int *terminal){
    (void)session;struct wdr_reply r;ssize_t n;int rc=0;
    while((n=read(fd,&r,sizeof(r)))==sizeof(r)){
        if(r.request||r.phase>WDR_RETAINED||(r.flags&~15u)||r.retained>1){rc=-EPROTO;continue;}
        *state=r;if(r.flags&WDR_CHILD_TERMINAL)*terminal=1;
    }
    if(n>0||(n<0&&errno!=EAGAIN&&errno!=EINTR))rc=-EPROTO;return rc;
}
int main(int argc,char **argv){
    if(argc<4||argc>6||strlen(argv[1])>1500||strlen(argv[2])>1500)return 89;
    for(int i=4;i<argc;++i){if(!strcmp(argv[i],"--audit")&&!audit_enabled)audit_enabled=1;else if(!strcmp(argv[i],"--verify-muted")&&!verify_muted)verify_muted=1;else return 89;}
#ifdef SONY_BT_RECEIVER_BROKER
    if(audit_enabled)return 89;
#endif
    /* argv[3] is retained for compatibility with existing launchers; it no
     * longer selects or authenticates an application UID. */
    setvbuf(stdout,NULL,_IONBF,0);signal(SIGINT,interrupted);signal(SIGTERM,interrupted);signal(SIGHUP,interrupted);signal(SIGPIPE,SIG_IGN);signal(SIGCHLD,child_changed);
    int listener=socket(AF_UNIX,SOCK_SEQPACKET|SOCK_NONBLOCK|SOCK_CLOEXEC,0);if(listener<0)return 91;
    struct sockaddr_un addr={.sun_family=AF_UNIX};memcpy(addr.sun_path+1,WDR_SOCKET,sizeof(WDR_SOCKET)-1);
    if(bind(listener,(struct sockaddr *)&addr,offsetof(struct sockaddr_un,sun_path)+sizeof(WDR_SOCKET))||listen(listener,4)){int saved=errno;dprintf(2,"RECEIVER_REFUSED stage=listen status=%d\n",-saved);close(listener);return 92;}
    int client=-1,stop=-1,updates=-1,terminal=0,latch=0;pid_t child=0;uint64_t last=0,session_id=0;
    struct wdr_reply state={.magic=WDR_MAGIC,.version=1,.phase=WDR_OFFLINE,.flags=WDR_CLEAN};printf("RECEIVER_READY\n");
    while(!quitting||child){
        if(quitting)request_stop(&stop);
        struct pollfd p[3]={{listener,POLLIN,0},{client,POLLIN,0},{updates,POLLIN,0}};
        int ready=poll(p,3,child?1000:-1);if(ready<0&&errno!=EINTR){latch=1;quitting=1;request_stop(&stop);continue;}
        if(updates>=0&&(p[2].revents&(POLLIN|POLLHUP|POLLERR))&&consume_updates(updates,state.session,&state,&terminal)){latch=1;request_stop(&stop);}
        if(child){int status=0;pid_t got=waitpid(child,&status,WNOHANG);
            if(got==child){fprintf(stderr,"WDR_CONTROLLER_REAP pid=%d status=%d exited=%d exit_code=%d signaled=%d signal=%d terminal=%d phase=%u\n",child,status,WIFEXITED(status),WIFEXITED(status)?WEXITSTATUS(status):-1,WIFSIGNALED(status),WIFSIGNALED(status)?WTERMSIG(status):0,terminal,state.phase);if(consume_updates(updates,state.session,&state,&terminal))latch=1;
                if(!terminal||!WIFEXITED(status)||WEXITSTATUS(status)||state.phase!=WDR_OFFLINE||!(state.flags&WDR_CLEAN)||state.retained||state.cleanup_error){latch=1;state.phase=WDR_RETAINED;state.retained=1;state.error=state.error?state.error:-ECHILD;state.flags&=~WDR_CLEAN;}
                request_stop(&stop);close(updates);updates=-1;child=0;
            }else if(got<0&&errno!=EINTR){latch=1;state.phase=WDR_RETAINED;state.retained=1;state.error=-ECHILD;state.flags&=~WDR_CLEAN;request_stop(&stop);close(updates);updates=-1;child=0;quitting=1;}
        }
        if(p[0].revents&POLLIN){int next=accept4(listener,NULL,NULL,SOCK_NONBLOCK|SOCK_CLOEXEC);
            if(next>=0){
                if(quitting||client>=0)close(next);
                else{client=next;last=0;}
            }
        }
        if(client>=0&&(p[1].revents&(POLLIN|POLLHUP|POLLERR))){
            struct wdr_request request;int rc=wdr_receive(client,last,&request);
            if(rc==-EAGAIN||rc==-EINTR)continue;if(rc){close(client);client=-1;request_stop(&stop);continue;}last=request.request;
            if(request.command==WDR_USB_ON||request.command==WDR_BT_ON){
#ifdef SONY_BT_RECEIVER_BROKER
                if(request.command!=WDR_BT_ON)rc=-ENOTSUP;
#else
                if(request.command!=WDR_USB_ON)rc=-ENOTSUP;
#endif
                else if(child||latch||state.retained)rc=-EBUSY;
                else if(session_id==UINT64_MAX)rc=-EOVERFLOW;
                else{uint64_t id=now();if(id<=session_id)id=session_id+1;session_id=id;char session[2048];rc=session_directory(argv[2],id,session);
                    int controls[2]={-1,-1},events[2]={-1,-1};if(!rc&&pipe2(controls,O_CLOEXEC|O_NONBLOCK))rc=-errno;if(!rc&&pipe2(events,O_CLOEXEC|O_NONBLOCK))rc=-errno;
                    if(!rc){child=fork();if(child<0){child=0;rc=-errno;}else if(!child){close(listener);close(client);close(controls[1]);close(events[0]);receiver_child(argv[1],session,controls[0],events[1],id);}
                        else{close(controls[0]);close(events[1]);stop=controls[1];updates=events[0];terminal=0;state=(struct wdr_reply){.magic=WDR_MAGIC,.version=1,.phase=WDR_STARTING,.session=id};}}
                    if(rc)for(unsigned i=0;i<2;++i){if(controls[i]>=0)close(controls[i]);if(events[i]>=0)close(events[i]);}
                }
            }else if(request.command==WDR_OFF){if(child){request_stop(&stop);if(!state.retained)state.phase=WDR_STOPPING;}}
            struct wdr_reply reply=state;reply.request=request.request;if(rc)reply.error=rc;
            /* Native OFF is provisional until this exact child has exited and
             * been reaped; namespace/worker receipt alone cannot acknowledge OFF. */
            if(child&&(reply.flags&WDR_CHILD_TERMINAL)){reply.phase=WDR_STOPPING;reply.flags&=~WDR_CLEAN;}
            if(send(client,&reply,sizeof(reply),MSG_DONTWAIT|MSG_NOSIGNAL)!=sizeof(reply)){close(client);client=-1;request_stop(&stop);}
        }
    }
    if(client>=0)close(client);close(listener);return latch||child?1:0;
}
