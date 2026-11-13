#include "sony_audio_usb_source_reader.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
struct sony_usb_source_reader {
    struct sony_usb_source_io io;struct sony_usb_source_scope scope;
    struct sony_usb_source_reader_state state;struct sony_usb_source_reader_cleanup cleanup;
    struct sony_source_feedback_info last_feedback;
    uint64_t instance;int busy,close_result,feedback_seen;
    unsigned char buffer[sizeof(struct sony_source_record)+SONY_SOURCE_QUEUE_MAX_READ];
};
static int lock(struct sony_usb_source_reader *p){return !p?-EINVAL:__atomic_exchange_n(&p->busy,1,__ATOMIC_ACQUIRE)?-EBUSY:0;}
static int unlock(struct sony_usb_source_reader *p,int rc){__atomic_store_n(&p->busy,0,__ATOMIC_RELEASE);return rc;}
static int status(int rc){return rc>0?-EPROTO:rc;}
static int fail(struct sony_usb_source_reader *p,int rc){rc=status(rc);if(rc){p->state.sealed=1;if(!p->state.first_error)p->state.first_error=rc;}return rc;}
static int refresh_caps(struct sony_usb_source_reader *p,int check_delivered){
    (void)check_delivered;struct sony_source_caps s;
    if(p->io.caps&&!p->io.caps(p->io.context,&s))p->state.caps=s;
    return 0;
}
static int active(struct sony_usb_source_reader *p){return p->state.close_attempted||p->state.sealed?p->state.first_error?p->state.first_error:-EPIPE:0;}
int sony_usb_source_reader_create(struct sony_usb_source_reader **out,unsigned activation,const struct sony_usb_source_io *io,const struct sony_usb_source_scope *scope){
    (void)activation;
    if(!out||*out||!io||!io->peek||!io->claim||!io->read_record||!io->release||!io->close_binding)return -EINVAL;
    struct sony_usb_source_reader *p=calloc(1,sizeof(*p));if(!p)return -ENOMEM;p->busy=1;p->io=*io;if(scope)p->scope=*scope;p->cleanup.global_mode_cleanup_unverified=1;*out=p;
    refresh_caps(p,1);p->instance=p->state.caps.instance;
    if(io->feedback){struct sony_source_feedback_info f;if(!io->feedback(io->context,&f)){p->last_feedback=f;p->feedback_seen=1;}}
    return unlock(p,0);
}
int sony_usb_source_reader_begin(struct sony_usb_source_reader *p,const struct sony_usb_event *event){
    if(!event)return -EINVAL;struct sony_usb_event requested=*event;event=&requested;
    int rc=lock(p);if(rc)return rc;rc=active(p);if(rc)return unlock(p,rc);if(p->state.claimed)return unlock(p,-EBUSY);
    rc=refresh_caps(p,1);
    struct sony_source_info s;memset(&s,0,sizeof(s));s.magic=SONY_SOURCE_QUEUE_MAGIC;s.version=1;s.bytes=sizeof(s);s.instance=p->instance;
    s.play_sequence=event->sequence;s.format=event->stream.format;s.rate=event->stream.rate;s.bits=event->stream.bit_width;
    if(!rc){++p->state.claim_attempts;rc=status(p->io.claim(p->io.context,&s));}
    if(!rc)rc=refresh_caps(p,1);
    if(!rc){p->state.source=s;p->state.event=*event;p->state.claimed=1;p->state.prefilled=p->state.eof=p->state.release_attempted=p->state.release_confirmed=0;p->state.returned_source=0;p->state.prefill_frames=event->stream.rate/10;}
    if(rc)fail(p,rc);return unlock(p,rc);
}
int sony_usb_source_reader_prefill(struct sony_usb_source_reader *p,unsigned *available){
    if(!available)return -EINVAL;int rc=lock(p);if(rc)return rc;rc=active(p);if(rc)return unlock(p,rc);if(!p->state.claimed)return unlock(p,-ENODATA);
    rc=refresh_caps(p,1);struct sony_source_info s;
    if(!rc)rc=status(p->io.peek(p->io.context,&s));
    if(!rc)rc=refresh_caps(p,1);
    if(rc)return unlock(p,fail(p,rc));p->state.source=s;
    uint64_t frames=s.accepted_bytes>=s.delivered_bytes?(s.accepted_bytes-s.delivered_bytes)/8:0;
    *available=frames>UINT32_MAX?UINT32_MAX:(unsigned)frames;
    if(frames<p->state.prefill_frames&&!(s.flags&SONY_SOURCE_FINAL))return unlock(p,-EAGAIN);
    p->state.prefilled=1;return unlock(p,0);
}
static int overlaps(const void *a,unsigned n,const void *b,unsigned count){uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;return x>UINTPTR_MAX-n||y>UINTPTR_MAX-count||(x<y+count&&y<x+n);}
int sony_usb_source_reader_read(struct sony_usb_source_reader *p,void *data,unsigned capacity,unsigned *out_frames){
    if(!data||!out_frames||!capacity||capacity>4096||overlaps(data,capacity*8,out_frames,sizeof(*out_frames))||
       (p&&(overlaps(data,capacity*8,p,sizeof(*p))||overlaps(out_frames,sizeof(*out_frames),p,sizeof(*p)))))return -EINVAL;
    int rc=lock(p);if(rc)return rc;rc=active(p);if(rc)return unlock(p,rc);if(!p->state.claimed)return unlock(p,-ENODATA);if(!p->state.prefilled)return unlock(p,-EAGAIN);
    rc=refresh_caps(p,1);if(rc)return unlock(p,fail(p,rc));if(p->state.eof)return unlock(p,SONY_USB_SOURCE_READER_END);
    unsigned header=sizeof(struct sony_source_record);
    ++p->state.read_attempts;int count=p->io.read_record(p->io.context,p->buffer,header+capacity*8);int after=refresh_caps(p,0);
    if(count==-EAGAIN){if(after)return unlock(p,fail(p,after));++p->state.transient_reads;return unlock(p,-EAGAIN);}
    if(count<0)return unlock(p,fail(p,count));if(after)return unlock(p,fail(p,after));
    if(count<(int)header||count>(int)(header+capacity*8))return unlock(p,fail(p,-EPROTO));struct sony_source_record record;memcpy(&record,p->buffer,header);
    unsigned bytes=record.payload_bytes;
    if(!rc&&(bytes!=(unsigned)count-header||bytes%8))rc=-EPROTO;
    if(rc)return unlock(p,fail(p,rc));p->state.source=record.source;
    if(!bytes){p->state.eof=1;return unlock(p,SONY_USB_SOURCE_READER_END);}
    memcpy(data,p->buffer+header,bytes);p->state.returned_total+=bytes;p->state.returned_source+=bytes;p->state.last_reservation=record.reservation;*out_frames=bytes/8;return unlock(p,0);
}
int sony_usb_source_reader_finish(struct sony_usb_source_reader *p){
    int rc=lock(p);if(rc)return rc;rc=active(p);if(rc)return unlock(p,rc);
    if(!p->state.claimed)return unlock(p,-ENODATA);if(!p->state.eof)return unlock(p,-EBUSY);if(p->state.release_attempted)return unlock(p,-EALREADY);
    rc=refresh_caps(p,1);if(rc)return unlock(p,fail(p,rc));p->state.release_attempted=1;
    rc=status(p->io.release(p->io.context,p->state.source.source_id));
    if(rc)return unlock(p,fail(p,rc));p->state.last_released_id=p->state.source.source_id;p->state.release_confirmed=1;p->state.claimed=0;return unlock(p,0);
}
int sony_usb_source_reader_feedback(struct sony_usb_source_reader *p,struct sony_source_feedback_info *out){
    if(!out)return -EINVAL;int rc=lock(p);if(rc)return rc;rc=active(p);if(rc)return unlock(p,rc);rc=refresh_caps(p,1);struct sony_source_feedback_info s;
    if(!rc)rc=p->io.feedback?status(p->io.feedback(p->io.context,&s)):-ENOSYS;
    if(!rc)rc=refresh_caps(p,1);if(!rc){p->last_feedback=s;p->feedback_seen=1;*out=s;}else fail(p,rc);return unlock(p,rc);
}
int sony_usb_source_reader_state(struct sony_usb_source_reader *p,struct sony_usb_source_reader_state *out){if(!out)return -EINVAL;int rc=lock(p);if(rc)return rc;*out=p->state;return unlock(p,0);}
int sony_usb_source_reader_close(struct sony_usb_source_reader **owner,struct sony_usb_source_reader_cleanup *out){
    if(!owner||!*owner||!out)return -EINVAL;struct sony_usb_source_reader *p=*owner;int rc=lock(p);if(rc)return rc;
    p->state.close_attempted=1;p->cleanup.close_status=status(p->io.close_binding(p->io.context));
    rc=p->cleanup.close_status;p->close_result=rc;p->cleanup.state=p->state;
    p->cleanup.binding_closed_verified=0;*out=p->cleanup;free(p);*owner=NULL;return rc;
}
