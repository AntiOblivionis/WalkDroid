#include "sony_audio_dsf_playback.h"
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
struct sony_dsf_playback {
    struct sony_dsf_client *dsf;struct sony_pcm_sink_client *sink;struct sony_dsf_source *source;
    struct sony_dsf_playback_owner owner;struct sony_dsf_playback_state state;
    struct sony_dsf_playback_cleanup cleanup;
    uint64_t sink_acked_base;
    unsigned retired_request;
    int ended_output;
    int cancel,dsf_attempted,dsf_opened,sink_attempted,sink_opened,quiesced,closed_sink,closed_dsf;
    pthread_mutex_t mutex;
};
static int lock(struct sony_dsf_playback *p){return !p?-EINVAL:pthread_mutex_trylock(&p->mutex)?-EBUSY:0;}
static int record(struct sony_dsf_playback *p,int rc){if(rc&&!p->state.first_error)p->state.first_error=rc;return rc;}
static int check(struct sony_dsf_playback *p){
    if(__atomic_load_n(&p->cancel,__ATOMIC_ACQUIRE)){p->state.cancel_requested=1;return record(p,-ECANCELED);}
    int rc=p->owner.check(p->owner.context);if(rc)return record(p,rc);
    return record(p,rc);
}
static int quiesce(struct sony_dsf_playback *p){
    if(!p->quiesced){p->quiesced=1;p->state.quiesce_status=p->owner.quiesce(p->owner.context);record(p,p->state.quiesce_status);}
    return p->state.quiesce_status;
}
static int fail(struct sony_dsf_playback *p,int rc){record(p,rc);p->state.failed=1;sony_dsf_source_cancel(p->source);quiesce(p);return p->state.first_error;}
static int read_at(void *v,uint64_t off,uint8_t *data,size_t n,size_t *actual){
    struct sony_dsf_playback *p=v;int rc=check(p);if(!rc)rc=sony_dsf_source_read_at(p->source,off,data,n,actual);return record(p,rc);
}
static int samples(void *v,const uint8_t *data,const struct sony_dsd_result *r){
    struct sony_dsf_playback *p=v;int rc=check(p);if(rc)return rc;
    if(r->format!=SONY_DSD_OUTPUT_PCM_S32LE||!r->bytes||r->bytes%8)return record(p,-ENOTSUP);
    if(!p->sink_opened){
        struct wdp_state state;p->sink_attempted=1;rc=sony_pcm_sink_open(p->sink,r->rate,&state);p->state.sink_status=rc;
        if(rc)return record(p,rc);p->sink_opened=1;p->state.rate=r->rate;
        rc=check(p);if(!rc)rc=p->owner.started(p->owner.context,r->rate);if(rc)return record(p,rc);
    }
    if(r->rate!=p->state.rate)return record(p,-EPROTO);
    unsigned frames=r->bytes/8;
    for(unsigned at=0;at<frames;){
        rc=check(p);if(rc)return rc;unsigned n=frames-at;if(n>WDP_MAX_FRAMES)n=WDP_MAX_FRAMES;
        rc=sony_pcm_sink_write(p->sink,(const int *)data+at*2,n);p->state.sink_status=rc;if(rc)return record(p,rc);
        p->state.pcm_acked_bytes+=(uint64_t)n*8;at+=n;
    }return 0;
}
static int eos(void *v){struct sony_dsf_playback *p=v;int rc=check(p);if(!rc)p->state.source_finished=1;return rc;}
int sony_dsf_playback_start(struct sony_dsf_playback **out,struct sony_dsf_client *dsf,struct sony_pcm_sink_client *sink,
    int directory,const char *name,uint64_t budget,const struct sony_chain_settings *settings,const struct sony_dsd_controls *dsd,
    const struct sony_output_controls *output,unsigned latency,const struct sony_dsf_playback_owner *owner){
    if(!out||*out||!dsf||!sink||directory<0||!name||!settings||!sony_dsd_controls_valid(dsd)||!sony_output_controls_valid(output)||latency>2||
       !owner||!owner->check||!owner->started||!owner->quiesce)return -EINVAL;
    if(dsd->mode==1&&output->device==SONY_OUTPUT_BTL)return -ENOTSUP;
    int rc=sony_dsf_client_ready(dsf);if(!rc)rc=sony_pcm_sink_ready(sink);if(rc)return rc;
    struct sony_dsf_playback *p=calloc(1,sizeof(*p));if(!p)return -ENOMEM;
    if(pthread_mutex_init(&p->mutex,NULL)){free(p);return -ENOMEM;}
    p->dsf=dsf;p->sink=sink;p->owner=*owner;p->cleanup.output_cleanup_unverified=1;*out=p;
    pthread_mutex_lock(&p->mutex);rc=check(p);
    if(!rc)rc=sony_dsf_source_open_at(&p->source,directory,name,budget);
    if(!rc)rc=sony_dsf_source_size(p->source,&p->state.source_bytes);
    if(!rc){
        struct sony_dsf_client_callbacks cb={read_at,samples,eos,p};struct wdf_state state;p->dsf_attempted=1;
        rc=sony_dsf_client_open_terminal(dsf,p->state.source_bytes,settings,dsd,output,latency,&cb,&state);p->state.dsf_status=rc;
        if(!rc)p->dsf_opened=1;
    }
    if(rc)rc=fail(p,rc);pthread_mutex_unlock(&p->mutex);return rc;
}
int sony_dsf_playback_step(struct sony_dsf_playback *p){
    int rc=lock(p);if(rc)return rc;
    if(p->state.failed)rc=-EPIPE;
    else if(p->state.drain_accepted)rc=1;
    else if((rc=check(p)))rc=fail(p,rc);
    else {
        struct wdf_state state;rc=sony_dsf_client_step(p->dsf,&state);p->state.dsf_status=rc;
        if(rc)rc=fail(p,rc);
        else if(state.flags&WDF_STATE_INPUT_FINISHED){
            if(!p->state.source_finished||!p->sink_opened||p->state.pcm_acked_bytes!=(state.output_acked_low|((uint64_t)state.output_acked_high<<32)))rc=-EPROTO;
            if(!rc)rc=check(p);
            if(!rc){struct wdp_state sink;rc=sony_pcm_sink_drain(p->sink,&sink);p->state.sink_status=rc;
                if(!rc&&((sink.accepted_low|((uint64_t)sink.accepted_high<<32))*8!=p->state.pcm_acked_bytes-p->sink_acked_base||sink.flags!=5))rc=-EPROTO;}
            if(rc)rc=fail(p,rc);else{p->state.drain_accepted=1;rc=1;}
        }
    }
    pthread_mutex_unlock(&p->mutex);return rc;
}
int sony_dsf_playback_state(struct sony_dsf_playback *p,struct sony_dsf_playback_state *out){if(!out)return -EINVAL;int rc=lock(p);if(rc)return rc;*out=p->state;pthread_mutex_unlock(&p->mutex);return 0;}
static int controls(struct sony_dsf_playback *p,unsigned command,unsigned revision,unsigned request,
                    const struct sony_dsd_live_controls *target,struct wdf_controls *out){
    if(!out||(command==WDF_CONTROLS_STAGE&&!target))return -EINVAL;
    int rc=lock(p);if(rc)return rc;
    struct wdf_controls result;
    if(p->state.failed)rc=-EPIPE;
    else if((rc=check(p)))rc=fail(p,rc);
    else if((!p->sink_opened&&!p->ended_output)||!p->state.pcm_acked_bytes)rc=-EAGAIN;
    else if(command==WDF_CONTROLS_STAGE&&p->state.source_finished)rc=-EINVAL;
    else{
        rc=command==WDF_CONTROLS_GET?sony_dsf_client_get_controls(p->dsf,&result):
           command==WDF_CONTROLS_STAGE?sony_dsf_client_stage_controls(p->dsf,revision,target,&result):
           sony_dsf_client_commit_controls(p->dsf,revision,request,&result);
        int checked=check(p);
        if(checked)rc=fail(p,checked);
        else if(command!=WDF_CONTROLS_GET&&rc==WDF_STALE)rc=-ESTALE;
        else if(command==WDF_CONTROLS_STAGE&&rc==WDF_BUSY)rc=-EBUSY;
        else if(command==WDF_CONTROLS_STAGE&&(rc==WDF_FORMAT||rc==-EINVAL))rc=-EINVAL;
        else if(rc<0){p->state.dsf_status=rc;rc=fail(p,rc);}
        else *out=result;
    }
    pthread_mutex_unlock(&p->mutex);return rc;
}
int sony_dsf_playback_get_controls(struct sony_dsf_playback *p,struct wdf_controls *out){return controls(p,WDF_CONTROLS_GET,0,0,NULL,out);}
int sony_dsf_playback_stage_controls(struct sony_dsf_playback *p,unsigned revision,const struct sony_dsd_live_controls *target,struct wdf_controls *out){
    return controls(p,WDF_CONTROLS_STAGE,revision,0,target,out);
}
int sony_dsf_playback_commit_controls(struct sony_dsf_playback *p,unsigned revision,unsigned request,struct wdf_controls *out){
    return controls(p,WDF_CONTROLS_COMMIT,revision,request,NULL,out);
}
int sony_dsf_playback_abort_controls(struct sony_dsf_playback *p,int error){
    if(error>=0)return -EINVAL;int rc=lock(p);if(rc)return rc;
    rc=fail(p,error);pthread_mutex_unlock(&p->mutex);return rc;
}
int sony_dsf_playback_retire_output(struct sony_dsf_playback *p,const struct wdf_controls *confirmed,
    int (*closed)(void *),void *context,struct sony_dsf_output_retirement *out){
    if(!confirmed||!closed||!out)return -EINVAL;
    int rc=lock(p);if(rc)return rc;
    if(p->state.failed)rc=-EPIPE;
    else if((rc=check(p)))rc=fail(p,rc);
    else if(!p->sink_opened||!p->state.pcm_acked_bytes)rc=-EAGAIN;
    else{
        {
            /* Mark before transport: an uncertain CLOSE is not automatically
             * repeated, and no OPEN follows it. The supervisor retains proof. */
            p->closed_sink=1;rc=sony_pcm_sink_close(p->sink);p->cleanup.sink_close=rc;p->state.sink_status=rc;
            if(!rc)p->sink_opened=0;
            if(!rc)rc=check(p);
            if(!rc)rc=closed(context);
            if(!rc)rc=check(p);
            if(!rc&&p->state.source_finished&&!p->state.drain_accepted)rc=-EPROTO;
            if(!rc&&!p->state.source_finished){
                struct wdp_state state;p->closed_sink=0;p->sink_attempted=1;
                rc=sony_pcm_sink_open(p->sink,p->state.rate,&state);p->state.sink_status=rc;
                if(!rc){p->sink_opened=1;p->sink_acked_base=p->state.pcm_acked_bytes;}
                if(!rc)rc=check(p);
            }
            if(rc)rc=fail(p,rc);
            else{
                p->retired_request=confirmed->stage_request;p->ended_output=p->state.source_finished;
                *out=(struct sony_dsf_output_retirement){confirmed->revision,confirmed->stage_request,p->state.rate,
                    p->sink_opened,p->state.pcm_acked_bytes};
            }
        }
    }
    pthread_mutex_unlock(&p->mutex);return rc;
}
void sony_dsf_playback_cancel(struct sony_dsf_playback *p){if(p)__atomic_store_n(&p->cancel,1,__ATOMIC_RELEASE);}
int sony_dsf_playback_close(struct sony_dsf_playback **owner,struct sony_dsf_playback_cleanup *out){
    if(!owner||!*owner||!out)return -EINVAL;struct sony_dsf_playback *p=*owner;int rc=lock(p);if(rc)return rc;
    p->cleanup.quiesce=quiesce(p);
    if(p->sink_attempted&&!p->closed_sink){p->closed_sink=1;p->cleanup.sink_close=sony_pcm_sink_close(p->sink);if(!p->sink_opened&&p->cleanup.sink_close==-EINVAL)p->cleanup.sink_close=0;}
    if(p->dsf_attempted&&!p->closed_dsf){p->closed_dsf=1;p->cleanup.dsf_close=sony_dsf_client_close_session(p->dsf);if(!p->dsf_opened&&p->cleanup.dsf_close==-EINVAL)p->cleanup.dsf_close=0;}
    p->cleanup.source_close=sony_dsf_source_close(&p->source);*out=p->cleanup;
    rc=out->quiesce?out->quiesce:out->sink_close?out->sink_close:out->dsf_close?out->dsf_close:out->source_close;
    if(p->source){pthread_mutex_unlock(&p->mutex);return rc?rc:-EBUSY;}
    pthread_mutex_unlock(&p->mutex);pthread_mutex_destroy(&p->mutex);free(p);*owner=NULL;return rc;
}
