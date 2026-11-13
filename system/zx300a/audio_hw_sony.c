/* WalkDroid original-Sony PCM HAL candidate. No installation or auto activation.
 * The current audio.primary.default.so/PCM0 source remains a separate rollback.
 */
#define _GNU_SOURCE
#include "sony_audio_root_verify.h"
#include "sony_audio_worker_process.h"
#include "sony_audio_output_adapter.h"
#include "sony_audio_pcm_format.h"
#include "sony_audio_route.h"
#include "sony_audio_jack.h"
#include "sony_audio_controls.h"
#ifdef SONY_SOUND_PRESETS
#include "sony_audio_preset_controls.h"
#endif
#include "sony_audio_owner.h"
#include "sony_audio_settings_relay.h"
#ifdef SONY_HAL_AI
#include "sony_audio_ai_hal.h"
#include <openssl/rand.h>
#endif
#include <hardware/audio.h>
#include <hardware/hardware.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <sched.h>
#include <sys/syscall.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
extern int __system_property_get(const char *,char *);
/* The pinned direct profile negotiates 3840 native frames per period. Round
 * one period up to a whole 512-frame DSP packet for Android's write quantum.
 * Advertising 512 falsely selected FastMixer for an 80ms bursty sink. */
#define SONY_SOURCE_QUANTUM 4096u
struct sony_out;
enum sony_job {JOB_WRITE=1,JOB_DRAIN,JOB_STANDBY,JOB_REAP,JOB_CONTROLS,JOB_ROUTING,JOB_EXTERNAL_CONTROLS};
struct sony_device {
    struct audio_hw_device device;
    pthread_mutex_t lock;
    int root,log,failed,native_error,route_error;
    unsigned int streams;
    struct sony_out *active;
    struct sony_worker_process *process;
#ifdef SONY_HAL_AI
    struct sony_ai_hal *ai;
    int ai_root,ai_log;
#endif
    struct wdc_output_adapter *adapter;
    struct sony_audio_route *route;
    struct sony_audio_owner *owner;
    struct sony_settings_relay *settings_relay;
    struct sony_worker_exit last_exit;
    pthread_t controller;
    pthread_cond_t changed;
    int controller_ready,controller_error,controller_policy,controller_priority;
    pid_t controller_tid;
    int busy,done,stopping;
    enum sony_job job;
    struct sony_out *job_stream;
    const void *job_buffer;
    unsigned int job_bytes;
    ssize_t job_result;
    struct sony_controls controls,job_controls;
    unsigned int controls_revision;
    int controls_pending,controls_error,job_profile; /* 1 queued, 2 native staged */
    uint64_t controls_start_frames;
    struct timespec controls_deadline;
#ifdef SONY_SOUND_PRESETS
    struct sony_sound_preset_plan preset;
    unsigned int preset_receipt_revision,preset_receipt_operation,preset_receipt_slot;
#endif
};
static void acknowledge_controls_locked(struct sony_device *d){
    d->controls=d->job_controls;++d->controls_revision;
#ifdef SONY_SOUND_PRESETS
    if(d->preset.operation){
        d->preset_receipt_revision=d->controls_revision;
        d->preset_receipt_operation=d->preset.operation;d->preset_receipt_slot=d->preset.slot;
        d->preset.operation=0;
    }
#endif
}
static int commit_controls_locked(struct sony_device *d,struct wdc_client *client){
#ifdef SONY_SOUND_PRESETS
    if(d->preset.operation)return sony_controls_commit_preset(client,&d->job_controls,&d->preset);
#endif
    return sony_controls_commit(client,&d->job_controls);
}
struct sony_out {
    struct audio_stream_out stream;
    struct sony_device *device;
    unsigned int rate,bits;
    audio_format_t format;
    audio_devices_t devices;
    audio_output_flags_t flags;
    uint64_t copied;
    uint64_t discarded; /* Explicit BUS disposal; never native/DAC consumption. */
    int write_discarded; /* Result of this stream's serialized JOB_WRITE. */
    int error;
    struct sony_output_timing timing;
};
static pthread_mutex_t module_lock=PTHREAD_MUTEX_INITIALIZER;
static int device_open;
static int ai_pending(const struct sony_device *d){
#ifdef SONY_HAL_AI
    return d->ai!=NULL;
#else
    (void)d;return 0;
#endif
}
#ifdef SONY_HAL_AI
static int ai_wanted(const struct sony_controls *c,unsigned rate){
    return !c->settings.direct&&c->dsee.enabled&&c->dsee.mode==1&&(rate==44100||rate==48000);
}
#endif
static ssize_t dispatch_locked(struct sony_device *,enum sony_job,struct sony_out *,const void *,unsigned int);
static int supported_route(unsigned long device) {
    return device==AUDIO_DEVICE_NONE||device==AUDIO_DEVICE_OUT_WIRED_HEADPHONE||device==AUDIO_DEVICE_OUT_BUS;
}
static int translated(int native) {
    if(!native)return 0;
    if(native==-19)return -ENODEV;
    if(native==-2)return -EINVAL;
    if(native==WDA_CLIENT_TIMEOUT||native==SONY_PROCESS_CLEANUP_PENDING)return -ETIMEDOUT;
    return -EIO;
}
static int fail(struct sony_device *d,struct sony_out *s,int native) {
    if(!d->failed){d->native_error=native;dprintf(d->log,"SONY_HAL_FAILURE job=%u native=%d copied=%llu controller_tid=%d\n",(unsigned)d->job,native,(unsigned long long)(s?s->copied:0),(int)d->controller_tid);}
    /* A failed output immediately disables the physical route. */
    sony_audio_route_end(&d->route);
    d->failed=1;if(s&&!s->error)s->error=translated(native);return translated(native);
}
/* Cooperative claim includes configuration writes. Only a normal native
 * shutdown plus independent muted/off/all-PCM-closed observation clears it.
 * Failure releases descriptors with ACTIVE retained for explicit recovery. */
