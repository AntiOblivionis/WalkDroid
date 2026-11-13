#include "sony_audio_receiver_playback.h"
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
struct sony_receiver_playback {
    struct sony_receiver_client *dsp;struct sony_pcm_sink_client *sink;
    struct wur_open opening;struct sony_receiver_playback_owner owner;
    struct sony_receiver_playback_state state;struct sony_receiver_playback_cleanup cleanup;
    unsigned dsp_opened,sink_opened,dsp_closed,sink_closed,muted,quiesced,bluetooth;
    int cancel;pthread_mutex_t mutex;
    int output[SONY_RECEIVER_S32_MAX_BYTES/4];
    unsigned periodic,period,pending;
    int *pending_pcm;
};
static int lock(struct sony_receiver_playback *p){return !p?-EINVAL:pthread_mutex_trylock(&p->mutex)?-EBUSY:0;}
static int record(struct sony_receiver_playback *p,int rc){if(rc&&!p->state.first_error)p->state.first_error=rc;return rc;}
static int quiesce(struct sony_receiver_playback *p){
    if(!p->quiesced){p->quiesced=1;p->state.quiesce_status=p->owner.quiesce(p->owner.context);record(p,p->state.quiesce_status);}
    return p->state.quiesce_status;
}
static int mute(struct sony_receiver_playback *p){
    if(!p->muted){p->muted=1;p->state.mute_status=p->owner.mute(p->owner.context);record(p,p->state.mute_status);
        if(p->state.mute_status)quiesce(p);}
    return p->state.mute_status;
}
static int fail(struct sony_receiver_playback *p,int rc){record(p,rc);p->state.failed=1;mute(p);return p->state.first_error;}
static int check(struct sony_receiver_playback *p){
    if(__atomic_load_n(&p->cancel,__ATOMIC_ACQUIRE)){p->state.cancel_requested=1;return record(p,-ECANCELED);}
    return record(p,p->owner.check(p->owner.context));
}
static uint64_t pair(unsigned lo,unsigned hi){return lo|((uint64_t)hi<<32);}
static int write_pcm(struct sony_receiver_playback *p,const int *pcm,unsigned frames){
    int rc=check(p);if(rc)return rc;
    if(p->state.sink_acked_bytes>UINT64_MAX-(uint64_t)frames*8)return record(p,-EOVERFLOW);
    rc=sony_pcm_sink_write(p->sink,pcm,frames);p->state.sink_status=rc;if(rc)return record(p,rc);
    p->state.sink_acked_bytes+=(uint64_t)frames*8;
    return check(p);
}
static int flush_pending(struct sony_receiver_playback *p){
    if(!p->pending)return 0;
    /* Retain an uncertain attempt, but never resubmit: the caller seals on
     * every error, and close discards without calling this function. */
    int rc=write_pcm(p,p->pending_pcm,p->pending);if(!rc)p->pending=0;return rc;
}
static int samples(void *context,const uint8_t *bytes,const struct sony_receiver_chain_result *r,unsigned tail){
    struct sony_receiver_playback *p=context;(void)tail;int rc=check(p);if(rc)return rc;
    struct sony_receiver_s32_info info;
    rc=p->bluetooth?sony_receiver_output_bt_s32(p->opening.track,r,bytes,r->bytes,p->output,sizeof(p->output),&info):
        sony_receiver_output_s32(p->opening.track,r,bytes,r->bytes,p->output,sizeof(p->output),&info);
    if(rc)return record(p,rc);
    if(p->state.native_acked_bytes>UINT64_MAX-r->bytes||p->state.sink_acked_bytes>UINT64_MAX-info.bytes)return record(p,-EOVERFLOW);
    if(!p->sink_opened){
        struct wdp_state s;p->cleanup.sink_attempted=1;rc=sony_pcm_sink_open(p->sink,info.rate,&s);p->state.sink_status=rc;
        if(rc)return record(p,rc);p->sink_opened=1;p->state.rate=info.rate;
        if(p->periodic){
            if(!s.period_frames||s.period_frames>WDP_MAX_FRAMES)return record(p,-ENOTSUP);
            p->period=s.period_frames;
        }
        rc=check(p);if(!rc)rc=p->owner.started(p->owner.context,info.rate);if(rc)return record(p,rc);
    }
    if(info.rate!=p->state.rate)return record(p,-EPROTO);
    for(unsigned at=0;at<info.frames;){
        unsigned n=info.frames-at;
        if(p->periodic){
            if(!p->period||p->pending>=p->period)return record(p,-EPROTO);
            if(n>p->period-p->pending)n=p->period-p->pending;
            memcpy(p->pending_pcm+p->pending*2,p->output+at*2,(size_t)n*8);p->pending+=n;at+=n;
            if(p->pending==p->period){rc=flush_pending(p);if(rc)return rc;}
        }else{
            if(n>WDP_MAX_FRAMES)n=WDP_MAX_FRAMES;
            rc=write_pcm(p,p->output+at*2,n);if(rc)return rc;at+=n;
        }
    }
    p->state.native_acked_bytes+=r->bytes;return 0;
}
static int start(struct sony_receiver_playback **out,struct sony_receiver_client *dsp,
    struct sony_pcm_sink_client *sink,const struct wur_open *opening,const struct sony_receiver_playback_owner *owner,unsigned bluetooth,unsigned periodic){
    if(!out||*out||!dsp||!sink||!opening||!owner||!owner->check||!owner->started||!owner->mute||!owner->quiesce)return -EINVAL;
    int rc=sony_receiver_client_ready(dsp);if(!rc)rc=sony_pcm_sink_ready(sink);if(rc)return rc;
    struct sony_receiver_playback *p=calloc(1,sizeof(*p));if(!p)return -ENOMEM;
    if(pthread_mutex_init(&p->mutex,NULL)){free(p);return -ENOMEM;}
    if(periodic){p->pending_pcm=malloc((size_t)WDP_MAX_FRAMES*8);if(!p->pending_pcm){pthread_mutex_destroy(&p->mutex);free(p);return -ENOMEM;}}
    p->dsp=dsp;p->sink=sink;p->opening=*opening;p->owner=*owner;p->bluetooth=bluetooth;p->periodic=periodic;p->cleanup.output_cleanup_unverified=1;*out=p;
    pthread_mutex_lock(&p->mutex);rc=check(p);
    if(!rc){struct wur_state s;p->cleanup.dsp_attempted=1;rc=bluetooth?sony_receiver_client_open_bt_service(dsp,opening,samples,p,&s):
        sony_receiver_client_open_service(dsp,opening,samples,p,&s);p->state.dsp_status=rc;if(!rc)p->dsp_opened=1;}
    if(rc)rc=fail(p,rc);pthread_mutex_unlock(&p->mutex);return rc;
}
int sony_receiver_playback_start(struct sony_receiver_playback **out,struct sony_receiver_client *dsp,
    struct sony_pcm_sink_client *sink,const struct wur_open *opening,const struct sony_receiver_playback_owner *owner){return start(out,dsp,sink,opening,owner,0,0);}
