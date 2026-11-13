#define _GNU_SOURCE
#include "sony_audio_dsd_power_profiles.h"
#include <dlfcn.h>
#include <errno.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
enum phase {OPENING,OPENED,OPEN_FAILED,CLOSING,CLOSE_PENDING,DONE};
struct sony_dsd_power_bridge {
    unsigned int *object;
    void *power,*sysfs,*mixer,*pcm,*library;
    struct sony_dsd_power_backend backend;
    struct sony_dsd_power_report report;
    enum phase phase;
    int close_error;
    unsigned expected_mute_mask;
};
static __thread struct sony_dsd_power_bridge *active;
static int error(struct sony_dsd_power_bridge *b,int rc){
    if(rc>=0)rc=-EPROTO;
    if(!b->report.first_error)b->report.first_error=rc;
    return rc;
}
static int owned(struct sony_dsd_power_bridge *b){return b&&active==b;}
static int string_copy(const unsigned int *text,char *out,unsigned cap){
    if(!text||!out||!cap)return -EINVAL;
    const unsigned char *raw=(const void *)text;
    unsigned n=raw[0]&1?text[1]:raw[0]>>1;
    const char *p=raw[0]&1?(const char *)text[2]:(const char *)text+1;
    if(!p||n>=cap||(!(raw[0]&1)&&n>10)||p[n])return -EPROTO;
    memcpy(out,p,n+1);return 0;
}
int sony_dsd_power_bind_profile(struct sony_dsd_power_bridge **out,void *native,const struct sony_dsd_power_backend *backend,unsigned profile){
    if(!out||*out||!native||!backend||!backend->acquire||!backend->output_closed||!backend->release||profile>2)return -EINVAL;
    if(active)return -EBUSY;
    unsigned int *o=native;
    struct sony_dsd_power_bridge *b=calloc(1,sizeof(*b));if(!b)return -ENOMEM;
    b->object=o;b->power=(void *)o[41];b->sysfs=(void *)o[40];b->mixer=(void *)o[39];b->backend=*backend;b->phase=OPENING;
    b->report.plan=(struct sony_dsd_power_plan){.minimum_cpus=2,.minimum_khz=profile?1300000:1040000,
        .block_deep_idle=1,.low_latency=o[6],.timed_mute_ms={80,80,o[6]?180:80}};
    b->expected_mute_mask=7;
    *out=b;active=b;return 0;
}
int sony_dsd_power_bind(struct sony_dsd_power_bridge **out,void *native,const struct sony_dsd_power_backend *backend){
    return sony_dsd_power_bind_profile(out,native,backend,2);
}
void sony_dsd_power_set_library(struct sony_dsd_power_bridge *b,void *library){if(b)b->library=library;}
int sony_dsd_power_open_result(struct sony_dsd_power_bridge *b,int native){
    if(!owned(b)||b->phase!=OPENING)return -EINVAL;
    b->report.native_open_result=native;
    if(native&&!b->report.first_error)error(b,-EIO);
    b->phase=b->report.first_error?OPEN_FAILED:OPENED;return b->report.first_error;
}
int sony_dsd_power_begin_close(struct sony_dsd_power_bridge *b){
    if(!owned(b)||(b->phase!=OPENED&&b->phase!=OPEN_FAILED))return -EINVAL;
    b->phase=CLOSING;return 0;
}
int sony_dsd_power_close_result(struct sony_dsd_power_bridge *b,int native){
    if(!owned(b)||(b->phase!=CLOSING&&b->phase!=CLOSE_PENDING))return -EINVAL;
    if(b->phase==CLOSING){b->report.native_close_result=native;b->phase=CLOSE_PENDING;}
    if(native)error(b,-EIO);
    if(b->report.release_attempted)return b->report.release_status?SONY_DSD_POWER_UNCERTAIN:-EPROTO;
    int closed=b->backend.output_closed(b->backend.context);
    b->report.verify_closed_status=closed;
    if(closed){b->report.resource_retained=!!b->report.token;return SONY_DSD_POWER_UNCERTAIN;}
    if(b->report.token){
        b->report.release_attempted=1;
        int rc=b->backend.release(b->backend.context,b->report.token);
        if(rc>0)rc=-EPROTO;
        b->report.release_status=rc;
        if(rc){error(b,rc);b->report.resource_retained=1;return SONY_DSD_POWER_UNCERTAIN;}
        b->report.token=0;b->report.release_confirmed=1;
    }
    b->report.resource_retained=0;b->report.completed=1;b->phase=DONE;
    return b->close_error?b->close_error:native?-EIO:0;
}
int sony_dsd_power_state(struct sony_dsd_power_bridge *b,struct sony_dsd_power_report *out){
    if(!owned(b)||!out)return -EINVAL;
    *out=b->report;return 0;
}
int sony_dsd_power_destroy(struct sony_dsd_power_bridge **owner){
    if(!owner||!owned(*owner))return -EINVAL;
    struct sony_dsd_power_bridge *b=*owner;
    if(b->phase!=DONE||b->report.token||!b->report.completed)return -EBUSY;
    active=NULL;free(b);*owner=NULL;return 0;
}
int sony_native_dsd_acquire(void *) __asm__("_ZN3pst8services8funcarch8powermgr21PowerMgrServiceClient25AcquireCpuNumBaseFullLockEv");
int sony_native_dsd_acquire(void *self){
    struct sony_dsd_power_bridge *b=active;
    if(!b){int (*next)(void *)=dlsym(RTLD_NEXT,"_ZN3pst8services8funcarch8powermgr21PowerMgrServiceClient25AcquireCpuNumBaseFullLockEv");return next?next(self):1;}
    if(b->report.acquire_attempted)return b->report.acquire_confirmed?0:1;
    b->report.acquire_attempted=1;
    if(b->report.first_error)return 1;
    uint64_t token=0;int rc=b->backend.acquire(b->backend.context,&b->report.plan,&token);
    b->report.token=token;b->report.resource_retained=!!token;
    if(rc||!token){error(b,rc?rc:-EPROTO);return 1;}
    b->report.acquire_confirmed=1;return 0;
}
int sony_native_dsd_release(void *) __asm__("_ZN3pst8services8funcarch8powermgr21PowerMgrServiceClient25ReleaseCpuNumBaseFullLockEv");
int sony_native_dsd_release(void *self){
    struct sony_dsd_power_bridge *b=active;
    if(!b){int (*next)(void *)=dlsym(RTLD_NEXT,"_ZN3pst8services8funcarch8powermgr21PowerMgrServiceClient25ReleaseCpuNumBaseFullLockEv");return next?next(self):1;}
    ++b->report.native_release_request;
    /* Defer the actual restoration until the caller independently proves the
     * native PCM closed. A missing native Release after Open failure is handled
     * by close_result using the same backend receipt. */
    return 0;
}
int sony_native_dsd_sysfs(void *,const unsigned int *,const unsigned int *)
 __asm__("_ZN3pst8services8funcarch10SysFsUtils5WriteERKNSt3__112basic_stringIcNS3_11char_traitsIcEENS3_9allocatorIcEEEESB_");
