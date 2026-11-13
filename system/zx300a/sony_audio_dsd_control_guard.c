#define _GNU_SOURCE
#include "sony_audio_dsd_control_guard.h"
#include <dlfcn.h>
#include <errno.h>
#include <string.h>
enum action {NONBLOCK,DRAIN,DROP,HW_FREE,CLOSE};
static __thread struct sony_dsd_control_report *active;
static int fail(int error){if(!active->first_error)active->first_error=error;return active->first_error;}
int sony_dsd_control_begin(struct sony_dsd_control_report *report,void *pcm,unsigned op){
    if(active)return -EBUSY;
    if(!report||!pcm||op<SONY_DSD_CTL_DRAIN||op>SONY_DSD_CTL_CLOSE)return -EINVAL;
    memset(report,0,sizeof(*report));report->operation=op;active=report;return 0;
}
static int before(enum action action,void *pcm,int mode){
    (void)action;(void)pcm;(void)mode;++active->attempted;++active->step;return 0;
}
static int after(enum action action,int result){
    struct sony_dsd_control_report *r=active;++r->forwarded;
    if(action==NONBLOCK)++r->nonblock_calls;
    else if(action==DRAIN)++r->drain_calls;
    else if(action==DROP)++r->drop_calls;
    else if(action==HW_FREE)++r->hw_free_calls;
    else ++r->close_calls;
    if(result)fail(result<0?result:-EPROTO);
    return result;
}
int sony_dsd_control_end(int native){
    if(!active)return -EINVAL;
    struct sony_dsd_control_report *r=active;r->native_result=native;
    if(!r->first_error&&native)fail(-EIO);
    r->complete=!r->first_error;
    int result=r->first_error;active=NULL;return result;
}
int snd_pcm_nonblock(void *pcm,int mode){
    int (*real)(void *,int)=dlsym(RTLD_NEXT,"snd_pcm_nonblock");
    if(!active)return real?real(pcm,mode):-ENOSYS;
    int rc=before(NONBLOCK,pcm,mode);if(rc)return rc;return after(NONBLOCK,real?real(pcm,mode):-ENOSYS);
}
#define WRAP(name,action) \
int name(void *pcm){ \
    int (*real)(void *)=dlsym(RTLD_NEXT,#name); \
    if(!active)return real?real(pcm):-ENOSYS; \
    int rc=before(action,pcm,0);if(rc)return rc;return after(action,real?real(pcm):-ENOSYS); \
}
WRAP(snd_pcm_drain,DRAIN)
WRAP(snd_pcm_drop,DROP)
WRAP(snd_pcm_hw_free,HW_FREE)
WRAP(snd_pcm_close,CLOSE)
