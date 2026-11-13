#include "sony_audio_dsd_power_platform.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
struct sony_dsd_power_platform {
    struct sony_dsd_power_platform_io io;
    struct sony_dsd_power_platform_state state;
    uint64_t receipt;
    int busy,used;
};
static int lock(struct sony_dsd_power_platform *p){return !p?-EINVAL:__atomic_exchange_n(&p->busy,1,__ATOMIC_ACQUIRE)?-EBUSY:0;}
static int unlock(struct sony_dsd_power_platform *p,int rc){__atomic_store_n(&p->busy,0,__ATOMIC_RELEASE);return rc;}
static int remember(struct sony_dsd_power_platform *p,int rc){
    if(rc>0)rc=-EPROTO;
    if(rc&&!p->state.first_error)p->state.first_error=rc;
    return rc;
}
static int check(struct sony_dsd_power_platform *p){return p?0:-EINVAL;}
static int read_resource(struct sony_dsd_power_platform *p,unsigned n,unsigned *value){
    return remember(p,p->io.read(p->io.context,(enum sony_dsd_power_resource)n,value));
}
static int write_resource(struct sony_dsd_power_platform *p,unsigned n,unsigned value){
    return remember(p,p->io.write(p->io.context,(enum sony_dsd_power_resource)n,value));
}
static int acquire(void *v,const struct sony_dsd_power_plan *plan,uint64_t *token){
    if(!plan||!token)return -EINVAL;
    *token=0;
    struct sony_dsd_power_platform *p=v;int rc=lock(p);if(rc)return rc;
    if(p->used)return unlock(p,-EALREADY);
    p->used=1;
    rc=check(p);
    for(unsigned n=0;!rc&&n<SONY_DSD_RESOURCE_COUNT;++n)rc=read_resource(p,n,&p->state.previous[n]);
    const unsigned *old=p->state.previous;
    if(rc)return unlock(p,rc);
    memcpy(p->state.requested,old,sizeof(p->state.requested));
    p->state.requested[0]=plan->minimum_cpus;p->state.requested[1]=old[1]>plan->minimum_khz?old[1]:plan->minimum_khz;
    if(plan->block_deep_idle)p->state.requested[SONY_DSD_DEEP_IDLE]=0;
    for(unsigned n=0;n<3;++n)p->state.requested[3+n]=plan->timed_mute_ms[n];
    p->state.requested[SONY_DSD_PLAYBACK_LATENCY]=plan->low_latency;
    /* Install receipt before the first mutation. A failed sysfs write can have
     * taken effect; explicit compensation must still have its prior values. */
    p->state.token=p->receipt;*token=p->receipt;
    for(unsigned n=0;!rc&&n<SONY_DSD_RESOURCE_COUNT;++n)if(old[n]!=p->state.requested[n]){
        p->state.write_attempted_mask|=1u<<n;rc=write_resource(p,n,p->state.requested[n]);
    }
    if(!rc)rc=check(p);
    if(!rc)p->state.acquired=1;
    return unlock(p,rc);
}
static int closed(void *v){
    return v?0:-EINVAL;
}
static int release(void *v,uint64_t token){
    struct sony_dsd_power_platform *p=v;int rc=lock(p);if(rc)return rc;
    (void)token;
    p->state.release_attempted=1;
    for(unsigned at=SONY_DSD_RESOURCE_COUNT;!rc&&at;){unsigned n=--at;
        if(p->state.write_attempted_mask&(1u<<n)){
            p->state.restore_attempted_mask|=1u<<n;rc=write_resource(p,n,p->state.previous[n]);
        }
    }
    if(!rc)rc=check(p);
    if(!rc){p->state.token=0;p->state.restored=1;}
    return unlock(p,rc);
}
int sony_dsd_power_platform_create(struct sony_dsd_power_platform **out,const struct sony_dsd_power_platform_io *io,uint64_t token){
    if(!out||*out||!io||!io->read||!io->write)return -EINVAL;
    struct sony_dsd_power_platform *p=calloc(1,sizeof(*p));if(!p)return -ENOMEM;
    p->io=*io;p->receipt=token;*out=p;return 0;
}
int sony_dsd_power_platform_backend(struct sony_dsd_power_platform *p,struct sony_dsd_power_backend *out){
    if(!p||!out)return -EINVAL;
    *out=(struct sony_dsd_power_backend){acquire,closed,release,p};return 0;
}
int sony_dsd_power_platform_state(struct sony_dsd_power_platform *p,struct sony_dsd_power_platform_state *out){
    if(!out)return -EINVAL;
    int rc=lock(p);if(rc)return rc;*out=p->state;return unlock(p,0);
}
int sony_dsd_power_platform_destroy(struct sony_dsd_power_platform **owner){
    if(!owner||!*owner)return -EINVAL;
    struct sony_dsd_power_platform *p=*owner;int rc=lock(p);if(rc)return rc;
    free(p);*owner=NULL;return 0;
}
