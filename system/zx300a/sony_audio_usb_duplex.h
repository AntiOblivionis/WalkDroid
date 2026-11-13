#ifndef SONY_AUDIO_USB_DUPLEX_H
#define SONY_AUDIO_USB_DUPLEX_H
#include "sony_audio_usb_ingress.h"
#include "sony_audio_receiver_session.h"
struct sony_usb_duplex;
#define SONY_USB_DUPLEX_EXPLICIT_START 0x57554431u
#define SONY_USB_DUPLEX_MAX_FRAMES 3584u
struct sony_usb_duplex_launch {
    struct sony_receiver_session_launch downstream;
    unsigned expected_output;
    /* Optional diagnostic observer of validated source BEFORE downstream.
     * Borrowed bytes/context, serialized callback, failure seals mode. This
     * receipt is not downstream acceptance. Production normally passesNULL. */
    int (*observe)(void *,const struct sony_usb_event *,const void *,unsigned);
    void *context;
};
struct sony_usb_duplex_state {
    uint64_t generation,read_calls,captured_delivery_frames,observed_frames,offered_frames,successful_push_frames,streams_started,streams_closed;
    struct sony_usb_event event;
    struct sony_usb_ingress_state input;
    struct sony_receiver_session_state output;
    unsigned attached,active,sealed,closing;
    int first_error;
};
struct sony_usb_duplex_cleanup {
    struct sony_usb_duplex_state state;
    struct sony_usb_ingress_cleanup input;
    struct sony_receiver_session_cleanup output;
    struct sony_receiver_playback_cleanup last_stream;
    int input_stream_close,output_finish,output_stream_close,input_stop,output_stop,owner_check;
    unsigned modules_cleanup_verified,global_mode_cleanup_unverified; /* Always1. */
};
/* Start downstream children/resources BEFORE gadget activation, using borrowed
 * exact typed USB owner and existing verified roots. No owner acquire/finish,
 * gadget mutation, clock conversion, capture or sound at entry. A nonNULL
 * failure result must be stopped. The caller validates compiled idle timer gate.
 * Native children are spawned before starting its output thread. Public API is
 * serialized on its creator thread; no fork/reentrant/foreign-thread clients. */
int sony_usb_duplex_start(struct sony_usb_duplex **,unsigned activation,
    struct sony_audio_owner *,const struct sony_usb_duplex_launch *);
/* Once exact UAC mode/card4 and original monitor exist, attach input once.
 * Opens no capture until a real delivered PLAY. Borrow monitor until stop. */
int sony_usb_duplex_attach(struct sony_usb_duplex *,struct sony_usb_monitor *,uint64_t receipt);
/* Select the source-kernel boundary ingress explicitly. Never auto-detect or
 * fall back to original event-only input. All state/launch/cleanup ABI stays
 * unchanged; EOF drains validated input before FINISH and next-source OPEN. */
int sony_usb_duplex_attach_boundary(struct sony_usb_duplex *,struct sony_usb_monitor *,uint64_t receipt);
/* Mandatory queued-source candidate only. Claims the kernel's retained real
 * PLAY journal in FIFO order, including sources predating subscription. Other
 * builds return ENOTSUP; no fallback to legacy/boundary ALSA capture. */
int sony_usb_duplex_attach_source(struct sony_usb_duplex *,struct sony_usb_monitor *,uint64_t receipt);
/* Queued-source candidate's borrowed kernel wait FD. A short bounded prefill
 * wait avoids spinning on a readable queue below its100ms admission threshold.
 * Other builds return ENOTSUP. Never close or read the returned FD. */
int sony_usb_duplex_source_wait(struct sony_usb_duplex *,int *fd,int *timeout_ms);
/* Process at most one event or one <=3584-frame capture block. Original Sony
 * route/DSP setup and native writes run in one worker behind a bounded copied
 * source FIFO. A PLAY queues its OPEN and starts capture without waiting for
 * that setup; data remains behind its own source barrier. Full FIFO returns
 * EAGAIN BEFORE consuming input, with no dropped/replayed/converted samples.
 * Read transient errors retry ONLY if input is not sealed/faulted. A new event
 * closes old input and enqueues FINISH behind all its verified data; the next
 * real PLAY may enqueue after that barrier while old output is still draining.
 * offered_frames and successful_push_frames are distinct enqueue/native ACK
 * counters; output is the latest completed worker observation, not a guarantee
 * that the current input format has already opened. Every actual write still
 * performs the original session's fresh lease/config/jack/resource checks.
 * No replay or implicit restart after any data/downstream/authority error.
 * EAGAIN means no progress now; callers arrange bounded polling/deadlines.
 * Success means component acknowledgement, not physical DAC consumption. */
int sony_usb_duplex_step(struct sony_usb_duplex *,unsigned max_frames);
/* Explicit mode/user cancellation, seals producer and closes input, then asks
 * worker to stop after its in-flight call. Pending source data is explicitly
 * discarded on cancellation, whereas a normal source STOP drains its FIFO.
 * This acknowledges the cancellation request, NOT worker/child termination. */
int sony_usb_duplex_quiesce(struct sony_usb_duplex *);
int sony_usb_duplex_state(struct sony_usb_duplex *,struct sony_usb_duplex_state *);
/* Stop both exact owned modules, retaining unresolved handles/receipts. Repeat
 * can wait on the SAME downstream child/cleanup, never respawn or reauthorize.
 * Always leaves gadget/monitor/static mounts/global lease to the outer mode.
 * Even success is not permission for global CLEAN without independent checks. */
int sony_usb_duplex_stop(struct sony_usb_duplex **,int timeout_ms,struct sony_usb_duplex_cleanup *);
/* After stop returns EAGAIN, the same output worker must be joined before
 * session stop, global CLEAN, CPU/idle release or context destruction. */
int sony_usb_duplex_cleanup_pending(const struct sony_usb_duplex *);
#endif
