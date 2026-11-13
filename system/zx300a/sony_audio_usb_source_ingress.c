#include "sony_audio_usb_source_ingress.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
struct sony_usb_source_ingress {
    struct sony_usb_ingress_state state;struct sony_usb_ingress_cleanup cleanup;
    struct sony_usb_source_ingress_info info;
    struct sony_usb_input_linux *input;struct sony_usb_input_policy *policy;
    struct sony_usb_input_policy_io policy_io;struct sony_usb_source_linux *linux_io;
    struct sony_usb_source_io source_io;struct sony_usb_source_reader *reader;
    struct sony_usb_monitor *monitor;pid_t pid;
    uint64_t consumed_base,published_frames;unsigned read_base,transient_base;
    int32_t data[4096*2];
    int busy,claiming,active,stop_attempted,stop_error;
};
static int lock(struct sony_usb_source_ingress *p){return !p?-EINVAL:__atomic_exchange_n(&p->busy,1,__ATOMIC_ACQUIRE)?-EBUSY:0;}
static int unlock(struct sony_usb_source_ingress *p,int rc){__atomic_store_n(&p->busy,0,__ATOMIC_RELEASE);return rc;}
static int status(int rc){return rc>0?-EPROTO:rc;}
static int fail(struct sony_usb_source_ingress *p,int rc){rc=status(rc);if(rc&&!p->state.first_error)p->state.first_error=rc;return rc;}
static int usable(struct sony_usb_source_ingress *p){return p->state.first_error?p->state.first_error:p->state.closing||p->info.cancel_attempted?-EPIPE:0;}
static int snapshot(struct sony_usb_source_ingress *p){
    int rc=0;if(p->policy)rc=sony_usb_input_policy_state(p->policy,&p->state.policy);
    if(!rc&&p->reader)rc=sony_usb_source_reader_state(p->reader,&p->info.reader);
    p->state.capture_active=!!p->active;
    if(!rc&&p->active){struct sony_usb_capture_state *c=&p->state.capture;const struct sony_usb_source_reader_state *r=&p->info.reader;
        c->event=p->state.current;c->captured_frames=r->caps.consumed_bytes>=p->consumed_base?(r->caps.consumed_bytes-p->consumed_base)/8:0;c->returned_frames=p->published_frames;
        c->read_attempts=(unsigned)(r->read_attempts-p->read_base);c->transient_reads=(unsigned)(r->transient_reads-p->transient_base);
        c->prefill_frames=r->prefill_frames;c->prefilled=r->prefilled;c->sealed=r->sealed;c->first_error=r->first_error;
        c->last_available=r->source.accepted_bytes>=r->source.delivered_bytes?(unsigned)((r->source.accepted_bytes-r->source.delivered_bytes)/8):0;if(c->last_available>c->max_available)c->max_available=c->last_available;
    }return rc;
}
static int parameters(struct sony_usb_source_ingress *p,int feedback){
    (void)feedback;return p?0:-EINVAL;
}
static int admit(void *v,const struct sony_usb_event *event,enum sony_usb_source_stage stage){
    (void)event;(void)stage;return usable(v);
}
int sony_usb_source_ingress_create(struct sony_usb_source_ingress **out,unsigned activation,struct sony_audio_owner *owner,struct sony_usb_monitor *monitor,uint64_t receipt){
    (void)activation;if(!out||*out||!owner||!monitor)return -EINVAL;
    int rc=sony_usb_monitor_fd(monitor);if(rc<0)return rc;
    struct sony_usb_source_ingress *p=calloc(1,sizeof(*p));if(!p)return -ENOMEM;
    p->pid=getpid();p->busy=1;p->monitor=monitor;p->state.generation=sony_audio_owner_generation(owner);p->state.receipt=receipt;p->cleanup.global_mode_cleanup_unverified=1;*out=p;
    rc=sony_usb_input_linux_create(&p->input,owner);if(!rc)rc=sony_usb_input_linux_io(p->input,&p->policy_io);
    /* Open the source resource before preparing its input parameters. */
    if(!rc)rc=sony_usb_source_linux_create(&p->linux_io,p->input);
    if(!rc)rc=sony_usb_source_linux_io(p->linux_io,&p->source_io);
    struct sony_usb_source_scope scope={admit,p};
    if(!rc)rc=sony_usb_source_reader_create(&p->reader,SONY_USB_SOURCE_READER_EXPLICIT,&p->source_io,&scope);
    if(!rc)rc=sony_usb_input_policy_create(&p->policy,&p->policy_io,receipt);
    if(!rc)rc=sony_usb_input_policy_prepare(p->policy);
    if(rc)fail(p,rc);int observed=snapshot(p);if(!rc&&observed)rc=fail(p,observed);return unlock(p,rc);
}
int sony_usb_source_ingress_next_event(struct sony_usb_source_ingress *p,struct sony_usb_event *out){
    if(!out)return -EINVAL;int rc=lock(p);if(rc)return rc;rc=usable(p);if(rc)return unlock(p,rc);
    if(p->active)return unlock(p,-EBUSY);if(p->state.event_claimable)return unlock(p,-EALREADY);
    struct sony_source_info s;rc=status(p->source_io.peek(p->source_io.context,&s));
    if(rc==-ENODATA)return unlock(p,-EAGAIN);if(rc)return unlock(p,fail(p,rc));
    struct sony_usb_event event={s.play_sequence,{SONY_USB_PLAY,s.format,s.rate,s.bits}};
    p->info.journal_source_id=s.source_id;p->state.current=event;p->state.event_claimable=1;++p->state.events_delivered;*out=event;return unlock(p,0);
}
int sony_usb_source_ingress_begin_pcm(struct sony_usb_source_ingress *p){
    int rc=lock(p);if(rc)return rc;rc=usable(p);if(rc)return unlock(p,rc);if(p->active)return unlock(p,-EBUSY);
    if(!p->state.event_claimable)return unlock(p,-ESTALE);
    p->state.event_claimable=0;p->claiming=1;rc=sony_usb_source_reader_begin(p->reader,&p->state.current);p->claiming=0;
    if(!rc)rc=sony_usb_source_reader_state(p->reader,&p->info.reader);
    if(!rc){memset(&p->state.capture,0,sizeof(p->state.capture));p->published_frames=0;p->consumed_base=p->info.reader.caps.consumed_bytes;p->read_base=p->info.reader.read_attempts;p->transient_base=p->info.reader.transient_reads;p->active=1;++p->state.streams_started;}
    if(rc)fail(p,rc);int s=snapshot(p);if(!rc&&s)rc=fail(p,s);return unlock(p,rc);
}
int sony_usb_source_ingress_poll_fd(struct sony_usb_source_ingress *p){int rc=lock(p);if(rc)return rc;rc=usable(p);if(!rc)rc=sony_usb_source_linux_poll_fd(p->linux_io);return unlock(p,rc);}
int sony_usb_source_ingress_read(struct sony_usb_source_ingress *p,void *data,unsigned frames,unsigned *out_frames){
    if(!data||!out_frames||!frames||frames>4096)return -EINVAL;int rc=lock(p);if(rc)return rc;rc=usable(p);if(rc)return unlock(p,rc);if(!p->active)return unlock(p,-ENODATA);
    rc=snapshot(p);if(!rc&&!p->info.reader.prefilled){unsigned available;rc=sony_usb_source_reader_prefill(p->reader,&available);}
    if(!rc){struct sony_usb_input_policy_state s;rc=sony_usb_input_policy_state(p->policy,&s);if(!rc&&!s.feedback_ready)rc=sony_usb_input_policy_feedback(p->policy,1);}
    unsigned count=0;if(!rc)rc=sony_usb_source_reader_read(p->reader,p->data,frames,&count);
    /* A completed read is returned without a parameter readback barrier. */
    if(!rc)rc=parameters(p,1);
    if(!rc){if(!count||count>frames)rc=-EPROTO;else{memcpy(data,p->data,count*8u);*out_frames=count;p->published_frames+=count;}}
    int observed=snapshot(p);if(observed)return unlock(p,fail(p,observed));
    if(rc&&rc!=SONY_USB_SOURCE_READER_END&&!(rc==-EAGAIN&&!p->info.reader.sealed))fail(p,rc);return unlock(p,rc);
}
static void account_closed(struct sony_usb_source_ingress *p){
    if(!p->active)return;p->state.captured_frames+=p->state.capture.captured_frames;p->state.returned_frames+=p->state.capture.returned_frames;++p->state.streams_closed;p->active=0;p->state.capture_active=0;
}
int sony_usb_source_ingress_close_stream(struct sony_usb_source_ingress *p){
    int rc=lock(p);if(rc)return rc;rc=usable(p);if(rc)return unlock(p,rc);if(!p->active)return unlock(p,-ENODATA);
    rc=sony_usb_source_reader_finish(p->reader);int observed=snapshot(p);if(!rc&&observed)rc=observed;
    if(!rc){account_closed(p);p->info.journal_source_id=0;}else fail(p,rc);return unlock(p,rc);
}
static int cancel(struct sony_usb_source_ingress *p){
    p->info.cancel_attempted=1;p->state.event_claimable=0;int rc=snapshot(p);
    /* Diagnostic snapshot failure does not forbid closing known owned FDs. */
    if(rc)fail(p,rc);rc=0;
    if(p->reader)rc=sony_usb_source_reader_close(&p->reader,&p->info.reader_cleanup);
    if(!p->reader&&p->linux_io){int closed=sony_usb_source_linux_close(p->linux_io,&p->info.linux_cleanup);
        p->info.linux_destroy_status=sony_usb_source_linux_destroy(&p->linux_io);
        if(!rc)rc=closed?closed:p->info.linux_destroy_status;}
    if(!p->reader&&!p->linux_io){p->info.binding_closed_verified=0;account_closed(p);}
    p->info.cancel_status=rc;p->cleanup.capture_close_status=rc;if(rc)fail(p,rc);return rc;
}
int sony_usb_source_ingress_cancel(struct sony_usb_source_ingress *p){int rc=lock(p);if(rc)return rc;rc=cancel(p);return unlock(p,rc);}
int sony_usb_source_ingress_state(struct sony_usb_source_ingress *p,struct sony_usb_ingress_state *out){if(!out)return -EINVAL;int rc=lock(p);if(rc)return rc;rc=snapshot(p);if(!rc)*out=p->state;return unlock(p,rc);}
int sony_usb_source_ingress_info(struct sony_usb_source_ingress *p,struct sony_usb_source_ingress_info *out){if(!out)return -EINVAL;int rc=lock(p);if(rc)return rc;rc=snapshot(p);if(!rc)*out=p->info;return unlock(p,rc);}
int sony_usb_source_ingress_stop(struct sony_usb_source_ingress **owner,struct sony_usb_ingress_cleanup *out,struct sony_usb_source_ingress_info *info){
    if(!owner||!*owner||!out||!info)return -EINVAL;struct sony_usb_source_ingress *p=*owner;int rc=lock(p);if(rc)return rc;
    p->stop_attempted=1;p->state.closing=1;
    rc=cancel(p);if(!rc&&p->policy){p->cleanup.parameter_release_status=sony_usb_input_policy_release(p->policy);rc=p->cleanup.parameter_release_status;
        if(!rc){int s=snapshot(p);rc=s?s:sony_usb_input_policy_destroy(&p->policy);}}
    if(!rc&&p->input){p->cleanup.linux_destroy_status=sony_usb_input_linux_destroy(&p->input);rc=p->cleanup.linux_destroy_status;}
    p->cleanup.state=p->state;p->cleanup.input_cleanup_verified=0;*out=p->cleanup;*info=p->info;
    if(rc){p->stop_error=rc;return unlock(p,rc);}free(p);*owner=NULL;return 0;
}
