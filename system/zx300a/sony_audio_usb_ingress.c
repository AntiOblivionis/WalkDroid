#include "sony_audio_usb_ingress.h"
#include <errno.h>
#include <stdlib.h>
struct sony_usb_ingress {
    struct sony_usb_ingress_state state;struct sony_usb_ingress_cleanup cleanup;
    struct sony_usb_input_linux *linux_io;struct sony_usb_input_policy *policy;
    struct sony_usb_input_policy_io io;struct sony_usb_capture *capture;
    struct sony_usb_monitor *monitor;int busy,boundary_mode,last_boundary_valid;
    struct sony_usb_capture_boundary_info last_boundary;
};
static int lock(struct sony_usb_ingress *p){return !p?-EINVAL:__atomic_exchange_n(&p->busy,1,__ATOMIC_ACQUIRE)?-EBUSY:0;}
static int unlock(struct sony_usb_ingress *p,int rc){__atomic_store_n(&p->busy,0,__ATOMIC_RELEASE);return rc;}
static int status(int rc){return rc>0?-EPROTO:rc;}
static int fail(struct sony_usb_ingress *p,int rc){rc=status(rc);if(rc&&!p->state.first_error)p->state.first_error=rc;return rc;}
static int snapshot(struct sony_usb_ingress *p){int rc=0;p->state.capture_active=!!p->capture;if(p->policy)rc=sony_usb_input_policy_state(p->policy,&p->state.policy);if(!rc&&p->capture)rc=sony_usb_capture_state(p->capture,&p->state.capture);return rc;}
static int identity(void *v){struct sony_usb_ingress *p=v;return sony_usb_input_linux_check(p->linux_io);}
static int closed(void *v){struct sony_usb_ingress *p=v;return p->io.capture_closed(p->io.context);}
static int boundary(void *v,struct sony_usb_boundary *out){struct sony_usb_ingress *p=v;return sony_usb_input_linux_boundary_snapshot(p->linux_io,out);}
static int parameters(struct sony_usb_ingress *p){
    ++p->state.parameter_checks;return 0;
}
static int source(void *v,enum sony_usb_capture_stage stage){
    struct sony_usb_ingress *p=v;if(p->state.closing||p->state.first_error)return p->state.first_error?p->state.first_error:-EPIPE;
    if(p->state.pending_valid&&(!p->boundary_mode||stage==SONY_USB_CAPTURE_OPEN))return -ESTALE;
    int rc=parameters(p);if(rc)return fail(p,rc);
    if(p->state.pending_valid)return 0;
    struct sony_usb_event event;rc=sony_usb_monitor_receive(p->monitor,&event);
    if(!rc){p->state.pending=event;p->state.pending_valid=1;return p->boundary_mode&&stage!=SONY_USB_CAPTURE_OPEN?0:-ESTALE;}
    if(rc==-EAGAIN||rc==-EINTR)return rc==-EAGAIN||(p->boundary_mode&&stage!=SONY_USB_CAPTURE_OPEN)?0:rc;return fail(p,rc);
}
static int create_ingress(struct sony_usb_ingress **out,unsigned activation,struct sony_audio_owner *owner,struct sony_usb_monitor *monitor,uint64_t receipt,int boundary_mode){
    (void)activation;if(!out||*out||!owner||!monitor)return -EINVAL;
    int rc=0;
    struct sony_usb_ingress *p=calloc(1,sizeof(*p));if(!p)return -ENOMEM;p->busy=1;p->boundary_mode=boundary_mode;p->monitor=monitor;p->state.generation=sony_audio_owner_generation(owner);p->state.receipt=receipt;p->cleanup.global_mode_cleanup_unverified=1;*out=p;
    rc=sony_usb_input_linux_create(&p->linux_io,owner);if(!rc)rc=sony_usb_input_linux_io(p->linux_io,&p->io);
    if(!rc)rc=sony_usb_input_policy_create(&p->policy,&p->io,receipt);if(!rc)rc=sony_usb_input_policy_prepare(p->policy);
    if(rc)fail(p,rc);snapshot(p);return unlock(p,rc);
}
int sony_usb_ingress_create(struct sony_usb_ingress **out,unsigned activation,struct sony_audio_owner *owner,struct sony_usb_monitor *monitor,uint64_t receipt){return create_ingress(out,activation,owner,monitor,receipt,0);}
int sony_usb_ingress_create_boundary(struct sony_usb_ingress **out,unsigned activation,struct sony_audio_owner *owner,struct sony_usb_monitor *monitor,uint64_t receipt){return create_ingress(out,activation,owner,monitor,receipt,1);}
int sony_usb_ingress_boundary_info(struct sony_usb_ingress *p,struct sony_usb_capture_boundary_info *out){if(!out)return -EINVAL;int rc=lock(p);if(rc)return rc;if(!p->boundary_mode)return unlock(p,-ENOTSUP);if(p->capture)rc=sony_usb_capture_boundary_info(p->capture,out);else if(p->last_boundary_valid)*out=p->last_boundary;else rc=-ENODATA;return unlock(p,rc);}
int sony_usb_ingress_next_event(struct sony_usb_ingress *p,struct sony_usb_event *out){
    if(!out)return -EINVAL;int rc=lock(p);if(rc)return rc;if(p->state.first_error||p->state.closing)return unlock(p,p->state.first_error?p->state.first_error:-EPIPE);if(p->capture)return unlock(p,-EBUSY);
    rc=sony_usb_input_linux_check(p->linux_io);if(rc)return unlock(p,fail(p,rc));struct sony_usb_event event;
    if(p->state.pending_valid){event=p->state.pending;p->state.pending_valid=0;rc=0;}else rc=sony_usb_monitor_receive(p->monitor,&event);
    if(!rc){p->state.current=event;p->state.event_claimable=event.stream.action==SONY_USB_PLAY;++p->state.events_delivered;*out=event;}
    else if(rc!=-EAGAIN&&rc!=-EINTR)fail(p,rc);return unlock(p,rc);
}
int sony_usb_ingress_begin_pcm(struct sony_usb_ingress *p){
    int rc=lock(p);if(rc)return rc;if(p->state.first_error||p->state.closing)return unlock(p,p->state.first_error?p->state.first_error:-EPIPE);if(p->capture)return unlock(p,-EBUSY);
    if(p->state.current.stream.format!=SONY_USB_PCM)return unlock(p,-ENOTSUP);
    p->state.event_claimable=0;struct sony_usb_capture_scope scope={identity,closed,source,p};
    if(p->boundary_mode){struct sony_usb_capture_boundary_scope b={scope,boundary};rc=sony_usb_capture_begin_boundary_pcm(&p->capture,SONY_USB_CAPTURE_EXPLICIT_START,&p->state.current,&b);}
    else rc=sony_usb_capture_begin_pcm(&p->capture,SONY_USB_CAPTURE_EXPLICIT_START,&p->state.current,&scope);
    if(!rc)++p->state.streams_started;if(rc&&!(!p->boundary_mode&&p->state.pending_valid&&rc==-ESTALE))fail(p,rc);snapshot(p);return unlock(p,rc);
}
static int capture_result(struct sony_usb_ingress *p,int rc){
    int s=snapshot(p);if(s)return fail(p,s);if(!rc||(rc==SONY_USB_CAPTURE_END&&p->boundary_mode))return rc;
    if(!p->boundary_mode&&p->state.pending_valid&&rc==-ESTALE)return rc;
    if(!p->state.capture.sealed&&(rc==-EAGAIN||rc==-EINTR))return rc;return fail(p,rc);
}
int sony_usb_ingress_poll_fd(struct sony_usb_ingress *p){int rc=lock(p);if(rc)return rc;if(p->state.first_error||p->state.closing)return unlock(p,p->state.first_error?p->state.first_error:-EPIPE);if(!p->capture)return unlock(p,-ENODATA);rc=sony_usb_capture_poll_fd(p->capture);if(rc<0)capture_result(p,rc);return unlock(p,rc);}
int sony_usb_ingress_read(struct sony_usb_ingress *p,void *data,unsigned frames,unsigned *out_frames){
    if(!data||!out_frames||!frames||frames>SONY_USB_CAPTURE_MAX_READ_FRAMES)return -EINVAL;int rc=lock(p);if(rc)return rc;
    if(p->state.first_error||p->state.closing)return unlock(p,p->state.first_error?p->state.first_error:-EPIPE);if(!p->capture)return unlock(p,-ENODATA);
    rc=snapshot(p);if(!rc&&!p->state.capture.prefilled){unsigned available=0;rc=sony_usb_capture_prefill(p->capture,&available);if(!rc){struct sony_usb_input_policy_state state;rc=sony_usb_input_policy_state(p->policy,&state);if(!rc&&!state.feedback_ready)rc=sony_usb_input_policy_feedback(p->policy,1);if(rc)fail(p,rc);}}
    if(!rc)rc=sony_usb_capture_read(p->capture,data,frames,out_frames);rc=capture_result(p,rc);return unlock(p,rc);
}
static int close_stream(struct sony_usb_ingress *p){
    if(!p->capture)return -ENODATA;if(p->boundary_mode&&!sony_usb_capture_boundary_info(p->capture,&p->last_boundary))p->last_boundary_valid=1;struct sony_usb_capture_cleanup cleanup={0};int rc=sony_usb_capture_close(&p->capture,&cleanup);p->cleanup.last_capture=cleanup;
    if(!p->capture){++p->state.streams_closed;p->state.captured_frames+=cleanup.state.captured_frames;p->state.returned_frames+=cleanup.state.returned_frames;p->state.capture=cleanup.state;if(cleanup.stop_status&&!rc)rc=status(cleanup.stop_status);}
    p->state.capture_active=!!p->capture;if(rc)fail(p,rc);return rc;
}
int sony_usb_ingress_close_stream(struct sony_usb_ingress *p){int rc=lock(p);if(rc)return rc;if(p->state.closing)return unlock(p,-EPIPE);return unlock(p,close_stream(p));}
int sony_usb_ingress_state(struct sony_usb_ingress *p,struct sony_usb_ingress_state *out){if(!out)return -EINVAL;int rc=lock(p);if(rc)return rc;rc=snapshot(p);if(!rc)*out=p->state;return unlock(p,rc);}
int sony_usb_ingress_stop(struct sony_usb_ingress **owner,struct sony_usb_ingress_cleanup *out){
    if(!owner||!*owner||!out)return -EINVAL;struct sony_usb_ingress *p=*owner;int rc=lock(p);if(rc)return rc;
    p->state.closing=1;
    if(p->capture){p->cleanup.capture_close_status=close_stream(p);if(p->cleanup.capture_close_status&&!rc)rc=p->cleanup.capture_close_status;}
    if(!p->capture&&p->policy){int release=sony_usb_input_policy_release(p->policy);p->cleanup.parameter_release_status=release;if(release&&!rc)rc=release;if(!release){snapshot(p);int destroy=sony_usb_input_policy_destroy(&p->policy);if(destroy&&!rc)rc=destroy;}}
    if(!p->capture&&!p->policy&&p->linux_io){p->cleanup.linux_destroy_status=sony_usb_input_linux_destroy(&p->linux_io);if(p->cleanup.linux_destroy_status&&!rc)rc=p->cleanup.linux_destroy_status;}
    p->cleanup.state=p->state;p->cleanup.input_cleanup_verified=0;*out=p->cleanup;
    if(p->capture||p->policy||p->linux_io)return unlock(p,rc?rc:-EAGAIN);
    free(p);*owner=NULL;return rc;
}
