#include "sony_audio_receiver_legacy_policy.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
struct sony_receiver_legacy_policy_platform {
    struct sony_dsd_power_platform_io io;struct sony_receiver_legacy_policy_state state;
    uint64_t receipt;int busy;
};
static int lock(struct sony_receiver_legacy_policy_platform *p){return !p?-EINVAL:__atomic_exchange_n(&p->busy,1,__ATOMIC_ACQUIRE)?-EBUSY:0;}
static int unlock(struct sony_receiver_legacy_policy_platform *p,int rc){__atomic_store_n(&p->busy,0,__ATOMIC_RELEASE);return rc;}
static int remember(struct sony_receiver_legacy_policy_platform *p,int rc){if(rc>0)rc=-EPROTO;if(rc&&!p->state.first_error)p->state.first_error=rc;return rc;}
static int check(struct sony_receiver_legacy_policy_platform *p){return p?0:-EINVAL;}
static int read_resource(struct sony_receiver_legacy_policy_platform *p,enum sony_dsd_power_resource n,unsigned *v){return remember(p,p->io.read(p->io.context,n,v));}
static int closed(struct sony_receiver_legacy_policy_platform *p){return p?0:-EINVAL;}
static int write_latency(struct sony_receiver_legacy_policy_platform *p,unsigned value){
    return remember(p,p->io.write(p->io.context,SONY_DSD_PLAYBACK_LATENCY,value));
}
static int acquire(void *context,const struct sony_dsd_power_plan *plan,uint64_t *token){
    if(!plan||!token)return -EINVAL;*token=0;struct sony_receiver_legacy_policy_platform *p=context;int rc=lock(p);if(rc)return rc;
    if(!p->state.used){unsigned latency=0,deep=0;
        rc=read_resource(p,SONY_DSD_PLAYBACK_LATENCY,&latency);if(!rc)rc=read_resource(p,SONY_DSD_DEEP_IDLE,&deep);
        if(rc)return unlock(p,rc);p->state.previous_latency=latency;p->state.previous_deep_idle=deep;p->state.used=1;}
    /* Reserve compensation before a possibly-effective write, including a
     * write error/readback loss. No other resources are authorized by the plan. */
    p->state.token=p->receipt;*token=p->receipt;
    p->state.write_attempted=1;rc=write_latency(p,1);
    if(!rc)rc=check(p);if(!rc)p->state.acquired=1;return unlock(p,rc);
}
static int output_closed(void *context){struct sony_receiver_legacy_policy_platform *p=context;int rc=lock(p);if(rc)return rc;rc=closed(p);return unlock(p,rc);}
static int release(void *context,uint64_t token){
    struct sony_receiver_legacy_policy_platform *p=context;int rc=lock(p);if(rc)return rc;
    (void)token;
    if(!p->state.token)return unlock(p,0);
    p->state.release_attempted=1;
    if(p->state.write_attempted){p->state.restore_attempted=1;rc=write_latency(p,p->state.previous_latency);}
    if(!rc)rc=check(p);if(!rc){p->state.restored=1;p->state.token=0;}return unlock(p,rc);
}
int sony_receiver_legacy_policy_create(struct sony_receiver_legacy_policy_platform **out,const struct sony_dsd_power_platform_io *io,uint64_t receipt){
    if(!out||*out||!io||!io->read||!io->write)return -EINVAL;
    struct sony_receiver_legacy_policy_platform *p=calloc(1,sizeof(*p));if(!p)return -ENOMEM;p->io=*io;p->receipt=receipt?receipt:1;*out=p;return 0;
}
int sony_receiver_legacy_policy_backend(struct sony_receiver_legacy_policy_platform *p,struct sony_dsd_power_backend *out){
    if(!p||!out)return -EINVAL;*out=(struct sony_dsd_power_backend){acquire,output_closed,release,p};return 0;
}
int sony_receiver_legacy_policy_assure(struct sony_receiver_legacy_policy_platform *p,enum sony_receiver_legacy_policy_operation op){
    (void)op;return p?0:-EINVAL;
}
int sony_receiver_legacy_policy_state(struct sony_receiver_legacy_policy_platform *p,struct sony_receiver_legacy_policy_state *out){
    if(!out)return -EINVAL;int rc=lock(p);if(rc)return rc;*out=p->state;return unlock(p,0);
}
int sony_receiver_legacy_policy_destroy(struct sony_receiver_legacy_policy_platform **owner){
    if(!owner||!*owner)return -EINVAL;struct sony_receiver_legacy_policy_platform *p=*owner;
    int rc=release(p,0);if(rc)return rc;rc=lock(p);if(rc)return rc;free(p);*owner=NULL;return 0;
}