int sony_native_dsd_sysfs(void *self,const unsigned int *path,const unsigned int *value){
    struct sony_dsd_power_bridge *b=active;char p[192];
    if(b&&!string_copy(path,p,sizeof(p))){
        if(!strcmp(p,"/sys/devices/system/cpu/cpu0/cpufreq/scaling_min_freq")){
            if(b->phase==OPENING)++b->report.cpu_request;else ++b->report.cpu_restore_request;return 0;
        }
        if(!strcmp(p,"/sys/power/dpidle_state")){
            if(b->phase==OPENING)++b->report.idle_block_request;else ++b->report.idle_release_request;return 0;
        }
        if(!strcmp(p,"/sys/module/snd_soc_cxd3778gf/parameters/timed_mute_ms"))return 0;
    }
    const char *symbol="_ZN3pst8services8funcarch10SysFsUtils5WriteERKNSt3__112basic_stringIcNS3_11char_traitsIcEENS3_9allocatorIcEEEESB_";
    int (*next)(void *,const unsigned int *,const unsigned int *)=b&&b->library?dlsym(b->library,symbol):NULL;
    if(!next)next=dlsym(RTLD_NEXT,symbol);
    return next?next(self,path,value):1;
}
int sony_native_dsd_latency(void *,const unsigned int *,const unsigned int *)
 __asm__("_ZN3pst8services8funcarch11AmixerUtils6SetStrERKNSt3__112basic_stringIcNS3_11char_traitsIcEENS3_9allocatorIcEEEESB_");
int sony_native_dsd_latency(void *self,const unsigned int *key,const unsigned int *value){
    struct sony_dsd_power_bridge *b=active;char k[64];
    if(b&&!string_copy(key,k,sizeof(k))&&!strcmp(k,"playback latency")){++b->report.latency_request;return 0;}
    const char *symbol="_ZN3pst8services8funcarch11AmixerUtils6SetStrERKNSt3__112basic_stringIcNS3_11char_traitsIcEENS3_9allocatorIcEEEESB_";
    int (*next)(void *,const unsigned int *,const unsigned int *)=b&&b->library?dlsym(b->library,symbol):NULL;
    if(!next)next=dlsym(RTLD_NEXT,symbol);
    return next?next(self,key,value):1;
}
int snd_pcm_open(void **pcm,const char *name,int stream,int mode){
    struct sony_dsd_power_bridge *b=active;
    int (*real_open)(void **,const char *,int,int)=dlsym(RTLD_NEXT,"snd_pcm_open");
    if(!real_open)return -ENOSYS;
    if(b)++b->report.pcm_open_forwarded;int rc=real_open(pcm,name,stream,mode);
    if(b&&rc<0)error(b,rc);
    if(b&&rc>=0)b->pcm=*pcm;
    return rc;
}
int snd_pcm_close(void *pcm){
    struct sony_dsd_power_bridge *b=active;
    int (*real_close)(void *)=dlsym(RTLD_NEXT,"snd_pcm_close");
    if(!real_close){if(b)b->close_error=-ENOSYS;return -ENOSYS;}
    if(b)++b->report.pcm_close_forwarded;int rc=real_close(pcm);
    if(b&&rc<0){b->close_error=rc;error(b,rc);}
    return rc;
}
