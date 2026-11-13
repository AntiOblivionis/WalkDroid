#define _GNU_SOURCE
#include "sony_audio_receiver_session.h"
#include "sony_audio_receiver_pcm.h"
#include "sony_audio_receiver_idle_guard_linux.h"
#include "sony_audio_dsd_power_linux.h"
#include "sony_audio_route.h"
#include "sony_audio_jack.h"
#include "sony_audio_snapshot_store.h"
#include "sony_audio_snapshot_eq.h"
#include "sony_audio_volume_policy.h"
#include "sony_audio_owner_commands.h"
#include "sony_audio_snapshot_dsd.h"
#include <tinyalsa/asoundlib.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

struct receiver_volume_save {
    pthread_t thread;
    struct sony_receiver_session *session;
    struct wdf_controls pending;
    int done,result,join_error;
    unsigned revision,volume;
    unsigned char before[SONY_SNAPSHOT_BYTES],next[SONY_SNAPSHOT_BYTES];
};

struct sony_receiver_session {
    struct sony_audio_owner *owner;
    struct sony_audio_route *route;
    struct sony_receiver_process *dsp;
    struct sony_receiver_sink_process *sink;
    struct sony_receiver_playback *playback;
    struct sony_receiver_pcm *input;
    struct sony_dsd_power_linux *platform;
    struct sony_dsd_power_platform_io io;
    struct sony_receiver_idle_guard_linux guard_linux;
    struct sony_receiver_idle_guard_io guard_io;
    struct sony_receiver_idle_guard *guard;
    struct sony_receiver_legacy_policy_platform *latency;
    struct sony_receiver_mode_policy policy;
    struct sony_controls controls;
    struct sony_command_server *commands;
    int volume_notify[2];
    unsigned command_pending,command_request,command_revision,command_volume;
    struct wdf_controls command_base;
    struct receiver_volume_save *save;
    int command_error;
    struct sony_receiver_session_state state;
    struct sony_receiver_session_cleanup cleanup;
    unsigned char snapshot[SONY_SNAPSHOT_BYTES];
    char *path;int dir,fd;
    struct mixer *monitor;struct mixer_ctl *mute_control;
    unsigned flags,revision,batch,mode;int sink_pid,busy,sealed,closing,dsp_shutdown_attempted,sink_shutdown_attempted,dsp_launched,sink_launched,config_loaded,parked,retain_boundary,audio_scheduler;
    int packet[1024];
};
static int lock(struct sony_receiver_session *s){return !s?-EINVAL:__atomic_exchange_n(&s->busy,1,__ATOMIC_ACQUIRE)?-EBUSY:0;}
static int unlock(struct sony_receiver_session *s,int rc){__atomic_store_n(&s->busy,0,__ATOMIC_RELEASE);return rc;}
static int remember(struct sony_receiver_session *s,int rc){if(rc>0)rc=-EPROTO;if(rc&&!s->state.first_error)s->state.first_error=rc;return rc;}
static int fail(struct sony_receiver_session *s,int rc){s->sealed=1;return remember(s,rc);}
static int owner_check(void *v){
    struct sony_receiver_session *s=v;return sony_audio_owner_check(s->owner);
}
static int bind_config_file(struct sony_receiver_session *s){
    if(s->fd>=0){close(s->fd);s->fd=-1;}s->fd=openat(s->dir,"current.bin",O_RDONLY|O_NOFOLLOW|O_CLOEXEC);if(s->fd<0)return -errno;
    return 0;
}
static int export_config(struct sony_receiver_session *s,const char *path){
    s->path=strdup(path);if(!s->path)return -ENOMEM;s->dir=open(path,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);if(s->dir<0)return -errno;
    int rc=sony_snapshot_commit(path,NULL,s->snapshot);if(!rc)rc=bind_config_file(s);return rc;
}
static int pcm_check(struct sony_receiver_session *s,unsigned rate,int require_open,int full){
    (void)s;(void)rate;(void)require_open;(void)full;return 0;
}
static int mute_check(struct sony_receiver_session *s,int muted){(void)s;(void)muted;return 0;}
static int resource_assure(void *v,unsigned operation){
    struct sony_receiver_session *s=v;int rc=owner_check(s);
    /* Cleanup uses retained ownership, not failed data admission/config state.
     * It may close after the route was quiesced by an earlier error. */
    if(!rc&&operation==SONY_RECEIVER_LEGACY_KEEP_IDLE_BLOCKED_ON_CLOSE)rc=mute_check(s,1);
    if(!rc&&operation!=SONY_RECEIVER_LEGACY_KEEP_IDLE_BLOCKED_ON_CLOSE){
        if(s->sealed)rc=s->state.first_error?s->state.first_error:-EPIPE;
        if(!rc&&!s->route)rc=-EPIPE;
        if(!rc)rc=sony_audio_route_check(s->route);
        if(!rc)rc=mute_check(s,!s->state.started||s->state.muted||!!(s->flags&SONY_RECEIVER_SESSION_VERIFY_MUTED));
        if(!rc)rc=pcm_check(s,s->state.output_rate,0,!s->batch||operation==SONY_RECEIVER_LEGACY_BEFORE_PCM_OPEN);
    }
    if(!rc)rc=sony_receiver_mode_policy_assure(&s->policy,operation);return rc;
}
static int publish(struct sony_receiver_session *s){
    if(s->save)return 0; /* Old public state was retired before mutation. */
    char *base=sony_controls_text(&s->controls,s->revision,!!s->state.first_error,s->state.started&&!s->state.muted),*text=NULL;if(!base)return -ENOMEM;
    int n=asprintf(&text,"%s;sony_source=%s;sony_receiver_enabled=1;sony_output_ready=%u;sony_output_rate=%u;sony_output_format=s32le;sony_output_channels=2;sony_output_muted=%u",base,s->mode==2?"bt_receiver":"usb_dac",s->state.started,s->state.output_rate,s->state.muted||!!(s->flags&SONY_RECEIVER_SESSION_VERIFY_MUTED));free(base);
    if(n<0)return -ENOMEM;
    if(s->commands){char *extended=NULL;if(asprintf(&extended,"%s;sony_live_controls=1;sony_live_volume_only=1",text)<0){free(text);return -ENOMEM;}free(text);text=extended;}
    int rc=sony_audio_owner_publish(s->owner,text);free(text);return rc;
}
static int command_read(void *,struct wdf_controls *);
static int command_stage(void *,uint64_t,unsigned,const struct sony_dsd_live_controls *,struct wdf_controls *);
static int check(void *v){
    struct sony_receiver_session *s=v;if(s->sealed)return s->state.first_error?s->state.first_error:-EPIPE;
    int rc=owner_check(s);if(!rc&&!s->route)rc=-EPIPE;if(!rc&&!s->batch)rc=sony_audio_route_check(s->route);
    if(!rc)rc=mute_check(s,!s->state.started||s->state.muted||!!(s->flags&SONY_RECEIVER_SESSION_VERIFY_MUTED));
    /* Each actual hardware write is gated by resource_assure, including
     * physical jack, selected PCM PID/state, mute and timer/latency. Protocol
     * callbacks inside this same serialized batch need not duplicate it. */
    if(!rc&&!s->batch)rc=pcm_check(s,s->state.output_rate,s->state.started,1);if(!rc&&!s->batch)rc=publish(s);return rc;
}
static int started(void *v,unsigned rate){
    struct sony_receiver_session *s=v;if(!s->route||s->state.started)return -EINVAL;
    int rc=check(s);if(!rc)rc=pcm_check(s,rate,1,1);
    if(!rc&&!(s->flags&SONY_RECEIVER_SESSION_VERIFY_MUTED))rc=sony_audio_route_enable(s->route);
    if(!rc)rc=mute_check(s,!!(s->flags&SONY_RECEIVER_SESSION_VERIFY_MUTED));
    if(!rc){s->state.started=1;s->state.muted=!!(s->flags&SONY_RECEIVER_SESSION_VERIFY_MUTED);s->state.output_rate=rate;rc=publish(s);}return rc;
}
static int mute(void *v){
    struct sony_receiver_session *s=v;int retired=sony_audio_owner_retire(s->owner),rc=0;
    if(s->route)rc=sony_audio_route_mute(s->route,1);s->state.muted=1;rc=retired?retired:rc;if(rc)fail(s,rc);return rc;
}
static int effects(const struct sony_controls *);
static int parked_check(struct sony_receiver_session *s){
    int rc=owner_check(s);
    if(!rc)rc=sony_audio_route_parked(s->route,(unsigned)s->controls.settings.volume,effects(&s->controls),&s->controls.output);return rc;
}
static int quiesce(void *v){
    struct sony_receiver_session *s=v;int retired=sony_audio_owner_retire(s->owner),rc=0;
    int keep=!retired&&s->route&&s->retain_boundary&&!s->sealed&&!s->closing&&!s->save&&(s->flags&SONY_RECEIVER_SESSION_KEEP_ROUTE);
    if(keep)rc=parked_check(s);
    if(keep&&!rc)s->parked=1;
    else{int closed=sony_audio_route_end(&s->route);s->parked=0;if(closed&&!s->cleanup.route_end)s->cleanup.route_end=closed;if(!rc)rc=closed;}
    s->state.started=0;s->state.muted=1;s->state.output_rate=0;return retired?retired:rc;
}
static int effects(const struct sony_controls *c){return !c->settings.direct&&(c->dsee.enabled||c->settings.dc_enabled||c->settings.vinyl_enabled||(c->tone.selector==2?c->settings.eq_enabled:c->tone.tone.enabled));}
static int prepare_route(struct sony_receiver_session *s,unsigned expected){
    (void)expected;
    int rc=owner_check(s);if(!rc)rc=s->parked?parked_check(s):sony_audio_route_idle();if(rc)return rc;
    struct sony_audio_jack_state jack;struct sony_controls planned;rc=sony_audio_jack_read(&jack);if(!rc)rc=sony_controls_output_event(&s->controls,jack.se,jack.btl,&planned);
    if(rc)return rc;
    int changed=memcmp(&planned,&s->controls,sizeof(planned))!=0;
    if(changed&&s->revision==UINT32_MAX)return -EOVERFLOW;
    if(s->parked){
        rc=sony_audio_route_end(&s->route);s->parked=0;if(rc)return rc;
    }
    rc=sony_audio_route_begin(&s->route,(unsigned)planned.settings.volume,effects(&planned),&planned.output);if(rc)return rc;
    unsigned char a[SONY_SNAPSHOT_BYTES],b[SONY_SNAPSHOT_BYTES];rc=sony_snapshot_encode_volume(s->snapshot,sizeof(s->snapshot),planned.settings.volume,a);
    if(!rc)rc=sony_snapshot_encode_volume_memory(a,sizeof(a),&planned.volume_memory,b);
    if(!rc&&memcmp(b,s->snapshot,sizeof(b)))rc=sony_snapshot_commit(SONY_AUDIO_CONFIG,s->snapshot,b);
    if(!rc&&memcmp(b,s->snapshot,sizeof(b))){rc=sony_snapshot_commit(s->path,s->snapshot,b);if(!rc){memcpy(s->snapshot,b,sizeof(b));rc=bind_config_file(s);}}
    if(rc){s->state.config_error=rc;return fail(s,rc);}s->controls=planned;if(changed)++s->revision;s->state.muted=1;return 0;
}
static int start_session(struct sony_receiver_session **out,unsigned activation,struct sony_audio_owner *owner,const struct sony_receiver_session_launch *l,unsigned mode){
    (void)activation;
    if(!out||*out||!owner||!l||!l->private_config)return -EINVAL;
    int rc=0;if(!rc)rc=sony_audio_owner_check(owner);uint64_t generation=sony_audio_owner_generation(owner);
    uint64_t receipt=l->cookie^generation^UINT64_C(0x5752504300000000),guard_receipt=l->cookie^generation^UINT64_C(0x5749474300000000);
    /* Local nonzero markers keep older resource cleanup bookkeeping usable. */
    if(!receipt)receipt=1;if(!guard_receipt)guard_receipt=1;if(rc)return rc;
    struct sony_receiver_session *s=calloc(1,sizeof(*s));if(!s)return -ENOMEM;s->dir=s->fd=s->volume_notify[0]=s->volume_notify[1]=-1;s->owner=owner;s->mode=mode;s->flags=l->flags;s->revision=1;s->busy=1;s->state=(struct sony_receiver_session_state){.generation=generation,.cookie=l->cookie,.muted=1};s->cleanup.input_and_global_cleanup_unverified=1;*out=s;
    rc=sony_audio_route_idle();if(!rc)rc=sony_controls_load(&s->controls);if(!rc)rc=sony_snapshot_read(SONY_AUDIO_CONFIG,s->snapshot);s->config_loaded=!rc;
    if(!rc)rc=export_config(s,l->private_config);
    if(!rc)rc=sony_dsd_power_linux_create(&s->platform,owner_check,s);if(!rc)rc=sony_dsd_power_linux_io(s->platform,&s->io);
    if(!rc)rc=sony_receiver_idle_guard_linux_io(&s->guard_linux,&s->io,&s->guard_io);
    if(!rc)rc=sony_receiver_idle_guard_create(&s->guard,&s->guard_io,guard_receipt);if(!rc)rc=sony_receiver_idle_guard_acquire(s->guard);
    if(!rc)rc=sony_receiver_legacy_policy_create(&s->latency,&s->io,receipt);
    struct sony_dsd_power_backend backend={0};if(!rc)rc=sony_receiver_legacy_policy_backend(s->latency,&backend);
    s->policy=(struct sony_receiver_mode_policy){.io=s->io,.latency=s->latency,.idle=s->guard,
        .observation_begin=sony_dsd_power_linux_observation_begin,.observation_end=sony_dsd_power_linux_observation_end,.observation_context=s->platform};
    if(!rc){s->monitor=mixer_open(0);if(!s->monitor)rc=-ENODEV;else{s->mute_control=mixer_get_ctl_by_name(s->monitor,"playback mute");if(!s->mute_control)rc=-ENODEV;}}
    if(!rc)rc=sony_receiver_process_start(&s->dsp,SONY_RECEIVER_PROCESS_EXPLICIT_START,l->dsp_root,l->dsp_log,1,l->timeout_ms,&s->cleanup.dsp);
    s->dsp_launched=!!s->dsp||s->cleanup.dsp.reaped||s->cleanup.dsp.child_lost||s->cleanup.dsp.launch_stage||s->cleanup.dsp.launch_errno;
    struct sony_receiver_sink_process_scope scope={l->cookie,generation,receipt,(unsigned)s->controls.cfw_profile,mode,backend,resource_assure,owner_check,s};
    if(!rc)rc=sony_receiver_sink_process_start(&s->sink,SONY_RECEIVER_SINK_PROCESS_EXPLICIT_START,l->sink_root,l->sink_log,&scope,l->timeout_ms,&s->cleanup.sink);
    s->sink_launched=!!s->sink||s->cleanup.sink.spawned_pid||s->cleanup.sink.reaped||s->cleanup.sink.child_lost;
    s->sink_pid=sony_receiver_sink_process_pid(s->sink);
    if(!rc&&(l->flags&SONY_RECEIVER_SESSION_LIVE_VOLUME)){
        if(pipe2(s->volume_notify,O_NONBLOCK|O_CLOEXEC))rc=-errno;
        if(!rc){struct sony_command_ops ops={command_read,command_stage,s};rc=sony_command_server_open(&s->commands,owner,&ops);}
    }
    if(!rc){s->state.enabled=1;s->state.cfw_profile=(unsigned)s->controls.cfw_profile;rc=publish(s);}return unlock(s,rc?fail(s,rc):0);
}
int sony_receiver_session_start_usb(struct sony_receiver_session **out,unsigned activation,struct sony_audio_owner *owner,const struct sony_receiver_session_launch *l){return start_session(out,activation,owner,l,1);}
int sony_receiver_session_start_bt(struct sony_receiver_session **out,unsigned activation,struct sony_audio_owner *owner,const struct sony_receiver_session_launch *l){return start_session(out,activation,owner,l,2);}
int sony_receiver_session_promote_audio(struct sony_receiver_session *s){
    int rc=lock(s);if(rc)return rc;
    if(s->mode!=2||!(s->flags&SONY_RECEIVER_SESSION_BT_PERIOD_WRITES))return unlock(s,-EPERM);
    if(s->audio_scheduler)return unlock(s,-EALREADY);
    if(!s->state.enabled||s->sealed||s->closing||s->state.stream_index||s->route||s->playback||s->input||s->save)return unlock(s,-EBUSY);
    int children[2]={sony_receiver_process_pid(s->dsp),sony_receiver_sink_process_pid(s->sink)};
    s->audio_scheduler=1;
    for(unsigned n=0;n<2;++n){
        struct sched_param priority={.sched_priority=2};
        if(children[n]>0&&sched_setscheduler(children[n],SCHED_RR,&priority))
            fprintf(stderr,"BT_AUDIO_CHILD_SCHEDULING pid=%d error=%d\n",children[n],errno);
    }
    return unlock(s,0);
}
static int begin_stream(struct sony_receiver_session *s,const void *stream,unsigned expected,unsigned bluetooth){
    if(!stream||(expected!=SONY_OUTPUT_SE&&expected!=SONY_OUTPUT_BTL))return -EINVAL;
    unsigned track[25];int rc=bluetooth?sony_receiver_pcm_bt_track(stream,track):sony_receiver_pcm_usb_track(stream,track);if(rc)return rc;rc=lock(s);if(rc)return rc;
    if(s->mode!=(bluetooth?2u:1u))return unlock(s,-EPERM);
    if(s->closing||s->sealed||!s->state.enabled)return unlock(s,-EPIPE);if(s->save||s->playback||s->input||(s->route&&!s->parked))return unlock(s,-EBUSY);if(s->state.stream_index==UINT64_MAX)return unlock(s,-EOVERFLOW);
    rc=bluetooth?sony_receiver_pcm_open_bt(&s->input,stream):sony_receiver_pcm_open_usb(&s->input,stream);if(rc)return unlock(s,rc);rc=prepare_route(s,expected);
    if(rc){sony_receiver_pcm_destroy(&s->input);if(s->route){int closed=sony_audio_route_end(&s->route);if(closed&&!s->cleanup.route_end)s->cleanup.route_end=closed;}s->parked=0;if(rc!=-ENXIO)fail(s,rc);return unlock(s,rc);}
    s->state.source_accepted_frames=s->state.source_taken_frames=0;s->state.source_rate=track[0];s->state.source_bits=bluetooth?track[1]:((const struct sony_usb_stream *)stream)->bit_width;s->state.finished=s->state.started=s->state.output_rate=0;s->state.muted=1;++s->state.stream_index;
    memset(&s->state.playback,0,sizeof(s->state.playback));memset(&s->cleanup.playback,0,sizeof(s->cleanup.playback));s->cleanup.playback.output_cleanup_unverified=1;
    struct wur_open opening={0};memcpy(opening.track,track,sizeof(track));opening.settings=s->controls.settings;opening.output=s->controls.output;opening.dsee=s->controls.dsee;opening.tone=s->controls.tone.tone;opening.selector=s->controls.tone.selector;
    struct sony_receiver_playback_owner callbacks={check,started,mute,quiesce,s};s->state.stream_open=1;
    rc=bluetooth?(s->flags&SONY_RECEIVER_SESSION_BT_PERIOD_WRITES?sony_receiver_playback_start_bt_period:sony_receiver_playback_start_bt)(&s->playback,sony_receiver_process_client(s->dsp),sony_receiver_sink_process_client(s->sink),&opening,&callbacks):
        sony_receiver_playback_start(&s->playback,sony_receiver_process_client(s->dsp),sony_receiver_sink_process_client(s->sink),&opening,&callbacks);
    if(rc)fail(s,rc);return unlock(s,rc);
}
int sony_receiver_session_begin_usb(struct sony_receiver_session *s,const struct sony_usb_stream *stream,unsigned expected){return begin_stream(s,stream,expected,0);}
int sony_receiver_session_begin_bt(struct sony_receiver_session *s,const struct sony_bt_pcm_stream *stream,unsigned expected){return begin_stream(s,stream,expected,1);}
static int packets(struct sony_receiver_session *s){
    for(;;){struct sony_receiver_pcm_packet packet;int rc=sony_receiver_pcm_take(s->input,s->packet,sizeof(s->packet),&packet);
        if(rc==-EAGAIN||rc==-ENODATA)return 0;if(rc)return rc;
        rc=sony_receiver_playback_push(s->playback,s->packet,packet.frames,packet.frames<512);if(rc)return rc;
    }
}
static void source_state(struct sony_receiver_session *s){
    if(s->input){unsigned queued;int sealed;sony_receiver_pcm_state(s->input,&s->state.source_accepted_frames,&s->state.source_taken_frames,&queued,&sealed);}
    if(s->playback)sony_receiver_playback_state(s->playback,&s->state.playback);
}
int sony_receiver_session_push(struct sony_receiver_session *s,const void *data,unsigned frames){
    if(!data||!frames||frames>SONY_RECEIVER_SESSION_MAX_INPUT_FRAMES)return -EINVAL;int rc=lock(s);if(rc)return rc;
    if(s->sealed||s->closing||!s->input||!s->playback||s->state.finished)return unlock(s,-EPIPE);
    /* Full six-output/rate/config proof at both bounded batch boundaries.
     * Inside serialized push, retain exact lease/config, physical jack/mute,
     * selected PCM PID/state and all resource guards for every native write.
     * Hardware Open still performs full checks; no wall-clock cache. */
    rc=check(s);if(!rc){s->batch=1;rc=s->mode==2?sony_receiver_pcm_push_bt(s->input,data,frames):sony_receiver_pcm_push(s->input,data,frames);if(!rc)rc=packets(s);s->batch=0;if(!rc)rc=check(s);}
    source_state(s);if(rc)fail(s,rc);return unlock(s,rc);
}
int sony_receiver_session_finish_stream(struct sony_receiver_session *s,unsigned terminal){
    if(terminal!=WUR_SERVICE_STOP&&terminal!=WUR_SERVICE_DRAIN)return -EINVAL;int rc=lock(s);if(rc)return rc;
    if(s->sealed||s->closing||!s->input||!s->playback)return unlock(s,-EPIPE);
    s->retain_boundary=!!(s->flags&SONY_RECEIVER_SESSION_KEEP_ROUTE);
    if(!s->state.finished){rc=sony_receiver_pcm_seal(s->input);if(!rc)rc=packets(s);if(!rc)rc=sony_receiver_playback_finish(s->playback,terminal);if(!rc)s->state.finished=1;}
    else rc=sony_receiver_playback_finish(s->playback,terminal);
    s->retain_boundary=0;
    source_state(s);if(rc)fail(s,rc);return unlock(s,rc);
}
static int close_stream(struct sony_receiver_session *s,struct sony_receiver_playback_cleanup *out){
    int rc=0;if(s->playback){rc=sony_receiver_playback_close(&s->playback,&s->cleanup.playback);if(s->playback)return rc;}
    else{int a=mute(s),b=quiesce(s);if(a||b)rc=a?a:b;}
    sony_receiver_pcm_destroy(&s->input);s->state.stream_open=0;
    if(s->cleanup.playback.first_error||rc)fail(s,rc?rc:s->cleanup.playback.first_error);
    if(s->parked&&(s->sealed||s->closing)){int closed=quiesce(s);if(!rc)rc=closed;}
    if(!rc&&!s->sealed){int a=owner_check(s);if(!a)a=s->parked?parked_check(s):sony_audio_route_idle();if(!a)a=publish(s);if(a){rc=fail(s,a);if(s->parked)quiesce(s);}}
    *out=s->cleanup.playback;return rc;
}
int sony_receiver_session_close_stream(struct sony_receiver_session *s,struct sony_receiver_playback_cleanup *out){if(!out)return -EINVAL;int rc=lock(s);if(rc)return rc;rc=close_stream(s,out);return unlock(s,rc);}
int sony_receiver_session_state(struct sony_receiver_session *s,struct sony_receiver_session_state *out){if(!out)return -EINVAL;int rc=lock(s);if(rc)return rc;source_state(s);*out=s->state;return unlock(s,0);}
int sony_receiver_session_observe(struct sony_receiver_session *s){
    int rc=lock(s);if(rc)return rc;
    if(s->save)return unlock(s,-EBUSY);
    if(s->sealed||s->closing||!s->state.enabled)return unlock(s,s->state.first_error?s->state.first_error:-EPIPE);
    rc=owner_check(s);
    if(!rc&&s->route&&!s->parked)rc=check(s);
    else if(!rc){rc=s->route?parked_check(s):sony_audio_route_idle();if(!rc)rc=publish(s);}
    if(rc)fail(s,rc);return unlock(s,rc);
}
static int volume_context(struct sony_receiver_session *s){
    if(s->sealed||s->closing||!s->state.enabled)return s->state.first_error?s->state.first_error:-EPIPE;
    if(s->flags&SONY_RECEIVER_SESSION_VERIFY_MUTED)return -ENOTSUP;
    if(s->state.finished&&s->state.stream_open)return -EBUSY;
    int rc=owner_check(s);
    if(!rc&&s->route){
        if(s->state.started){
            rc=check(s);
            if(!rc)rc=sony_audio_route_live_volume(s->route,(unsigned)s->controls.settings.volume,
                (unsigned)s->controls.settings.volume,&s->controls.output);
        }else rc=sony_audio_route_parked(s->route,(unsigned)s->controls.settings.volume,effects(&s->controls),&s->controls.output);
    }else if(!rc)rc=sony_audio_route_idle();
    return rc;
}
int sony_receiver_session_get_controls(struct sony_receiver_session *s,struct sony_controls *out,unsigned *revision){
    if(!out||!revision)return -EINVAL;int rc=lock(s);if(rc)return rc;
    if(s->save)return unlock(s,-EBUSY);
    rc=volume_context(s);if(!rc){*out=s->controls;*revision=s->revision;}
    return unlock(s,rc);
}
static uint64_t volume_clock(void){struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t))return 0;return (uint64_t)t.tv_sec*1000000u+(unsigned)t.tv_nsec/1000u;}
int sony_receiver_session_volume(struct sony_receiver_session *s,uint64_t generation,unsigned revision,unsigned volume,unsigned *applied){
    (void)generation;(void)revision;
    if(volume>120||!applied)return -EINVAL;int rc=lock(s);if(rc)return rc;
    if(s->save)return unlock(s,-EBUSY);
    rc=volume_context(s);if(rc)return unlock(s,rc);
    if(s->revision==UINT32_MAX)return unlock(s,-EOVERFLOW);
    struct sony_audio_jack_state jack;rc=sony_audio_jack_read(&jack);if(rc)return unlock(s,rc);
    const struct sony_controls *c=&s->controls;
    struct sony_volume_state current={c->ship_selector,c->settings.volume,c->output.device,c->volume_memory.last_headphone,
        !!jack.se,!!jack.btl,c->output.se_gain,c->output.btl_gain,c->volume_memory.avls,c->avls_threshold};
    struct sony_volume_plan plan;rc=sony_volume_plan_set(&current,(int)volume,&plan);if(rc)return unlock(s,rc);
    if(plan.next.volume!=(int)volume)return unlock(s,-ERANGE);
    if(volume==(unsigned)c->settings.volume){*applied=s->revision;return unlock(s,0);}
    unsigned char next[SONY_SNAPSHOT_BYTES];
    rc=sony_snapshot_encode_volume(s->snapshot,sizeof(s->snapshot),(int)volume,next);if(rc)return unlock(s,rc);
    uint64_t clock0=volume_clock(),clock1,clock2,clock3,clock4;
    rc=(s->flags&SONY_RECEIVER_SESSION_BT_PERIOD_WRITES)&&s->playback?sony_receiver_playback_flush(s->playback):0;
    if(!rc)rc=sony_audio_owner_retire(s->owner);
    if(!rc&&s->state.stream_open){
        struct wur_state notified;
        rc=sony_receiver_client_volume(sony_receiver_process_client(s->dsp),(unsigned)c->settings.volume,volume,&notified);
    }
    clock1=volume_clock();
    if(!rc&&s->route)rc=s->state.started?
        sony_audio_route_live_volume(s->route,(unsigned)c->settings.volume,volume,&c->output):
        sony_audio_route_volume(s->route,volume);
    clock2=volume_clock();
    if(!rc)rc=sony_snapshot_commit(SONY_AUDIO_CONFIG,s->snapshot,next);
    clock3=volume_clock();
    if(!rc)rc=sony_snapshot_commit(s->path,s->snapshot,next);
    clock4=volume_clock();
    if(!rc){memcpy(s->snapshot,next,sizeof(next));rc=bind_config_file(s);}
    if(!rc){s->controls.settings.volume=(int)volume;++s->revision;rc=volume_context(s);}
    if(!rc)rc=publish(s);
    fprintf(stderr,"USB_VOLUME_TIMING rc=%d old=%d next=%u dsp_us=%llu dac_us=%llu global_us=%llu private_us=%llu final_us=%llu\n",rc,c->settings.volume,volume,(unsigned long long)(clock1-clock0),(unsigned long long)(clock2-clock1),(unsigned long long)(clock3-clock2),(unsigned long long)(clock4-clock3),(unsigned long long)(volume_clock()-clock4));
    if(rc){s->state.config_error=rc;(void)mute(s);return unlock(s,fail(s,rc));}
    *applied=s->revision;return unlock(s,0);
}
static int command_read(void *v,struct wdf_controls *out){
    struct sony_receiver_session *s=v;if(s->command_error)return s->command_error;
    if(s->save){int rc=volume_context(s);if(!rc)*out=s->save->pending;return rc;}
    struct sony_controls c;unsigned revision;int rc=sony_receiver_session_get_controls(s,&c,&revision);if(rc)return rc;
    struct wdf_controls wire={.revision=revision,.pending=s->command_pending,.stage_request=s->command_request};
    wire.controls.settings=c.settings;wire.controls.tone=c.tone.tone;wire.controls.selector=c.tone.selector;wire.controls.dsee=c.dsee;
    rc=sony_snapshot_apply_dsd(s->snapshot,sizeof(s->snapshot),&wire.controls.dsd);if(rc)return rc;
    *out=wire;return 0;
}
static int command_stage(void *v,uint64_t generation,unsigned revision,const struct sony_dsd_live_controls *target,struct wdf_controls *out){
    (void)generation;(void)revision;
    struct sony_receiver_session *s=v;
    struct wdf_controls base;int rc=command_read(s,&base);if(rc)return rc;
    if(base.pending)return -EBUSY;
    struct sony_dsd_live_controls allowed=base.controls;allowed.settings.volume=target->settings.volume;
    if(memcmp(&allowed,target,sizeof(allowed)))return -ENOTSUP;
    if(target->settings.volume<0||target->settings.volume>120)return -EINVAL;
    struct sony_controls c;unsigned checked_revision;rc=sony_receiver_session_get_controls(s,&c,&checked_revision);if(rc)return rc;
    struct sony_volume_state vs={c.ship_selector,c.settings.volume,c.output.device,c.volume_memory.last_headphone,
        0,0,c.output.se_gain,c.output.btl_gain,c.volume_memory.avls,c.avls_threshold};struct sony_volume_plan plan;
    rc=sony_volume_plan_set(&vs,target->settings.volume,&plan);if(rc)return rc;
    if(plan.next.volume!=target->settings.volume)return -ERANGE;
    if(target->settings.volume==base.controls.settings.volume){*out=base;return 0;}
    if(s->command_request==UINT32_MAX)return -EOVERFLOW;
    s->command_pending=1;s->command_revision=base.revision;s->command_volume=(unsigned)target->settings.volume;++s->command_request;
    base.pending=1;base.stage_request=s->command_request;s->command_base=base;*out=base;return 0;
}
#include "sony_audio_receiver_volume_async.inc"
int sony_receiver_session_control_descriptors(struct sony_receiver_session *s,int out[2]){
    if(!s||!out)return -EINVAL;if(s->busy)return -EBUSY;if(!s->commands)return -ENOTSUP;
    int fd=sony_command_server_descriptor(s->commands);if(fd<0)return fd;
    if(s->volume_notify[0]<0)return -EBADF;out[0]=fd;out[1]=s->volume_notify[0];return 0;
}
int sony_receiver_session_poll_controls(struct sony_receiver_session *s){
    if(!s)return -EINVAL;if(s->busy)return -EBUSY;if(!s->commands)return 0;
    if(s->command_error)return s->command_error;
    if(s->save&&__atomic_load_n(&s->save->done,__ATOMIC_ACQUIRE)){
        int rc=lock(s);if(rc)return rc;rc=volume_save_collect(s);unlock(s,rc);
        if(rc){s->command_error=rc;return rc;}
        /* Collect joins the writer first. Draining before join could miss a
         * notification sent just after done=1 and leave the fd always ready. */
        unsigned char bytes[32];while(read(s->volume_notify[0],bytes,sizeof(bytes))>0){}
    }
    if(s->command_pending&&!s->save){
        unsigned applied;int rc=s->state.started?volume_save_start(s):sony_receiver_session_volume(s,s->state.generation,s->command_revision,s->command_volume,&applied);
        if(rc==-EBUSY)return 1;
        if(rc){s->command_error=rc;return rc;}
        if(!s->save)s->command_pending=0;
    }
    int rc=sony_command_server_poll(s->commands);if(rc<0)s->command_error=rc;return rc<0?rc:s->save?2:s->command_pending?1:0;
}
int sony_receiver_session_cleanup_pending(const struct sony_receiver_session *s){return s&&s->save&&!s->save->join_error;}
static int retained(struct sony_receiver_session *s,struct sony_receiver_session_cleanup *out,int rc){s->cleanup.first_error=s->state.first_error;s->cleanup.config_error=s->state.config_error;*out=s->cleanup;return unlock(s,rc);}
int sony_receiver_session_stop(struct sony_receiver_session **owner,int timeout,struct sony_receiver_session_cleanup *out){
    if(!owner||!*owner||!out||timeout<1||timeout>30000)return -EINVAL;struct sony_receiver_session *s=*owner;int rc=lock(s);if(rc)return rc;s->closing=1;
    if(s->commands){int ended=sony_command_server_close(&s->commands);if(ended){s->state.config_error=ended;fail(s,ended);}}
    int stream_close_status=close_stream(s,&s->cleanup.playback);if(stream_close_status&&!s->cleanup.stream_close)s->cleanup.stream_close=stream_close_status;if(s->playback)return retained(s,out,stream_close_status?stream_close_status:-EBUSY);
    if(s->save){
        if(!__atomic_load_n(&s->save->done,__ATOMIC_ACQUIRE))return retained(s,out,-EAGAIN);
        int saved=volume_save_collect(s);if(saved&&!s->state.config_error)s->state.config_error=saved;
        if(s->save)return retained(s,out,saved?saved:-EBUSY);
    }
    if(s->dsp){
        if(!s->dsp_shutdown_attempted){s->dsp_shutdown_attempted=1;struct sony_receiver_client *c=sony_receiver_process_client(s->dsp);s->cleanup.dsp_shutdown=c?sony_receiver_client_shutdown(c):-EPIPE;}
        s->cleanup.dsp_stop=sony_receiver_process_stop(&s->dsp,timeout,&s->cleanup.dsp);
    }
    if(s->sink){
        if(!s->sink_shutdown_attempted){s->sink_shutdown_attempted=1;struct sony_pcm_sink_client *c=sony_receiver_sink_process_client(s->sink);s->cleanup.sink_shutdown=c?sony_pcm_sink_shutdown(c):-EPIPE;}
        s->cleanup.sink_stop=sony_receiver_sink_process_stop(&s->sink,timeout,&s->cleanup.sink);
    }
    if(s->dsp||s->sink)return retained(s,out,s->cleanup.dsp_stop?s->cleanup.dsp_stop:s->cleanup.sink_stop?s->cleanup.sink_stop:-EBUSY);
    if(s->latency){s->cleanup.latency_destroy=sony_receiver_legacy_policy_destroy(&s->latency);if(s->latency)return retained(s,out,s->cleanup.latency_destroy);}
    if(s->guard){struct sony_receiver_idle_guard_state state;int a=sony_receiver_idle_guard_state(s->guard,&state);if(a)return retained(s,out,a);
        if(state.token){s->cleanup.idle_release=sony_receiver_idle_guard_release(s->guard);if(s->cleanup.idle_release)return retained(s,out,s->cleanup.idle_release);}
        s->cleanup.idle_destroy=sony_receiver_idle_guard_destroy(&s->guard);if(s->guard)return retained(s,out,s->cleanup.idle_destroy);
    }
    int route_end=quiesce(s);if(!s->cleanup.route_end)s->cleanup.route_end=route_end;
    if(s->platform){s->cleanup.platform_destroy=sony_dsd_power_linux_destroy(&s->platform);if(s->platform)return retained(s,out,s->cleanup.platform_destroy);}
    /* No independent hardware or configuration readback is performed. */
    s->cleanup.output_resources_verified=0;s->cleanup.config_verified=0;
    s->cleanup.first_error=s->state.first_error;s->cleanup.config_error=s->state.config_error;*out=s->cleanup;
    const int errors[]={s->state.config_error,s->cleanup.stream_close,s->cleanup.playback.sink_close,
        s->cleanup.playback.dsp_close,s->cleanup.playback.mute,s->cleanup.playback.quiesce,
        s->cleanup.dsp_shutdown,s->cleanup.sink_shutdown,s->cleanup.dsp_stop,s->cleanup.sink_stop,
        s->cleanup.route_end,s->cleanup.latency_destroy,s->cleanup.idle_release,s->cleanup.idle_destroy,s->cleanup.platform_destroy};
    rc=0;for(unsigned n=0;n<sizeof(errors)/sizeof(errors[0]);++n)if(!rc&&errors[n])rc=errors[n];
    if(s->volume_notify[0]>=0)close(s->volume_notify[0]);if(s->volume_notify[1]>=0)close(s->volume_notify[1]);
    if(s->fd>=0)close(s->fd);if(s->dir>=0)close(s->dir);if(s->monitor)mixer_close(s->monitor);free(s->path);free(s);*owner=NULL;return rc;
}
