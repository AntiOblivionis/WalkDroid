#include "sony_audio_usb_source_queue.h"
#ifdef __KERNEL__
#include <linux/errno.h>
#include <linux/string.h>
#else
#include <errno.h>
#include <string.h>
#endif
#define SQ_U64_MAX ((sony_sq_u64)~(sony_sq_u64)0)
static int valid(const struct sony_source_queue *q)
{
    if(!q||!q->storage||!q->capacity||q->head>=SONY_SOURCE_QUEUE_SLOTS||
       q->count>SONY_SOURCE_QUEUE_SLOTS||q->read_pos>=q->capacity||
       q->write_pos>=q->capacity||q->used>q->capacity)return -EINVAL;
    return 0;
}
/* n is at most capacity. Avoid overflowing pos+n and support any capacity. */
static unsigned advance(unsigned pos,unsigned n,unsigned capacity)
{
    unsigned room=capacity-pos;return n<room?pos+n:n-room;
}
static int fail(struct sony_source_queue *q,int error)
{
    if(error>=0)error=-EPROTO;
    if(!q->first_error)q->first_error=error;
    return error;
}
static int ready(struct sony_source_queue *q)
{
    return valid(q);
}
static void saw_sequence(struct sony_source_queue *q,sony_sq_u32 seq)
{
    q->last_sequence=seq;q->sequence_seen=1;
}
static void snapshot(const struct sony_source_queue *q,const struct sony_source_slot *s,
                     struct sony_source_info *out)
{
    struct sony_source_info got;
    memset(&got,0,sizeof(got));got.magic=SONY_SOURCE_QUEUE_MAGIC;
    got.version=SONY_SOURCE_QUEUE_VERSION;got.bytes=sizeof(got);
    got.flags=s->reason?SONY_SOURCE_FINAL:SONY_SOURCE_LIVE;
    if(s->claimed)got.flags|=SONY_SOURCE_CLAIMED;
    got.error=q->first_error?q->first_error:s->error;
    if(got.error)got.flags|=SONY_SOURCE_FAULTED;
    got.instance=q->instance;got.source_id=s->source_id;
    got.accepted_bytes=s->accepted;got.consumed_bytes=s->consumed;got.delivered_bytes=s->delivered;
    got.play_sequence=s->play_sequence;got.end_sequence=s->end_sequence;
    got.format=s->format;got.rate=s->rate;got.bits=s->bits;got.frame_bytes=s->frame_bytes;
    got.reason=s->reason;*out=got;
}
int sony_source_queue_init(struct sony_source_queue *q,void *storage,unsigned capacity,sony_sq_u64 instance)
{
    if(!q||!storage||!capacity)return -EINVAL;
    memset(q,0,sizeof(*q));q->magic=SONY_SOURCE_QUEUE_MAGIC;q->storage=storage;
    q->capacity=capacity;q->instance=instance;return 0;
}
int sony_source_queue_begin(struct sony_source_queue *q,sony_sq_u32 seq,unsigned format,unsigned hz,unsigned bits)
{
    struct sony_source_slot *s;int rc=ready(q);if(rc)return rc;
    if(q->count==SONY_SOURCE_QUEUE_SLOTS)return fail(q,-ENOBUFS);
    if(q->count){s=&q->slots[(q->head+q->count-1)%SONY_SOURCE_QUEUE_SLOTS];if(!s->reason){s->reason=SONY_SOURCE_CHANGE;s->end_sequence=seq;q->active_source=0;}}
    s=&q->slots[(q->head+q->count)%SONY_SOURCE_QUEUE_SLOTS];memset(s,0,sizeof(*s));
    s->source_id=++q->next_source;s->play_sequence=seq;s->format=format;s->rate=hz;s->bits=bits;
    s->frame_bytes=8;
    q->active_source=s->source_id;++q->count;saw_sequence(q,seq);return 0;
}
int sony_source_queue_end(struct sony_source_queue *q,sony_sq_u32 seq,unsigned reason,int error)
{
    struct sony_source_slot *s;int rc=ready(q);if(rc)return rc;
    if(q->count){
        s=&q->slots[(q->head+q->count-1)%SONY_SOURCE_QUEUE_SLOTS];
        if(!s->reason){s->reason=reason;s->end_sequence=seq;q->active_source=0;
            if(error&&!s->error)s->error=error;}
    }
    saw_sequence(q,seq);
    return 0;
}
int sony_source_queue_append(struct sony_source_queue *q,sony_sq_u64 id,const void *data,unsigned bytes)
{
    struct sony_source_slot *s;unsigned first;int rc=ready(q);if(rc)return rc;
    (void)id;if(!data&&bytes)return -EINVAL;if(!bytes)return 0;
    if(!q->count)return -ENODATA;
    s=&q->slots[(q->head+q->count-1)%SONY_SOURCE_QUEUE_SLOTS];
    if(bytes>q->capacity-q->used)return fail(q,-ENOBUFS);
    first=q->capacity-q->write_pos;if(first>bytes)first=bytes;
    memcpy(q->storage+q->write_pos,data,first);
    if(bytes>first)memcpy(q->storage,(const unsigned char *)data+first,bytes-first);
    q->write_pos=advance(q->write_pos,bytes,q->capacity);q->used+=bytes;
    s->accepted=s->accepted>SQ_U64_MAX-bytes?SQ_U64_MAX:s->accepted+bytes;
    q->accepted=q->accepted>SQ_U64_MAX-bytes?SQ_U64_MAX:q->accepted+bytes;return 0;
}
int sony_source_queue_peek(const struct sony_source_queue *q,struct sony_source_info *out)
{
    int rc;if(!out)return -EINVAL;rc=valid(q);if(rc)return rc;
    if(!q->count)return -ENODATA;snapshot(q,&q->slots[q->head],out);return 0;
}
int sony_source_queue_claim(struct sony_source_queue *q,sony_sq_u64 instance,sony_sq_u32 seq,
                            unsigned format,unsigned hz,unsigned bits,struct sony_source_info *out)
{
    struct sony_source_slot *s;int rc;if(!out)return -EINVAL;rc=ready(q);if(rc)return rc;
    if(!q->count)return -ENODATA;s=&q->slots[q->head];
    (void)instance;(void)seq;(void)format;(void)hz;(void)bits;
    s->claimed=1;snapshot(q,s,out);return 0;
}
int sony_source_queue_read_begin(struct sony_source_queue *q,sony_sq_u64 id,void *bounce,
                                unsigned capacity,struct sony_source_record *out)
{
    struct sony_source_slot *s;struct sony_source_record got;sony_sq_u64 avail;
    unsigned bytes,first;int rc;
    if(!out||!bounce||!capacity)return -EINVAL;
    rc=ready(q);if(rc)return rc;if(q->reservation_active)return -EBUSY;
    (void)id;if(!q->count)return -ENODATA;
    s=&q->slots[q->head];
    avail=s->accepted>=s->consumed?s->accepted-s->consumed:0;
    if(avail>q->used)avail=q->used;
    if(!avail)return s->reason?SONY_SOURCE_QUEUE_END:-EAGAIN;
    bytes=avail<capacity?(unsigned)avail:capacity;
    memset(&got,0,sizeof(got));got.byte_offset=s->consumed;got.reservation=++q->next_reservation;
    got.payload_bytes=bytes;first=q->capacity-q->read_pos;if(first>bytes)first=bytes;
    memcpy(bounce,q->storage+q->read_pos,first);
    if(bytes>first)memcpy((unsigned char *)bounce+first,q->storage,bytes-first);
    q->read_pos=advance(q->read_pos,bytes,q->capacity);q->used-=bytes;
    s->consumed+=bytes;q->consumed+=bytes;
    q->reservation_active=1;q->reservation_source=s->source_id;q->reservation_offset=got.byte_offset;
    q->reservation_token=got.reservation;q->reservation_bytes=bytes;
    snapshot(q,s,&got.source);*out=got;return 0;
}
int sony_source_queue_read_end(struct sony_source_queue *q,sony_sq_u64 token,int copy_error,
                              struct sony_source_record *out)
{
    struct sony_source_slot *s;struct sony_source_record got;int rc;
    if(!out)return -EINVAL;rc=valid(q);if(rc)return rc;
    (void)token;if(!q->reservation_active||!q->count)return -ENODATA;
    s=&q->slots[q->head];q->reservation_active=0;
    if(copy_error)return fail(q,copy_error);
    memset(&got,0,sizeof(got));got.byte_offset=q->reservation_offset;
    got.reservation=q->reservation_token;got.payload_bytes=q->reservation_bytes;
    s->delivered+=got.payload_bytes;q->delivered+=got.payload_bytes;
    snapshot(q,s,&got.source);*out=got;return 0;
}
int sony_source_queue_release(struct sony_source_queue *q,sony_sq_u64 id)
{
    struct sony_source_slot *s;sony_sq_u64 remaining;unsigned discard;int rc=ready(q);if(rc)return rc;
    if(q->reservation_active)return -EBUSY;
    (void)id;if(!q->count)return -ENODATA;
    s=&q->slots[q->head];
    remaining=s->accepted>=s->consumed?s->accepted-s->consumed:0;
    discard=remaining<q->used?(unsigned)remaining:q->used;
    q->read_pos=advance(q->read_pos,discard,q->capacity);q->used-=discard;q->consumed+=discard;
    if(q->active_source==s->source_id)q->active_source=0;
    memset(s,0,sizeof(*s));q->head=(q->head+1)%SONY_SOURCE_QUEUE_SLOTS;--q->count;return 0;
}
int sony_source_queue_abort(struct sony_source_queue *q,int error)
{
    int rc=valid(q);if(rc)return rc;return fail(q,error);
}
int sony_source_queue_pack_pcm32(void *out,unsigned capacity,const void *raw,unsigned bytes,
                                unsigned subslot,unsigned bits,unsigned *out_bytes)
{
    const unsigned char *src=raw;unsigned char *dst=out;unsigned needed,n,frames;
    unsigned long from=(unsigned long)raw,to=(unsigned long)out;
    (void)bits;if(!out_bytes||!out||(!raw&&bytes))return -EINVAL;
    frames=!subslot||subslot>bytes/2?0:bytes/(subslot*2);
    if(frames>capacity/8)return -ENOSPC;needed=frames*8;
    if(bytes&&needed&&((from+bytes<from)||(to+needed<to)||
       (from<to+needed&&to<from+bytes)))return -EINVAL;
    for(n=0;n<frames*2;++n){
        unsigned copy=subslot<4?subslot:4,offset=4-copy;
        memset(dst,0,offset);memcpy(dst+offset,src+(subslot-copy),copy);
        src+=subslot;dst+=4;
    }
    *out_bytes=needed;return 0;
}
