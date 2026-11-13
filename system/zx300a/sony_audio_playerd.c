/* Native root broker. Apps supply one readonly imported DSF descriptor, never
 * commands/paths. One child/private mount namespace and exact owner per play.
 * Socket loss requests Stop; no automatic play, retry, owner reset or flashing. */
#define _GNU_SOURCE
#define _LARGEFILE64_SOURCE
#include "sony_audio_player_protocol.h"
#include "sony_audio_dsf_owned.h"
#include "sony_audio_dsf_native_owned.h"
#include "sony_audio_player_route_policy.h"
#include "sony_audio_jack.h"
#include "sony_audio_snapshot_store.h"
#include "sony_audio_snapshot_dsd.h"
#include "sony_audio_dsf_reader.h"
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
static int journal=-1;
static void interrupted(int n){(void)n;quitting=1;}
static uint64_t now_ns(void){struct timespec t;if(clock_gettime(CLOCK_BOOTTIME,&t))return 0;return (uint64_t)t.tv_sec*1000000000u+t.tv_nsec;}
static int directory(const char *p){return open(p,O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);}
static int bind_mount(const char *source,const char *target,int readonly){
 int rc=mount(source,target,NULL,MS_BIND,NULL)?-errno:0;
 if(!rc&&readonly&&mount(NULL,target,NULL,MS_BIND|MS_REMOUNT|MS_RDONLY,NULL))rc=-errno;
 if(journal>=0)dprintf(journal,"BIND %s -> %s ro=%d rc=%d\n",source,target,readonly,rc);
 return rc;
}
static int mounted_roots(const char *runtime,const char *session){
 int prepare=unshare(CLONE_NEWNS)?-errno:0;if(!prepare&&mount(NULL,"/",NULL,MS_REC|MS_PRIVATE,NULL))prepare=-errno;
 if(journal>=0)dprintf(journal,"PRIVATE_NAMESPACE %d\n",prepare);if(prepare)return prepare;
 char source[2048],target[2048];int rc=0;
 for(unsigned i=0;i<3&&!rc;++i){const char *kind=(const char *[]){"dsf","sink","native"}[i];
  snprintf(target,sizeof(target),"%s/%s-root",runtime,kind);rc=bind_mount(target,target,1);
  if(!rc&&i<2){snprintf(source,sizeof(source),"%s/private",session);snprintf(target,sizeof(target),"%s/%s-root/data/config",runtime,kind);rc=bind_mount(source,target,0);}
  if(!rc){snprintf(source,sizeof(source),"%s/tmp-%s",session,kind);snprintf(target,sizeof(target),"%s/%s-root/tmp",runtime,kind);rc=bind_mount(source,target,0);}
 }
 const char *native_nodes[]={"null","snd/controlC0","snd/pcmC0D2p"};
 for(unsigned n=0;n<3&&!rc;++n){
  snprintf(source,sizeof(source),"/dev/%s",native_nodes[n]);
  snprintf(target,sizeof(target),"%s/native-root/dev/%s",runtime,native_nodes[n]);rc=bind_mount(source,target,0);
 }
 const char *nodes[]={"null","urandom","snd/controlC0","snd/pcmC0D0p","snd/pcmC0D4p","snd/timer"};
 for(unsigned i=0;i<2&&!rc;++i)for(unsigned n=0;n<(i?6u:3u)&&!rc;++n){
  snprintf(source,sizeof(source),"/dev/%s",nodes[n]);snprintf(target,sizeof(target),"%s/%s-root/dev/%s",runtime,i?"sink":"dsf",nodes[n]);rc=bind_mount(source,target,0);
 }
 return rc;
}
static void progress(int fd,const struct wds_player_reply *r,int terminal){
 /* Status can coalesce when the receiver stalls; terminal cannot be silently
  * dropped. Parent must see terminal AND reap this exact child. */
 if(terminal){struct pollfd p={fd,POLLOUT,0};if(poll(&p,1,2000)<=0)return;}
 (void)write(fd,r,sizeof(*r));
}
static int stop_requested(int fd){char c;ssize_t n=read(fd,&c,1);return n==1||n==0||quitting;}
static void play_child(const char *runtime,const char *session,int input,int stop,int updates,uint64_t id){
 signal(SIGINT,interrupted);signal(SIGTERM,interrupted);signal(SIGHUP,interrupted);signal(SIGPIPE,SIG_IGN);
 struct wds_player_reply r={.magic=WDS_PLAYER_MAGIC,.version=1,.phase=WDS_PLAYER_STARTING,.session=id};
 char journalpath[2048];int length=snprintf(journalpath,sizeof(journalpath),"%s/launch.log",session);
 if(length>0&&length<(int)sizeof(journalpath))journal=open(journalpath,O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600);
 int rc=mounted_roots(runtime,session);
 char drp[2048],srp[2048],nrp[2048],config[2048],media_name[32],logpath[2048];
 snprintf(drp,sizeof(drp),"%s/dsf-root",runtime);snprintf(srp,sizeof(srp),"%s/sink-root",runtime);
 snprintf(nrp,sizeof(nrp),"%s/native-root",runtime);
 snprintf(config,sizeof(config),"%s/private",session);snprintf(media_name,sizeof(media_name),"%d",input);snprintf(logpath,sizeof(logpath),"%s/workers.log",session);
 int dr=-1,sr=-1,nr=-1,md=-1,log=-1;
 if(!rc){dr=directory(drp);sr=directory(srp);nr=directory(nrp);md=directory("/proc/self/fd");log=open(logpath,O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600);if(dr<0||sr<0||nr<0||md<0||log<0)rc=-errno;}
 unsigned char snapshot[SONY_SNAPSHOT_BYTES];struct sony_dsd_controls dsd;
 if(!rc)rc=sony_snapshot_read(SONY_AUDIO_CONFIG,snapshot);
 if(!rc)rc=sony_snapshot_apply_dsd(snapshot,sizeof(snapshot),&dsd);
 struct sony_dsf_owned *owned=NULL;struct sony_dsf_native_owned *native_owned=NULL;
 struct sony_dsf_playback_state state={0};struct sony_dsf_native_playback_state native_state={0};
 int native=0;unsigned expected=0;
 if(!rc){struct sony_audio_jack_state jack;rc=sony_audio_jack_read(&jack);
  if(!rc)rc=sony_player_route_select(&dsd,jack.se,jack.btl,&dsd,&expected,&native);}
 if(native)r.flags|=WDS_PLAYER_NATIVE_DSD;
 if(journal>=0)dprintf(journal,"OUTPUT_POLICY native=%d expected=%u mode=%d rc=%d\n",native,expected,rc?0:dsd.mode,rc);
 int stopped=0;if(!rc&&stop_requested(stop)){stopped=1;rc=-ECANCELED;}
 struct sony_dsf_source *validation=NULL;struct sony_dsf_reader *reader=NULL;uint64_t source_bytes=0;
 if(!rc)rc=sony_dsf_source_open_at(&validation,md,media_name,WDS_PLAYER_MAX_FILE);
 if(!rc)rc=sony_dsf_source_size(validation,&source_bytes);
 if(!rc)rc=sony_dsf_reader_open(&reader,source_bytes,sony_dsf_source_read_at,validation);
 sony_dsf_reader_close(reader);
 /* Reap the validation monitor before any worker fork. This is only an early
  * format rejection, not admission of cached bytes: the production pipeline
  * independently leases and parses this file again before accepting samples. */
 if(validation){int released=sony_dsf_source_close(&validation);if(!rc&&released)rc=released;}
 int attempted=0;
 if(!rc){attempted=1;
  rc=native?sony_dsf_native_owned_start(&native_owned,SONY_DSF_NATIVE_OWNED_EXPLICIT_START,dr,nr,log,config,md,media_name,WDS_PLAYER_MAX_FILE,&dsd,0,1):
      sony_dsf_owned_start_output(&owned,SONY_DSF_OWNED_EXPLICIT_START,dr,sr,log,config,md,media_name,WDS_PLAYER_MAX_FILE,&dsd,0,expected);}
 if(!rc)rc=native?sony_dsf_native_owned_enable_commands(native_owned):sony_dsf_owned_enable_commands(owned);
 uint64_t published=0;
 while(!rc){
  if(stop_requested(stop)){stopped=1;if(native)sony_dsf_native_owned_cancel(native_owned);else sony_dsf_owned_cancel(owned);}
  int readback;
  if(native){rc=sony_dsf_native_owned_step(native_owned);readback=sony_dsf_native_owned_state(native_owned,&native_state);
   r.source_bytes=native_state.source_bytes;r.output_acked_bytes=native_state.dsd_acked_bytes;r.rate=native_state.bit_rate;r.source_finished=native_state.source_finished;}
  else{rc=sony_dsf_owned_step(owned);readback=sony_dsf_owned_state(owned,&state);
   r.source_bytes=state.source_bytes;r.output_acked_bytes=state.pcm_acked_bytes;r.rate=state.rate;r.source_finished=state.source_finished;}
  if(!rc&&readback)rc=readback;
  uint64_t now=now_ns();int due=now-published>=250000000u;
  if(r.rate){r.phase=stopped?WDS_PLAYER_STOPPING:WDS_PLAYER_PLAYING;
   /* This read assembles the 4 Hz player-status reply, not the audio/control
    * transaction. owned_step still checks every pending native ACK and every
    * PCM callback still validates owner/jack/private-config identity. Do not
    * send a redundant WDF GET on every audio step. Always refresh at EOF/error
    * before close, so the terminal reply retains the actual final revision. */
   if(due||rc){
    if(native){struct sony_dsf_native_owned_controls c;if(!sony_dsf_native_owned_get_controls(native_owned,&c)){r.generation=c.generation;r.revision=c.acknowledged.revision;}}
    else{struct sony_dsf_owned_controls c;if(!sony_dsf_owned_get_controls(owned,&c)){r.generation=c.generation;r.revision=c.acknowledged.revision;}}
   }
  }
  if(due){progress(updates,&r,0);published=now;}
 }
 struct sony_dsf_owned_cleanup cleanup={0};struct sony_dsf_native_owned_cleanup nc={0};int clean=0;
 for(unsigned n=0;(owned||native_owned)&&n<3;++n){
  clean=native?sony_dsf_native_owned_close(&native_owned,&nc):sony_dsf_owned_close(&owned,&cleanup);
  if(owned||native_owned){struct timespec t={0,20000000};nanosleep(&t,NULL);}}
 for(unsigned i=0;i<5;++i){int fd=(int[]){dr,sr,nr,md,log}[i];if(fd>=0)close(fd);}
 close(input);
 int retained=owned||native_owned;
 const struct sony_dsf_process_exit *dx=native?&nc.dsf:&cleanup.dsf;
 int sink_reaped=native?nc.native.reaped:cleanup.sink.reaped,sink_killed=native?nc.native.kill_sent:cleanup.sink.kill_sent;
 int sink_exit=native?nc.native.exit_code:cleanup.sink.exit_code,sink_lost=native?nc.native.child_lost:cleanup.sink.child_lost;
 int sink_launch=native?nc.native.launch_stage:cleanup.sink.launch_stage;
 int refusal=(rc==-EBUSY||rc==-ENXIO||rc==-ENOTSUP)&&!retained&&!clean&&!r.rate&&!dx->reaped&&!sink_reaped&&!dx->launch_stage&&!sink_launch;
 r.cleanup_error=clean;r.retained=retained;r.flags=(native?WDS_PLAYER_NATIVE_DSD:0)|(attempted&&!refusal?WDS_PLAYER_ATTEMPTED:WDS_PLAYER_REFUSED);
 if(dx->reaped&&sink_reaped&&!dx->kill_sent&&!sink_killed&&!dx->exit_code&&!sink_exit&&!dx->child_lost&&!sink_lost)r.flags|=WDS_PLAYER_CHILDREN_CLEAN;
 int ok=!retained&&!clean&&(rc==1||(rc==-ECANCELED&&stopped));
 r.phase=ok?(stopped?WDS_PLAYER_STOPPED:WDS_PLAYER_ENDED):WDS_PLAYER_FAILED;r.error=ok?0:rc<0?rc:clean?clean:-EIO;
 if(journal>=0){dprintf(journal,"TERMINAL phase=%u error=%d attempted=%d cleanup=%d flags=%u\n",r.phase,r.error,attempted,clean,r.flags);close(journal);journal=-1;}
 printf("{\"session\":%llu,\"phase\":%u,\"error\":%d,\"cleanup\":%d,\"retained\":%u,\"native_children_naturally_reaped\":%s,\"dsf_close\":%d,\"sink_close\":%d,\"route\":%d}\n",
  (unsigned long long)id,r.phase,r.error,r.cleanup_error,r.retained,r.flags&WDS_PLAYER_CHILDREN_CLEAN?"true":"false",
  native?nc.playback.dsf_close:cleanup.playback.dsf_close,native?nc.playback.sink_close:cleanup.playback.sink_close,native?nc.route:cleanup.route);
 progress(updates,&r,1);close(stop);close(updates);_exit(ok||!attempted||refusal?0:1);
}
static int session_directory(const char *sessions,uint64_t id,char out[2048]){
 if(snprintf(out,2048,"%s/%016llx",sessions,(unsigned long long)id)>=2048)return -ENAMETOOLONG;
 if(mkdir(out,0700))return -errno;
 const char *dirs[]={"private","tmp-dsf","tmp-sink","tmp-native"};char p[2048];
 for(unsigned i=0;i<sizeof(dirs)/sizeof(dirs[0]);++i){int n=snprintf(p,sizeof(p),"%s/%s",out,dirs[i]);if(n<0||n>=(int)sizeof(p))return -ENAMETOOLONG;if(mkdir(p,0700))return -errno;}
 return 0;
}
int main(int argc,char **argv){
 if(argc!=4||strlen(argv[1])>1500||strlen(argv[2])>1500)return 89;
 /* argv[3] is retained for existing launchers; it no longer authenticates a UID. */
 setvbuf(stdout,NULL,_IONBF,0);signal(SIGINT,interrupted);signal(SIGTERM,interrupted);signal(SIGHUP,interrupted);signal(SIGPIPE,SIG_IGN);
 int listener=socket(AF_UNIX,SOCK_SEQPACKET|SOCK_NONBLOCK|SOCK_CLOEXEC,0);if(listener<0)return 91;
 struct sockaddr_un addr={.sun_family=AF_UNIX};memcpy(addr.sun_path+1,WDS_PLAYER_SOCKET,sizeof(WDS_PLAYER_SOCKET)-1);
 if(bind(listener,(struct sockaddr *)&addr,offsetof(struct sockaddr_un,sun_path)+sizeof(WDS_PLAYER_SOCKET))||listen(listener,4))return 92;
 int client=-1,stop=-1,updates=-1;pid_t child=0;uint64_t last=0;int have_terminal=0,latch=0;
 struct wds_player_reply state={.magic=WDS_PLAYER_MAGIC,.version=1,.phase=WDS_PLAYER_IDLE};
 printf("PLAYER_READY\n");
 while(!quitting||child){
  if(quitting&&stop>=0){char s=1;(void)write(stop,&s,1);close(stop);stop=-1;}
  struct pollfd p[3]={{listener,POLLIN,0},{client,POLLIN,0},{updates,POLLIN,0}};
  int ready=poll(p,3,100);if(ready<0&&errno!=EINTR)break;
  if(p[2].revents&(POLLIN|POLLHUP)){
   struct wds_player_reply r;ssize_t n;
   while((n=read(updates,&r,sizeof(r)))==sizeof(r)){
    state=r;if(r.phase>=WDS_PLAYER_ENDED)have_terminal=1;
   }
  }
  if(child){int status=0;pid_t reaped=waitpid(child,&status,WNOHANG);
   if(reaped==child){
    /* The exit can become visible just after poll returned a different event.
     * Drain buffered terminal data before deciding a receipt was lost. */
    struct wds_player_reply terminal;ssize_t bytes;
    while((bytes=read(updates,&terminal,sizeof(terminal)))==sizeof(terminal)){
     state=terminal;if(terminal.phase>=WDS_PLAYER_ENDED)have_terminal=1;
    }
    if(!have_terminal||!WIFEXITED(status)||WEXITSTATUS(status)){state.phase=WDS_PLAYER_FAILED;state.error=state.error?state.error:-ECHILD;state.retained=have_terminal?state.retained:1;latch=1;}
    if(state.retained||state.cleanup_error||(state.phase==WDS_PLAYER_FAILED&&(state.flags&8)))latch=1;
    if(stop>=0)close(stop);if(updates>=0)close(updates);stop=updates=-1;child=0;
   }else if(reaped<0&&errno!=EINTR){state.phase=WDS_PLAYER_FAILED;state.error=-ECHILD;state.retained=1;latch=1;break;}
  }
  if(p[0].revents&POLLIN){int next=accept4(listener,NULL,NULL,SOCK_CLOEXEC|SOCK_NONBLOCK);
   if(next>=0){
    if(quitting||client>=0)close(next);
    else{client=next;last=0;}
   }
  }
  if(client>=0&&(p[1].revents&(POLLIN|POLLHUP|POLLERR))){
   struct wds_player_request request;int input=-1;int rc=wds_player_receive(client,0,last,&request,&input);
   if(rc==-EAGAIN)continue;
   if(rc){close(client);client=-1;if(stop>=0){char s=1;(void)write(stop,&s,1);close(stop);stop=-1;}continue;}
   last=request.request;
   if(request.command==WDS_PLAYER_START){
    if(child||latch||state.retained)rc=-EBUSY;
    else{
     uint64_t id=now_ns();if(!id||id<=state.session)id=state.session+1;
     char session[2048];rc=session_directory(argv[2],id,session);int controls[2]={-1,-1},events[2]={-1,-1};
     if(!rc&&pipe2(controls,O_CLOEXEC|O_NONBLOCK))rc=-errno;
     if(!rc&&pipe2(events,O_CLOEXEC|O_NONBLOCK))rc=-errno;
     if(!rc){child=fork();if(child<0){rc=-errno;child=0;}
      else if(!child){close(listener);close(client);close(controls[1]);close(events[0]);play_child(argv[1],session,input,controls[0],events[1],id);}
      else{close(controls[0]);close(events[1]);stop=controls[1];updates=events[0];state=(struct wds_player_reply){.magic=WDS_PLAYER_MAGIC,.version=1,.phase=WDS_PLAYER_STARTING,.session=id};have_terminal=0;}
     }
     if(rc){for(unsigned i=0;i<2;++i){if(controls[i]>=0)close(controls[i]);if(events[i]>=0)close(events[i]);}}
    }
   }else if(request.command==WDS_PLAYER_STOP){
    if(child){if(stop>=0){char s=1;if(write(stop,&s,1)!=1)rc=-EIO;}state.phase=WDS_PLAYER_STOPPING;}
   }
   if(input>=0)close(input);
   struct wds_player_reply reply=state;reply.request=request.request;if(rc)reply.error=rc;
   /* A terminal child receipt is provisional until this daemon reaps it. */
   if(child&&reply.phase>=WDS_PLAYER_ENDED)reply.phase=WDS_PLAYER_STOPPING;
   if(send(client,&reply,sizeof(reply),MSG_DONTWAIT|MSG_NOSIGNAL)!=sizeof(reply)){close(client);client=-1;if(stop>=0){char s=1;(void)write(stop,&s,1);close(stop);stop=-1;}}
  }
 }
 if(client>=0)close(client);close(listener);return child||latch?1:0;
}
