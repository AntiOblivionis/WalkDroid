/* Request, callback and parent lifetimes for the exact Sony UAC2 source.
 * Slot owns one context reference. Each successful submission owns another
 * until its callback RETURNS; bus_refs ends earlier, at callback entry after
 * UDC DMA unmap. No context lock spans a USB API. process_lock serializes
 * submission against retirement; synchronous givebacks defer their processing
 * until the submitting API has returned and that gate has been released.
 * Parent destruction runs in work context after all child/control/feedback
 * references finish, and blocks publication of a replacement parent meanwhile.
 */
static void wd_uac_fail(int error);
static int wd_uac_is_quiescing(void);
static void wd_agdev_iso_process(struct usb_ep *,struct usb_request *);
static void wd_audio_cleanup_work(struct work_struct *);
static unsigned wd_parent_retiring;
struct wd_uac_req_ctx {
    struct uac2_req ur;
    struct audio_dev *parent;
    struct usb_ep *ep;
    struct kref refs;
    spinlock_t lock;
    unsigned queued,retiring,active,submitting,pending;
    sony_ub_u64 queue_sequence;
};
static void wd_parent_release(struct kref *ref)
{
    struct audio_dev *a=container_of(ref,struct audio_dev,wd_refs);
    schedule_work(&a->wd_cleanup_work);
}
static void wd_parent_put(struct audio_dev *a){kref_put(&a->wd_refs,wd_parent_release);}
static int wd_parent_get(struct audio_dev *a,int closing_ok)
{
    unsigned long flags;int ok;
    spin_lock_irqsave(&wd_uac_data_lock,flags);
    ok=a&&agdev_g==a&&(closing_ok||!a->wd_closing);
    if(ok)kref_get(&a->wd_refs);
    spin_unlock_irqrestore(&wd_uac_data_lock,flags);return ok;
}
static void wd_parent_initialize(struct audio_dev *a)
{
    kref_init(&a->wd_refs);INIT_WORK(&a->wd_cleanup_work,wd_audio_cleanup_work);
    init_completion(&a->wd_platform_released);
    spin_lock_init(&a->wd_process_lock);
    spin_lock_init(&a->uac2.c_prm.lock);spin_lock_init(&a->uac2.p_prm.lock);
}
static struct audio_dev *wd_parent_current(int closing_ok)
{
    struct audio_dev *a;unsigned long flags;
    spin_lock_irqsave(&wd_uac_data_lock,flags);a=agdev_g;
    if(a&&(closing_ok||!a->wd_closing))kref_get(&a->wd_refs);else a=NULL;
    spin_unlock_irqrestore(&wd_uac_data_lock,flags);return a;
}
static int wd_parent_publish(struct audio_dev *a)
{
    unsigned long flags;int rc=0;
    spin_lock_irqsave(&wd_uac_data_lock,flags);
    if(agdev_g||wd_parent_retiring||(wd_uac_boundary.flags&SONY_USB_BOUNDARY_BOUND))rc=-EBUSY;
    else agdev_g=a;
    spin_unlock_irqrestore(&wd_uac_data_lock,flags);return rc;
}
static void wd_parent_detach(struct audio_dev *a)
{
    unsigned long flags;int drop=0;
    spin_lock_irqsave(&wd_uac_data_lock,flags);
    if(agdev_g==a){a->wd_closing=1;agdev_g=NULL;++wd_parent_retiring;drop=1;}
    spin_unlock_irqrestore(&wd_uac_data_lock,flags);
    if(drop)wd_parent_put(a);
}
static int wd_parent_can_enable(struct audio_dev *a)
{
    unsigned long flags;int rc=0;
    spin_lock_irqsave(&wd_uac_data_lock,flags);
    if(agdev_g!=a||a->wd_closing||!(wd_uac_boundary.flags&SONY_USB_BOUNDARY_BOUND))rc=-ENODEV;
    else if(wd_uac_boundary.error)rc=wd_uac_boundary.error;
    else if(a->wd_bus_refs)rc=-EBUSY;
    spin_unlock_irqrestore(&wd_uac_data_lock,flags);return rc;
}
static void wd_request_release(struct kref *ref)
{
    struct wd_uac_req_ctx *c=container_of(ref,struct wd_uac_req_ctx,refs);
    struct audio_dev *a=c->parent;unsigned long flags;
    /* A missing completion is retained, never converted into permission to
     * free DMA/context. This branch indicates an internal ownership bug. */
    if(c->queued||c->active){wd_uac_fail(-EUCLEAN);return;}
    usb_ep_free_request(c->ep,c->ur.req);
    spin_lock_irqsave(&wd_uac_data_lock,flags);
    if(a->wd_requests)--a->wd_requests;
    else if(!wd_uac_boundary.error)wd_uac_boundary.error=-EUCLEAN;
    spin_unlock_irqrestore(&wd_uac_data_lock,flags);
    kfree(c);wd_parent_put(a);
}
static void wd_request_put(struct wd_uac_req_ctx *c){kref_put(&c->refs,wd_request_release);}
static void agdev_iso_complete(struct usb_ep *ep,struct usb_request *req);
static struct wd_uac_req_ctx *wd_request_alloc(struct audio_dev *a,struct uac2_rtd_params *prm,struct usb_ep *ep)
{
    struct usb_request *req;struct wd_uac_req_ctx *c;unsigned long flags;
    if(!wd_parent_get(a,0))return ERR_PTR(-ENODEV);
    req=usb_ep_alloc_request(ep,GFP_ATOMIC);
    if(!req){wd_parent_put(a);return ERR_PTR(-ENOMEM);}
    c=kzalloc(sizeof(*c),GFP_ATOMIC);
    if(!c){usb_ep_free_request(ep,req);wd_parent_put(a);return ERR_PTR(-ENOMEM);}
    c->ur.pp=prm;c->ur.req=req;c->parent=a;c->ep=ep;
    kref_init(&c->refs);spin_lock_init(&c->lock);
    req->context=c;req->complete=agdev_iso_complete;
    spin_lock_irqsave(&wd_uac_data_lock,flags);++a->wd_requests;
    spin_unlock_irqrestore(&wd_uac_data_lock,flags);
    /* Returned reference belongs to the creator until submission finishes. */
    return c;
}
static int wd_request_install(struct wd_uac_req_ctx *c,unsigned slot)
{
    struct uac2_rtd_params *prm=c->ur.pp;unsigned long flags;int rc=0;
    spin_lock_irqsave(&prm->lock,flags);
    if(slot>=USB_XFERS||!prm->ep_enabled||prm->ureq[slot].req)rc=-ESTALE;
    else {kref_get(&c->refs);prm->ureq[slot]=c->ur;}
    spin_unlock_irqrestore(&prm->lock,flags);return rc;
}
static int wd_request_retired(struct wd_uac_req_ctx *c)
{
    unsigned long flags;int value;
    spin_lock_irqsave(&c->lock,flags);value=c->retiring;
    spin_unlock_irqrestore(&c->lock,flags);return value;
}
static void wd_request_finish(struct wd_uac_req_ctx *c);
static int wd_request_queue(struct wd_uac_req_ctx *c,int process_held)
{
    unsigned long flags,pflags=0;sony_ub_u64 sequence=0;int rc=0,drop=0,pending;
    if(!process_held)spin_lock_irqsave(&c->parent->wd_process_lock,pflags);
    spin_lock_irqsave(&c->lock,flags);
    if(c->retiring)rc=-ECANCELED;
    else if(c->queued)rc=-EBUSY;
    else if(c->queue_sequence==~(sony_ub_u64)0)rc=-EOVERFLOW;
    else {
        sequence=++c->queue_sequence;kref_get(&c->refs);c->queued=1;c->submitting=1;
        spin_lock(&wd_uac_data_lock);++c->parent->wd_bus_refs;spin_unlock(&wd_uac_data_lock);
    }
    spin_unlock_irqrestore(&c->lock,flags);
    if(rc){
        if(!process_held)spin_unlock_irqrestore(&c->parent->wd_process_lock,pflags);
        return rc;
    }
    /* Caller owns a creator or active-callback reference across this call. */
    rc=usb_ep_queue(c->ep,c->ur.req,GFP_ATOMIC);
    spin_lock_irqsave(&c->lock,flags);c->submitting=0;
    pending=c->pending;c->pending=0;
    if(rc){
        if(c->queued&&c->queue_sequence==sequence){
            c->queued=0;drop=1;spin_lock(&wd_uac_data_lock);
            if(c->parent->wd_bus_refs)--c->parent->wd_bus_refs;
            else if(!wd_uac_boundary.error)wd_uac_boundary.error=-EUCLEAN;
            spin_unlock(&wd_uac_data_lock);
        }
    }
    spin_unlock_irqrestore(&c->lock,flags);
    if(rc)wd_uac_fail(drop?rc:-EUCLEAN);
    if(!process_held)spin_unlock_irqrestore(&c->parent->wd_process_lock,pflags);
    if(drop)wd_request_put(c);
    /* Initial activation holds process_lock but is quiesced, so this path
     * only retires the deferred loan; the activation will reject an empty
     * initial queue. Streaming submissions release process_lock first. */
    if(pending)wd_request_finish(c);
    return rc;
}
static void wd_request_finish(struct wd_uac_req_ctx *c)
{
    struct audio_dev *a=c->parent;unsigned long flags,process_flags;int rc;
    if(!wd_request_retired(c)&&!wd_uac_is_quiescing()){
        spin_lock_irqsave(&a->wd_process_lock,process_flags);
        if(!wd_request_retired(c)&&!wd_uac_is_quiescing()&&!a->wd_closing)
            wd_agdev_iso_process(c->ep,c->ur.req);
        spin_unlock_irqrestore(&a->wd_process_lock,process_flags);
    }
    /* This caller holds neither process nor context lock. The queue helper
     * reacquires the process gate and handles synchronous giveback deferral. */
    if(!wd_request_retired(c)&&!wd_uac_is_quiescing()&&c->ur.pp->ep_enabled){
        rc=wd_request_queue(c,0);if(rc&&rc!=-ECANCELED)wd_uac_fail(rc);
    }
    spin_lock_irqsave(&c->lock,flags);--c->active;
    spin_unlock_irqrestore(&c->lock,flags);wd_request_put(c);
}
static void agdev_iso_complete(struct usb_ep *ep,struct usb_request *req)
{
    struct wd_uac_req_ctx *c=req->context;
    struct audio_dev *a=c->parent;unsigned long flags;int pending;
    /* The submission reference exists before UDC queueing, including the
     * list-del/unmap-to-entry gap and synchronous callbacks. */
    spin_lock_irqsave(&c->lock,flags);
    if(c->ep!=ep||c->ur.req!=req||!c->queued){
        spin_unlock_irqrestore(&c->lock,flags);wd_uac_fail(-EPROTO);return;
    }
    c->queued=0;++c->active;
    pending=c->submitting;if(pending)c->pending=1;
    spin_lock(&wd_uac_data_lock);
    if(a->wd_bus_refs)--a->wd_bus_refs;
    else if(!wd_uac_boundary.error)wd_uac_boundary.error=-EUCLEAN;
    spin_unlock(&wd_uac_data_lock);spin_unlock_irqrestore(&c->lock,flags);
    if(!pending)wd_request_finish(c);
}
static void wd_retire_requests(struct uac2_rtd_params *prm,struct usb_ep *ep)
{
    struct wd_uac_req_ctx *old[USB_XFERS]={0};unsigned long flags;unsigned i;
    spin_lock_irqsave(&prm->lock,flags);prm->ep_enabled=0;
    for(i=0;i<USB_XFERS;++i)if(prm->ureq[i].req){
        old[i]=prm->ureq[i].req->context;
        spin_lock(&old[i]->lock);old[i]->retiring=1;spin_unlock(&old[i]->lock);
        prm->ureq[i].req=NULL;
    }
    spin_unlock_irqrestore(&prm->lock,flags);
    /* The MTK QMU submits more than the list head to hardware. Dequeuing a
     * non-head request can give it back without flushing its DMA descriptor.
     * Disabling the endpoint flushes the queue before giving requests back.
     * Keep the slot references until that operation has returned; any late
     * completion still owns its separate submission reference. */
    usb_ep_disable(ep);
    for(i=0;i<USB_XFERS;++i)if(old[i])wd_request_put(old[i]);
}
