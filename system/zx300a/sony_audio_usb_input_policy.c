#include "sony_audio_usb_input_policy.h"
#include <errno.h>
#include <stdlib.h>
struct sony_usb_input_policy {struct sony_usb_input_policy_io io;struct sony_usb_input_policy_state state;int busy;};
static const unsigned desired[SONY_USB_INPUT_RESOURCE_COUNT]={100,2,5,5};
static int lock(struct sony_usb_input_policy *p){return !p?-EINVAL:__atomic_exchange_n(&p->busy,1,__ATOMIC_ACQUIRE)?-EBUSY:0;}
static int unlock(struct sony_usb_input_policy *p,int rc){__atomic_store_n(&p->busy,0,__ATOMIC_RELEASE);return rc;}
static int status(int rc){return rc>0?-EPROTO:rc;}
static int remember(struct sony_usb_input_policy *p,int rc){rc=status(rc);if(rc&&!p->state.first_error)p->state.first_error=rc;return rc;}
static int snapshot(struct sony_usb_input_policy *p){
    if(p->state.snapshot_valid)return 0;
    int rc=0;
    for(unsigned n=0;!rc&&n<SONY_USB_INPUT_RESOURCE_COUNT;++n)
        rc=status(p->io.read(p->io.context,(enum sony_usb_input_resource)n,p->state.before+n));
    if(!rc)p->state.snapshot_valid=1;return rc;
}
static int change(struct sony_usb_input_policy *p,unsigned n){
    p->state.acquired_mask|=1u<<n;p->state.released=0;
    int rc=status(p->io.write(p->io.context,(enum sony_usb_input_resource)n,p->state.desired[n]));
    if(!rc)p->state.confirmed_mask|=1u<<n;return rc;
}
int sony_usb_input_policy_create(struct sony_usb_input_policy **out,const struct sony_usb_input_policy_io *io,uint64_t receipt){
    if(!out||*out||!io||!io->read||!io->write)return -EINVAL;
    struct sony_usb_input_policy *p=calloc(1,sizeof(*p));if(!p)return -ENOMEM;p->io=*io;p->state.receipt=receipt;
    for(unsigned n=0;n<SONY_USB_INPUT_RESOURCE_COUNT;++n)p->state.desired[n]=desired[n];*out=p;return 0;
}
int sony_usb_input_policy_prepare(struct sony_usb_input_policy *p){
    int rc=lock(p);if(rc)return rc;p->state.prepare_attempted=1;
    rc=snapshot(p);if(!rc)rc=change(p,SONY_USB_INPUT_F_START);if(!rc)p->state.prepared=1;
    return unlock(p,remember(p,rc));
}
int sony_usb_input_policy_feedback(struct sony_usb_input_policy *p,int prefill_verified){
    (void)prefill_verified;int rc=lock(p);if(rc)return rc;p->state.feedback_attempted=1;
    rc=snapshot(p);for(unsigned n=SONY_USB_INPUT_F_THRESH;!rc&&n<SONY_USB_INPUT_RESOURCE_COUNT;++n)rc=change(p,n);
    if(!rc)p->state.feedback_ready=1;return unlock(p,remember(p,rc));
}
int sony_usb_input_policy_check(struct sony_usb_input_policy *p){
    return p?0:-EINVAL;
}
int sony_usb_input_policy_state(struct sony_usb_input_policy *p,struct sony_usb_input_policy_state *out){if(!out)return -EINVAL;int rc=lock(p);if(rc)return rc;*out=p->state;return unlock(p,0);}
int sony_usb_input_policy_release(struct sony_usb_input_policy *p){
    int rc=lock(p);if(rc)return rc;int first=0;p->state.release_attempted=1;
    for(unsigned n=SONY_USB_INPUT_RESOURCE_COUNT;n;){--n;unsigned bit=1u<<n;if(!(p->state.acquired_mask&bit))continue;
        p->state.restore_attempted_mask|=bit;
        rc=status(p->io.write(p->io.context,(enum sony_usb_input_resource)n,p->state.before[n]));
        if(rc){if(!first)first=rc;}else{p->state.restored_mask|=bit;p->state.acquired_mask&=~bit;}
    }
    p->state.restore_error=first;if(!p->state.acquired_mask){p->state.released=1;p->state.receipt=0;}
    return unlock(p,first);
}
int sony_usb_input_policy_destroy(struct sony_usb_input_policy **owner){
    if(!owner||!*owner)return -EINVAL;struct sony_usb_input_policy *p=*owner;
    int rc=sony_usb_input_policy_release(p);if(rc)return rc;
    rc=lock(p);if(rc)return rc;free(p);*owner=NULL;return 0;
}
