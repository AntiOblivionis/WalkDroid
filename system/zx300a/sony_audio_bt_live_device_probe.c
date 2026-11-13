/* Bounded exact-player original RF -> native PCM -> async Sony/CFW check.
 * No firmware, service replacement or global lease recovery/reset operation. */
#define SONY_BT_SUPERVISOR_HELPERS_ONLY 1
#include "sony_audio_bt_stream_device_probe.c"
#include "sony_audio_bt_live_wire.h"
#include "sony_audio_receiver_async.h"
#include "sony_audio_jack.h"
#include "sony_audio_cpu.h"
#include "sony_audio_dsd_power_linux.h"
#ifdef SONY_BT_CONTROLLER_AUDIT_DSP
#include "test/sony_pcm_sink_audit.h"
#endif
#include <poll.h>
#include <sys/socket.h>
#include <sys/resource.h>
#include <sched.h>
#include <time.h>
#ifdef SONY_BT_PERSISTENT_CONTROLLER
#include "sony_audio_receiver_protocol.h"
static volatile sig_atomic_t requested_stop;
static void request_stop_signal(int signal){(void)signal;requested_stop=1;}
static int broker_stop=-1,broker_updates=-1;
static uint64_t broker_session,broker_last_report;
static unsigned broker_last_phase=UINT32_MAX;
#endif
static uint64_t now_ms(void){struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t))return 0;return (uint64_t)t.tv_sec*1000+t.tv_nsec/1000000;}
static int cpu_owned(void *owner){return sony_audio_owner_check(owner);}
static int cpu_closed(void *owner){int rc=cpu_owned(owner);return rc?rc:sony_audio_route_idle();}
static int receiver_scheduling(void){
    /* This disposable controller and its children only. A runaway native
     * real-time thread is bounded; the long-lived broker stays unchanged. */
    struct rlimit limit;
    if(!getrlimit(RLIMIT_RTTIME,&limit)){
        if(limit.rlim_cur>500000)limit.rlim_cur=500000;if(limit.rlim_max>1000000)limit.rlim_max=1000000;
        if(limit.rlim_cur>limit.rlim_max)limit.rlim_cur=limit.rlim_max;
        if(setrlimit(RLIMIT_RTTIME,&limit))fprintf(stderr,"BT_RTTIME status=%d\n",-errno);
    }
    struct sched_param priority={.sched_priority=2};
    if(sched_setscheduler(0,SCHED_RR,&priority))fprintf(stderr,"BT_SCHEDULER status=%d\n",-errno);
    return 0;
}
static void runtime_error_observation(int rc){
    int saved=errno;const char *paths[]={"/proc/self/stat","/proc/self/sched","/sys/devices/system/cpu/online","/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor","/sys/devices/system/cpu/cpu0/cpufreq/scaling_min_freq","/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq","/sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq"};
    printf("BT_RUNTIME_ERROR status=%d monotonic_ms=%llu\n",rc,(unsigned long long)now_ms());
    for(unsigned n=0;n<sizeof(paths)/sizeof(paths[0]);++n){char text[4096];int fd=open(paths[n],O_RDONLY|O_NOFOLLOW|O_CLOEXEC);if(fd<0){printf("BT_RUNTIME_READ path=%s error=%d\n",paths[n],errno);continue;}ssize_t size=read(fd,text,sizeof(text));if(size>0){printf("BT_RUNTIME_READ path=%s\n",paths[n]);fwrite(text,1,(size_t)size,stdout);printf("\n");}close(fd);}errno=saved;
}
#ifdef SONY_BT_PERSISTENT_CONTROLLER
static int broker_setup(int argc,char **argv){
    if(argc==10)return 0;if(argc!=14||strcmp(argv[10],"--broker"))return -EINVAL;
    int fds[2];for(unsigned n=0;n<2;++n){char *end;long fd=strtol(argv[11+n],&end,10);
        if(*end||fd<3||fd>65535)return -EINVAL;
        int flags=fcntl((int)fd,F_GETFL);if(flags<0||fcntl((int)fd,F_SETFL,flags|O_NONBLOCK)||fcntl((int)fd,F_SETFD,FD_CLOEXEC))return -errno;fds[n]=(int)fd;
    }
    char *end;uint64_t id=strtoull(argv[13],&end,16);if(*end||!id||fds[0]==fds[1])return -EINVAL;
    broker_stop=fds[0];broker_updates=fds[1];broker_session=id;return 0;
}
static int broker_report(unsigned phase,uint64_t generation,const struct sony_receiver_async_state *state,int error,int clean,int terminal){
    if(broker_updates<0)return 0;uint64_t time=now_ms();
    if(!terminal&&phase==broker_last_phase&&time-broker_last_report<1000)return 0;
    struct wdr_reply r={.magic=WDR_MAGIC,.version=1,.phase=phase,.error=error,.session=broker_session,.generation=generation,
        .flags=clean?WDR_CLEAN:WDR_ENABLED,.retained=phase==WDR_RETAINED};
    if(terminal)r.flags|=WDR_CHILD_TERMINAL;if(clean&&!generation)r.flags|=WDR_REFUSED;
    if(state){r.source_frames=state->pushed_frames;r.output_acked_bytes=state->output.playback.sink_acked_bytes;r.rate=state->output.output_rate;}
    if(r.retained)r.cleanup_error=error?error:-EUCLEAN;
    uint64_t end=time+2000;for(;;){ssize_t n=write(broker_updates,&r,sizeof(r));if(n==sizeof(r)){broker_last_report=time;broker_last_phase=phase;return 0;}
        if(n<0&&errno==EINTR)continue;if(n<0&&errno==EAGAIN){if(!terminal)return 0;if(now_ms()>=end)return -ETIMEDOUT;struct pollfd p={broker_updates,POLLOUT,0};poll(&p,1,20);continue;}return -EPIPE;
    }
}
static int broker_refused(int error,int original_exit){return broker_updates<0?original_exit:broker_report(WDR_OFFLINE,0,NULL,error,1,1)?1:0;}
static int broker_poll_stop(void){
    if(broker_stop<0||requested_stop)return 0;unsigned char byte;ssize_t n=read(broker_stop,&byte,1);
    if(n>=0){if(n>0&&byte!=1)return -EPROTO;requested_stop=1;return 0;}
    return errno==EAGAIN||errno==EINTR?0:-errno;
}
#endif
static int live_spawn(int root,int log,int fd,int other,uint64_t cookie,int live){
    pid_t parent=getpid(),pid=fork();if(pid)return pid;
    if(prctl(PR_SET_PDEATHSIG,SIGKILL)||getppid()!=parent||dup2(log,1)<0||dup2(log,2)<0||fchdir(root)||chroot(".")||chdir("/"))_exit(126);
    if(other>=0)close(other);
    if(live&&(fd<3||fcntl(fd,F_SETFD,0)))_exit(124);
    int workspace=open("/etc/sony-bt-properties",O_RDONLY|O_NOFOLLOW);struct stat prop;
    if(workspace<0||fstat(workspace,&prop))_exit(125);
    char property_env[96],descriptor[24],nonce[24];snprintf(property_env,sizeof(property_env),"ANDROID_PROPERTY_WORKSPACE=%d,%lld",workspace,(long long)prop.st_size);snprintf(descriptor,sizeof(descriptor),"%d",fd);snprintf(nonce,sizeof(nonce),"%llx",(unsigned long long)cookie);
    const char *rf_mode="--rf-live";
#ifdef SONY_BT_PERSISTENT_CONTROLLER
    rf_mode="--rf-persistent";
#endif
    char *env[]={"LD_LIBRARY_PATH=/lib","PATH=/bin",property_env,NULL};char *args[]={"/bin/sony-bt-live-source",live?(char *)rf_mode:"--software-init",live?descriptor:NULL,live?nonce:NULL,NULL};execve(args[0],args,env);_exit(127);
}
static int log_open(const char *path){return open(path,O_CREAT|O_EXCL|O_NOFOLLOW|O_WRONLY|O_CLOEXEC,0600);}
static int control(int fd,uint64_t cookie,uint64_t sequence,unsigned kind){struct sony_bt_live_header h={SONY_BT_LIVE_MAGIC,1,kind,0,cookie,1,sequence,0};return sony_bt_live_wire_send(fd,&h,NULL);}
static int snapshot_pcm(int *owner,uint64_t *hardware){
    char text[2048];int fd=open("/proc/asound/card0/pcm4p/sub0/status",O_RDONLY|O_NOFOLLOW|O_CLOEXEC);if(fd<0)return -errno;ssize_t n=read(fd,text,sizeof(text)-1);int saved=errno;close(fd);if(n<=0)return n?-saved:-EIO;text[n]=0;
    if(!strstr(text,"state: RUNNING"))return -EAGAIN;char *p=strstr(text,"owner_pid");int pid;if(!p||sscanf(p,"owner_pid : %d",&pid)!=1||pid<1)return -EPROTO;
    p=strstr(text,"hw_ptr");unsigned long long frames;if(!p||sscanf(p,"hw_ptr : %llu",&frames)!=1)return -EPROTO;*owner=pid;*hardware=frames;printf("LIVE_KERNEL_PCM\n%s",text);return 0;
}
static int offer(struct sony_receiver_async *async,const void *data,unsigned frames,unsigned frame_bytes){
    unsigned at=0;uint64_t deadline=now_ms()+6000;
    while(at<frames){unsigned n=frames-at;if(n>SONY_RECEIVER_ASYNC_MAX_FRAMES)n=SONY_RECEIVER_ASYNC_MAX_FRAMES;
        /* Caller supplies one already authenticated, retained packet. EAGAIN
         * admits nothing; it may wait without reading another source packet.
         * Every successful segment is advanced once, never replayed. */
        int rc=sony_receiver_async_push(async,1,(const char *)data+at*frame_bytes,n);if(!rc){at+=n;continue;}if(rc!=-EAGAIN)return rc;if(now_ms()>=deadline)return -ETIMEDOUT;struct timespec wait={0,2000000};nanosleep(&wait,NULL);
    }return 0;
}
static int offer_volume(struct sony_receiver_async *async,int step){
    uint64_t deadline=now_ms()+6000;
    for(;;){int rc=sony_receiver_async_volume_step(async,step);if(rc!=-EAGAIN)return rc;if(now_ms()>=deadline)return -ETIMEDOUT;struct timespec wait={0,2000000};nanosleep(&wait,NULL);}
}
#ifndef SONY_BT_CONTROLLER_HELPERS_ONLY
int main(int argc,char **argv){
    if((argc!=10
#ifdef SONY_BT_PERSISTENT_CONTROLLER
        &&argc!=14
#endif
    )||(strcmp(argv[1],"--software")&&strcmp(argv[1],"--verify-muted")&&strcmp(argv[1],"--play")))return 64;
    setvbuf(stdout,NULL,_IONBF,0);int live=strcmp(argv[1],"--software")!=0,muted=strcmp(argv[1],"--play")!=0;
#ifdef SONY_BT_PERSISTENT_CONTROLLER
    /* Serialized controller thread remains the only async producer. Signals
     * request an authenticated upstream STOP; they never clear an owner. */
    signal(SIGTERM,request_stop_signal);signal(SIGINT,request_stop_signal);signal(SIGHUP,request_stop_signal);signal(SIGPIPE,SIG_IGN);
    if(broker_setup(argc,argv))return 64;
    printf("BT_MODE_CONTROLLER pid=%d persistent=1 raw_pcm_capture=0\n",getpid());
#endif
    struct sony_controls controls;int controls_error=sony_controls_load(&controls);
    unsigned expected_output=SONY_OUTPUT_SE;
    if(!controls_error){struct sony_audio_jack_state jack;controls_error=sony_audio_jack_read(&jack);if(!controls_error&&jack.se==jack.btl)controls_error=-ENXIO;if(!controls_error)expected_output=jack.btl?SONY_OUTPUT_BTL:SONY_OUTPUT_SE;}
    if(controls_error){
#ifdef SONY_BT_PERSISTENT_CONTROLLER
        return broker_refused(controls_error,66);
#else
        return 66;
#endif
    }
    int root=open(argv[2],O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC),dr=open(argv[3],O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC),sr=open(argv[4],O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    int dl=log_open(argv[6]),pl=log_open(argv[7]),dsp_log=log_open(argv[8]),sink_log=log_open(argv[9]);if(root<0||dr<0||sr<0||dl<0||pl<0||dsp_log<0||sink_log<0){
#ifdef SONY_BT_PERSISTENT_CONTROLLER
        return broker_refused(-errno,67);
#else
        return 67;
#endif
    }
    struct sony_audio_owner *owner=NULL;struct sony_bt_radio_owner *radio=NULL;int rc=sony_audio_owner_acquire(&owner,SONY_OWNER_BT_RX);if(rc){
#ifdef SONY_BT_PERSISTENT_CONTROLLER
        return broker_refused(rc,68);
#else
        return 68;
#endif
    }uint64_t generation=sony_audio_owner_generation(owner),cookie=generation^UINT64_C(0x57424c31a9e53278);
    rc=sony_bt_radio_acquire(&radio,SONY_BT_RADIO_NATIVE_RX);if(rc){sony_audio_owner_finish(&owner,1);
#ifdef SONY_BT_PERSISTENT_CONTROLLER
        return broker_refused(rc,69);
#else
        return 69;
#endif
    }
    /* The coordinated lights HAL selects performance while the display is
     * lit. That unregisters hotplug's sysfs group, including cpu_num_base.
     * Hold the same governor lease used by USB before any resource snapshot;
     * retain it through native children and exact resource restoration. */
    struct sony_audio_cpu *cpu_policy=NULL;rc=sony_audio_cpu_acquire(&cpu_policy,generation,cpu_owned,cpu_closed,owner);int cpu_start=rc;
    struct sony_dsd_power_linux *resource_linux=NULL;struct sony_dsd_power_platform *boost=NULL;struct sony_dsd_power_platform_io resource_io={0};struct sony_dsd_power_backend boost_io={0};uint64_t boost_token=0;
    if(!rc)rc=sony_dsd_power_linux_create(&resource_linux,cpu_owned,owner);if(!rc)rc=sony_dsd_power_linux_io(resource_linux,&resource_io);
    if(!rc)rc=sony_dsd_power_platform_create(&boost,&resource_io,generation^UINT64_C(0x5742544300000000));if(!rc)rc=sony_dsd_power_platform_backend(boost,&boost_io);
    /* Same bounded two-core/1.3GHz reservation as the USB receiver. The
     * downstream snapshot is taken AFTER it, then restored before this
     * outer reservation. No PCM samples or XRUN checks are altered. */
    struct sony_dsd_power_plan boost_plan={2,1300000,1,0,{80,80,80}};if(!rc)rc=boost_io.acquire(boost_io.context,&boost_plan,&boost_token);int boost_start=rc;
    if(!rc)rc=receiver_scheduling();int scheduling_start=rc;
    printf("BT_RECEIVER_SCHEDULING status=%d policy=rr priority=2 soft_cpu_us=500000 hard_cpu_us=1000000\n",scheduling_start);
    int pair[2]={-1,-1};unsigned send_buffer=0;if(!rc&&live){if(socketpair(AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC,0,pair))rc=-errno;else{rc=sony_bt_live_wire_prepare(pair[0],&send_buffer);if(!rc)rc=sony_bt_live_wire_prepare(pair[1],&send_buffer);}}
    int daemon=-1,probe=-1,de=-99,pe=-99,dc=-99,pc=-99,df=0,pf=0,daemon_ready=0;
    struct sony_receiver_session *session=NULL;struct sony_receiver_async *async=NULL;struct sony_receiver_session_cleanup cleanup={0};struct sony_receiver_async_state final={0};
    if(!rc){daemon=spawn(root,dl,"/bin/mtkbt",NULL);if(daemon<1)rc=-errno;}
    for(unsigned n=0;!rc&&n<300;++n){struct stat s;if(!fstatat(root,"tmp/bt.int.adp",&s,AT_SYMLINK_NOFOLLOW)&&S_ISSOCK(s.st_mode)){daemon_ready=1;break;}usleep(10000);}if(!rc&&!daemon_ready)rc=-ETIMEDOUT;
    if(!rc){probe=live_spawn(root,pl,pair[1],pair[0],cookie,live);if(probe<1)rc=-errno;}
    if(pair[1]>=0){close(pair[1]);pair[1]=-1;}
    /* All four children are created before this parent creates an async thread.
     * Source cannot decode until FORMAT -> actual OPEN -> authenticated READY. */
    struct sony_receiver_session_launch launch={dr,sr,dsp_log,sink_log,argv[5],cookie,muted?SONY_RECEIVER_SESSION_VERIFY_MUTED:0,5000};
#ifdef SONY_BT_CONTROLLER_PERIOD_WRITES
    launch.flags|=SONY_RECEIVER_SESSION_BT_PERIOD_WRITES;
#endif
#ifdef SONY_BT_CONTROLLER_LIVE_VOLUME
    if(!muted)launch.flags|=SONY_RECEIVER_SESSION_LIVE_VOLUME;
#endif
#ifdef SONY_BT_CONTROLLER_PERIOD_WRITES
    /* Static-root hashes/configuration are bounded setup, not audio work.
     * Cached reads need not sleep, so doing all setup under RR can exhaust
     * RLIMIT_RTTIME before READY. The already-forked RF children remain RR2. */
    if(!rc){struct sched_param ordinary={0};if(sched_setscheduler(0,SCHED_OTHER,&ordinary))rc=-errno;else if(sched_getscheduler(0)!=SCHED_OTHER)rc=-ESTALE;}
    printf("BT_SETUP_SCHEDULING status=%d policy=other watchdog_unchanged=1\n",rc);
#endif
    if(!rc)rc=sony_receiver_session_start_bt(&session,SONY_RECEIVER_SESSION_EXPLICIT_START,owner,&launch);
#ifdef SONY_BT_CONTROLLER_PERIOD_WRITES
    if(!rc)rc=sony_receiver_session_promote_audio(session);
    if(!rc)rc=receiver_scheduling();
    printf("BT_AUDIO_SCHEDULING status=%d policy=rr priority=2 watchdog_unchanged=1\n",rc);
#endif
#ifdef SONY_BT_CONTROLLER_AUDIT_DSP
    int audit_started=0;if(!rc&&live){rc=sony_pcm_sink_audit_begin(cookie,SONY_PCM_SINK_AUDIT_MAX_BYTES);audit_started=!rc;}
#endif
    if(!rc&&live){
#ifdef SONY_BT_CONTROLLER_LIVE_VOLUME
        if(!muted)rc=sony_receiver_async_start_bt_controls(&async,SONY_RECEIVER_ASYNC_EXPLICIT_START,session,owner);else
#endif
        rc=sony_receiver_async_start_bt(&async,SONY_RECEIVER_ASYNC_EXPLICIT_START,session,owner);
    }
    unsigned format=0,ready_sent=0,quiet=0,finished=0,kernel_seen=0;int kernel_pid=0;uint64_t packets=0,received=0;struct sony_bt_live_cursor cursor,next;sony_bt_live_cursor_init(&cursor,cookie,1);
    struct sony_bt_live_volume_cursor volume_cursor,volume_next;sony_bt_live_volume_init(&volume_cursor,cookie);
    uint64_t kernel_first=0,kernel_second=0,kernel_time=0;unsigned kernel_progress=0;
    unsigned char scratch[SONY_BT_LIVE_MAX_BYTES];unsigned char *recording=NULL;unsigned recorded=0;const unsigned capacity=8u*1024*1024;
#if !defined(SONY_BT_PERSISTENT_CONTROLLER) || defined(SONY_BT_CONTROLLER_AUDIT_PCM)
    if(!rc&&live){recording=malloc(capacity);if(!recording)rc=-ENOMEM;else memset(recording,0,capacity);}
#endif
    uint64_t deadline=now_ms()+48000;
#ifdef SONY_BT_PERSISTENT_CONTROLLER
    deadline=UINT64_MAX;unsigned stop_sent=0;
#endif
    while(!rc&&live&&now_ms()<deadline){
#ifdef SONY_BT_PERSISTENT_CONTROLLER
        rc=broker_poll_stop();if(rc)break;
        if(requested_stop&&!stop_sent){int sent=control(pair[0],cookie,ready_sent?1:0,SONY_BT_LIVE_STOP);if(!sent){stop_sent=1;deadline=now_ms()+15000;printf("BT_MODE_STOP_REQUESTED\n");}else if(sent!=-EAGAIN){rc=sent;break;}}
#endif
        struct sony_receiver_async_state state;rc=sony_receiver_async_state(async,&state);if(rc)break;if(state.first_error){rc=state.first_error;break;}
#ifdef SONY_BT_PERSISTENT_CONTROLLER
        rc=broker_report(stop_sent?WDR_STOPPING:cursor.listening?(state.output.started?WDR_STREAMING:WDR_READY):WDR_STARTING,generation,&state,0,0,0);if(rc)break;
#endif
        if(format&&!ready_sent&&state.output.stream_open&&!state.worker_busy&&!state.queued_jobs
#ifdef SONY_BT_PERSISTENT_CONTROLLER
            &&!stop_sent
#endif
        ){rc=control(pair[0],cookie,0,SONY_BT_LIVE_READY);if(rc)break;ready_sent=1;printf("LIVE_CONSUMER_READY rate=%u bits=%u\n",state.output.source_rate,state.output.source_bits);}
        if(!kernel_seen&&state.output.started){int seen=snapshot_pcm(&kernel_pid,&kernel_first);if(!seen){kernel_seen=1;kernel_time=now_ms();}else if(seen!=-EAGAIN){rc=seen;break;}}
        if(kernel_seen&&!kernel_progress&&state.output.stream_open&&state.output.started&&now_ms()-kernel_time>=250){int pid=0,seen=snapshot_pcm(&pid,&kernel_second);if(!seen){if(pid!=kernel_pid){rc=-ESTALE;break;}kernel_progress=kernel_second>kernel_first;}else if(seen!=-EAGAIN){rc=seen;break;}}
        if(quiet){if(format&&!finished){rc=sony_receiver_async_finish(async,1);if(rc)break;finished=1;}if(state.finished_streams==(format?1u:0u)&&!state.worker_busy&&!state.queued_jobs)break;struct pollfd unused={-1,0,0};poll(&unused,0,2);continue;}
        struct sony_bt_live_header h;int got=sony_bt_live_wire_receive(pair[0],probe,0,&h,scratch,sizeof(scratch));if(got==-EAGAIN){struct pollfd wait={pair[0],POLLIN,0};if(poll(&wait,1,10)<0&&errno!=EINTR)rc=-errno;continue;}if(got){rc=got;break;}
        if(h.source==SONY_BT_LIVE_VOLUME_SOURCE||h.kind==SONY_BT_LIVE_VOLUME_STEP){
            int step;rc=sony_bt_live_volume_next(&volume_cursor,&h,scratch,&volume_next,&step);
            if(!rc)rc=offer_volume(async,step);if(!rc)volume_cursor=volume_next;continue;
        }
        rc=sony_bt_live_next(&cursor,&h,scratch,&next);if(rc)break;
        if(h.kind==SONY_BT_LIVE_LISTENING){
#ifdef SONY_BT_PERSISTENT_CONTROLLER
            printf("BT_MODE_LISTENING generation=%llu pid=%d\n",(unsigned long long)generation,getpid());
#else
            rc=-EPROTO;
#endif
        }else if(h.kind==SONY_BT_LIVE_FORMAT){
            rc=sony_receiver_async_open_bt(async,1,&next.format.pcm,expected_output);if(!rc)format=1;
        }else if(h.kind==SONY_BT_LIVE_PCM){
            if(!ready_sent){rc=-EPROTO;break;}
            if(recording){if(h.bytes>capacity-recorded){rc=-ENOBUFS;break;}memcpy(recording+recorded,scratch,h.bytes);}
            rc=offer(async,scratch,h.bytes/cursor.frame_bytes,cursor.frame_bytes);if(!rc){if(recording)recorded+=h.bytes;received+=h.bytes;++packets;}
        }else if(h.kind==SONY_BT_LIVE_QUIET){quiet=1;volume_cursor.sealed=1;}
        else if(h.kind==SONY_BT_LIVE_ERROR){rc=-(int)next.source_error;break;}
        if(!rc)cursor=next;
    }
    if(live&&!rc&&(!quiet||(format&&!finished)||now_ms()>=deadline))rc=-ETIMEDOUT;
#ifdef SONY_BT_PERSISTENT_CONTROLLER
    if(live&&!rc&&(!cursor.listening||!stop_sent))rc=-EPROTO;
#endif
    if(rc&&live)runtime_error_observation(rc);
    if(rc&&live&&pair[0]>=0)control(pair[0],cookie,ready_sent?1:0,SONY_BT_LIVE_STOP);
    int async_stop=0;if(async){uint64_t end=now_ms()+10000;do{async_stop=sony_receiver_async_stop(&async,&final);if(async_stop==-EAGAIN){struct timespec delay={0,10000000};nanosleep(&delay,NULL);}}while(async_stop==-EAGAIN&&now_ms()<end);}
    int session_stop=-EAGAIN;if(!async&&session){session_stop=sony_receiver_session_stop(&session,5000,&cleanup);for(unsigned n=0;session&&session_stop==-EAGAIN&&n<200;++n){usleep(10000);session_stop=sony_receiver_session_stop(&session,5000,&cleanup);}}
    if(pair[0]>=0){close(pair[0]);pair[0]=-1;}
    if(probe>0){pc=reap(probe,15000,&pe);if(pc)terminate(probe,&pe,&pf);}
    if(daemon>0)dc=terminate(daemon,&de,&df);
    if(!rc&&live){if(final.first_error)rc=final.first_error;else if(final.finish_status)rc=final.finish_status;else if(final.close_status)rc=final.close_status;else if(final.state_status)rc=final.state_status;}
    int fds=-1,idle=-1;int cleanup_ok=!async&&!session&&!async_stop&&!session_stop&&cleanup.dsp.reaped&&cleanup.sink.reaped&&!cleanup.dsp.exit_code&&!cleanup.sink.exit_code&&!cleanup.dsp.kill_sent&&!cleanup.sink.kill_sent&&!pc&&!pe&&!dc&&!pf&&!df;
    int boost_release=0,boost_destroy=0,resource_destroy=0;
    if(!async&&!session&&!async_stop&&!session_stop){
        if(boost_token){boost_release=boost_io.release(boost_io.context,boost_token);if(!boost_release)boost_token=0;}
        if(boost&&!boost_token)boost_destroy=sony_dsd_power_platform_destroy(&boost);
        if(resource_linux&&!boost)resource_destroy=sony_dsd_power_linux_destroy(&resource_linux);
    }
    if(boost_release||boost_destroy||resource_destroy||boost_token||boost||resource_linux)cleanup_ok=0;
    printf("{\"bt_cpu_resources\":true,\"start_status\":%d,\"release_status\":%d,\"destroy_status\":%d,\"platform_destroy_status\":%d,\"retained\":%s}\n",boost_start,boost_release,boost_destroy,resource_destroy,boost||boost_token||resource_linux?"true":"false");
    int cpu_release=0;if(cleanup_ok&&cpu_policy)cpu_release=sony_audio_cpu_release(&cpu_policy);if(cpu_release||cpu_policy)cleanup_ok=0;
    printf("{\"bt_cpu_policy\":true,\"start_status\":%d,\"release_status\":%d,\"retained\":%s,\"generation\":%llu}\n",cpu_start,cpu_release,cpu_policy?"true":"false",(unsigned long long)generation);
    int radio_finish=sony_bt_radio_finish(&radio,cleanup_ok);if(radio_finish)cleanup_ok=0;int owner_finish=sony_audio_owner_finish(&owner,cleanup_ok);
    int exact=!live||(quiet&&cursor.sealed&&cursor.frames==final.offered_frames&&final.offered_frames==final.pushed_frames&&final.pushed_frames==final.output.source_accepted_frames&&final.output.source_taken_frames==final.pushed_frames&&final.output.playback.source_acked_frames==final.pushed_frames&&!final.failed_frames&&!final.discarded_frames&&received==final.pushed_frames*cursor.frame_bytes&&(!recording||received==recorded));
    exact=exact&&volume_cursor.next_sequence==final.volume_offered&&final.volume_offered==final.volume_applied&&!final.volume_discarded&&!final.volume_failed;
    int output_ok=ready_sent&&kernel_seen&&kernel_progress&&kernel_pid==cleanup.sink.spawned_pid&&received>0&&final.finished_streams==1;
#ifdef SONY_BT_PERSISTENT_CONTROLLER
    if(!format)output_ok=!ready_sent&&!kernel_seen&&!received&&!final.finished_streams;
#endif
    int passed=!rc&&cleanup_ok&&!owner_finish&&exact&&(!live||output_ok);
    printf("{\"live_dma_observation\":true,\"progress\":%s,\"first_hw_ptr\":%llu,\"second_hw_ptr\":%llu,\"analog_sound_proven\":false}\n",kernel_progress?"true":"false",(unsigned long long)kernel_first,(unsigned long long)kernel_second);
    printf("{\"live_volume_observation\":true,\"received\":%llu,\"offered\":%llu,\"applied\":%llu,\"discarded\":%llu,\"failed\":%llu,\"last_volume\":%u,\"last_revision\":%u,\"actual_phone_relative_event_verified\":%s}\n",(unsigned long long)volume_cursor.next_sequence,(unsigned long long)final.volume_offered,(unsigned long long)final.volume_applied,(unsigned long long)final.volume_discarded,(unsigned long long)final.volume_failed,final.last_volume,final.last_volume_revision,volume_cursor.next_sequence&&volume_cursor.next_sequence==final.volume_applied?"true":"false");
    /* Diagnostic dump only AFTER radio/output closure. This is not a live
     * filesystem write in the callback or consumer path. */
    int dump=0;if(recording&&recorded){char path[1024];int n=snprintf(path,sizeof(path),"%s/wire-pcm.bin",argv[5]);if(n<0||(unsigned)n>=sizeof(path))dump=-ENAMETOOLONG;else{int fd=log_open(path);if(fd<0)dump=-errno;else{unsigned at=0;while(at<recorded){ssize_t wrote=write(fd,recording+at,recorded-at);if(wrote<0&&errno==EINTR)continue;if(wrote<=0){dump=wrote?-errno:-EIO;break;}at+=(unsigned)wrote;}if(close(fd)&&!dump)dump=-errno;}}}if(dump)passed=0;free(recording);
#ifdef SONY_BT_CONTROLLER_AUDIT_DSP
    if(audit_started){char path[1024];unsigned bytes=0;int n=snprintf(path,sizeof(path),"%s/dsp-output.pcm",argv[5]),saved=0;
        if(n<0||(unsigned)n>=sizeof(path))saved=-ENAMETOOLONG;else{int fd=log_open(path);if(fd<0)saved=-errno;else{saved=sony_pcm_sink_audit_save(cookie,fd,&bytes);if(close(fd)&&!saved)saved=-errno;}}
        printf("BT_DSP_OUTPUT_AUDIT bytes=%u status=%d\n",bytes,saved);if(saved){if(!dump)dump=saved;passed=0;}}
#endif
    printf("{\"live_device_result\":true,\"passed\":%s,\"live_radio\":%s,\"muted\":%s,\"status\":%d,\"generation\":%llu,\"source_frames\":%llu,\"received_bytes\":%llu,\"packets\":%llu,\"queued_offered\":%llu,\"queued_pushed\":%llu,\"dsp_source_acked\":%llu,\"sink_bytes\":%llu,\"exact_source_to_session\":%s,\"kernel_seen\":%u,\"kernel_pid\":%d,\"sink_pid\":%d,\"ready_sent\":%u,\"quiesced\":%u,\"cleanup_ok\":%s,\"owner_finish\":%d,\"radio_finish\":%d,\"async_stop\":%d,\"session_stop\":%d,\"source_exit\":%d,\"source_wait\":%d,\"source_forced\":%d,\"daemon_exit\":%d,\"daemon_forced\":%d,\"driver_fds\":%d,\"independent_idle\":%d,\"dsp_reaped\":%d,\"dsp_exit\":%d,\"dsp_killed\":%d,\"sink_reaped\":%d,\"sink_exit\":%d,\"sink_killed\":%d,\"dump_status\":%d,\"physical_consumption_proven\":false}\n",passed?"true":"false",live?"true":"false",muted?"true":"false",rc,(unsigned long long)generation,(unsigned long long)cursor.frames,(unsigned long long)received,(unsigned long long)packets,(unsigned long long)final.offered_frames,(unsigned long long)final.pushed_frames,(unsigned long long)final.output.source_accepted_frames,(unsigned long long)final.output.playback.sink_acked_bytes,exact?"true":"false",kernel_seen,kernel_pid,cleanup.sink.spawned_pid,ready_sent,quiet,cleanup_ok?"true":"false",owner_finish,radio_finish,async_stop,session_stop,pe,pc,pf,de,df,fds,idle,cleanup.dsp.reaped,cleanup.dsp.exit_code,cleanup.dsp.kill_sent,cleanup.sink.reaped,cleanup.sink.exit_code,cleanup.sink.kill_sent,dump);
    close(root);close(dr);close(sr);close(dl);close(pl);close(dsp_log);close(sink_log);
#ifdef SONY_BT_PERSISTENT_CONTROLLER
    if(broker_updates>=0)return broker_report(cleanup_ok&&!owner_finish?WDR_OFFLINE:WDR_RETAINED,generation,&final,rc?rc:cleanup_ok&&!owner_finish?0:-EUCLEAN,cleanup_ok&&!owner_finish,1)?1:0;
#endif
    return passed?0:70;
}
#endif
