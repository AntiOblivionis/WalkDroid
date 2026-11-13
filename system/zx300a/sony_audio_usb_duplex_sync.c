/* Synchronous production backend.
 * Keeps the stable outer-mode ABI and cleanup ordering. The async backend in
 * sony_audio_usb_duplex.c remains a separate, unqualified experiment. */
#include "sony_audio_usb_duplex.h"
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <sys/stat.h>
#include <stdio.h>
#ifndef WALKDROID_REQUIRE_USB_BOUNDARY
#define WALKDROID_REQUIRE_USB_BOUNDARY 0
#endif
#ifndef WALKDROID_REQUIRE_USB_SOURCE
#define WALKDROID_REQUIRE_USB_SOURCE 0
#endif
#ifndef WALKDROID_USB_SETTINGS_OBSERVE
#define WALKDROID_USB_SETTINGS_OBSERVE 0
#endif
#ifndef WALKDROID_USB_LIVE_VOLUME
#define WALKDROID_USB_LIVE_VOLUME 0
#endif
#if WALKDROID_REQUIRE_USB_SOURCE
#include "sony_audio_usb_source_ingress.h"
#endif
struct sony_usb_duplex {
    struct sony_audio_owner *owner;struct sony_usb_ingress *input;struct sony_receiver_session *output;
#if WALKDROID_REQUIRE_USB_SOURCE
    struct sony_usb_source_ingress *source_input;struct sony_usb_source_ingress_info source_info;
#endif
    struct sony_usb_duplex_launch launch;struct sony_usb_duplex_state state;struct sony_usb_duplex_cleanup cleanup;
    int32_t data[SONY_USB_DUPLEX_MAX_FRAMES*2];int busy,attach_attempted,input_created,output_attempted,boundary_mode;
    uint64_t last_idle_observation_ns;int controls_pending;
};
static int lock(struct sony_usb_duplex *p){return !p?-EINVAL:__atomic_exchange_n(&p->busy,1,__ATOMIC_ACQUIRE)?-EBUSY:0;}
static int unlock(struct sony_usb_duplex *p,int rc){__atomic_store_n(&p->busy,0,__ATOMIC_RELEASE);return rc;}
static int status(int rc){return rc>0?-EPROTO:rc;}
static int fail(struct sony_usb_duplex *p,int rc){rc=status(rc);p->state.sealed=1;if(rc&&!p->state.first_error)p->state.first_error=rc;return rc;}
static int owner_check(struct sony_usb_duplex *p){return p&&p->owner?0:-EINVAL;}
#if WALKDROID_USB_SETTINGS_OBSERVE
static int observe_idle(struct sony_usb_duplex *p){
    struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t))return -errno;
    if(t.tv_sec<0||t.tv_nsec<0||t.tv_nsec>=1000000000)return -EIO;
    uint64_t now=(uint64_t)t.tv_sec*UINT64_C(1000000000)+(unsigned long)t.tv_nsec;
    if(now<p->last_idle_observation_ns)return -EIO;
    if(p->last_idle_observation_ns&&now-p->last_idle_observation_ns<UINT64_C(500000000))return 0;
    int rc=sony_receiver_session_observe(p->output);if(!rc)p->last_idle_observation_ns=now;return rc;
}
#endif
static int input_state(struct sony_usb_duplex *p){
#if WALKDROID_REQUIRE_USB_SOURCE
    if(p->source_input)return sony_usb_source_ingress_state(p->source_input,&p->state.input);
#endif
    return p->input?sony_usb_ingress_state(p->input,&p->state.input):0;
}
static int input_event(struct sony_usb_duplex *p,struct sony_usb_event *event){
#if WALKDROID_REQUIRE_USB_SOURCE
    if(p->source_input)return sony_usb_source_ingress_next_event(p->source_input,event);
#endif
    return sony_usb_ingress_next_event(p->input,event);
}
static int input_begin(struct sony_usb_duplex *p){
#if WALKDROID_REQUIRE_USB_SOURCE
    if(p->source_input)return sony_usb_source_ingress_begin_pcm(p->source_input);
#endif
    return sony_usb_ingress_begin_pcm(p->input);
}
static int input_read(struct sony_usb_duplex *p,unsigned maximum,unsigned *frames){
#if WALKDROID_REQUIRE_USB_SOURCE
    if(p->source_input)return sony_usb_source_ingress_read(p->source_input,p->data,maximum,frames);
#endif
    return sony_usb_ingress_read(p->input,p->data,maximum,frames);
}
static int snapshot(struct sony_usb_duplex *p){int rc=input_state(p);if(!rc&&p->output)rc=sony_receiver_session_state(p->output,&p->state.output);return status(rc);}
int sony_usb_duplex_start(struct sony_usb_duplex **out,unsigned activation,struct sony_audio_owner *owner,const struct sony_usb_duplex_launch *launch){
    (void)activation;if(!out||*out||!owner||!launch)return -EINVAL;
    int rc=0;uint64_t generation=sony_audio_owner_generation(owner);
    struct sony_usb_duplex *p=calloc(1,sizeof(*p));if(!p)return -ENOMEM;p->busy=1;p->owner=owner;p->launch=*launch;p->state.generation=generation;p->cleanup.global_mode_cleanup_unverified=1;*out=p;
    rc=sony_receiver_session_start_usb(&p->output,SONY_RECEIVER_SESSION_EXPLICIT_START,owner,&launch->downstream);if(rc&&!p->output){free(p);*out=NULL;return rc;}if(rc)fail(p,rc);snapshot(p);return unlock(p,rc);
}
static int attach(struct sony_usb_duplex *p,struct sony_usb_monitor *monitor,uint64_t receipt,int boundary_mode){
#if WALKDROID_REQUIRE_USB_SOURCE
    if(boundary_mode!=2)return -ENOTSUP;
#else
    if(boundary_mode==2)return -ENOTSUP;
#endif
    if(!monitor)return -EINVAL;int rc=lock(p);if(rc)return rc;if(p->state.sealed||p->state.closing)return unlock(p,p->state.first_error?p->state.first_error:-EPIPE);if(p->attach_attempted)return unlock(p,-EALREADY);rc=owner_check(p);if(rc)return unlock(p,fail(p,rc));p->attach_attempted=1;p->boundary_mode=boundary_mode;
#if WALKDROID_REQUIRE_USB_SOURCE
    rc=sony_usb_source_ingress_create(&p->source_input,SONY_USB_SOURCE_INGRESS_EXPLICIT,p->owner,monitor,receipt);p->input_created=!!p->source_input;
#else
    if(boundary_mode)rc=sony_usb_ingress_create_boundary(&p->input,SONY_USB_INGRESS_EXPLICIT_START,p->owner,monitor,receipt);
    else rc=sony_usb_ingress_create(&p->input,SONY_USB_INGRESS_EXPLICIT_START,p->owner,monitor,receipt);
    p->input_created=!!p->input;
#endif
    if(!rc)p->state.attached=1;else fail(p,rc);snapshot(p);return unlock(p,rc);
}
int sony_usb_duplex_attach(struct sony_usb_duplex *p,struct sony_usb_monitor *monitor,uint64_t receipt){return attach(p,monitor,receipt,WALKDROID_REQUIRE_USB_SOURCE?2:WALKDROID_REQUIRE_USB_BOUNDARY);}
int sony_usb_duplex_attach_boundary(struct sony_usb_duplex *p,struct sony_usb_monitor *monitor,uint64_t receipt){return attach(p,monitor,receipt,1);}
int sony_usb_duplex_attach_source(struct sony_usb_duplex *p,struct sony_usb_monitor *monitor,uint64_t receipt){return attach(p,monitor,receipt,2);}
static int close_stream(struct sony_usb_duplex *p,unsigned terminal){
    int rc=snapshot(p);if(rc)return fail(p,rc);int had_stream=p->state.active;int first=0;
#if WALKDROID_REQUIRE_USB_SOURCE
    if(p->source_input&&(p->state.input.capture_active||terminal!=WUR_SERVICE_DRAIN)){
        p->cleanup.input_stream_close=terminal==WUR_SERVICE_DRAIN?sony_usb_source_ingress_close_stream(p->source_input):sony_usb_source_ingress_cancel(p->source_input);
        if(p->cleanup.input_stream_close)first=status(p->cleanup.input_stream_close);
    }
#endif
    if(p->input&&p->state.input.capture_active){p->cleanup.input_stream_close=sony_usb_ingress_close_stream(p->input);if(p->cleanup.input_stream_close)first=status(p->cleanup.input_stream_close);}
    /* Cancellation still quiesces output when input close is uncertain. Normal
     * boundary EOF gets here only after every validated final frame was pushed. */
    if(p->output&&p->output_attempted){
        int s=sony_receiver_session_state(p->output,&p->state.output);if(s&&!first)first=status(s);
        if(!s&&p->state.output.stream_open&&!p->state.output.finished){p->cleanup.output_finish=sony_receiver_session_finish_stream(p->output,terminal);if(p->cleanup.output_finish&&!first)first=status(p->cleanup.output_finish);}
        p->cleanup.output_stream_close=sony_receiver_session_close_stream(p->output,&p->cleanup.last_stream);if(p->cleanup.output_stream_close&&!first)first=status(p->cleanup.output_stream_close);if(!p->cleanup.output_stream_close)p->output_attempted=0;
    }
    rc=snapshot(p);if(rc&&!first)first=rc;if(!p->state.input.capture_active&&!p->state.output.stream_open){p->state.active=0;if(had_stream)++p->state.streams_closed;}
    return first?fail(p,first):0;
}
static int begin(struct sony_usb_duplex *p,const struct sony_usb_event *event){
    p->state.event=*event;if(event->stream.action!=SONY_USB_PLAY)return 0;if(event->stream.format!=SONY_USB_PCM)return fail(p,-ENOTSUP);
    /* Sony route/DSP preparation stays synchronous. The source-FD candidate
     * retains USB packets in its kernel queue throughout this preparation;
     * the separate legacy/boundary candidates retain their old START behavior. */
    p->output_attempted=1;int rc=sony_receiver_session_begin_usb(p->output,&event->stream,p->launch.expected_output);if(rc)return fail(p,rc);
    rc=input_begin(p);int s=snapshot(p);if(s)return fail(p,s);
    if(rc){if(!p->boundary_mode&&rc==-ESTALE&&p->state.input.pending_valid&&!p->state.input.first_error)return close_stream(p,WUR_SERVICE_STOP);return fail(p,rc);}
    p->state.active=1;++p->state.streams_started;rc=snapshot(p);return rc?fail(p,rc):0;
}
int sony_usb_duplex_step(struct sony_usb_duplex *p,unsigned maximum){
    if(!maximum||maximum>SONY_USB_DUPLEX_MAX_FRAMES)return -EINVAL;int rc=lock(p);if(rc)return rc;
    if(p->state.sealed||p->state.closing)return unlock(p,p->state.first_error?p->state.first_error:-EPIPE);if(!p->state.attached)return unlock(p,-ENOTCONN);rc=owner_check(p);if(rc)return unlock(p,fail(p,rc));
#if WALKDROID_USB_LIVE_VOLUME
    rc=sony_receiver_session_poll_controls(p->output);if(rc<0)return unlock(p,fail(p,rc));
    p->controls_pending=rc==2;
    if(rc==1||(p->controls_pending&&!p->state.active))return unlock(p,-EAGAIN);
#endif
#if WALKDROID_USB_SETTINGS_OBSERVE
    if(!p->state.active){rc=observe_idle(p);if(rc)return unlock(p,fail(p,rc));}
#endif
    if(!p->state.active){struct sony_usb_event event;rc=input_event(p,&event);if(!rc)rc=begin(p,&event);else if(rc!=-EAGAIN&&rc!=-EINTR)fail(p,rc);return unlock(p,rc);}
    unsigned frames=0;++p->state.read_calls;rc=input_read(p,maximum,&frames);int s=snapshot(p);if(s)return unlock(p,fail(p,s));
    /* Active writes already publish validated output state. A transient empty
     * input must not insert an extra full route/config walk in that deadline.
     * A genuinely stalled stream's old publication is allowed to expire. */
    /* A validated source EOF is not a user cancellation: native Drain writes
     * the final partial ALSA period. STOP/Drop would discard those samples. */
    if(rc==SONY_USB_CAPTURE_END&&p->boundary_mode)return unlock(p,close_stream(p,WUR_SERVICE_DRAIN));
    if(rc){if(!p->boundary_mode&&rc==-ESTALE&&p->state.input.pending_valid&&!p->state.input.first_error){rc=close_stream(p,WUR_SERVICE_STOP);return unlock(p,rc);}if((rc==-EAGAIN||rc==-EINTR)&&!p->state.input.first_error&&!p->state.input.capture.sealed)return unlock(p,rc);return unlock(p,fail(p,rc));}
    if(!frames||frames>maximum)return unlock(p,fail(p,-EPROTO));if(p->state.captured_delivery_frames>UINT64_MAX-frames||p->state.offered_frames>UINT64_MAX-frames)return unlock(p,fail(p,-EOVERFLOW));p->state.captured_delivery_frames+=frames;
    if(p->launch.observe){rc=status(p->launch.observe(p->launch.context,&p->state.event,p->data,frames));if(rc)return unlock(p,fail(p,rc));p->state.observed_frames+=frames;}
    p->state.offered_frames+=frames;rc=sony_receiver_session_push(p->output,p->data,frames);if(!rc)p->state.successful_push_frames+=frames;
    s=snapshot(p);if(!rc&&s)rc=s;if(rc)fail(p,rc);return unlock(p,rc);
}
int sony_usb_duplex_quiesce(struct sony_usb_duplex *p){int rc=lock(p);if(rc)return rc;p->state.sealed=1;rc=close_stream(p,WUR_SERVICE_STOP);return unlock(p,rc);}
int sony_usb_duplex_state(struct sony_usb_duplex *p,struct sony_usb_duplex_state *out){if(!out)return -EINVAL;int rc=lock(p);if(rc)return rc;rc=snapshot(p);if(!rc)*out=p->state;return unlock(p,rc);}
int sony_usb_duplex_stop(struct sony_usb_duplex **owner,int timeout,struct sony_usb_duplex_cleanup *out){
    if(!owner||!*owner||!out||timeout<1||timeout>30000)return -EINVAL;struct sony_usb_duplex *p=*owner;int rc=lock(p);if(rc)return rc;p->state.closing=p->state.sealed=1;close_stream(p,WUR_SERVICE_STOP);
    if(p->input){p->cleanup.input_stop=sony_usb_ingress_stop(&p->input,&p->cleanup.input);p->state.input=p->cleanup.input.state;}
#if WALKDROID_REQUIRE_USB_SOURCE
    if(p->source_input){p->cleanup.input_stop=sony_usb_source_ingress_stop(&p->source_input,&p->cleanup.input,&p->source_info);p->state.input=p->cleanup.input.state;}
#endif
    if(p->output){p->cleanup.output_stop=sony_receiver_session_stop(&p->output,timeout,&p->cleanup.output);}
    p->cleanup.owner_check=owner_check(p);p->cleanup.modules_cleanup_verified=0;
    int pending=!!p->input||!!p->output;
#if WALKDROID_REQUIRE_USB_SOURCE
    pending=pending||!!p->source_input;
#endif
    p->cleanup.state=p->state;*out=p->cleanup;
    rc=p->cleanup.input_stop?p->cleanup.input_stop:p->cleanup.output_stop?p->cleanup.output_stop:p->cleanup.owner_check;
    if(pending)return unlock(p,rc?rc:-EBUSY);
    free(p);*owner=NULL;return rc;
}
int sony_usb_duplex_cleanup_pending(const struct sony_usb_duplex *p){return p&&p->output&&sony_receiver_session_cleanup_pending(p->output);}
int sony_usb_duplex_source_wait(struct sony_usb_duplex *p,int *fd,int *timeout){
    if(!fd||!timeout)return -EINVAL;int rc=lock(p);if(rc)return rc;
#if WALKDROID_REQUIRE_USB_SOURCE
    if(p->state.closing||p->state.sealed)return unlock(p,p->state.first_error?p->state.first_error:-EPIPE);
    if(!p->source_input)return unlock(p,-ENOTCONN);
    /* A claimed live source below100ms prefill is already readable but not
     * yet admitted. Avoid a level-triggered POLLIN spin during that bounded
     * interval. Normal data/new PLAY/FINAL wait on the actual source FD. */
    rc=input_state(p);if(rc)return unlock(p,rc);int f=-1,t=-1;
    if((p->controls_pending&&!p->state.active)||(p->state.input.capture_active&&!p->state.input.capture.prefilled))t=10;
    else{f=sony_usb_source_ingress_poll_fd(p->source_input);if(f<0)return unlock(p,f);}
    *fd=f;*timeout=t;return unlock(p,0);
#else
    return unlock(p,-ENOTSUP);
#endif
}
