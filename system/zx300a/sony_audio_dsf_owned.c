#define _GNU_SOURCE
#include "sony_audio_dsf_owned.h"
#include "sony_audio_pcm_sink_root.h"
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
struct sony_dsf_owned {
    struct sony_dsf_route *route;struct sony_dsf_route_binding binding;
    struct sony_dsf_playback_owner callbacks;struct sony_dsf_playback *playback;
    struct sony_dsf_process *dsf;struct sony_pcm_sink_process *sink;
    struct sony_dsf_owned_cleanup cleanup;
    struct sony_command_server *commands;
    unsigned live_revision,live_request;
    int live_pending;
    int armed,closing,dsf_stopped,sink_stopped,quiesced,cleanup_error,playback_fault;
};
static int first(struct sony_dsf_owned *s,int rc){if(rc&&!s->cleanup_error)s->cleanup_error=rc;return rc;}
static void playback_fault(struct sony_dsf_owned *s){
    struct sony_dsf_playback_state state;
    if(sony_dsf_playback_state(s->playback,&state)||(state.first_error&&state.first_error!=-ECANCELED&&state.first_error!=-ENXIO))s->playback_fault=1;
}
static int live_failed(struct sony_dsf_owned *s,int rc){
    /* The route remembers persistence/publication uncertainty independently of
     * playback's first_error; both objects must reject all subsequent work. */
    sony_dsf_route_live_fail(s->route,rc);
    rc=sony_dsf_playback_abort_controls(s->playback,rc);playback_fault(s);return rc;
}
static int start(struct sony_dsf_owned **out,unsigned activation,int dr,int sr,int log,
    const char *config,int media,const char *name,uint64_t max_bytes,const struct sony_dsd_controls *dsd,unsigned latency,unsigned expected){
    if(!out||*out||activation!=SONY_DSF_OWNED_EXPLICIT_START||dr<0||sr<0||log<0||media<0||!config||!name||
       !sony_dsd_controls_valid(dsd)||latency>2)return -EINVAL;
    int rc;
    struct sony_dsf_owned *s=calloc(1,sizeof(*s));if(!s)return -ENOMEM;*out=s;
    rc=expected?sony_dsf_route_pcm_prepare_output(&s->route,SONY_DSF_ROUTE_EXPLICIT_START,dsd,expected):
        sony_dsf_route_prepare(&s->route,SONY_DSF_ROUTE_EXPLICIT_START,dsd);
    if(!rc)rc=sony_dsf_route_export_config(s->route,config);
    if(!rc){rc=sony_dsf_route_arm(s->route,&s->binding,&s->callbacks);if(!rc)s->armed=1;}
    if(!rc)rc=sony_dsf_process_start(&s->dsf,SONY_DSF_PROCESS_EXPLICIT_START,dr,log,3000,&s->cleanup.dsf);
    if(!rc)rc=sony_pcm_sink_process_start_profile(&s->sink,SONY_PCM_SINK_PROCESS_EXPLICIT_START,sr,log,s->binding.controls.cfw_profile,3000,&s->cleanup.sink);
    if(!rc)rc=sony_dsf_playback_start(&s->playback,sony_dsf_process_client(s->dsf),sony_pcm_sink_process_client(s->sink),
        media,name,max_bytes,&s->binding.controls.settings,&s->binding.dsd,&s->binding.controls.output,latency,&s->callbacks);
    if(rc&&s->playback&&rc!=-ECANCELED&&rc!=-ENXIO)s->playback_fault=1;
    return rc;
}
int sony_dsf_owned_start(struct sony_dsf_owned **out,unsigned activation,int dr,int sr,int log,
    const char *config,int media,const char *name,uint64_t max_bytes,const struct sony_dsd_controls *dsd,unsigned latency){
    return start(out,activation,dr,sr,log,config,media,name,max_bytes,dsd,latency,0);
}
int sony_dsf_owned_start_output(struct sony_dsf_owned **out,unsigned activation,int dr,int sr,int log,
    const char *config,int media,const char *name,uint64_t max_bytes,const struct sony_dsd_controls *dsd,unsigned latency,unsigned expected){
    if(!dsd||dsd->mode!=2||(expected!=SONY_OUTPUT_SE&&expected!=SONY_OUTPUT_BTL))return -EINVAL;
    return start(out,activation,dr,sr,log,config,media,name,max_bytes,dsd,latency,expected);
}
int sony_dsf_owned_step(struct sony_dsf_owned *s){
    if(!s||s->closing||!s->playback)return -EINVAL;
    if(s->commands){int command=sony_dsf_owned_poll_commands(s);if(command<0)return command;}
    int rc=sony_dsf_playback_step(s->playback);
    if(rc>=0&&s->live_pending){
        struct wdf_controls reply;
        int committed=sony_dsf_playback_commit_controls(s->playback,s->live_revision,s->live_request,&reply);
        if(committed<0)return live_failed(s,committed);
        int checked;
        if(committed)checked=sony_dsf_route_live_check(s->route,&reply);
        else if(sony_dsf_route_live_is_volume(s->route))checked=sony_dsf_route_live_commit_volume(s->route,&reply,rc==0);
        else{
            struct sony_dsf_output_retirement retired;
            checked=sony_dsf_playback_retire_output(s->playback,&reply,sony_dsf_route_live_output_closed,s->route,&retired);
            if(!checked)checked=sony_dsf_route_live_commit(s->route,&reply,&retired);
        }
        if(checked)return live_failed(s,checked);
        if(!committed)s->live_pending=0;
    }
    if(rc<0)playback_fault(s);
    return rc;
}
int sony_dsf_owned_state(struct sony_dsf_owned *s,struct sony_dsf_playback_state *out){return !s||!s->playback?-EINVAL:sony_dsf_playback_state(s->playback,out);}
int sony_dsf_owned_get_controls(struct sony_dsf_owned *s,struct sony_dsf_owned_controls *out){
    if(!s||!out||s->closing||!s->playback)return -EINVAL;
    struct sony_dsf_owned_controls result={.generation=s->binding.generation};
    int rc=sony_dsf_playback_get_controls(s->playback,&result.acknowledged);
    if(rc){playback_fault(s);return rc;}
    rc=sony_dsf_route_live_check(s->route,&result.acknowledged);
    if(rc)return live_failed(s,rc);
    *out=result;return 0;
}
int sony_dsf_owned_stage_controls(struct sony_dsf_owned *s,uint64_t generation,unsigned revision,
    const struct sony_dsd_live_controls *target,struct sony_dsf_owned_controls *out){
    if(!s||!target||!out||s->closing||!s->playback)return -EINVAL;
    struct sony_dsf_playback_state state;int rc=sony_dsf_playback_state(s->playback,&state);if(rc)return rc;
    if(state.failed)return -EPIPE;
    if(state.source_finished)return -EINVAL;
    if(s->live_pending)return -EBUSY;
    struct sony_dsf_owned_controls base;rc=sony_dsf_owned_get_controls(s,&base);if(rc)return rc;
    struct sony_dsd_live_controls normalized;
    rc=sony_dsf_route_live_begin(s->route,generation,revision,&base.acknowledged,target,&normalized);
    if(rc<0){int checked=s->callbacks.check(s->callbacks.context);return checked?live_failed(s,rc):rc;}
    if(rc==1){*out=base;return 0;}
    struct wdf_controls staged;
    rc=sony_dsf_playback_stage_controls(s->playback,revision,&normalized,&staged);
    if(rc)return live_failed(s,rc);
    rc=sony_dsf_route_live_staged(s->route,&staged);if(rc)return live_failed(s,rc);
    s->live_pending=1;s->live_revision=revision;s->live_request=staged.stage_request;
    *out=(struct sony_dsf_owned_controls){.generation=s->binding.generation,.acknowledged=staged};return 0;
}
void sony_dsf_owned_cancel(struct sony_dsf_owned *s){if(s&&!s->closing)sony_dsf_playback_cancel(s->playback);}
static int command_read(void *context,struct wdf_controls *out){
    struct sony_dsf_owned_controls got;int rc=sony_dsf_owned_get_controls(context,&got);if(!rc)*out=got.acknowledged;return rc;
}
static int command_stage(void *context,uint64_t generation,unsigned revision,const struct sony_dsd_live_controls *target,struct wdf_controls *out){
    struct sony_dsf_owned_controls got;int rc=sony_dsf_owned_stage_controls(context,generation,revision,target,&got);if(!rc)*out=got.acknowledged;return rc;
}
int sony_dsf_owned_enable_commands(struct sony_dsf_owned *s){
    if(!s||s->closing||!s->playback||!s->armed)return -EINVAL;
    if(s->commands)return -EALREADY;
    struct sony_command_ops ops={command_read,command_stage,s};return sony_dsf_route_commands_open(s->route,&s->commands,&ops);
}
int sony_dsf_owned_poll_commands(struct sony_dsf_owned *s){
    if(!s||s->closing||!s->commands)return -EINVAL;
    int rc=sony_command_server_poll(s->commands);return rc<0?live_failed(s,rc):rc;
}
int sony_dsf_owned_close(struct sony_dsf_owned **owner,struct sony_dsf_owned_cleanup *out){
    if(!owner||!*owner||!out)return -EINVAL;
    struct sony_dsf_owned *s=*owner;s->closing=1;
    if(s->commands)first(s,sony_command_server_close(&s->commands));
    if(s->playback){first(s,sony_dsf_playback_close(&s->playback,&s->cleanup.playback));if(s->playback){*out=s->cleanup;return s->cleanup_error?s->cleanup_error:-EBUSY;}}
    else if(s->armed&&!s->quiesced){s->quiesced=1;first(s,s->callbacks.quiesce(s->callbacks.context));}
    if(s->sink){
        if(!s->sink_stopped){s->sink_stopped=1;struct sony_pcm_sink_client *c=sony_pcm_sink_process_client(s->sink);
            s->cleanup.sink_shutdown=c?sony_pcm_sink_shutdown(c):-EIO;first(s,s->cleanup.sink_shutdown);}
        first(s,sony_pcm_sink_process_stop(&s->sink,1000,&s->cleanup.sink));
    }
    if(s->dsf){
        if(!s->dsf_stopped){s->dsf_stopped=1;struct sony_dsf_client *c=sony_dsf_process_client(s->dsf);
            s->cleanup.dsf_shutdown=c?sony_dsf_client_shutdown(c):-EIO;first(s,s->cleanup.dsf_shutdown);}
        first(s,sony_dsf_process_stop(&s->dsf,1000,&s->cleanup.dsf));
    }
    if(s->sink||s->dsf){*out=s->cleanup;return s->cleanup_error?s->cleanup_error:-EBUSY;}
    if(s->route){
        /* Shutdown transport uncertainty must not be hidden by an eventual
         * natural child exit. Withhold receipts to retain the ACTIVE barrier. */
        int receipts=s->armed&&!s->cleanup_error&&!s->playback_fault;
        s->cleanup.route=sony_dsf_route_finish(&s->route,receipts?&s->cleanup.playback:NULL,receipts?&s->cleanup.dsf:NULL,receipts?&s->cleanup.sink:NULL);
        first(s,s->cleanup.route);
    }
    *out=s->cleanup;int rc=s->cleanup_error;free(s);*owner=NULL;return rc;
}
