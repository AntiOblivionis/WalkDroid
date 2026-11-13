/* Source transport integration. Parent/request references protect producers;
 * an open consumer owns an independent source reference across parent teardown.
 * Lock order: parent process -> boundary data -> source lock. Source readers
 * may take only source lock. Packet scratch is protected by process_lock.
 */
#include <linux/vmalloc.h>
#include <linux/miscdevice.h>
#include <linux/mutex.h>
#include <linux/poll.h>
#include <linux/uaccess.h>
#include "sony_audio_usb_source_queue.c"
#include "sony_audio_usb_source_device.h"
#define WD_SOURCE_RAW_MAX (QMU_PACKAGE_NUM*QMU_PACKAGE_LENGTH)
struct wd_uac_source {
    spinlock_t lock;
    struct kref refs;
    struct mutex read_mutex;
    wait_queue_head_t ready;
    struct miscdevice misc;
    struct sony_source_queue queue;
    unsigned char *storage,*raw,*pcm,*record_buffer;
    unsigned packet_bytes,subslot,parent_live,registered,consumer,consumer_claimed;
    sony_sq_u64 consumer_source;
    sony_sq_u64 feedback_source,feedback_initial_frames;
    struct sony_source_feedback_info feedback;
};
#include "sony_uac2_source_feedback.inc"
#include "sony_uac2_source_device.inc"
/* Called in the SAME publication lock as the original sequence transition,
 * before its netlink message. Queue failure remains explicit in this new
 * collector, and never silently changes the existing legacy ALSA recovery. */
static void wd_source_event(struct audio_dev *a,const struct uac2_uevent *m,u32 seq,unsigned reason,int error)
{
    struct wd_uac_source *s=a->wd_source;unsigned long flags;int rc;
    if(!s)return;spin_lock_irqsave(&s->lock,flags);
    if(m->action==ACTION_PLAY)rc=sony_source_queue_begin(&s->queue,seq,wd_uac_format(m),m->freq,wd_uac_bits(m));
    else rc=sony_source_queue_end(&s->queue,seq,reason,error);
    if(rc&&!s->queue.first_error)sony_source_queue_abort(&s->queue,rc);
    spin_unlock_irqrestore(&s->lock,flags);
    wake_up_interruptible(&s->ready);
}
static void wd_source_error(struct wd_uac_source *s,int error)
{
    unsigned long flags;if(!s)return;spin_lock_irqsave(&s->lock,flags);
    sony_source_queue_abort(&s->queue,error<0?error:-EPROTO);spin_unlock_irqrestore(&s->lock,flags);
    wake_up_interruptible(&s->ready);
}
/* Preserve raw ISO payload BEFORE the legacy compactor/converter mutates the
 * request buffer or req->actual. Do not compact QMU a second time in-place. */
static void wd_source_save_packet(struct audio_dev *a,struct usb_request *req,unsigned subslot)
{
    struct wd_uac_source *s=a->wd_source;unsigned n,at=0,end=0;
    if(!s)return;s->packet_bytes=0;s->subslot=subslot;
    if(!req->buf||!req->actual||req->actual>WD_SOURCE_RAW_MAX||req->actual>req->length){
        if(req->actual)wd_source_error(s,-EPROTO);return;
    }
#if QMU_PACKAGE_NUM
    if(!req->iso_frame_desc||!req->number_of_packets||req->number_of_packets>QMU_PACKAGE_NUM){wd_source_error(s,-EPROTO);return;}
    for(n=0;n<req->number_of_packets;++n){
        struct usb_iso_packet_descriptor *d=&req->iso_frame_desc[n];
        if(d->status||d->offset<end||d->offset>req->length||d->length>req->length-d->offset||
           d->actual_length>d->length||d->actual_length>req->actual-at||(!n&&d->offset)){
            wd_source_error(s,d->status<0?d->status:-EPROTO);return;
        }
        memcpy(s->raw+at,(unsigned char *)req->buf+d->offset,d->actual_length);
        at+=d->actual_length;end=d->offset+d->length;
    }
    if(at!=req->actual){wd_source_error(s,-EPROTO);return;}
#else
    memcpy(s->raw,req->buf,req->actual);at=req->actual;
#endif
    s->packet_bytes=at;
}
#include "sony_uac2_source_packet.inc"
