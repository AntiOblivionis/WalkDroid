/* Source integration candidate, not a bootable kernel or deployment proof.
 * Source adapter for the exact Sony3.10 UAC2 driver, included after its event
 * accessors. Requires the accompanying driver patch. Not a loadable probe and
 * not a claim that the unmodified B90 kernel provides this interface.
 * Lock order: original prm->lock -> wd_uac_data_lock. Events serialize with
 * wd_uac_event_lock -> wd_uac_data_lock. No path takes prm or event from data.
 * Actual ring memcpy and its cursor reservation hold BOTH prm and data locks.
 */
#include "sony_audio_usb_boundary.c"
static DEFINE_SPINLOCK(wd_uac_data_lock);
static DEFINE_SPINLOCK(wd_uac_event_lock);
static struct sony_usb_boundary wd_uac_boundary = {
    .magic=SONY_USB_BOUNDARY_MAGIC,.version=SONY_USB_BOUNDARY_VERSION,
    .bytes=SONY_USB_BOUNDARY_BYTES
};
static u32 wd_uac_next_sequence;
static int wd_uac_quiescing;
#include "sony_uac2_lifetime.inc"
static struct {
    struct audio_dev *owner;
    struct usb_request *request;
    void *previous;
    void (*previous_complete)(struct usb_ep *,struct usb_request *);
    sony_ub_u64 instance,generation;
    unsigned alt;
    int pending;
} wd_uac_rate;
/* Called under data_lock. A canceled EP0 cannot reopen an old altsetting. */
static struct audio_dev *wd_uac_rate_release_locked(void)
{
    struct usb_request *req=wd_uac_rate.request;
    struct audio_dev *owner=wd_uac_rate.owner;
    if(req&&req->context==&wd_uac_rate){
        req->context=wd_uac_rate.previous;
        req->complete=wd_uac_rate.previous_complete;
    }
    wd_uac_rate.pending=0;wd_uac_rate.owner=NULL;wd_uac_rate.request=NULL;
    return owner; /* pending loan transfers to the caller */
}
static void wd_uac_rate_cancel(struct audio_dev *owner,int error)
{
    unsigned long flags;struct audio_dev *drop=NULL;
    spin_lock_irqsave(&wd_uac_data_lock,flags);
    /* Invalidates a completion which already took the pending loan but has
     * not yet acquired process_lock. No delayed control may reopen alt0. */
    if(owner->wd_clock_generation==~(u64)0){
        if(!wd_uac_boundary.error)wd_uac_boundary.error=-EOVERFLOW;
        wd_uac_quiescing=1;
    }else ++owner->wd_clock_generation;
    if(wd_uac_rate.pending&&wd_uac_rate.owner==owner){
        drop=wd_uac_rate_release_locked();
        if(error){
            if(!wd_uac_boundary.error)wd_uac_boundary.error=error<0?error:-EIO;
            wd_uac_quiescing=1;
        }
    }
    spin_unlock_irqrestore(&wd_uac_data_lock,flags);
    if(drop)wd_parent_put(drop);
}
static void wd_uac_rate_abort(struct usb_request *req,int error)
{
    unsigned long flags;struct audio_dev *drop=NULL;
    spin_lock_irqsave(&wd_uac_data_lock,flags);
    if(wd_uac_rate.pending&&wd_uac_rate.request==req){
        drop=wd_uac_rate_release_locked();
        if(!wd_uac_boundary.error)wd_uac_boundary.error=error<0?error:-EIO;
        wd_uac_quiescing=1;
    }
    spin_unlock_irqrestore(&wd_uac_data_lock,flags);
    if(drop)wd_parent_put(drop);
}
static void wd_uac_fail(int error)
{
    unsigned long flags;
    spin_lock_irqsave(&wd_uac_data_lock,flags);
    if(!wd_uac_boundary.error)wd_uac_boundary.error=error<0?error:-EIO;
    wd_uac_quiescing=1;
    if(agdev_g)wd_source_error(agdev_g->wd_source,error<0?error:-EIO);
    spin_unlock_irqrestore(&wd_uac_data_lock,flags);
}
static unsigned wd_uac_format(const struct uac2_uevent *m)
{
    return m->format==FORMAT_PCM?1:m->format==FORMAT_DSD?2:m->format==FORMAT_DOP?3:0;
}
static unsigned wd_uac_bits(const struct uac2_uevent *m)
{
    return m->format==FORMAT_PCM?m->bitwidth:1;
}
static void wd_uac_quiesce(int value)
{
    unsigned long flags;
    spin_lock_irqsave(&wd_uac_data_lock,flags);wd_uac_quiescing=value;
    spin_unlock_irqrestore(&wd_uac_data_lock,flags);
}
static int wd_uac_is_quiescing(void)
{
    unsigned long flags;int result;
    spin_lock_irqsave(&wd_uac_data_lock,flags);result=wd_uac_quiescing;
    spin_unlock_irqrestore(&wd_uac_data_lock,flags);return result;
}
static int wd_uac_bind_boundary(void)
{
    unsigned long flags;int rc;
    spin_lock_irqsave(&wd_uac_data_lock,flags);
    rc=sony_usb_boundary_bind(&wd_uac_boundary);if(!rc)wd_uac_quiescing=0;
    spin_unlock_irqrestore(&wd_uac_data_lock,flags);return rc;
}
static void wd_uac_unbind_boundary(void)
{
    unsigned long flags;
    spin_lock_irqsave(&wd_uac_data_lock,flags);wd_uac_quiescing=1;
    if(wd_uac_boundary.flags&SONY_USB_BOUNDARY_BOUND)
        sony_usb_boundary_unbind(&wd_uac_boundary);
    spin_unlock_irqrestore(&wd_uac_data_lock,flags);
}
/* Caller holds event lock; metadata freezes BEFORE the legacy event is sent.
 * No GFP_KERNEL/shared uac2->msg allocation inside IRQ/spinlock context. The
 * original four ASCII fields and sequence ordering remain unchanged. */