static int owner_finish_locked(struct sony_device *d,int clean){
    if(!d->owner)return 0;
    if(d->process||d->adapter||d->route||ai_pending(d))return -EBUSY;
    int rc=clean?sony_audio_route_idle():0;
    int released=sony_audio_owner_finish(&d->owner,clean&&!rc);return rc?rc:released;
}
static int accept_saved_controls_locked(struct sony_device *d,struct sony_controls *current,int controls_request){
    (void)controls_request;
    /* Snapshot stores gains/lastHeadphone, not the live DSP selector. */
    current->output.device=d->controls.output.device;
    if(!memcmp(current,&d->controls,sizeof(*current)))return 0;
    if(d->controls_revision>=INT_MAX)return -EOVERFLOW;
    d->controls=*current;++d->controls_revision;
    return 0;
}
static int owner_begin_locked(struct sony_device *d,int controls_request){
    if(d->owner)return sony_audio_owner_check(d->owner);
    int rc=sony_audio_owner_acquire(&d->owner,SONY_OWNER_HAL);if(rc)return rc;
    rc=sony_audio_route_idle();
    if(rc){sony_audio_owner_finish(&d->owner,0);return rc;}
    struct sony_controls current;rc=sony_controls_load(&current);
    /* The snapshot stores gains and lastHeadphone, not the current DSP output
     * selector. Loading it seeds SE for a new worker; that is not an external
     * edit and must not reset this controller's BTL state on every standby.
     * Fresh playback still re-evaluates actual jacks before route/worker open. */
    if(!rc)rc=accept_saved_controls_locked(d,&current,controls_request);
    if(rc){int closed=owner_finish_locked(d,1);if(closed)rc=closed;}return rc;
}
static uint32_t sample_rate(const struct audio_stream *s) {return ((const struct sony_out *)s)->rate;}
static int set_rate(struct audio_stream *s,uint32_t rate) {return rate==sample_rate(s)?0:-EINVAL;}
static size_t buffer_size(const struct audio_stream *s) {return SONY_SOURCE_QUANTUM*((const struct sony_out *)s)->bits/4;}
static audio_channel_mask_t channels(const struct audio_stream *s) {(void)s;return AUDIO_CHANNEL_OUT_STEREO;}
static audio_format_t format(const struct audio_stream *s) {return ((const struct sony_out *)s)->format;}
static int set_format(struct audio_stream *s,audio_format_t f) {return f==format(s)?0:-EINVAL;}
static int parameters(struct audio_stream *stream,const char *parameters) {
    if(!parameters||strlen(parameters)>=256)return -EINVAL;
    struct sony_out *s=(struct sony_out *)stream;char copy[256];strcpy(copy,parameters);
    char *save=NULL;int have_route=0;audio_devices_t route=AUDIO_DEVICE_NONE;
    for(char *part=strtok_r(copy,";",&save);part;part=strtok_r(NULL,";",&save)) {
        char *equals=strchr(part,'=');if(!equals||equals==part)return -EINVAL;
        *equals++=0;if(*equals<'0'||*equals>'9')return -EINVAL;
        errno=0;char *end=NULL;unsigned long value=strtoul(equals,&end,10);
        if(errno||!end||*end)return -EINVAL;
        unsigned long expected;
        if(!strcmp(part,AUDIO_PARAMETER_STREAM_ROUTING)) {
            if(have_route||!supported_route(value)||
               (value==AUDIO_DEVICE_OUT_BUS&&(s->flags&AUDIO_OUTPUT_FLAG_DIRECT)))return -EINVAL;
            route=(audio_devices_t)value;have_route=1;continue;
        }
        else if(!strcmp(part,AUDIO_PARAMETER_STREAM_SAMPLING_RATE))expected=s->rate;
        else if(!strcmp(part,AUDIO_PARAMETER_STREAM_FORMAT))expected=s->format;
        else if(!strcmp(part,AUDIO_PARAMETER_STREAM_CHANNELS))expected=AUDIO_CHANNEL_OUT_STEREO;
        else return -ENOSYS;
        if(value!=expected)return -EINVAL; /* Format changes still require reopen. */
    }
    pthread_mutex_lock(&s->device->lock);int rc=s->device->failed?-EIO:0;
    if(!rc&&have_route)rc=(int)dispatch_locked(s->device,JOB_ROUTING,s,NULL,route);
    pthread_mutex_unlock(&s->device->lock);return rc;
}
static int has_key(const char *keys,const char *key) {
    size_t n=strlen(key);
    for(const char *p=keys;*p;) {
        const char *end=strchr(p,';');size_t length=end?(size_t)(end-p):strlen(p);
        if(length==n&&!memcmp(p,key,n))return 1;
        if(!end)break;
        p=end+1;
    }
    return 0;
}
static char *get_parameters(const struct audio_stream *s,const char *keys) {
    if(!keys)return strdup("");
    const struct sony_out *out=(const struct sony_out *)s;char reply[384]={0},current[80];
    if(has_key(keys,AUDIO_PARAMETER_STREAM_SUP_SAMPLING_RATES))strcat(reply,"sup_sampling_rates=8000|11025|12000|16000|22050|24000|32000|44100|48000|88200|96000|176400|192000|352800|384000;");
    if(has_key(keys,AUDIO_PARAMETER_STREAM_SUP_FORMATS))strcat(reply,"sup_formats=AUDIO_FORMAT_PCM_16_BIT|AUDIO_FORMAT_PCM_32_BIT|AUDIO_FORMAT_PCM_FLOAT;");
    if(has_key(keys,AUDIO_PARAMETER_STREAM_SUP_CHANNELS))strcat(reply,"sup_channels=AUDIO_CHANNEL_OUT_STEREO;");
    if(has_key(keys,AUDIO_PARAMETER_STREAM_ROUTING)){
        pthread_mutex_lock(&out->device->lock);audio_devices_t target=out->devices;pthread_mutex_unlock(&out->device->lock);
        snprintf(current,sizeof(current),"routing=%u;",target);strcat(reply,current);
    }
    if(has_key(keys,AUDIO_PARAMETER_STREAM_SAMPLING_RATE)){snprintf(current,sizeof(current),"sampling_rate=%u;",out->rate);strcat(reply,current);}
    if(has_key(keys,AUDIO_PARAMETER_STREAM_FORMAT)){snprintf(current,sizeof(current),"format=%u;",out->format);strcat(reply,current);}
    if(has_key(keys,AUDIO_PARAMETER_STREAM_CHANNELS)){snprintf(current,sizeof(current),"channels=%u;",AUDIO_CHANNEL_OUT_STEREO);strcat(reply,current);}
    size_t n=strlen(reply);if(n)reply[n-1]=0;return strdup(reply);
}
static int effect(const struct audio_stream *s,effect_handle_t e) {(void)s;(void)e;return -ENOSYS;}
static int volume(struct audio_stream_out *s,float left,float right) {
    (void)s;(void)left;(void)right;return -ENOSYS; /* Native volume control is not yet wired; do not acknowledge ignored attenuation. */
}
static uint32_t latency(const struct audio_stream_out *s) {
    const struct sony_out *out=(const struct sony_out *)s;
    pthread_mutex_lock(&out->device->lock);struct sony_output_timing t=out->timing;
    audio_devices_t route=out->devices;pthread_mutex_unlock(&out->device->lock);
    /* BUS has no hardware queue. One software write quantum is the upper
     * disposal delay, not the previously negotiated physical DAC capacity. */
    if(route==AUDIO_DEVICE_OUT_BUS)return out->rate?(uint32_t)(((uint64_t)SONY_SOURCE_QUANTUM*1000u+out->rate-1u)/out->rate):0;
    /* Before lazy Open scale the verified 48 kHz direct-profile capacities.
     * Afterwards use negotiated ALSA values. Include Adler and native adapter
     * capacity; this is an upper queue estimate, NOT a hardware timestamp. */
    if(!t.valid){t.sample_rate=out->rate;t.period_frames=(unsigned)(((uint64_t)out->rate*2u+24u)/25u);t.buffer_frames=(unsigned)(((uint64_t)out->rate*16u+24u)/25u);}
    uint64_t queued=(uint64_t)t.buffer_frames+t.period_frames;
    uint64_t buffered=t.sample_rate&&queued?((queued-1u)*1000u+t.sample_rate-1u)/t.sample_rate:0;
    uint64_t source=out->rate?(((uint64_t)sony_pcm_source_frames(out->rate)-1u)*1000u+out->rate-1u)/out->rate:0;
    return (uint32_t)(buffered+source);
}
static int render_position(const struct audio_stream_out *s,uint32_t *frames) {(void)s;(void)frames;return -ENOSYS;}
static int presentation(const struct audio_stream_out *s,uint64_t *frames,struct timespec *time) {(void)s;(void)frames;(void)time;return -ENOSYS;}
static int write_timestamp(const struct audio_stream_out *s,int64_t *time) {(void)s;(void)time;return -ENOSYS;}
/* Caller holds d->lock. An unreaped child remains owned by d, never by freed s. */
static int stop_locked(struct sony_device *d,struct sony_out *s) {
    if(d->active!=s)return 0;
    int result=0;
    /* Abort/standby must disable the physical path before any potentially
     * blocking native close or child cleanup. drain() is a separate request. */
    int route_status=sony_audio_route_end(&d->route);
    if(route_status)result=fail(d,s,route_status);
    if(d->adapter) {
        int rc=wdc_adapter_abort_close(&d->adapter);if(rc)result=fail(d,s,rc);
    }
    if(d->process) {
        struct wdc_client *c=sony_worker_process_client(d->process);
        if(c&&!d->failed) {
            int rc=wdc_client_shutdown_output(c);if(rc)result=fail(d,s,rc);
        }
        int rc=sony_worker_process_stop(&d->process,1000,&d->last_exit);
        if(rc)result=fail(d,s,rc);
        if(!rc&&(d->last_exit.kill_sent||d->last_exit.child_lost||d->last_exit.exit_code!=0))result=fail(d,s,-EIO);
    }
#ifdef SONY_HAL_AI
    if(!d->process&&d->ai){int rc=sony_ai_hal_end(&d->ai,1000);if(rc)result=fail(d,s,rc);}
#endif
    if(!d->process&&!ai_pending(d))d->active=NULL;
    if(!d->process&&!d->adapter&&!d->route&&!ai_pending(d)){int rc=owner_finish_locked(d,!d->failed&&!result);if(rc)result=fail(d,s,rc);}
    return result;
}
static int route_result_locked(struct sony_device *d,struct sony_out *s,int rc) {
    if(!rc){d->route_error=0;return 0;}
    if(d->controls_pending){d->controls_pending=0;d->controls_error=rc;}
    if(rc!=-ENXIO)return fail(d,s,rc);
    /* A physical connection change is not a poisoned DSP or IPC session.
     * Cancel uncommitted controls and discard old pending PCM on teardown.
     * Cleanup errors still latch a failure; never reopen an unreaped child. */
    d->route_error=rc;
    int stopped=s?stop_locked(d,s):sony_audio_route_end(&d->route);
    if(stopped)return fail(d,s,stopped);
    return rc;
}
static ssize_t dispatch_locked(struct sony_device *,enum sony_job,struct sony_out *,const void *,unsigned int);
static int standby(struct audio_stream *stream) {
    struct sony_out *s=(struct sony_out *)stream;struct sony_device *d=s->device;
    pthread_mutex_lock(&d->lock);int result=(int)dispatch_locked(d,JOB_STANDBY,s,NULL,0);pthread_mutex_unlock(&d->lock);return result;
}
static int controls_effects(const struct sony_controls *c) {
    return !c->settings.direct&&(c->dsee.enabled||c->settings.dc_enabled||c->settings.vinyl_enabled||
        (c->tone.selector==2?c->settings.eq_enabled:c->tone.tone.enabled));
}
/* A saved profile is used when a new worker is opened. Existing workers keep
 * their current library until they are closed. */
