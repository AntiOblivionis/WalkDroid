#ifndef SONY_AUDIO_USB_CAPTURE_H
#define SONY_AUDIO_USB_CAPTURE_H
#include "sony_audio_usb_stream.h"
#include "sony_audio_usb_boundary.h"
struct sony_usb_capture;
#define SONY_USB_CAPTURE_EXPLICIT_START 0x57554331u
/* Historical chunk size; callers may provide larger buffers. */
#define SONY_USB_CAPTURE_MAX_READ_FRAMES 4096u
enum sony_usb_capture_stage {SONY_USB_CAPTURE_OPEN=1,SONY_USB_CAPTURE_PREFILL=2,SONY_USB_CAPTURE_READ=3};
struct sony_usb_capture_scope {
    /* Legacy callbacks retained for API compatibility; not admission gates. */
    int (*identity_check)(void *);
    int (*capture_closed)(void *);
    int (*source_check)(void *,enum sony_usb_capture_stage);
    void *context;
};
struct sony_usb_capture_state {
    struct sony_usb_event event;
    uint64_t captured_frames,returned_frames,read_attempts,transient_reads;
    unsigned period_frames,buffer_frames,prefill_frames,last_available,max_available;
    unsigned open_attempted,prepare_attempted,prepared,start_attempted,started,prefilled,sealed;
    int first_error;
};
struct sony_usb_capture_cleanup {
    struct sony_usb_capture_state state;
    int stop_status,close_status,fd_identity_status,independent_closed_status;
    unsigned stop_attempted,close_attempted,capture_closed_verified; /* Independent state was not checked. */
    unsigned global_mode_cleanup_unverified; /* Always1. */
};
/* Separate entry contract; the legacy scope/state ABI is unchanged. The
 * boundary path reads snapshots through the caller's borrowed context. */
struct sony_usb_capture_boundary_scope {
    struct sony_usb_capture_scope admission;
    int (*snapshot)(void *,struct sony_usb_boundary *);
};
struct sony_usb_capture_boundary_info {
    struct sony_usb_boundary_reader reader;
    struct sony_usb_boundary last;
    unsigned ready,finished;
};
#define SONY_USB_CAPTURE_END 1
/* One source lifetime. This path requests S32_LE stereo using the calculated
 * ALSA rate, regardless of the event's format or width. Callers choosing a
 * different source format must provide any necessary conversion themselves.
 * Original500ms/10ms requests. No gadget or parameter writes, global-owner
 * operations, mixing, gain or resampling.
 * Caller prepares f_start100 first and holds typed USB mode owner throughout.
 * *out startsNULL; failure after allocation returns a handle needing close.
 * Uses card4 PCM0c, prepares once and starts. A later XRUN is returned.
 * Calls are serialized, callback reentry BUSY, poll FD stays borrowed. */
int sony_usb_capture_begin_pcm(struct sony_usb_capture **,unsigned activation,const struct sony_usb_event *,const struct sony_usb_capture_scope *);
int sony_usb_capture_begin_boundary_pcm(struct sony_usb_capture **,unsigned activation,const struct sony_usb_event *,const struct sony_usb_capture_boundary_scope *);
int sony_usb_capture_boundary_info(struct sony_usb_capture *,struct sony_usb_capture_boundary_info *);
int sony_usb_capture_poll_fd(struct sony_usb_capture *);
/* Read actual old-kernel STATUS. Returns EAGAIN until original derived prefill
 * round(actual_buffer_frames*100/500), without consuming any samples. Caller
 * then applies/validates feedback2/5/5 before READ. No blocking waits here. */
int sony_usb_capture_prefill(struct sony_usb_capture *,unsigned *available);
/* Direct nonblocking READI_FRAMES, one syscall per call, no pcm_read/EPIPE
 * recovery or automatic retry. EAGAIN/EINTR mean no returned data; every other
 * failure seals. Capture consumption and returned frames remain separate.
 * Sample bytes are returned as received without inspecting their lower bits.
 * out_frames unchanged on failure; never replay on error.
 * Larger downstream session chunks must be split at its own3585-frame limit. */
/* Handles created with begin_boundary_pcm use producer snapshots for cursor
 * accounting. Snapshot identity, format and before/after values do not gate
 * a read; the caller coordinates source changes and ALSA ring ownership.
 * SONY_USB_CAPTURE_END means the current final cursor was reached. */
int sony_usb_capture_read(struct sony_usb_capture *,void *,unsigned capacity_frames,unsigned *out_frames);
int sony_usb_capture_state(struct sony_usb_capture *,struct sony_usb_capture_state *);
/* Close the locally held PCM once; a successful call does not independently
 * establish hardware state, DSP stop, parameter restore or global cleanup. */
int sony_usb_capture_close(struct sony_usb_capture **,struct sony_usb_capture_cleanup *);
#endif
