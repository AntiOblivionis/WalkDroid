/* Source queue feedback; called with a live parent and process_lock held.
 * Take only the source IRQ lock; no ALSA, allocation, usercopy or sleeping API.
 * While the source FD is open it supplies feedback; otherwise the caller
 * continues with the original ALSA feedback path.
 */
static void wd_source_feedback_init(struct wd_uac_source *s)
{
    memset(&s->feedback,0,sizeof(s->feedback));
    s->feedback.magic=SONY_SOURCE_QUEUE_MAGIC;s->feedback.version=1;
    s->feedback.bytes=sizeof(s->feedback);s->feedback.instance=s->queue.instance;
}
static int wd_source_feedback_clock(struct wd_uac_source *s,unsigned rate,unsigned nominal,
                                    unsigned threshold,unsigned plus,unsigned minus,unsigned *clock)
{
    struct sony_source_feedback_info f;struct sony_source_slot *front;
    unsigned long flags;unsigned target,queued,delta;u64 wide;
    if(!s)return 0;
    spin_lock_irqsave(&s->lock,flags);
    if(!s->consumer){spin_unlock_irqrestore(&s->lock,flags);return 0;}
    memset(&f,0,sizeof(f));f.magic=SONY_SOURCE_QUEUE_MAGIC;f.version=1;f.bytes=sizeof(f);
    f.instance=s->queue.instance;f.sample=s->feedback.sample;
    f.rate=rate;f.threshold=threshold;f.plus=plus;f.minus=minus;f.nominal=nominal;
    ++f.sample;
    f.error=s->queue.first_error;
    f.state=SONY_SOURCE_FEEDBACK_NOMINAL;
    f.clock=nominal;f.source_id=s->queue.active_source;
    front=s->queue.count?&s->queue.slots[s->queue.head]:NULL;
    if(front){
        /* The original500ms geometry remains the minimum target. The finite
         * preclaim prefix is retained as a reservoir; do not convert8MiB into
         * an artificial multi-second clock target. Published consumption,
         * not freed ring capacity or a pending usercopy, drives feedback. */
        wide=div_u64((u64)(rate/2)*threshold,100);
        target=wide>0xffffffffu?0xffffffffu:(unsigned)wide;
        if(s->feedback_initial_frames>target)target=s->feedback_initial_frames>0xffffffffu?0xffffffffu:(unsigned)s->feedback_initial_frames;
        wide=front->accepted>=front->delivered?div_u64(front->accepted-front->delivered,8):0;
        queued=wide>0xffffffffu?0xffffffffu:(unsigned)wide;
        f.state=SONY_SOURCE_FEEDBACK_TRACKING;f.initial_frames=s->feedback_initial_frames;
        f.queued_frames=queued;f.target_frames=target;
        /* Keep arithmetic bounded for caller-supplied feedback settings. */
        wide=div_u64((u64)nominal*(queued<target?plus:minus),10000);
        delta=wide>0xffffu?0xffffu:(unsigned)wide;
        if(queued<target)f.clock=nominal>0xffffffffu-delta?0xffffffffu:nominal+delta;
        else f.clock=nominal<delta?0:nominal-delta;
    }
    s->feedback=f;*clock=f.clock;
    spin_unlock_irqrestore(&s->lock,flags);
    return 1;
}