static int start_context_locked(struct sony_device *d,int profile,int signature){
    int ship=sony_controls_feature_ship(&d->controls);if(ship<0)return -EINVAL;
    return sony_worker_process_start_profile_context(&d->process,SONY_PROCESS_EXPLICIT_START,d->root,d->log,1,profile,signature,ship,3000,&d->last_exit);
}
static int apply_profile_locked(struct sony_device *d) {
    if(d->job_profile==2) {
        /* GMD selects a pair of kernel tables for the next boot. Never report
         * it as applied now, or restart Android audio from a binder callback. */
        int rc=sony_cfw_gain_commit(SONY_AUDIO_CONFIG,d->controls.cfw_gain,d->job_controls.cfw_gain);
        if(rc==-117)return fail(d,NULL,rc);
        if(rc)return rc;
        acknowledge_controls_locked(d);return 0;
    }
    if(d->job_profile==3?d->job_controls.cfw_signature==d->controls.cfw_signature:d->job_controls.cfw_profile==d->controls.cfw_profile){++d->controls_revision;return 0;}
    int rc=d->job_profile==3?sony_cfw_signature_commit(SONY_AUDIO_CONFIG,d->controls.cfw_signature,d->job_controls.cfw_signature):sony_cfw_profile_commit(SONY_AUDIO_CONFIG,d->controls.cfw_profile,d->job_controls.cfw_profile);
    if(rc==-117)return fail(d,NULL,rc); /* rename succeeded, durability unknown */
    if(rc)return rc;
    acknowledge_controls_locked(d);return 0;
}
static int apply_controls_locked(struct sony_device *d) {
    if(d->failed)return -EIO;
    if(d->job_profile)return apply_profile_locked(d);
#ifdef SONY_SOUND_PRESETS
    if(d->adapter&&d->preset.operation&&!memcmp(&d->job_controls,&d->controls,sizeof(d->controls))){
        /* Save (and an identical recall) changes no DSP controls. Reissuing
         * output/volume notifications would add an original native fade even
         * though the user only asked to save. Query and commit the existing
         * settled state without muting, staging or touching the DAC route. */
        int rc=commit_controls_locked(d,sony_worker_process_client(d->process));
        if(!rc)acknowledge_controls_locked(d);
        else{route_result_locked(d,d->active,rc);if(d->active)stop_locked(d,d->active);}
        return rc;
    }
#endif
    if(d->adapter) {
        d->controls_pending=1;d->controls_error=0;
        clock_gettime(CLOCK_MONOTONIC,&d->controls_deadline);d->controls_deadline.tv_sec+=5;
        return 0; /* Accepted for next real PCM block, not yet applied/saved. */
    }
    int own_process=!d->process,own_session=!d->adapter,rc=0;
    if(d->route)rc=sony_audio_route_mute(d->route,1);
    if(rc)dprintf(d->log,"SONY_CONTROL_FAILURE route-mute rc=%d\n",rc);
    if(!rc&&own_process)rc=start_context_locked(d,d->controls.cfw_profile,d->controls.cfw_signature);
    struct wdc_client *client=d->process?sony_worker_process_client(d->process):NULL;
    if(!rc&&own_session)rc=wdc_client_open_output_rate(client,&d->controls.settings,48000);
    if(!rc){
#ifdef SONY_SOUND_PRESETS
        if(d->preset.operation){
            if(memcmp(&d->job_controls,&d->controls,sizeof(d->controls)))rc=sony_controls_stage(client,&d->job_controls);
            if(!rc)rc=commit_controls_locked(d,client);
        }
        else
#endif
        rc=sony_controls_apply(client,&d->job_controls);
    }
    if(!rc)acknowledge_controls_locked(d);
    if(own_session&&client&&!wdc_client_failed(client)) {
        int closed=wdc_client_close_stream(client);if(!rc)rc=closed;
    }
    if(own_process&&d->process) {
        if(!rc)rc=wdc_client_shutdown_output(client);
        int stopped=sony_worker_process_stop(&d->process,1000,&d->last_exit);
        if(!rc)rc=stopped;
        if(!rc&&(d->last_exit.kill_sent||d->last_exit.child_lost||d->last_exit.exit_code))rc=-EIO;
    }
    if(!rc&&d->route){rc=sony_audio_route_effects(d->route,controls_effects(&d->job_controls));if(rc)dprintf(d->log,"SONY_CONTROL_FAILURE route-effects rc=%d\n",rc);}
    if(!rc&&d->route)rc=sony_audio_route_apply(d->route,(unsigned int)d->job_controls.settings.volume,&d->job_controls.output);
    if(!rc&&d->route){rc=sony_audio_route_enable(d->route);if(rc)dprintf(d->log,"SONY_CONTROL_FAILURE route-enable rc=%d\n",rc);}
    if(rc) {
        int error=route_result_locked(d,d->active,rc);
        if(d->active)stop_locked(d,d->active);
        return error;
    }
    return 0;
}
static int owned_controls_locked(struct sony_device *d){
    if(d->failed)return -EIO;
    int fresh=!d->owner,rc=owner_begin_locked(d,1);if(rc)return rc;
    rc=apply_controls_locked(d);
    if(fresh&&!d->process&&!d->adapter&&!d->route&&!ai_pending(d)){
        int ended=owner_finish_locked(d,!d->failed);if(ended)rc=fail(d,NULL,ended);
    }
    return rc;
}
static int finish_live_controls_locked(struct sony_device *d) {
    int rc=commit_controls_locked(d,sony_worker_process_client(d->process));
    /* Save is already durable at this point, even if a plug is removed
     * before the subsequent hardware writes/unmute. Keep readback truthful. */
    if(!rc)acknowledge_controls_locked(d);
    if(!rc)rc=sony_audio_route_effects(d->route,controls_effects(&d->job_controls));
    if(!rc)rc=sony_audio_route_apply(d->route,(unsigned int)d->job_controls.settings.volume,&d->job_controls.output);
    if(!rc)rc=sony_audio_route_enable(d->route);
    d->controls_pending=0;d->controls_error=rc;
    if(rc){route_result_locked(d,d->active,rc);if(d->active)stop_locked(d,d->active);}
    return rc;
}
static int finish_idle_controls_locked(struct sony_device *d,int result) {
    if(!d->controls_pending)return result;
    d->controls_pending=0;
    if(!result)result=owned_controls_locked(d);
    d->controls_error=result;return result;
}
static int start_locked(struct sony_out *s) {
    struct sony_device *d=s->device;
    struct sony_controls planned=d->controls;int volume_event=0;
    if(d->failed||s->error)return s->error?s->error:-EIO;
    /* The attached BUS is an explicit no-physical-output default required by
     * Android policy. It must never start a worker/DAC, even with a plug in. */
    if(s->devices!=AUDIO_DEVICE_OUT_WIRED_HEADPHONE)return -ENXIO;
    if(d->active&&d->active!=s)return -EBUSY;
    if(d->route) {
        int rc=sony_audio_owner_check(d->owner);if(!rc)rc=sony_audio_route_check(d->route);
        if(rc)return route_result_locked(d,s,rc);
    }
    if(!d->active) {
        if(d->process||ai_pending(d))return fail(d,s,-EBUSY);
        struct sony_audio_jack_state jack;
        int rc=sony_audio_jack_read(&jack);if(rc)return route_result_locked(d,s,rc);
        if(!jack.se&&!jack.btl)return route_result_locked(d,s,-ENXIO);
        rc=owner_begin_locked(d,0);if(rc)return rc==-EBUSY?rc:fail(d,s,rc);
        rc=sony_controls_output_event(&d->controls,jack.se,jack.btl,&planned);
        if(rc){int ended=owner_finish_locked(d,1);return fail(d,s,ended?ended:rc);}
        volume_event=planned.output.device!=d->controls.output.device||planned.settings.volume!=d->controls.settings.volume||
            memcmp(&planned.volume_memory,&d->controls.volume_memory,sizeof(planned.volume_memory))||
            planned.avls_threshold!=d->controls.avls_threshold;
#ifdef SONY_HAL_AI
        if(ai_wanted(&planned,s->rate)){
            d->active=s;
            rc=sony_ai_hal_prepare(&d->ai,d->owner,&d->process,d->ai_root,d->ai_log);
            if(rc){int original=fail(d,s,rc);(void)stop_locked(d,s);return original;}
        }
#endif
        /* One private value drives native DSP context and hardware volume.
         * Android software volume remains active until policy integration. */
        int route_status=sony_audio_route_begin(&d->route,(unsigned int)planned.settings.volume,controls_effects(&planned),&planned.output);
        if(route_status){
            int error=route_result_locked(d,s,route_status),ended=owner_finish_locked(d,route_status==-ENXIO);
            return ended?fail(d,s,ended):error;
        }
        d->active=s;d->route_error=0;
#ifdef SONY_HAL_AI
        if(d->ai){
            uint64_t nonce=0;rc=RAND_bytes((unsigned char *)&nonce,sizeof(nonce))==1&&nonce?0:-EIO;
            if(!rc)rc=sony_ai_hal_launch_context(d->ai,d->root,d->log,d->controls.cfw_profile,d->controls.cfw_signature,sony_controls_feature_ship(&d->controls),nonce,5000,&d->last_exit);
        }else
#endif
        rc=start_context_locked(d,d->controls.cfw_profile,d->controls.cfw_signature);
        if(rc){int original=fail(d,s,rc);if(ai_pending(d))(void)stop_locked(d,s);return original;}
    }
    if(!d->adapter) {
        memset(&s->timing,0,sizeof(s->timing));
        unsigned int pcm_format=s->format==AUDIO_FORMAT_PCM_FLOAT?WDC_PCM_FLOAT_LE:s->bits;
        int rc=wdc_adapter_open(&d->adapter,sony_worker_process_client(d->process),&planned.settings,s->rate,2,pcm_format);
        if(rc)return fail(d,s,rc);
        if(!volume_event&&planned.output.device==SONY_OUTPUT_BTL) {
            /* Every fresh worker starts with snapshot SE context. Reopening
             * balanced output needs DSP selection even without a saved edit. */
            rc=wdc_client_update_output(sony_worker_process_client(d->process),&planned.output);
            if(rc)return fail(d,s,rc);
        }
        if(volume_event) {
            /* Still muted, before any submitted PCM. The same worker owns the
             * complete snapshot CAS; no external edit races its cached copy. */
            rc=sony_controls_apply(sony_worker_process_client(d->process),&planned);
            if(rc)return fail(d,s,rc);
            d->controls=planned;++d->controls_revision;
        }
        rc=sony_audio_route_enable(d->route);
        if(rc)return route_result_locked(d,s,rc);
    }
    return 0;
}
static ssize_t write_audio(struct audio_stream_out *stream,const void *buffer,size_t bytes) {
    struct sony_out *s=(struct sony_out *)stream;struct sony_device *d=s->device;
    if((!buffer&&bytes)||bytes>INT_MAX||bytes%(s->bits/4))return -EINVAL;
    if(!bytes)return 0;
    /* Bound absent-route retries without consuming unheard PCM or holding
     * the HAL lock. This is error backoff, not Android connection/noisy policy.
     * Account for teardown time already spent, and capture stream fields
     * before unlock so the delay never dereferences a released stream. */
    uint64_t delay=(uint64_t)(bytes/(s->bits/4))*1000000000ull/s->rate;
    if(delay<20000000ull)delay=20000000ull;
    if(delay>100000000ull)delay=100000000ull;
    struct timespec started;clock_gettime(CLOCK_MONOTONIC,&started);
    pthread_mutex_lock(&d->lock);ssize_t result=dispatch_locked(d,JOB_WRITE,s,buffer,(unsigned int)bytes);
    int discarded=s->write_discarded;
    if(discarded&&result>0)delay=(uint64_t)(result/(s->bits/4))*1000000000ull/s->rate;
    pthread_mutex_unlock(&d->lock);
    if(result==-ENXIO||result==-EBUSY||(discarded&&result>0)) {
        uint64_t deadline=(uint64_t)started.tv_sec*1000000000ull+started.tv_nsec+delay;
        for(;;) {
            struct timespec now;clock_gettime(CLOCK_MONOTONIC,&now);
            uint64_t at=(uint64_t)now.tv_sec*1000000000ull+now.tv_nsec;if(at>=deadline)break;
            uint64_t left=deadline-at;struct timespec rest={(time_t)(left/1000000000ull),(long)(left%1000000000ull)};
            if(!nanosleep(&rest,NULL)||errno!=EINTR)break;
        }
    }
    return result;
}
static int drain_locked(struct sony_device *d,struct sony_out *s) {
    int result=0;
    if(d->failed||s->error)result=s->error?s->error:-EIO;
    else if(d->active==s&&d->adapter) {
        int route=sony_audio_owner_check(d->owner);if(!route)route=sony_audio_route_check(d->route);
        if(route)return route_result_locked(d,s,route);
        int rc=wdc_adapter_finish(d->adapter,100);
        if(rc)result=fail(d,s,rc);
        else {rc=wdc_adapter_abort_close(&d->adapter);if(rc)result=fail(d,s,rc);}
    }
#ifdef SONY_HAL_AI
    /* The AI endpoint is one stream. Retire both children before the next
     * adapter OPEN; a successful drain is never followed by input replay. */
    if(ai_pending(d)&&d->active==s){int stopped=stop_locked(d,s);if(!result)result=stopped;}
#endif
    return result;
}
/* One persistent creator thread owns fork, client and adapter. Android/HIDL
 * callers may be transient threads; PR_SET_PDEATHSIG must not bind to them. */
