#ifndef SONY_AUDIO_USB_INGRESS_H
#define SONY_AUDIO_USB_INGRESS_H
#include "sony_audio_usb_capture.h"
#include "sony_audio_usb_input_linux.h"
#include "sony_audio_usb_monitor.h"
struct sony_usb_ingress;
#define SONY_USB_INGRESS_EXPLICIT_START 0x57554931u
struct sony_usb_ingress_state {
    uint64_t generation,receipt,streams_started,streams_closed,events_delivered,parameter_checks;
    uint64_t captured_frames,returned_frames;
    struct sony_usb_event current,pending;
    struct sony_usb_input_policy_state policy;
    struct sony_usb_capture_state capture;
    unsigned event_claimable,capture_active,pending_valid,closing;
    int first_error;
};
struct sony_usb_ingress_cleanup {
    struct sony_usb_ingress_state state;
    struct sony_usb_capture_cleanup last_capture;
    int capture_close_status,parameter_release_status,linux_destroy_status;
    unsigned input_cleanup_verified,global_mode_cleanup_unverified; /* Always1. */
};
/* Compose real parameter IO/policy + original-event authority + strict capture.
 * The outer controller opens monitor before arming its host source, then
 * creates this object AFTER exact audio_func,adb/card4 activation. This old
 * kernel registers protocol24 only with UAC enabled: subscription can follow
 * activation, but an already-playing source is NEVER inferred as fresh PLAY.
 * A missed first event must time out or wait for a genuinely new kernel PLAY.
 * Both monitor and
 * typed USB owner are borrowed until this object and all downstream work end.
 * Never changes gadget, acquires/finishes global ownership or closes monitor.
 * Serialized API, callback reentry BUSY. Failure may leave a
 * nonNULL handle with an owned parameter receipt: stop is still mandatory. */
int sony_usb_ingress_create(struct sony_usb_ingress **,unsigned activation,
    struct sony_audio_owner *,struct sony_usb_monitor *,uint64_t receipt);
/* The boundary path reads its provider when capture actually needs it.
 * The legacy create/state ABI remains available. */
int sony_usb_ingress_create_boundary(struct sony_usb_ingress **,unsigned activation,
    struct sony_audio_owner *,struct sony_usb_monitor *,uint64_t receipt);
int sony_usb_ingress_boundary_info(struct sony_usb_ingress *,struct sony_usb_capture_boundary_info *);
/* No active capture allowed. A real delivered PLAY can be claimed once; no
 * synthetic event argument or reuse of a previous PLAY. Pending STOP/new PLAY
 * is delivered only after old capture closes. EAGAIN preserves current claim. */
int sony_usb_ingress_next_event(struct sony_usb_ingress *,struct sony_usb_event *);
int sony_usb_ingress_begin_pcm(struct sony_usb_ingress *);
/* Borrowed exact capture FD for caller poll only; never close or reuse it. */
int sony_usb_ingress_poll_fd(struct sony_usb_ingress *);
/* READ includes actual prefill and requests feedback2/5/5.
 * This backend is not performance-accepted yet.
 * No gain/resample/mix/replay. out_frames unchanged on error. EAGAIN/EINTR
 * are retryable ONLY while state.capture.sealed==0 and first_error==0.
 * A latched new event seals old input but permits explicit close + next PLAY. */
int sony_usb_ingress_read(struct sony_usb_ingress *,void *,unsigned,unsigned *out_frames);
/* For a boundary-created ingress, read returns SONY_USB_CAPTURE_END only
 * after the immutable old NORMAL/CHANGE final cursor is fully consumed.
 * Pending STOP/new PLAY is latched, not treated as permission to discard a
 * valid already-read block. Close old capture before next_event. */
int sony_usb_ingress_close_stream(struct sony_usb_ingress *);
int sony_usb_ingress_state(struct sony_usb_ingress *,struct sony_usb_ingress_state *);
/* Stop/close capture, reverse restore parameters, and close owned references.
 * A retained resource can be retried after an actual operation failure.
 * Global/gadget/downstream cleanup remains the outer controller's responsibility. */
int sony_usb_ingress_stop(struct sony_usb_ingress **,struct sony_usb_ingress_cleanup *);
#endif
