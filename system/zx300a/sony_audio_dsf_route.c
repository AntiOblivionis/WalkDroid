#define _GNU_SOURCE
#include "sony_audio_dsf_route.h"
#include "sony_audio_owner.h"
#include "sony_audio_route.h"
#include "sony_audio_jack.h"
#include "sony_audio_snapshot_store.h"
#include "sony_audio_snapshot_eq.h"
#include "sony_audio_snapshot_tone.h"
#include "sony_audio_snapshot_dsd.h"
#include "sony_audio_volume_policy.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
struct sony_dsf_route {
    struct sony_audio_owner *owner;struct sony_audio_route *route;
    struct sony_dsf_route_binding binding;
    unsigned char snapshot[SONY_SNAPSHOT_BYTES];
    unsigned char live_snapshot[SONY_SNAPSHOT_BYTES];
    struct wdf_controls live_base,live_staged;
    struct sony_dsd_live_controls live_target;
    unsigned revision,rate;
    int live_bound,live_pending,live_output_closed,quiet_finished,live_volume;
    char *config_path,*state_text;
    int armed,started,quiesced,error,uncertain,commands_enabled;
    int native_mode,native_pid,native_pending,native_eof;
    struct wdf_controls native_ack,native_staged;
    struct sony_controls native_target;
};
static int remember(struct sony_dsf_route *r,int rc){
    if(rc&&!r->error)r->error=rc;
    if(rc&&rc!=-ENXIO&&rc!=-ECANCELED&&rc!=-ENOTSUP)r->uncertain=1;
    return rc;
}
static int route_check(void *v){
    struct sony_dsf_route *r=v;if(!r||r->quiesced)return -EPIPE;
    int rc=sony_audio_owner_check(r->owner);
    if(!rc)rc=r->quiet_finished?sony_audio_route_idle():sony_audio_route_check(r->route);
    if(!rc&&r->state_text&&!r->native_pending&&!r->native_eof)rc=sony_audio_owner_publish(r->owner,r->state_text);
    return remember(r,rc);
}
static int quiesce(void *v){
    struct sony_dsf_route *r=v;if(!r)return -EINVAL;
    if(r->quiesced)return 0;
    r->quiesced=1;
    int retired=sony_audio_owner_retire(r->owner);
    int ended=sony_audio_route_end(&r->route); /* Always mute/off, even if retirement fails. */
    return remember(r,retired?retired:ended);
}
static int pcm_format(unsigned pcm,unsigned rate){
    (void)pcm;(void)rate;return 0;
}
static int pcm_closed(void){return 0;}
static int state_text(struct sony_dsf_route *r){
    char *controls=sony_controls_text(&r->binding.controls,r->revision,0,0),*text=NULL;
    if(!controls)return -ENOMEM;
    int n=asprintf(&text,"%s;sony_output_ready=1;sony_output_rate=%u;sony_output_format=%s;sony_output_channels=2;sony_source=dsf;sony_dsd_mode=%d;sony_dsd_filter=%d;sony_dsd_gain=%d;sony_live_controls=%d;sony_dsd_native=%d;sony_live_volume_only=%d",
        controls,r->rate,r->native_mode?"dsd_u8":"s32le",r->binding.dsd.mode,r->binding.dsd.filter,r->binding.dsd.gain,r->commands_enabled,r->native_mode,r->native_mode);
    free(controls);if(n<0)return -ENOMEM;
    free(r->state_text);r->state_text=text;return 0;
}
static int started(void *v,unsigned rate){
    struct sony_dsf_route *r=v;if(!r||!r->armed||r->started)return -EINVAL;
    int rc=route_check(r);
    if(!rc)rc=pcm_format(r->binding.controls.cfw_profile==1?0:4,rate);
    if(!rc)rc=sony_audio_route_enable(r->route);
    if(!rc){
        /* The first original-DSP packet is ready; no PCM write has happened yet. */
        r->rate=rate;rc=state_text(r);
        if(!rc)rc=sony_audio_owner_publish(r->owner,r->state_text);
    }
    if(!rc)r->started=1;
    return remember(r,rc);
}
static int effects(const struct sony_controls *c){
    return !c->settings.direct&&(c->dsee.enabled||c->settings.dc_enabled||c->settings.vinyl_enabled||
        (c->tone.selector==2?c->settings.eq_enabled:c->tone.tone.enabled));
}
int sony_dsf_route_live_fail(struct sony_dsf_route *r,int error){
    if(!r||error>=0)return -EINVAL;
    remember(r,error);quiesce(r);return r->error;
}
int sony_dsf_route_live_check(struct sony_dsf_route *r,const struct wdf_controls *b){
    if(r&&r->native_mode)return -ENOTSUP;
    if(!r||!b||!r->started)return -EINVAL;
    int rc=route_check(r);if(rc)return sony_dsf_route_live_fail(r,rc);
    if(!r->live_pending){r->live_base=*b;r->live_bound=1;}
    return 0;
}
int sony_dsf_route_live_begin(struct sony_dsf_route *r,uint64_t generation,unsigned revision,
    const struct wdf_controls *base,const struct sony_dsd_live_controls *target,struct sony_dsd_live_controls *normalized){
    if(r&&r->native_mode)return -ENOTSUP;
    if(!r||!base||!target||!normalized||!r->started)return -EINVAL;
    if(r->quiet_finished)return -EPIPE;
    (void)generation;(void)revision;
    const struct sony_controls *current=&r->binding.controls;
    struct sony_volume_state vs={current->ship_selector,current->settings.volume,current->output.device,current->volume_memory.last_headphone,
        0,0,current->output.se_gain,current->output.btl_gain,current->volume_memory.avls,current->avls_threshold};
    struct sony_volume_plan plan;int volume_rc=sony_volume_plan_set(&vs,target->settings.volume,&plan);if(volume_rc)return volume_rc;
    struct sony_dsd_live_controls canonical=*target;canonical.settings.volume=plan.next.volume;
    unsigned char next[SONY_SNAPSHOT_BYTES];
    int rc=sony_snapshot_encode_supported_effects(r->snapshot,sizeof(r->snapshot),1,&canonical.settings,next);
    if(!rc)rc=sony_snapshot_encode_volume(next,sizeof(next),canonical.settings.volume,next);
    if(!rc&&canonical.selector==3)rc=sony_snapshot_encode_tone(next,sizeof(next),&canonical.tone,next);
    if(!rc)rc=sony_snapshot_encode_dsee(next,sizeof(next),&canonical.dsee,next);
    struct sony_dsd_controls stored;
    if(!rc)rc=sony_snapshot_apply_dsd(r->snapshot,sizeof(r->snapshot),&stored);
    if(!rc){
        if(canonical.dsd.filter!=r->binding.dsd.filter)stored.filter=canonical.dsd.filter;
        if(canonical.dsd.gain!=r->binding.dsd.gain)stored.gain=canonical.dsd.gain;
        rc=sony_snapshot_encode_dsd(next,sizeof(next),&stored,next);
    }
    if(rc)return rc;
    rc=sony_dsf_route_live_check(r,base);if(rc)return rc;
    if(canonical.selector==2)canonical.tone=base->controls.tone;
    if(!memcmp(&canonical,&base->controls,sizeof(canonical))){*normalized=canonical;return 1;}
    struct sony_dsd_live_controls volume_only=base->controls;volume_only.settings.volume=canonical.settings.volume;
    r->live_volume=!memcmp(&volume_only,&canonical,sizeof(canonical));
    r->live_target=canonical;memcpy(r->live_snapshot,next,sizeof(next));r->live_pending=1;r->live_output_closed=0;
    rc=sony_audio_owner_retire(r->owner);
    free(r->state_text);r->state_text=NULL; /* No old state can be republished. */
    if(!rc&&r->live_volume){
        /* Follow the original SetVolume's successful main-path ordering:
         * actual DAC level -> saved volume -> processing notification. Native
         * STAGE follows this function. Do not mute/drop/reopen for a pure level
         * change; the original driver's live volume/fader does that operation.
         * Durable/hardware changes may precede a failed notification, so every
         * admitted failure quiesces and never claims rollback. */
        rc=pcm_format(current->cfw_profile==1?0:4,r->rate);
        if(!rc)rc=sony_audio_route_live_volume(r->route,(unsigned)current->settings.volume,(unsigned)canonical.settings.volume,&current->output);
        if(!rc)rc=sony_snapshot_commit(SONY_AUDIO_CONFIG,r->snapshot,r->live_snapshot);
        if(!rc)memcpy(r->snapshot,r->live_snapshot,sizeof(r->snapshot));
    }else if(!rc)rc=sony_audio_route_mute(r->route,1);
    if(rc)return sony_dsf_route_live_fail(r,rc);
    *normalized=canonical;return 0;
}
int sony_dsf_route_live_staged(struct sony_dsf_route *r,const struct wdf_controls *reply){
    if(r&&r->native_mode)return -ENOTSUP;
    if(!r||!reply)return -EINVAL;
    int rc=route_check(r);if(rc)return sony_dsf_route_live_fail(r,rc);
    r->live_staged=*reply;r->live_pending=2;return 0;
}
int sony_dsf_route_live_output_closed(void *v){
    struct sony_dsf_route *r=v;if(!r)return -EINVAL;
    int rc=route_check(r);
    if(!rc)rc=sony_audio_route_mute(r->route,1);
    if(!rc)rc=pcm_closed();
    if(rc)return sony_dsf_route_live_fail(r,rc);
    r->live_output_closed=1;return 0;
}
int sony_dsf_route_live_is_volume(const struct sony_dsf_route *r){return r&&r->live_pending==2&&r->live_volume;}
int sony_dsf_route_live_commit_volume(struct sony_dsf_route *r,const struct wdf_controls *reply,int output_ready){
    if(r&&r->native_mode)return -ENOTSUP;
    if(!r||!reply)return -EINVAL;
    int rc=route_check(r);if(rc)return sony_dsf_route_live_fail(r,rc);
    rc=pcm_format(r->binding.controls.cfw_profile==1?0:4,r->rate);
    if(!rc)rc=sony_audio_route_live_volume(r->route,(unsigned)reply->controls.settings.volume,(unsigned)reply->controls.settings.volume,&r->binding.controls.output);
    if(rc)return sony_dsf_route_live_fail(r,rc);
    r->binding.controls.settings=reply->controls.settings;r->revision=reply->revision;r->live_base=*reply;
    if(output_ready){rc=state_text(r);if(!rc)rc=sony_audio_owner_publish(r->owner,r->state_text);}
    /* At EOF the queue is already drained. Keep the acknowledgement but leave
     * publication retired; the normal owner close follows, without a reopen. */
    if(rc)return sony_dsf_route_live_fail(r,rc);
    r->live_pending=r->live_volume=0;return 0;
}
int sony_dsf_route_live_commit(struct sony_dsf_route *r,const struct wdf_controls *reply,const struct sony_dsf_output_retirement *retired){
    if(r&&r->native_mode)return -ENOTSUP;
    if(!r||!reply||!retired)return -EINVAL;
    int rc=route_check(r);if(rc)return sony_dsf_route_live_fail(r,rc);
    rc=retired->output_ready?pcm_format(r->binding.controls.cfw_profile==1?0:4,r->rate):pcm_closed();
    /* Apply DAC level under mute before saving and publishing the new state. */
    if(!rc&&reply->controls.settings.volume!=r->binding.controls.settings.volume)
        rc=sony_audio_route_volume(r->route,(unsigned)reply->controls.settings.volume);
    if(!rc)rc=sony_snapshot_commit(SONY_AUDIO_CONFIG,r->snapshot,r->live_snapshot);
    if(rc)return sony_dsf_route_live_fail(r,rc);
    /* Durable state is now new, even if subsequent hardware/publication fails.
     * Never claim it was rolled back; failed public reads remain unavailable. */
    memcpy(r->snapshot,r->live_snapshot,sizeof(r->snapshot));
    r->binding.controls.settings=reply->controls.settings;r->binding.controls.dsee=reply->controls.dsee;
    r->binding.controls.tone.selector=reply->controls.selector;
    if(reply->controls.selector==3)r->binding.controls.tone.tone=reply->controls.tone;
    r->binding.dsd=reply->controls.dsd;r->revision=reply->revision;r->live_base=*reply;
    if(retired->output_ready){
        rc=sony_audio_route_effects(r->route,effects(&r->binding.controls));
        if(!rc)rc=sony_audio_route_enable(r->route);
        if(!rc)rc=state_text(r);
        if(!rc)rc=sony_audio_owner_publish(r->owner,r->state_text);
    }else{
        /* Confirmed at EOF: preserve acknowledgement for get, but do not reopen
         * or publish output-ready for an already-ended output. */
        rc=sony_audio_route_end(&r->route);if(!rc)r->quiet_finished=1;
    }
    if(rc)return sony_dsf_route_live_fail(r,rc);
    r->live_pending=0;return 0;
}
static int prepare(struct sony_dsf_route **out,unsigned activation,const struct sony_dsd_controls *dsd,int native,unsigned expected){
    if(!out||*out||!dsd)return -EINVAL;
    (void)activation;
    struct sony_dsf_route *r=calloc(1,sizeof(*r));if(!r)return -ENOMEM;r->revision=1;
    r->native_mode=native;
    int rc=sony_audio_owner_acquire(&r->owner,SONY_OWNER_DSF);if(rc){free(r);return rc;}*out=r;
    r->binding.generation=sony_audio_owner_generation(r->owner);r->binding.dsd=*dsd;
    rc=sony_audio_route_idle();if(rc)return remember(r,rc);
    struct sony_audio_jack_state jack;rc=sony_audio_jack_read(&jack);if(rc)return remember(r,rc);
    struct sony_controls base;rc=sony_controls_load(&base);
    if(!rc)rc=sony_controls_output_event(&base,jack.se,jack.btl,&r->binding.controls);
    (void)expected;
    if(rc)return remember(r,rc);
    rc=sony_snapshot_read(SONY_AUDIO_CONFIG,r->snapshot);if(rc)return remember(r,rc);
    struct sony_controls *c=&r->binding.controls;
    rc=sony_audio_route_begin(&r->route,(unsigned)c->settings.volume,native?0:effects(c),&c->output);
    if(rc)return remember(r,rc);
    unsigned char staged[SONY_SNAPSHOT_BYTES],next[SONY_SNAPSHOT_BYTES];
    rc=sony_snapshot_encode_volume(r->snapshot,sizeof(r->snapshot),c->settings.volume,staged);
    if(!rc)rc=sony_snapshot_encode_volume_memory(staged,sizeof(staged),&c->volume_memory,next);
    if(!rc&&memcmp(next,r->snapshot,sizeof(next)))rc=sony_snapshot_commit(SONY_AUDIO_CONFIG,r->snapshot,next);
    if(!rc)memcpy(r->snapshot,next,sizeof(next));
    return remember(r,rc);
}
int sony_dsf_route_prepare(struct sony_dsf_route **out,unsigned activation,const struct sony_dsd_controls *dsd){return prepare(out,activation,dsd,0,0);}
int sony_dsf_route_pcm_prepare_output(struct sony_dsf_route **out,unsigned activation,const struct sony_dsd_controls *dsd,unsigned expected){
    if(!dsd)return -EINVAL;
    return prepare(out,activation,dsd,0,expected);
}
int sony_dsf_route_native_prepare(struct sony_dsf_route **out,unsigned activation,const struct sony_dsd_controls *dsd){return prepare(out,activation,dsd,1,0);}
int sony_dsf_route_export_config(struct sony_dsf_route *r,const char *path){
    if(!r||!path||r->armed||r->config_path||r->error)return -EINVAL;
    int rc=route_check(r);if(rc)return rc;
    rc=sony_snapshot_commit(path,NULL,r->snapshot);
    if(!rc){r->config_path=strdup(path);if(!r->config_path)rc=-ENOMEM;}
    return remember(r,rc);
}
int sony_dsf_route_arm(struct sony_dsf_route *r,struct sony_dsf_route_binding *binding,struct sony_dsf_playback_owner *callbacks){
    if(!r||r->native_mode||!binding||!callbacks||r->armed||r->error)return -EINVAL;
    int rc=route_check(r);if(rc)return remember(r,rc);
    r->armed=1;*binding=r->binding;*callbacks=(struct sony_dsf_playback_owner){route_check,started,quiesce,r};return 0;
}
int sony_dsf_route_commands_open(struct sony_dsf_route *r,struct sony_command_server **out,const struct sony_command_ops *ops){
    if(!r||!r->armed||!out||*out)return -EINVAL;
    if(r->commands_enabled)return -EALREADY;
    int rc=route_check(r);if(rc)return rc;
    rc=sony_command_server_open(out,r->owner,ops);if(rc)return rc;
    r->commands_enabled=1;
    if(r->state_text){rc=state_text(r);if(!rc)rc=sony_audio_owner_publish(r->owner,r->state_text);}
    if(rc){r->commands_enabled=0;sony_command_server_close(out);return sony_dsf_route_live_fail(r,rc);}
    return 0;
}
int sony_dsf_route_finish(struct sony_dsf_route **owner,const struct sony_dsf_playback_cleanup *c,
    const struct sony_dsf_process_exit *d,const struct sony_pcm_sink_process_exit *s){
    if(!owner||!*owner||(*owner)->native_mode)return -EINVAL;
    struct sony_dsf_route *r=*owner;
    int rc=quiesce(r);
    int cleanup=c?(c->dsf_close?c->dsf_close:c->sink_close?c->sink_close:c->source_close?c->source_close:c->quiesce):0;
    if(!cleanup&&((d&&(d->exit_code||d->signal_number||d->launch_errno))||
                  (s&&(s->exit_code||s->signal_number||s->launch_errno))))cleanup=-EIO;
    int released=sony_audio_owner_finish(&r->owner,1);if(!rc)rc=released;if(!rc)rc=cleanup;
    free(r->config_path);free(r->state_text);free(r);*owner=NULL;return rc;
}
static int native_pcm(struct sony_dsf_route *r,unsigned rate){
    (void)r;(void)rate;return 0;
}
static void native_controls(struct sony_dsf_route *r){
    struct sony_controls *c=&r->binding.controls;
    r->native_ack=(struct wdf_controls){.revision=r->revision};
    r->native_ack.controls.settings=c->settings;r->native_ack.controls.dsd=r->binding.dsd;
    r->native_ack.controls.dsee=c->dsee;r->native_ack.controls.selector=c->tone.selector;r->native_ack.controls.tone=c->tone.tone;
}
static int native_started(void *v,const struct wdn_state *state){
    struct sony_dsf_route *r=v;if(!r||!state)return -EINVAL;
    int rc=route_check(r);if(!rc)rc=native_pcm(r,state->bit_rate);
    if(!rc)rc=sony_audio_route_enable(r->route);
    if(!rc){r->rate=state->bit_rate;native_controls(r);rc=state_text(r);}
    if(!rc)rc=sony_audio_owner_publish(r->owner,r->state_text);
    if(rc)return sony_dsf_route_live_fail(r,rc);
    r->started=1;return 0;
}
int sony_dsf_route_native_arm(struct sony_dsf_route *r,struct sony_dsf_route_binding *binding,struct sony_dsf_native_playback_owner *callbacks){
    if(!r||!r->native_mode||!binding||!callbacks||r->armed||r->error)return -EINVAL;
    int rc=route_check(r);if(rc)return remember(r,rc);
    r->armed=1;*binding=r->binding;*callbacks=(struct sony_dsf_native_playback_owner){route_check,native_started,quiesce,r};return 0;
}
int sony_dsf_route_native_child(struct sony_dsf_route *r,int pid){
    if(!r)return -EINVAL;
    int rc=route_check(r);if(rc)return rc;r->native_pid=pid;return 0;
}
int sony_dsf_route_native_cleanup_check(void *v){
    struct sony_dsf_route *r=v;if(!r||!r->native_mode||!r->owner)return -EINVAL;
    return sony_audio_owner_check(r->owner);
}
static int native_context(struct sony_dsf_route *r){
    int rc=route_check(r);if(!rc)rc=native_pcm(r,r->rate);
    const struct sony_controls *c=r->native_pending?&r->native_target:&r->binding.controls;
    if(!rc)rc=sony_audio_route_live_volume(r->route,(unsigned)c->settings.volume,(unsigned)c->settings.volume,&c->output);
    return rc;
}
int sony_dsf_route_native_get(struct sony_dsf_route *r,struct wdf_controls *out){
    if(!r||!r->native_mode||!out)return -EINVAL;
    if(!r->started)return -EAGAIN;
    int rc=native_context(r);if(rc)return sony_dsf_route_live_fail(r,rc);
    *out=r->native_pending?r->native_staged:r->native_ack;return 0;
}
int sony_dsf_route_native_stage_volume(struct sony_dsf_route *r,uint64_t generation,unsigned revision,
    const struct sony_dsd_live_controls *target,struct wdf_controls *out){
    if(!r||!r->native_mode||!target||!out||!r->started)return -EINVAL;
    (void)generation;(void)revision;
    struct sony_controls next=r->binding.controls;struct sony_volume_state vs={next.ship_selector,next.settings.volume,next.output.device,next.volume_memory.last_headphone,
        0,1,next.output.se_gain,next.output.btl_gain,next.volume_memory.avls,next.avls_threshold};struct sony_volume_plan plan;
    int rc=sony_volume_plan_set(&vs,target->settings.volume,&plan);if(rc)return rc;
    unsigned char staged[SONY_SNAPSHOT_BYTES];rc=sony_snapshot_encode_volume(r->snapshot,sizeof(r->snapshot),plan.next.volume,staged);if(rc)return rc;
    struct wdf_controls base;rc=sony_dsf_route_native_get(r,&base);if(rc)return rc;
    if(plan.next.volume==base.controls.settings.volume){*out=base;return 0;}
    next.settings.volume=plan.next.volume;r->native_target=next;r->native_pending=1;
    r->native_staged=base;r->native_staged.pending=1;r->native_staged.stage_request=base.stage_request+1;
    rc=sony_audio_owner_retire(r->owner);
    if(!rc)rc=sony_audio_route_live_volume(r->route,(unsigned)base.controls.settings.volume,(unsigned)next.settings.volume,&next.output);
    if(!rc)rc=sony_snapshot_commit(SONY_AUDIO_CONFIG,r->snapshot,staged);
    if(rc)return sony_dsf_route_live_fail(r,rc);
    memcpy(r->snapshot,staged,sizeof(staged));*out=r->native_staged;return 0;
}
int sony_dsf_route_native_commit_volume(struct sony_dsf_route *r,int output_ready){
    if(!r||!r->native_mode)return -EINVAL;
    int rc=native_context(r);if(rc)return sony_dsf_route_live_fail(r,rc);
    r->binding.controls=r->native_target;++r->revision;
    unsigned request=r->native_staged.stage_request;native_controls(r);r->native_ack.stage_request=request;r->native_pending=0;
    if(output_ready){rc=state_text(r);if(!rc)rc=sony_audio_owner_publish(r->owner,r->state_text);}
    else{r->native_eof=1;rc=sony_audio_owner_retire(r->owner);}
    if(rc)return sony_dsf_route_live_fail(r,rc);
    return 0;
}
int sony_dsf_route_native_finish(struct sony_dsf_route **owner,const struct sony_dsf_native_playback_cleanup *c,
    const struct sony_dsf_process_exit *d,const struct sony_dsd_sink_process_exit *s){
    if(!owner||!*owner||!(*owner)->native_mode)return -EINVAL;
    struct sony_dsf_route *r=*owner;
    int rc=quiesce(r);
    int cleanup=c?(c->dsf_close?c->dsf_close:c->sink_close?c->sink_close:c->source_close?c->source_close:c->quiesce):0;
    if(!cleanup&&((d&&(d->exit_code||d->signal_number||d->launch_errno))||
                  (s&&(s->exit_code||s->signal_number||s->launch_errno))))cleanup=-EIO;
    if(!cleanup&&s&&s->power_cleanup_status)cleanup=s->power_cleanup_status;
    int released=sony_audio_owner_finish(&r->owner,1);if(!rc)rc=released;if(!rc)rc=cleanup;
    free(r->config_path);free(r->state_text);free(r);*owner=NULL;return rc;
}
