#include "sony_audio_usb_gadget_policy.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>

struct sony_usb_gadget_policy {
    struct sony_usb_gadget_io io;
    struct sony_usb_gadget_state state;
    int busy;
};
static int lock(struct sony_usb_gadget_policy *p){return !p?-EINVAL:__atomic_exchange_n(&p->busy,1,__ATOMIC_ACQUIRE)?-EBUSY:0;}
static int unlock(struct sony_usb_gadget_policy *p,int rc){__atomic_store_n(&p->busy,0,__ATOMIC_RELEASE);return rc;}
static int status(int rc){return rc>0?-EPROTO:rc;}
static int remember(struct sony_usb_gadget_policy *p,int rc){rc=status(rc);if(rc&&!p->state.first_error)p->state.first_error=rc;return rc;}
static int snapshot(struct sony_usb_gadget_policy *p){
    if(p->state.snapshot_valid)return 0;
    const unsigned attrs[]={SONY_USB_GADGET_ENABLE,SONY_USB_GADGET_FUNCTIONS};
    for(unsigned i=0;i<sizeof(attrs)/sizeof(attrs[0]);++i){
        unsigned n=attrs[i];char *value=p->state.before.value[n];
        memset(value,0,SONY_USB_GADGET_TEXT_BYTES);
        int rc=status(p->io.read(p->io.context,(enum sony_usb_gadget_attr)n,value,SONY_USB_GADGET_TEXT_BYTES));
        if(rc)return rc;value[SONY_USB_GADGET_TEXT_BYTES-1]=0;
    }
    p->state.snapshot_valid=1;return 0;
}
static int change(struct sony_usb_gadget_policy *p,unsigned n,const char *value){
    p->state.pending_write=1;++p->state.writes_attempted;
    int rc=status(p->io.write(p->io.context,(enum sony_usb_gadget_attr)n,value));
    if(!rc){p->state.pending_write=0;++p->state.writes_confirmed;}return rc;
}
int sony_usb_gadget_policy_create(struct sony_usb_gadget_policy **out,const struct sony_usb_gadget_io *io,uint64_t receipt){
    if(!out||*out||!io||!io->read||!io->write)return -EINVAL;
    struct sony_usb_gadget_policy *p=calloc(1,sizeof(*p));if(!p)return -ENOMEM;
    p->io=*io;p->state.receipt=receipt;*out=p;return 0;
}
int sony_usb_gadget_policy_prepare(struct sony_usb_gadget_policy *p){
    int rc=lock(p);if(rc)return rc;p->state.prepare_attempted=1;
    rc=snapshot(p);return unlock(p,remember(p,rc));
}
int sony_usb_gadget_policy_enter(struct sony_usb_gadget_policy *p){
    int rc=lock(p);if(rc)return rc;p->state.enter_attempted=1;
    rc=snapshot(p);
    if(!rc){p->state.restored=0;p->state.restore_stage=0;p->state.restore_error=0;}
    if(!rc)rc=change(p,SONY_USB_GADGET_ENABLE,"0");
    if(!rc)rc=change(p,SONY_USB_GADGET_FUNCTIONS,"audio_func,ffs");
    if(!rc)rc=change(p,SONY_USB_GADGET_ENABLE,"1");
    if(!rc)p->state.active=1;
    return unlock(p,remember(p,rc));
}
int sony_usb_gadget_policy_check(struct sony_usb_gadget_policy *p){return p?0:-EINVAL;}
int sony_usb_gadget_policy_restore(struct sony_usb_gadget_policy *p){
    int rc=lock(p);if(rc)return rc;
    if(!p->state.writes_attempted||p->state.restored){p->state.restored=1;p->state.active=0;return unlock(p,0);}
    p->state.restore_started=1;
    const unsigned attrs[]={SONY_USB_GADGET_ENABLE,SONY_USB_GADGET_FUNCTIONS,SONY_USB_GADGET_ENABLE};
    const char *values[]={"0",p->state.before.value[SONY_USB_GADGET_FUNCTIONS],p->state.before.value[SONY_USB_GADGET_ENABLE]};
    while(!rc&&p->state.restore_stage<3){
        rc=change(p,attrs[p->state.restore_stage],values[p->state.restore_stage]);
        if(!rc)++p->state.restore_stage;
    }
    p->state.restore_error=rc;
    if(!rc){p->state.restored=1;p->state.active=0;}
    return unlock(p,rc);
}
int sony_usb_gadget_policy_state(struct sony_usb_gadget_policy *p,struct sony_usb_gadget_state *out){
    if(!out)return -EINVAL;int rc=lock(p);if(rc)return rc;*out=p->state;return unlock(p,0);
}
int sony_usb_gadget_policy_destroy(struct sony_usb_gadget_policy **owner){
    if(!owner||!*owner)return -EINVAL;struct sony_usb_gadget_policy *p=*owner;
    int rc=sony_usb_gadget_policy_restore(p);if(rc)return rc;
    rc=lock(p);if(rc)return rc;free(p);*owner=NULL;return 0;
}