int sony_receiver_playback_start_bt(struct sony_receiver_playback **out,struct sony_receiver_client *dsp,
    struct sony_pcm_sink_client *sink,const struct wur_open *opening,const struct sony_receiver_playback_owner *owner){return start(out,dsp,sink,opening,owner,1,0);}
int sony_receiver_playback_start_bt_period(struct sony_receiver_playback **out,struct sony_receiver_client *dsp,
    struct sony_pcm_sink_client *sink,const struct wur_open *opening,const struct sony_receiver_playback_owner *owner){return start(out,dsp,sink,opening,owner,1,1);}
int sony_receiver_playback_flush(struct sony_receiver_playback *p){
    int rc=lock(p);if(rc)return rc;
    if(p->state.failed||p->state.finished)rc=-EPIPE;
    else{rc=check(p);if(!rc)rc=flush_pending(p);if(rc)rc=fail(p,rc);}
    pthread_mutex_unlock(&p->mutex);return rc;
}
int sony_receiver_playback_pending(struct sony_receiver_playback *p,unsigned *frames,unsigned *period){
    if(!frames||!period)return -EINVAL;int rc=lock(p);if(rc)return rc;*frames=p->pending;*period=p->period;
    pthread_mutex_unlock(&p->mutex);return 0;
}
int sony_receiver_playback_push(struct sony_receiver_playback *p,const void *data,unsigned frames,int final){
    if(!data||!frames||frames>512||(final!=0&&final!=1)||(final?frames>=512:frames!=512))return -EINVAL;
    int rc=lock(p);if(rc)return rc;
    if(p->state.failed||p->state.finished||p->state.source_final)rc=-EPIPE;
    else if(p->state.source_acked_frames>UINT64_MAX-frames)rc=-EOVERFLOW;
    else{
        if(p->opening.track[17]==24)for(unsigned i=0;i<frames*2;++i)if(((const uint8_t *)data)[i*4]){rc=-EINVAL;break;}
        if(!rc){
            rc=check(p);
            if(!rc){struct wur_state s;rc=sony_receiver_client_process_service(p->dsp,data,frames,final,&s);p->state.dsp_status=rc;
                if(!rc){
                    p->state.source_acked_frames+=frames;p->state.source_final=final;
                    if(pair(s.input_processed_low,s.input_processed_high)!=p->state.source_acked_frames||
                       pair(s.output_acked_low,s.output_acked_high)!=p->state.native_acked_bytes)rc=-EPROTO;
                }}
            if(!rc)rc=check(p);
            if(rc)rc=fail(p,rc);
        }
    }
    pthread_mutex_unlock(&p->mutex);return rc;
}
static int close_sink(struct sony_receiver_playback *p){
    if(p->cleanup.sink_attempted&&!p->sink_closed){p->sink_closed=1;p->cleanup.sink_close=sony_pcm_sink_close(p->sink);
        if(!p->sink_opened&&p->cleanup.sink_close==-EINVAL)p->cleanup.sink_close=0;
        if(!p->cleanup.sink_close)p->sink_opened=0;record(p,p->cleanup.sink_close);}
    return p->cleanup.sink_close;
}
int sony_receiver_playback_finish(struct sony_receiver_playback *p,unsigned terminal){
    if(terminal!=WUR_SERVICE_STOP&&terminal!=WUR_SERVICE_DRAIN)return -EINVAL;int rc=lock(p);if(rc)return rc;
    if(p->state.failed)rc=-EPIPE;
    else if(p->state.finished)rc=p->state.terminal==terminal?0:-EINVAL;
    else{
        rc=check(p);p->state.terminal=terminal;
        if(!rc){struct wur_state s;rc=sony_receiver_client_finish_service(p->dsp,terminal,&s);p->state.dsp_status=rc;
            if(!rc&&(pair(s.input_processed_low,s.input_processed_high)!=p->state.source_acked_frames||
                pair(s.output_acked_low,s.output_acked_high)!=p->state.native_acked_bytes||!s.stop_accepted))rc=-EPROTO;}
        if(!rc)rc=check(p);
        if(!rc)rc=flush_pending(p);
        if(!rc&&p->sink_opened){
            struct wdp_state s;
            rc=terminal==WUR_SERVICE_DRAIN?sony_pcm_sink_drain(p->sink,&s):sony_pcm_sink_state(p->sink,&s);p->state.sink_status=rc;
            if(!rc&&(pair(s.accepted_low,s.accepted_high)!=p->state.sink_acked_bytes/8||
               s.flags!=(terminal==WUR_SERVICE_DRAIN?(WDP_SEALED|WDP_DRAIN_ACCEPTED):0)))rc=-EPROTO;
            if(!rc&&terminal==WUR_SERVICE_DRAIN)p->state.drain_accepted=1;
        }
        if(!rc)rc=check(p);
        if(!rc)rc=mute(p);
        if(!rc)rc=close_sink(p);
        if(!rc)rc=quiesce(p);
        if(rc)rc=fail(p,rc);else p->state.finished=1;
    }
    pthread_mutex_unlock(&p->mutex);return rc;
}
int sony_receiver_playback_state(struct sony_receiver_playback *p,struct sony_receiver_playback_state *out){
    if(!out)return -EINVAL;int rc=lock(p);if(rc)return rc;*out=p->state;pthread_mutex_unlock(&p->mutex);return 0;
}
void sony_receiver_playback_cancel(struct sony_receiver_playback *p){if(p)__atomic_store_n(&p->cancel,1,__ATOMIC_RELEASE);}
int sony_receiver_playback_close(struct sony_receiver_playback **owner,struct sony_receiver_playback_cleanup *out){
    if(!owner||!*owner||!out)return -EINVAL;struct sony_receiver_playback *p=*owner;int rc=lock(p);if(rc)return rc;
    p->cleanup.mute=mute(p);close_sink(p);
    if(p->cleanup.dsp_attempted&&!p->dsp_closed){p->dsp_closed=1;p->cleanup.dsp_close=sony_receiver_client_close(p->dsp);
        if(!p->dsp_opened&&p->cleanup.dsp_close==-EINVAL)p->cleanup.dsp_close=0;record(p,p->cleanup.dsp_close);}
    p->cleanup.quiesce=quiesce(p);p->cleanup.first_error=p->state.first_error;
    p->cleanup.terminal=p->state.terminal;p->cleanup.finished=p->state.finished;p->cleanup.drain_accepted=p->state.drain_accepted;
    *out=p->cleanup;rc=out->mute?out->mute:out->sink_close?out->sink_close:out->dsp_close?out->dsp_close:out->quiesce;
    pthread_mutex_unlock(&p->mutex);pthread_mutex_destroy(&p->mutex);free(p->pending_pcm);free(p);*owner=NULL;return rc;
}