static int wd_uac_publish_one(struct snd_uac2_chip *uac2,
                             struct uac2_uevent *m,unsigned reason)
{
    unsigned long flags;u32 seq;int rc,len;
    char message[128];
    spin_lock_irqsave(&wd_uac_data_lock,flags);
    if(m->action==ACTION_PLAY&&wd_uac_quiescing){
        spin_unlock_irqrestore(&wd_uac_data_lock,flags);return -ECANCELED;
    }
    seq=wd_uac_next_sequence;
    if(m->action==ACTION_PLAY)
        rc=sony_usb_boundary_play(&wd_uac_boundary,seq,
                    wd_uac_format(m),m->freq,wd_uac_bits(m));
    else rc=sony_usb_boundary_stop(&wd_uac_boundary,seq,reason,
                    reason==SONY_USB_BOUNDARY_ERROR?-EPROTO:0);
    if(!rc){
        wd_source_event(container_of(uac2,struct audio_dev,uac2),m,seq,reason,
                        reason==SONY_USB_BOUNDARY_ERROR?-EPROTO:0);
        ++wd_uac_next_sequence;
    }
    else {if(!wd_uac_boundary.error)wd_uac_boundary.error=rc;wd_uac_quiescing=1;}
    spin_unlock_irqrestore(&wd_uac_data_lock,flags);
    if(rc)return rc;
    len=scnprintf(message,sizeof(message),"%s%s%s%s",
        UAC2EVENT[uac2_event_map(m->action)].s,UAC2EVENT[uac2_event_map(m->format)].s,
        UAC2EVENT[uac2_event_map(m->freq)].s,UAC2EVENT[uac2_event_map(m->bitwidth)].s);
    rc=uac2_netlink_send(uac2->sock,message,len,seq);
    if(!rc)uac2_uevent_store_msg(uac2,m);
    else {
        spin_lock_irqsave(&wd_uac_data_lock,flags);
        if(!wd_uac_boundary.error)wd_uac_boundary.error=rc<0?rc:-EIO;
        wd_uac_quiescing=1;
        if(agdev_g)wd_source_error(agdev_g->wd_source,rc<0?rc:-EIO);
        spin_unlock_irqrestore(&wd_uac_data_lock,flags);
    }
    return rc;
}
static int wd_uac_publish(struct snd_uac2_chip *uac2,struct uac2_uevent *m,unsigned reason)
{
    unsigned long flags;int rc=0;
    /* FINAL stays immutable; disconnect is a separate terminal error even
     * when the legacy STOP event has already been cached. */
    if(reason==SONY_USB_BOUNDARY_DISCONNECT)wd_uac_fail(-ENODEV);
    spin_lock_irqsave(&wd_uac_event_lock,flags);
    if(m->action==uac2->uevent.action&&m->format==uac2->uevent.format&&
       m->freq==uac2->uevent.freq&&m->bitwidth==uac2->uevent.bitwidth)
        goto out;
    if(m->action==ACTION_PLAY&&uac2->uevent.action==ACTION_PLAY){
        struct uac2_uevent stop={ACTION_STOP,FORMAT_NONE,BIT_NONE,FREQ_NONE};
        rc=wd_uac_publish_one(uac2,&stop,SONY_USB_BOUNDARY_CHANGE);
    }
    if(!rc)rc=wd_uac_publish_one(uac2,m,reason);
out:
    spin_unlock_irqrestore(&wd_uac_event_lock,flags);return rc;
}
static ssize_t wd_uac_boundary_show(char *buf)
{
    struct sony_usb_boundary snapshot;
    unsigned long flags;
    spin_lock_irqsave(&wd_uac_data_lock,flags);snapshot=wd_uac_boundary;
    spin_unlock_irqrestore(&wd_uac_data_lock,flags);
    return sony_usb_boundary_encode(&snapshot,buf,PAGE_SIZE);
}
