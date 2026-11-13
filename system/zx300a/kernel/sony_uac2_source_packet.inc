/* Convert a saved packet and append it to the current source queue. */
static void wd_source_collect_packet(struct audio_dev *a,const struct uac2_uevent *m)
{
    struct wd_uac_source *s=a->wd_source;unsigned bytes=0;
    unsigned long flags;int rc;
    if(!s||!s->packet_bytes)return;
    rc=sony_source_queue_pack_pcm32(s->pcm,SONY_SOURCE_QUEUE_MAX_READ,s->raw,s->packet_bytes,s->subslot,wd_uac_bits(m),&bytes);
    if(rc){wd_source_error(s,rc);return;}
    spin_lock_irqsave(&s->lock,flags);
    rc=sony_source_queue_append(&s->queue,0,s->pcm,bytes);
    if(rc&&!s->queue.first_error)sony_source_queue_abort(&s->queue,rc);
    spin_unlock_irqrestore(&s->lock,flags);
    wake_up_interruptible(&s->ready);
}
