#define _GNU_SOURCE
#include "sony_audio_dsf_native_owned.h"
#include "sony_audio_dsd_power_linux.h"
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
struct sony_dsf_native_owned {
    struct sony_dsf_route *route;struct sony_dsf_route_binding binding;
    struct sony_dsf_native_playback_owner callbacks;struct sony_dsf_native_playback *playback;
    struct sony_dsf_process *dsf;struct sony_dsd_sink_process *native;
    struct sony_dsd_power_linux *adapter;struct sony_dsd_power_platform *platform;
    struct sony_command_server *commands;struct sony_dsf_native_owned_cleanup cleanup;
    int armed,closing,quiesced,dsf_stopped,native_stopped,live_pending,terminal;
    int resources_captured,cleanup_error,playback_fault;
};
static int first(struct sony_dsf_native_owned *s,int rc){if(rc&&!s->cleanup_error)s->cleanup_error=rc;return rc;}
static void fault(struct sony_dsf_native_owned *s,int rc){if(rc&&rc!=-ECANCELED&&rc!=-ENXIO)s->playback_fault=1;}
static int live_failed(struct sony_dsf_native_owned *s,int rc){
    sony_dsf_route_live_fail(s->route,rc);sony_dsf_native_playback_cancel(s->playback);fault(s,rc);return rc;
}
int sony_dsf_native_owned_start(struct sony_dsf_native_owned **out,unsigned activation,int dr,int nr,int log,
    const char *config,int media,const char *name,uint64_t max_bytes,const struct sony_dsd_controls *dsd,unsigned latency,unsigned low){
    if(!out||*out||activation!=SONY_DSF_NATIVE_OWNED_EXPLICIT_START||dr<0||nr<0||log<0||media<0||!config||!name||
       !sony_dsd_controls_valid(dsd)||dsd->mode!=1||latency>2||low>1)return -EINVAL;
    int rc;
    struct sony_dsf_native_owned *s=calloc(1,sizeof(*s));if(!s)return -ENOMEM;*out=s;
    rc=sony_dsf_route_native_prepare(&s->route,SONY_DSF_ROUTE_NATIVE_EXPLICIT_START,dsd);
    if(!rc)rc=sony_dsf_route_export_config(s->route,config);
    if(!rc){rc=sony_dsf_route_native_arm(s->route,&s->binding,&s->callbacks);if(!rc)s->armed=1;}
    if(!rc)rc=sony_dsd_power_linux_create(&s->adapter,sony_dsf_route_native_cleanup_check,s->route);
    if(!rc){rc=sony_dsd_power_linux_inspect(s->adapter,s->cleanup.resources_before);if(!rc)s->resources_captured=1;}
    struct sony_dsd_power_platform_io io;struct sony_dsd_power_backend backend;
    if(!rc)rc=sony_dsd_power_linux_io(s->adapter,&io);
    /* One-use platform receipt is the exact nonzero global owner generation;
     * WKP7 independently binds it to this session and the exact spawned PID. */
    if(!rc)rc=sony_dsd_power_platform_create(&s->platform,&io,s->binding.generation);
    if(!rc)rc=sony_dsd_power_platform_backend(s->platform,&backend);
    if(!rc){
        struct sony_dsd_sink_process_scope scope={s->binding.generation,s->binding.generation,s->binding.generation,
            s->binding.controls.cfw_profile,low,backend,sony_dsf_route_native_cleanup_check,s->route};
        rc=sony_dsd_sink_process_start(&s->native,SONY_DSD_SINK_PROCESS_EXPLICIT_START,nr,log,&scope,3000,&s->cleanup.native);
    }
    if(!rc){struct sony_dsd_sink_process_exit child;rc=sony_dsd_sink_process_state(s->native,&child);
        if(!rc)rc=sony_dsf_route_native_child(s->route,child.spawned_pid);}
    if(!rc)rc=sony_dsf_process_start(&s->dsf,SONY_DSF_PROCESS_EXPLICIT_START,dr,log,3000,&s->cleanup.dsf);
    if(!rc)rc=sony_dsf_native_playback_start(&s->playback,sony_dsf_process_client(s->dsf),sony_dsd_sink_process_client(s->native),
        media,name,max_bytes,&s->binding.controls.settings,&s->binding.dsd,&s->binding.controls.output,latency,&s->callbacks);
    if(rc&&s->playback)fault(s,rc);
    return rc;
}
int sony_dsf_native_owned_step(struct sony_dsf_native_owned *s){
    if(!s||s->closing||!s->playback)return -EINVAL;
    if(s->commands){int command=sony_dsf_native_owned_poll_commands(s);if(command<0)return command;}
    int rc=sony_dsf_native_playback_step(s->playback);
    if(rc>=0&&s->live_pending){
        int committed=sony_dsf_route_native_commit_volume(s->route,rc==0);
        if(committed)return live_failed(s,committed);
        s->live_pending=0;
    }
    if(rc==1)s->terminal=1;
    else if(rc<0)fault(s,rc);
    return rc;
}
int sony_dsf_native_owned_state(struct sony_dsf_native_owned *s,struct sony_dsf_native_playback_state *out){
    return !s||!s->playback?-EINVAL:sony_dsf_native_playback_state(s->playback,out);
}
int sony_dsf_native_owned_get_controls(struct sony_dsf_native_owned *s,struct sony_dsf_native_owned_controls *out){
    if(!s||!out||s->closing||!s->playback)return -EINVAL;
    struct sony_dsf_native_owned_controls got={.generation=s->binding.generation};
    int rc=sony_dsf_route_native_get(s->route,&got.acknowledged);
    if(rc)return rc==-EAGAIN?rc:live_failed(s,rc);
    *out=got;return 0;
}
int sony_dsf_native_owned_stage_controls(struct sony_dsf_native_owned *s,uint64_t generation,unsigned revision,
    const struct sony_dsd_live_controls *target,struct sony_dsf_native_owned_controls *out){
    if(!s||!target||!out||s->closing||!s->playback)return -EINVAL;
    if(generation!=s->binding.generation)return -ESTALE;
    struct sony_dsf_native_playback_state state;int rc=sony_dsf_native_playback_state(s->playback,&state);if(rc)return rc;
    if(state.failed)return -EPIPE;
    if(s->terminal||state.source_finished)return -EINVAL;
    if(s->live_pending)return -EBUSY;
    struct sony_dsf_native_owned_controls staged={.generation=generation};
    rc=sony_dsf_route_native_stage_volume(s->route,generation,revision,target,&staged.acknowledged);
    if(rc){int checked=s->callbacks.check(s->callbacks.context);return checked?live_failed(s,rc):rc;}
    s->live_pending=!!staged.acknowledged.pending;*out=staged;return 0;
}
void sony_dsf_native_owned_cancel(struct sony_dsf_native_owned *s){if(s&&!s->closing)sony_dsf_native_playback_cancel(s->playback);}
static int command_read(void *context,struct wdf_controls *out){
    struct sony_dsf_native_owned_controls got;int rc=sony_dsf_native_owned_get_controls(context,&got);if(!rc)*out=got.acknowledged;return rc;
}
static int command_stage(void *context,uint64_t generation,unsigned revision,const struct sony_dsd_live_controls *target,struct wdf_controls *out){
    struct sony_dsf_native_owned_controls got;int rc=sony_dsf_native_owned_stage_controls(context,generation,revision,target,&got);if(!rc)*out=got.acknowledged;return rc;
}
int sony_dsf_native_owned_enable_commands(struct sony_dsf_native_owned *s){
    if(!s||s->closing||!s->playback||!s->armed)return -EINVAL;
    if(s->commands)return -EALREADY;
    struct sony_command_ops ops={command_read,command_stage,s};return sony_dsf_route_commands_open(s->route,&s->commands,&ops);
}
int sony_dsf_native_owned_poll_commands(struct sony_dsf_native_owned *s){
    if(!s||s->closing||!s->commands)return -EINVAL;
    int rc=sony_command_server_poll(s->commands);return rc<0?live_failed(s,rc):rc;
}
int sony_dsf_native_owned_close(struct sony_dsf_native_owned **owner,struct sony_dsf_native_owned_cleanup *out){
    if(!owner||!*owner||!out)return -EINVAL;
    struct sony_dsf_native_owned *s=*owner;s->closing=1;
    if(s->commands)first(s,sony_command_server_close(&s->commands));
    if(s->commands){*out=s->cleanup;return s->cleanup_error?s->cleanup_error:-EBUSY;}
    if(s->playback){
        int rc=sony_dsf_native_playback_close(&s->playback,&s->cleanup.playback);
        if(rc!=-EBUSY||!s->playback)first(s,rc);
        if(s->playback){*out=s->cleanup;return s->cleanup_error?s->cleanup_error:-EBUSY;}
        s->quiesced=1;
    }else if(s->armed&&!s->quiesced){s->quiesced=1;first(s,s->callbacks.quiesce(s->callbacks.context));}
    if(s->native){
        if(!s->native_stopped){s->native_stopped=1;struct sony_dsd_sink_client *c=sony_dsd_sink_process_client(s->native);
            s->cleanup.native_shutdown=c?sony_dsd_sink_shutdown(c):-EIO;first(s,s->cleanup.native_shutdown);}
        int rc=sony_dsd_sink_process_stop(&s->native,1000,&s->cleanup.native);
        if(rc!=SONY_DSD_SINK_PROCESS_CLEANUP_PENDING)first(s,rc);
    }
    if(s->dsf){
        if(!s->dsf_stopped){s->dsf_stopped=1;struct sony_dsf_client *c=sony_dsf_process_client(s->dsf);
            s->cleanup.dsf_shutdown=c?sony_dsf_client_shutdown(c):-EIO;first(s,s->cleanup.dsf_shutdown);}
        int rc=sony_dsf_process_stop(&s->dsf,1000,&s->cleanup.dsf);
        if(rc!=SONY_DSF_PROCESS_CLEANUP_PENDING)first(s,rc);
    }
    if(s->native||s->dsf){*out=s->cleanup;return s->cleanup_error?s->cleanup_error:-EBUSY;}
    if(s->adapter&&s->resources_captured){
        s->cleanup.resource_readback=sony_dsd_power_linux_inspect(s->adapter,s->cleanup.resources_after);first(s,s->cleanup.resource_readback);
        s->cleanup.resource_mismatch=!s->cleanup.resource_readback&&memcmp(s->cleanup.resources_before,s->cleanup.resources_after,sizeof(s->cleanup.resources_before))!=0;
        if(s->cleanup.resource_mismatch)first(s,-ESTALE);
    }
    if(s->platform){
        first(s,sony_dsd_power_platform_state(s->platform,&s->cleanup.power));
        if(s->cleanup.power.token||(s->cleanup.power.acquired&&!s->cleanup.power.restored))first(s,-EUCLEAN);
        s->cleanup.platform_destroy=sony_dsd_power_platform_destroy(&s->platform);first(s,s->cleanup.platform_destroy);
        if(s->platform){*out=s->cleanup;return s->cleanup_error?s->cleanup_error:-EBUSY;}
    }
    if(s->adapter)first(s,sony_dsd_power_linux_destroy(&s->adapter));
    if(s->adapter){*out=s->cleanup;return s->cleanup_error?s->cleanup_error:-EBUSY;}
    if(s->route){
        int receipts=s->armed&&!s->cleanup_error&&!s->playback_fault;
        s->cleanup.route=sony_dsf_route_native_finish(&s->route,receipts?&s->cleanup.playback:NULL,
            receipts?&s->cleanup.dsf:NULL,receipts?&s->cleanup.native:NULL);first(s,s->cleanup.route);
    }
    *out=s->cleanup;
    if(s->route)return s->cleanup_error?s->cleanup_error:-EBUSY;
    int rc=s->cleanup_error;free(s);*owner=NULL;return rc;
}
