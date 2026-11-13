#ifndef SONY_AUDIO_RECEIVER_PLAYBACK_H
#define SONY_AUDIO_RECEIVER_PLAYBACK_H
#include "sony_audio_receiver_client.h"
#include "sony_audio_receiver_output.h"
#include "sony_audio_pcm_sink_client.h"
struct sony_receiver_playback;
struct sony_receiver_playback_owner {
    /* Caller retains the real global owner/config/physical route throughout.
     * started verifies opened PCM and may enable the prepared muted route.
     * mute retires public state and mutes, retaining the selected output until
     * native Drop/Drain/Close; quiesce ends the route after Close. No lease is
     * established by these callbacks themselves. One serialized controller. */
    int (*check)(void *);
    int (*started)(void *,unsigned actual_rate);
    int (*mute)(void *);
    int (*quiesce)(void *);
    void *context;
};
struct sony_receiver_playback_state {
    uint64_t source_acked_frames,native_acked_bytes,sink_acked_bytes;
    unsigned rate,source_final,terminal,finished,drain_accepted,failed,cancel_requested;
    int first_error,dsp_status,sink_status,mute_status,quiesce_status;
};
struct sony_receiver_playback_cleanup {
    int dsp_close,sink_close,mute,quiesce,first_error;
    unsigned dsp_attempted,sink_attempted,terminal,finished,drain_accepted;
    int output_cleanup_unverified; /* Always1; no acoustic/power/owner proof. */
};
/* Borrow idle clients from exact verified supervisors. Settings/format/config
 * immutable for lifetime. No spawning, target choice, capture, persistence,
 * source packet adaptation or global arbitration. Callback reentry is BUSY.
 * Out starts NULL; failure after allocation retains it for mandatory close.
 * Output opens lazily using actual original DSP format/rate. No gain, mixing,
 * resampling, sample replay or zero-padding is added by this coordinator. */
int sony_receiver_playback_start(struct sony_receiver_playback **,
    struct sony_receiver_client *,struct sony_pcm_sink_client *,const struct wur_open *,
    const struct sony_receiver_playback_owner *);
/* Explicit BT latency2 service and native BT output container. Caller must
 * supply the separately verified factory2 sink and retain the BT_RX owner. */
int sony_receiver_playback_start_bt(struct sony_receiver_playback **,
    struct sony_receiver_client *,struct sony_pcm_sink_client *,const struct wur_open *,
    const struct sony_receiver_playback_owner *);
/* Explicit BT-only packetization experiment. Preserve each Sony output sample
 * and order; write one actual sink period at a time. Holds less than one
 * period (bounded by WDP_MAX_FRAMES), never pads or waits for a timer. Existing
 * start/start_bt retain immediate writes. A new backend must opt in. */
int sony_receiver_playback_start_bt_period(struct sony_receiver_playback **,
    struct sony_receiver_client *,struct sony_pcm_sink_client *,const struct wur_open *,
    const struct sony_receiver_playback_owner *);
/* Submit a short pending period before changing DSP/DAC controls. No-op for
 * immediate mode. Errors seal and forbid replay, including uncertain writes.
 * STOP/DRAIN finish flushes after all original Sony tail callbacks. Abort
 * close drops pending data; it never adds a write after mute/failure. */
int sony_receiver_playback_flush(struct sony_receiver_playback *);
/* Local buffer observation only; not hardware consumption or lease authority. */
int sony_receiver_playback_pending(struct sony_receiver_playback *,unsigned *frames,unsigned *period);
/* Native sourceS16/S32 from receiver_pcm;512frames, or final1..511. All locally
 * invalid inputs reject before mutation. Any RPC/owner/output failure seals;
 * unchanged source counters after error NEVER authorize replay. */
int sony_receiver_playback_push(struct sony_receiver_playback *,const void *,unsigned frames,int final);
/* STOP2: original held-tail/fade/pad/STOP -> mute -> native Drop/Close.
 * DRAIN4: original held-tail/pad/EOS/STOP -> native Drain -> mute -> Close.
 * Then quiesce route. Same terminal success is idempotent; other mode rejected.
 * Empty source never opens output. Does not close DSP/shutdown/reap clients or
 * release global ownership. Actual upstream capture must already be stopped. */
int sony_receiver_playback_finish(struct sony_receiver_playback *,unsigned terminal);
int sony_receiver_playback_state(struct sony_receiver_playback *,struct sony_receiver_playback_state *);
void sony_receiver_playback_cancel(struct sony_receiver_playback *);
/* Abort if unfinished: mute -> sink Close -> DSP Close -> quiesce. No automatic
 * Drain/extra source/terminal on abort and no retry of uncertain CLOSE. Caller
 * then SHUTDOWN/reaps both workers and independently verifies route/all PCMs/
 * resources before CLEAN. first_error remains in receipt even if closes pass.
 * Join other callers/cancel users before close. BUSY retains the handle. */
int sony_receiver_playback_close(struct sony_receiver_playback **,struct sony_receiver_playback_cleanup *);
#endif