struct controller_scheduler {int old_policy,saved,changed,target_policy;struct sched_param old_priority,target_priority;};
static int controller_scheduler_enter(struct controller_scheduler *s){
    memset(s,0,sizeof(*s));s->old_policy=sched_getscheduler(0);if(s->old_policy<0)return 0;
    if(sched_getparam(0,&s->old_priority))return 0;
    s->saved=1;s->target_policy=s->old_policy;s->target_priority=s->old_priority;
    if((s->old_policy!=SCHED_RR&&s->old_policy!=SCHED_FIFO)||s->old_priority.sched_priority<2){
        s->target_policy=SCHED_RR;s->target_priority=(struct sched_param){.sched_priority=2};
        if(!sched_setscheduler(0,s->target_policy,&s->target_priority))s->changed=1;
        else {s->target_policy=s->old_policy;s->target_priority=s->old_priority;}
    }
    return 0;
}
static int controller_scheduler_leave(struct controller_scheduler *s){
    if(!s->saved||!s->changed)return 0;
    if(sched_setscheduler(0,s->old_policy,&s->old_priority))return -errno;
    return 0;
}
static void *controller_main(void *opaque) {
    struct sony_device *d=opaque;struct controller_scheduler scheduler;
    /* Only this dedicated audio TID and its forked native output worker run
     * at RR/2. Binder/UI callers keep their scheduler. Blocking PCM/IPC and
     * condition waits yield normally; no polling thread or PCM retry is added. */
    int scheduled=controller_scheduler_enter(&scheduler);
    pthread_mutex_lock(&d->lock);d->controller_tid=(pid_t)syscall(SYS_gettid);
    d->controller_policy=scheduler.target_policy;d->controller_priority=scheduler.target_priority.sched_priority;
    d->controller_error=scheduled;d->controller_ready=1;
    dprintf(d->log,"SONY_AUDIO_CONTROLLER tid=%d policy=%d priority=%d result=%d\n",(int)d->controller_tid,d->controller_policy,d->controller_priority,scheduled);
    pthread_cond_broadcast(&d->changed);
    if(scheduled){pthread_mutex_unlock(&d->lock);(void)controller_scheduler_leave(&scheduler);return NULL;}
    for(;;) {
        while((!d->busy||d->done)&&!d->stopping) {
            int remote=sony_settings_relay_busy(d->settings_relay);
            if(!d->controls_pending&&!d->route&&!remote){pthread_cond_wait(&d->changed,&d->lock);continue;}
            struct timespec deadline=d->controls_deadline,now;
            if(d->route||remote) {
                clock_gettime(CLOCK_MONOTONIC,&deadline);deadline.tv_nsec+=remote?20000000:100000000;
                if(deadline.tv_nsec>=1000000000){++deadline.tv_sec;deadline.tv_nsec-=1000000000;}
                if(d->controls_pending&&(d->controls_deadline.tv_sec<deadline.tv_sec||
                    (d->controls_deadline.tv_sec==deadline.tv_sec&&d->controls_deadline.tv_nsec<deadline.tv_nsec)))deadline=d->controls_deadline;
            }
            pthread_cond_timedwait(&d->changed,&d->lock,&deadline);
            if(sony_settings_relay_busy(d->settings_relay))sony_settings_relay_poll(d->settings_relay);
            if(d->route) {
                int rc=sony_audio_owner_check(d->owner);if(!rc)rc=sony_audio_route_check(d->route);
                if(rc){route_result_locked(d,d->active,rc);if(d->active)stop_locked(d,d->active);}
            }
            clock_gettime(CLOCK_MONOTONIC,&now);
            if(d->controls_pending&&(now.tv_sec>d->controls_deadline.tv_sec||
                (now.tv_sec==d->controls_deadline.tv_sec&&now.tv_nsec>=d->controls_deadline.tv_nsec))) {
                int staged=d->controls_pending==2;d->controls_pending=0;d->controls_error=-ETIMEDOUT;
                if(staged){fail(d,d->active,-ETIMEDOUT);if(d->active)stop_locked(d,d->active);}
                pthread_cond_broadcast(&d->changed);
            }
        }
        if(d->stopping)break;
        struct sony_out *s=d->job_stream;ssize_t result;
        if(d->job==JOB_WRITE) {
            s->write_discarded=0;
            if(s->devices==AUDIO_DEVICE_OUT_BUS&&!d->failed&&!s->error) {
                /* Android needs a clocked default sink to retire paused and
                 * terminated tracks. Dispose only on the explicit BUS port;
                 * physical WIRED/NONE failures still return their real errno.
                 * Nothing is retained or sent to the Sony worker. Returning a
                 * positive count acknowledges disposal, not hardware playback.
                 * Bound each acknowledgement to one source quantum, allowing
                 * partial writes without unbounded sleeps. Pacing is lock-free. */
                unsigned int count=d->job_bytes/(s->bits/4);
                if(count>SONY_SOURCE_QUANTUM)count=SONY_SOURCE_QUANTUM;
                s->discarded+=count;s->write_discarded=1;
                result=(ssize_t)(count*(s->bits/4));
                goto job_complete;
            }
            int rc=start_locked(s);unsigned int copied=0;
#ifdef SONY_HAL_AI
            if(!rc&&d->controls_pending==1&&ai_wanted(&d->controls,s->rate)!=ai_wanted(&d->job_controls,s->rate)){
                /* No current input consumed yet. Finish the old stream once,
                 * retire it, durably apply through a non-playing native
                 * session, then open the newly selected backend. */
                rc=drain_locked(d,s);
                if(!rc&&d->active==s)rc=stop_locked(d,s);
                d->controls_pending=0;
                if(!rc)rc=owned_controls_locked(d);
                d->controls_error=rc;
                if(!rc)rc=start_locked(s);
            }
#endif
            struct wdc_adapter_state before={0};
            if(!rc&&d->controls_pending) {
                rc=wdc_adapter_get_state(d->adapter,&before);
                if(!rc&&d->controls_pending==1) {
                    rc=sony_audio_route_mute(d->route,1);
                    if(!rc)rc=sony_controls_stage(sony_worker_process_client(d->process),&d->job_controls);
                    if(!rc){d->controls_pending=2;d->controls_start_frames=before.native_frames_accepted;}
                }
                if(rc)fail(d,s,rc);
            }
            if(!rc) {
                int native=wdc_adapter_write(d->adapter,d->job_buffer,d->job_bytes,&copied);
                s->copied+=copied;if(native)rc=fail(d,s,native);
                if(!rc&&(!s->timing.valid||d->controls_pending==2)) {
                    struct wdc_adapter_state a;
                    native=wdc_adapter_get_state(d->adapter,&a);
                    if(!native&&a.native_frames_accepted)native=wdc_client_get_output_timing(sony_worker_process_client(d->process),&s->timing);
                    if(native)rc=fail(d,s,native);
                }
                if(!rc&&d->controls_pending==2) {
                    struct wdc_adapter_state after;
                    rc=wdc_adapter_get_state(d->adapter,&after);
                    if(!rc&&after.native_frames_accepted-d->controls_start_frames>=1024
#ifdef SONY_HAL_AI
                       &&(!ai_pending(d)||s->timing.valid)
#endif
                    ) {
                        rc=finish_live_controls_locked(d);
                    }
                    if(rc&&rc!=-ENXIO)fail(d,s,rc);
                }
            }
            if(rc&&d->controls_pending){d->controls_pending=0;d->controls_error=rc;stop_locked(d,s);}
            /* Copied input ownership is consumed even if native output failed.
             * Return the partial count once, latch the error, never resubmit. */
            result=copied?(ssize_t)(copied*(s->bits/4)):rc;
        }else if(d->job==JOB_ROUTING) {
            result=0;
            if(s->devices!=d->job_bytes) {
                /* Only the active stream can tear down its owned route. An
                 * idle fallback must not interrupt another direct stream. */
                if(d->active==s) {
                    if(d->controls_pending){d->controls_pending=0;d->controls_error=-ENXIO;}
                    result=stop_locked(d,s);
                }
                if(!result)s->devices=d->job_bytes;
            }
        }else if(d->job==JOB_EXTERNAL_CONTROLS){
            struct sony_settings_relay *next=NULL;
            struct sony_settings_patch patch;char published[SONY_OWNER_STATE_TEXT_BYTES];struct sony_audio_owner_observation observed;
            result=sony_settings_patch_parse(d->job_buffer,&patch);
            if(!result)result=sony_audio_owner_observe(published,sizeof(published),&observed);
            if(!result)result=sony_settings_relay_start_kind(&next,(enum sony_audio_owner_kind)observed.kind,d->job_buffer,8000);
            if(!result){sony_settings_relay_close(&d->settings_relay);d->settings_relay=next;}
        }else if(d->job==JOB_CONTROLS)result=owned_controls_locked(d);
        else if(d->job==JOB_DRAIN)result=finish_idle_controls_locked(d,drain_locked(d,s));
        else if(d->job==JOB_STANDBY)result=finish_idle_controls_locked(d,stop_locked(d,s));
        else {
            int rc=d->process?sony_worker_process_stop(&d->process,1000,&d->last_exit):0;
#ifdef SONY_HAL_AI
            if(!d->process&&d->ai){int ended=sony_ai_hal_end(&d->ai,1000);if(!rc)rc=ended;}
            result=d->process||ai_pending(d)?translated(rc?rc:-EBUSY):rc?translated(rc):0;
#else
            result=d->process?translated(rc):0;
#endif
        }
job_complete:
        d->job_result=result;d->done=1;pthread_cond_broadcast(&d->changed);
    }
    pthread_mutex_unlock(&d->lock);
    int restored=controller_scheduler_leave(&scheduler);
    if(restored)dprintf(d->log,"SONY_AUDIO_CONTROLLER_EXIT tid=%d restore=%d; thread exiting\n",(int)d->controller_tid,restored);
    return NULL;
}
static ssize_t dispatch_locked(struct sony_device *d,enum sony_job job,struct sony_out *s,const void *data,unsigned int bytes) {
    while(d->busy)pthread_cond_wait(&d->changed,&d->lock);
    d->job=job;d->job_stream=s;d->job_buffer=data;d->job_bytes=bytes;d->busy=1;d->done=0;
    pthread_cond_broadcast(&d->changed);
    while(!d->done)pthread_cond_wait(&d->changed,&d->lock);
    ssize_t result=d->job_result;d->busy=0;d->done=0;d->job_buffer=NULL;d->job_stream=NULL;
    pthread_cond_broadcast(&d->changed);return result;
}
static int drain_audio(struct audio_stream_out *stream,audio_drain_type_t type) {
    if(type!=AUDIO_DRAIN_ALL)return -ENOSYS;
    struct sony_out *s=(struct sony_out *)stream;struct sony_device *d=s->device;
    pthread_mutex_lock(&d->lock);int result=(int)dispatch_locked(d,JOB_DRAIN,s,NULL,0);pthread_mutex_unlock(&d->lock);return result;
}
static int dump_stream(const struct audio_stream *stream,int fd) {
    struct sony_out *s=(struct sony_out *)stream;struct sony_device *d=s->device;
    pthread_mutex_lock(&d->lock);
    dprintf(fd,"WalkDroid Sony PCM: rate=%u bits=%u copied=%llu error=%d active=%d native_error=%d device=%u\n",
        s->rate,s->bits,(unsigned long long)s->copied,s->error,d->active==s,d->native_error,s->devices);
    dprintf(fd,"Sony output geometry: negotiated=%u rate=%u period=%u buffer=%u source_quantum=%u presentation=unavailable\n",
        s->timing.valid,s->timing.sample_rate,s->timing.period_frames,s->timing.buffer_frames,SONY_SOURCE_QUANTUM);
    dprintf(fd,"Disconnected software sink: discarded=%llu hardware_frames=0 queued_frames=0\n",(unsigned long long)s->discarded);
    pthread_mutex_unlock(&d->lock);return 0;
}
static int open_output(struct audio_hw_device *device,audio_io_handle_t handle,audio_devices_t devices,
    audio_output_flags_t flags,struct audio_config *config,struct audio_stream_out **out,const char *address) {
    (void)handle;(void)address;if(!config||!out)return -EINVAL;*out=NULL;
    if(!devices||!supported_route(devices)||(flags&~(AUDIO_OUTPUT_FLAG_PRIMARY|AUDIO_OUTPUT_FLAG_DIRECT)))return -EINVAL;
    if(devices==AUDIO_DEVICE_OUT_BUS&&(flags&AUDIO_OUTPUT_FLAG_DIRECT))return -EINVAL;
    unsigned int rate=config->sample_rate?config->sample_rate:48000;
    audio_format_t f=config->format==AUDIO_FORMAT_DEFAULT?AUDIO_FORMAT_PCM_16_BIT:config->format;
    if((f!=AUDIO_FORMAT_PCM_16_BIT&&f!=AUDIO_FORMAT_PCM_32_BIT&&f!=AUDIO_FORMAT_PCM_FLOAT)||
       (config->channel_mask!=AUDIO_CHANNEL_NONE&&config->channel_mask!=AUDIO_CHANNEL_OUT_STEREO))return -EINVAL;
    struct sony_device *d=(struct sony_device *)device;struct sony_out *s=calloc(1,sizeof(*s));if(!s)return -ENOMEM;
    s->device=d;s->rate=rate;s->bits=f==AUDIO_FORMAT_PCM_16_BIT?16:32;s->format=f;s->devices=devices;s->flags=flags;
    pthread_mutex_lock(&d->lock);if(d->failed){pthread_mutex_unlock(&d->lock);free(s);return -EIO;}++d->streams;pthread_mutex_unlock(&d->lock);
    struct audio_stream *c=&s->stream.common;
    c->get_sample_rate=sample_rate;c->set_sample_rate=set_rate;c->get_buffer_size=buffer_size;c->get_channels=channels;
    c->get_format=format;c->set_format=set_format;c->standby=standby;c->dump=dump_stream;
    c->set_parameters=parameters;c->get_parameters=get_parameters;c->add_audio_effect=effect;c->remove_audio_effect=effect;
    s->stream.get_latency=latency;s->stream.set_volume=volume;s->stream.write=write_audio;
    s->stream.get_render_position=render_position;s->stream.get_presentation_position=presentation;
    s->stream.get_next_write_timestamp=write_timestamp;s->stream.drain=drain_audio;
    config->sample_rate=rate;config->format=f;config->channel_mask=AUDIO_CHANNEL_OUT_STEREO;*out=&s->stream;return 0;
}
static void close_output(struct audio_hw_device *device,struct audio_stream_out *stream) {
    if(!stream)return;
    struct sony_device *d=(struct sony_device *)device;struct sony_out *s=(struct sony_out *)stream;
    pthread_mutex_lock(&d->lock);dispatch_locked(d,JOB_STANDBY,s,NULL,0);
    if(d->active==s)d->active=NULL; /* Pending process is retained by failed d. */
    --d->streams;pthread_mutex_unlock(&d->lock);free(s);
}
static int init_check(const struct audio_hw_device *device) {
    struct sony_device *d=(struct sony_device *)device;pthread_mutex_lock(&d->lock);int rc=d->failed?-EIO:0;pthread_mutex_unlock(&d->lock);return rc;
}
static int voice_volume(struct audio_hw_device *d,float v) {(void)d;(void)v;return -ENOSYS;}
static int master_get(struct audio_hw_device *d,float *v) {(void)d;(void)v;return -ENOSYS;}
static int mute_set(struct audio_hw_device *d,bool v) {(void)d;(void)v;return -ENOSYS;}
static int mute_get(const struct audio_hw_device *d,bool *v) {(void)d;(void)v;return -ENOSYS;}
static int master_mute_get(struct audio_hw_device *d,bool *v) {(void)d;(void)v;return -ENOSYS;}
static int mode(struct audio_hw_device *d,audio_mode_t m) {(void)d;return m==AUDIO_MODE_NORMAL?0:-ENOSYS;}
static int external_control_request(const char *p) {
    /* Header order is not preserved by AudioParameter/HIDL. A request carrying
     * either ownership header must pass the strict external parser; it cannot
     * fall through to local controls when another mandatory header is absent. */
    for(const char *key=p;key;key=strchr(key,';'),key=key?key+1:NULL)
        if(!strncmp(key,"sony_owner_generation=",22)||!strncmp(key,"sony_request_id=",16))return 1;
    return 0;
}
static int device_parameters(struct audio_hw_device *device,const char *p) {
    if(!p||strncmp(p,"sony_",5))return -ENOSYS;
    struct sony_device *d=(struct sony_device *)device;pthread_mutex_lock(&d->lock);
    while(d->busy)pthread_cond_wait(&d->changed,&d->lock);
    if(external_control_request(p)){
        struct sony_settings_patch patch;
        int rc=sony_settings_patch_parse(p,&patch);
        if(!rc)rc=d->failed?-EIO:(d->owner||d->adapter||d->controls_pending||sony_settings_relay_busy(d->settings_relay))?-EBUSY:0;
        if(!rc)rc=(int)dispatch_locked(d,JOB_EXTERNAL_CONTROLS,NULL,p,0);
        pthread_mutex_unlock(&d->lock);return rc;
    }
    int rc=d->failed?-EIO:(d->controls_pending||sony_settings_relay_busy(d->settings_relay))?-EBUSY:0;
    if(!rc){
#ifdef SONY_SOUND_PRESETS
        if(strstr(p,"sony_preset_"))rc=sony_controls_prepare_preset(p,d->controls_revision,&d->controls,&d->job_controls,&d->preset);
        else{
            rc=sony_controls_parse(p,d->controls_revision,&d->controls,&d->job_controls);
            if(!rc)d->preset.operation=0;
        }
#else
        rc=sony_controls_parse(p,d->controls_revision,&d->controls,&d->job_controls);
#endif
    }
    if(!rc){d->job_profile=strstr(p,"sony_cfw_signature=")?3:strstr(p,"sony_cfw_gain=")?2:strstr(p,"sony_cfw_profile=")?1:0;
        d->controls_error=0;rc=(int)dispatch_locked(d,JOB_CONTROLS,NULL,NULL,0);if(rc)d->controls_error=rc;}
    pthread_mutex_unlock(&d->lock);return rc;
}
static char *device_get_parameters(const struct audio_hw_device *device,const char *p) {
    /* HIDL getParameters carries KEYS only; AudioParameter::keysToString drops
     * the value of key=id. Encode the exact nonce in the key itself. Retain the
     * old direct-HAL spelling for compatibility, never infer a different id. */
    if(p&&(!strncmp(p,"sony_control_result_",20)||!strncmp(p,"sony_control_result=",20))){
        struct sony_device *d=(struct sony_device *)device;pthread_mutex_lock(&d->lock);
        char *text=sony_settings_relay_result(d->settings_relay,p+20);
        pthread_mutex_unlock(&d->lock);return text?text:strdup("");
    }
    if(p&&!strcmp(p,"sony_jack")) {
        struct sony_device *d=(struct sony_device *)device;struct sony_audio_jack_state state={-1,-1};
        pthread_mutex_lock(&d->lock);int rc=d->failed?-EIO:0;pthread_mutex_unlock(&d->lock);
        if(!rc)rc=sony_audio_jack_read(&state);
        char *text=NULL;if(asprintf(&text,"sony_jack_se=%d;sony_jack_btl=%d;sony_jack_error=%d",state.se,state.btl,rc)<0)return NULL;
        return text;
    }
    if(!p||strcmp(p,"sony_state"))return strdup("");
    struct sony_device *d=(struct sony_device *)device;pthread_mutex_lock(&d->lock);
    int state_error=d->failed?-EIO:0,external=0;
    char observed[SONY_OWNER_STATE_TEXT_BYTES];struct sony_audio_owner_observation observation={0};
    if(!state_error&&d->owner)state_error=sony_audio_owner_check(d->owner);
    else if(!state_error){
        struct sony_audio_owner_reader *reader=NULL;struct sony_controls current;
        state_error=sony_audio_owner_read_begin(&reader);
        if(state_error==-EBUSY){
            int observed_error=sony_audio_owner_observe(observed,sizeof(observed),&observation);
            if(!observed_error&&(observation.kind==SONY_OWNER_DSF||observation.kind==SONY_OWNER_USB_DAC||observation.kind==SONY_OWNER_BT_RX)){state_error=0;external=1;}
            else if(observed_error&&observed_error!=-ENOENT)state_error=observed_error;
        }
        if(!state_error&&!external){
            int loaded=sony_controls_load(&current),ended=sony_audio_owner_read_end(&reader);
            state_error=ended?ended:loaded;
            if(!state_error)state_error=accept_saved_controls_locked(d,&current,0);
        }
    }
    char *base=NULL,*text=NULL;
    if(external)base=strdup(observed);
    else if(!state_error)base=sony_controls_text(&d->controls,d->controls_revision,d->failed,d->adapter!=NULL);
    else if(asprintf(&base,"sony_revision=%u;sony_failed=%d",d->controls_revision,d->failed)<0)base=NULL;
#ifdef SONY_HAL_AI
    if(base&&!state_error&&!external){
        /* Report the live controller-owned backend separately from the saved
         * DSEE switch. A saved request alone is never proof of processing. */
        char *with_ai=NULL;
        int processing=d->ai&&d->adapter&&d->active&&!d->failed;
        if(asprintf(&with_ai,"%s;sony_ai_available=1;sony_ai_processing=%d;sony_pcm_input_rate=%u;sony_pcm_output_rate=%u",base,
            processing,d->active?d->active->rate:0,d->active&&d->active->timing.valid?d->active->timing.sample_rate:0)<0)with_ai=NULL;
        free(base);base=with_ai;
    }
#endif
#ifdef SONY_SOUND_PRESETS
    if(base&&!state_error&&!external){
        char *with_preset=NULL;
        if(asprintf(&with_preset,"%s;sony_preset_revision=%u;sony_preset_operation=%u;sony_preset_slot=%u",base,
            d->preset_receipt_revision,d->preset_receipt_operation,d->preset_receipt_slot)<0)with_preset=NULL;
        free(base);base=with_preset;
    }
#endif
    if(base){
        if(asprintf(&text,"%s;sony_pending=%d;sony_control_error=%d;sony_route_error=%d;sony_state_fresh=%d;sony_state_error=%d;sony_owner_self=%d;sony_readonly=%d;sony_owner_kind=%u;sony_owner_generation=%llu",
            base,external?0:d->controls_pending!=0,external?0:d->controls_error,external?0:d->route_error,!state_error,state_error,d->owner!=NULL,
            external,external?observation.kind:0,(unsigned long long)(external?observation.generation:0))<0)text=NULL;
        free(base);
    }
    pthread_mutex_unlock(&d->lock);return text;
}
static size_t input_size(const struct audio_hw_device *d,const struct audio_config *c) {(void)d;(void)c;return 0;}
static int open_input(struct audio_hw_device *d,audio_io_handle_t h,audio_devices_t v,struct audio_config *c,
    struct audio_stream_in **s,audio_input_flags_t f,const char *a,audio_source_t o) {
    (void)d;(void)h;(void)v;(void)c;(void)f;(void)a;(void)o;if(s)*s=NULL;return -ENOSYS;
}
static void close_input(struct audio_hw_device *d,struct audio_stream_in *s) {(void)d;(void)s;}
static int dump_device(const struct audio_hw_device *device,int fd) {
    struct sony_device *d=(struct sony_device *)device;pthread_mutex_lock(&d->lock);
    dprintf(fd,"WalkDroid Sony candidate: streams=%u failed=%d native_error=%d child_pending=%d\n",d->streams,d->failed,d->native_error,d->process!=NULL);
    dprintf(fd,"Sony audio controller: tid=%d policy=%d priority=%d init_error=%d\n",(int)d->controller_tid,d->controller_policy,d->controller_priority,d->controller_error);
    dprintf(fd,"Sony output ownership: held=%d generation=%llu\n",d->owner!=NULL,(unsigned long long)sony_audio_owner_generation(d->owner));
#ifdef SONY_HAL_AI
    dprintf(fd,"Sony AI: available=1 processing=%d input_rate=%u output_rate=%u\n",
        d->ai&&d->adapter&&d->active&&!d->failed,d->active?d->active->rate:0,
        d->active&&d->active->timing.valid?d->active->timing.sample_rate:0);
#endif
    char *controls=sony_controls_text(&d->controls,d->controls_revision,d->failed,d->adapter!=NULL);
    if(controls){dprintf(fd,"Sony controls: %s\n",controls);free(controls);}
    pthread_mutex_unlock(&d->lock);return 0;
}
static int close_device(struct hw_device_t *device) {
    struct sony_device *d=(struct sony_device *)device;pthread_mutex_lock(&d->lock);
    if(d->streams){pthread_mutex_unlock(&d->lock);return -EBUSY;}
    if(d->process||ai_pending(d)) {
        int rc=(int)dispatch_locked(d,JOB_REAP,NULL,NULL,0);
        if(d->process||ai_pending(d)){pthread_mutex_unlock(&d->lock);return rc;}
    }
    if(d->adapter){pthread_mutex_unlock(&d->lock);return -EBUSY;}
    d->stopping=1;pthread_cond_broadcast(&d->changed);pthread_mutex_unlock(&d->lock);
    sony_audio_route_end(&d->route);
    pthread_join(d->controller,NULL);
    sony_settings_relay_close(&d->settings_relay);
    if(d->owner)owner_finish_locked(d,!d->failed&&!d->adapter&&!d->process&&!d->route);
    close(d->root);close(d->log);
#ifdef SONY_HAL_AI
    close(d->ai_root);close(d->ai_log);
#endif
    pthread_cond_destroy(&d->changed);pthread_mutex_destroy(&d->lock);free(d);
    pthread_mutex_lock(&module_lock);device_open=0;pthread_mutex_unlock(&module_lock);return 0;
}
static int open_device(const hw_module_t *module,const char *name,hw_device_t **out) {
    if(!name||!out)return -EINVAL;
    *out=NULL;
    if(strcmp(name,AUDIO_HARDWARE_INTERFACE))return -EINVAL;
    int rc=0;
    pthread_mutex_lock(&module_lock);if(device_open){pthread_mutex_unlock(&module_lock);return -EBUSY;}
    struct sony_device *d=calloc(1,sizeof(*d));if(!d){pthread_mutex_unlock(&module_lock);return -ENOMEM;}
    d->root=sony_audio_root_open();d->log=-1;
#ifdef SONY_HAL_AI
    d->ai_root=d->ai_log=-1;
#endif
    if(d->root<0)rc=d->root;
#ifdef SONY_HAL_AI
    if(!rc){d->ai_root=sony_ai_hal_root_open();if(d->ai_root<0)rc=d->ai_root;}
    if(!rc){d->ai_log=sony_ai_hal_log_open();if(d->ai_log<0)rc=d->ai_log;}
#endif
    if(!rc){rc=sony_controls_load(&d->controls);d->controls_revision=1;}
    if(!rc){d->log=dup(STDERR_FILENO);if(d->log<0)rc=-errno;}
    int mutex_ready=0,cond_ready=0;
    if(!rc){int error=pthread_mutex_init(&d->lock,NULL);if(error)rc=-error;else mutex_ready=1;}
    if(!rc){pthread_condattr_t attr;int error=pthread_condattr_init(&attr);
        if(!error){error=pthread_condattr_setclock(&attr,CLOCK_MONOTONIC);if(!error)error=pthread_cond_init(&d->changed,&attr);pthread_condattr_destroy(&attr);}
        if(error)rc=-error;else cond_ready=1;}
    if(!rc){int error=pthread_create(&d->controller,NULL,controller_main,d);if(error)rc=-error;
        else{pthread_mutex_lock(&d->lock);while(!d->controller_ready)pthread_cond_wait(&d->changed,&d->lock);rc=d->controller_error;pthread_mutex_unlock(&d->lock);if(rc)pthread_join(d->controller,NULL);}}
    if(rc) {
        if(cond_ready)pthread_cond_destroy(&d->changed);
        if(mutex_ready)pthread_mutex_destroy(&d->lock);
        if(d->root>=0)close(d->root);
        if(d->log>=0)close(d->log);
#ifdef SONY_HAL_AI
        if(d->ai_root>=0)close(d->ai_root);if(d->ai_log>=0)close(d->ai_log);
#endif
        free(d);pthread_mutex_unlock(&module_lock);return rc;
    }
    d->device.common.tag=HARDWARE_DEVICE_TAG;d->device.common.version=AUDIO_DEVICE_API_VERSION_2_0;
    d->device.common.module=(hw_module_t *)module;d->device.common.close=close_device;
    d->device.init_check=init_check;d->device.set_voice_volume=voice_volume;d->device.set_master_volume=voice_volume;
    d->device.get_master_volume=master_get;d->device.set_master_mute=mute_set;d->device.get_master_mute=master_mute_get;
    d->device.set_mode=mode;d->device.set_mic_mute=mute_set;d->device.get_mic_mute=mute_get;
    d->device.set_parameters=device_parameters;d->device.get_parameters=device_get_parameters;
    d->device.get_input_buffer_size=input_size;d->device.open_input_stream=open_input;d->device.close_input_stream=close_input;
    d->device.open_output_stream=open_output;d->device.close_output_stream=close_output;d->device.dump=dump_device;
    device_open=1;*out=&d->device.common;pthread_mutex_unlock(&module_lock);return 0;
}
static struct hw_module_methods_t methods={.open=open_device};
struct audio_module HAL_MODULE_INFO_SYM={.common={.tag=HARDWARE_MODULE_TAG,.module_api_version=AUDIO_MODULE_API_VERSION_0_1,
    .hal_api_version=HARDWARE_HAL_API_VERSION,.id=AUDIO_HARDWARE_MODULE_ID,.name="Sony Walkman ZX300A original PCM candidate",
    .author="WalkDroid",.methods=&methods}};
