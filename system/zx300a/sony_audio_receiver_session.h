#ifndef SONY_AUDIO_RECEIVER_SESSION_H
#define SONY_AUDIO_RECEIVER_SESSION_H
#include "sony_audio_owner.h"
#include "sony_audio_receiver_playback.h"
#include "sony_audio_receiver_process.h"
#include "sony_audio_receiver_sink_process.h"
#include "sony_audio_controls.h"
#include "sony_audio_usb_stream.h"
#include "sony_audio_receiver_pcm.h"
struct sony_receiver_session;
#define SONY_RECEIVER_SESSION_EXPLICIT_START 0x57525331u
#define SONY_RECEIVER_SESSION_VERIFY_MUTED 1u
/* Explicit experimental optimization. Keep only a verified muted route between
 * successfully finished USB streams under the SAME borrowed mode owner. No PCM
 * or DSP stream remains open. Stop/error/invalidation must close the route.
 * Default remains off; changing this flag requires a newly tested backend. */
#define SONY_RECEIVER_SESSION_KEEP_ROUTE 2u
#define SONY_RECEIVER_SESSION_LIVE_VOLUME 4u
/* BT only, explicit candidate: group unchanged Sony output into actual native
 * periods. Short pending output is flushed before DSP/DAC volume mutation. */
#define SONY_RECEIVER_SESSION_BT_PERIOD_WRITES 8u
/* Adapter may retain511frames. A3585-frame call still fits its4096-frame FIFO
 * before native packets are consumed; callers split larger capture reads. */
#define SONY_RECEIVER_SESSION_MAX_INPUT_FRAMES 3585u
struct sony_receiver_session_launch {
    int dsp_root,sink_root,dsp_log,sink_log;
    const char *private_config; /* Directory for the session's configuration. */
    uint64_t cookie; /* Optional caller-provided session metadata. */
    unsigned flags;int timeout_ms;
};
struct sony_receiver_session_state {
    uint64_t generation,cookie,stream_index,source_accepted_frames,source_taken_frames;
    struct sony_receiver_playback_state playback;
    unsigned enabled,stream_open,finished,started,muted,cfw_profile,source_rate,source_bits,output_rate;
    int first_error,config_error;
};
struct sony_receiver_session_cleanup {
    struct sony_receiver_playback_cleanup playback;
    struct sony_receiver_process_exit dsp;
    struct sony_receiver_sink_process_exit sink;
    int stream_close,dsp_shutdown,sink_shutdown,dsp_stop,sink_stop,route_end;
    int latency_destroy,idle_release,idle_destroy,platform_destroy,independent_idle;
    unsigned output_resources_verified,config_verified;
    int first_error,config_error;
    /* Always1: input capture/gadget/controller and global lease remain owned
     * by the outer mode. This module NEVER acquires or finishes a global owner. */
    unsigned input_and_global_cleanup_unverified;
};
/* Production downstream receiver session. Borrow the exact USB_DAC owner for
 * its whole mode, before gadget/capture activation. It remains held through
 * idle/stream boundaries, settings observations and all pending cleanup.
 * Caller must validate the B82 compiled max-timer gate before deployment.
 * Roots/runtime mounts are prepared separately and verified by supervisors.
 * Private config is shared by both roots; immutable CFW comes from real NVP.
 * The module prepares actual guarded resources and children but no output at
 * entry. No sound, capture/gadget changes, global arbitration or auto restart.
 * SourceDirect feature1 is the validated original capability. Starts *out=NULL;
 * a nonNULL failure result must be stopped, never overwritten/restarted. */
int sony_receiver_session_start_usb(struct sony_receiver_session **,unsigned activation,
    struct sony_audio_owner *borrowed_owner,const struct sony_receiver_session_launch *);
/* Separate fixed BT_RX owner/factory2 lifetime. Owns the same guarded native
 * DSP/CFW output session, never the radio/controller. Borrowed owner remains
 * held through idle and every stream boundary until upstream+downstream cleanup
 * is independently proven by the outer BT controller. No HCI takeover here. */
int sony_receiver_session_start_bt(struct sony_receiver_session **,unsigned activation,
    struct sony_audio_owner *borrowed_owner,const struct sony_receiver_session_launch *);
/* One source-format lifetime; DSP service metadata exactly matches Sony USB
 * TrackParam. expected_output is exact SE/BTL, checked against real GPIO/NVP.
 * No headphone/unsupported source tuple rejects without releasing outer mode.
 * Route is prepared muted; actual DSP output lazily opens the original USB
 * Legacy sink at its actual rate. No resampling/software gain/mixing here. */
int sony_receiver_session_begin_usb(struct sony_receiver_session *,const struct sony_usb_stream *,unsigned expected_output);
/* Original decoded BT format. USB and BT entry points reject the other mode
 * before route/output mutation. Caller closes a prior stream before changing
 * codec/rate. Native S16 or LDAC24/container4, copied without USB conversion. */
int sony_receiver_session_begin_bt(struct sony_receiver_session *,const struct sony_bt_pcm_stream *,unsigned expected_output);
/* Actual USB S32 stereo container, <=MAX_INPUT_FRAMES per call, exact16/24-bit lower
 * bit validation and512-frame native packet adaptation. Source admission and
 * downstream confirmation remain distinct; an error NEVER authorizes replay. */
int sony_receiver_session_push(struct sony_receiver_session *,const void *,unsigned frames);
/* Caller first stops upstream at a known boundary. Exact adapter remainder is
 * sent before original STOP2/DRAIN4. No invented source silence or data retry. */
int sony_receiver_session_finish_stream(struct sony_receiver_session *,unsigned terminal);
/* Close DSP/output stream, leave children, resources and outer mode owned.
 * Normal closed streams permit a new format. Uncertain/failed data sessions
 * seal admission; stop the session, independently verify outer cleanup and
 * wait for an explicit new upstream lifetime before creating a fresh cookie.
 * Not a force-stop/restart loop and never replays the failed source. */
int sony_receiver_session_close_stream(struct sony_receiver_session *,struct sony_receiver_playback_cleanup *);
int sony_receiver_session_state(struct sony_receiver_session *,struct sony_receiver_session_state *);
/* Fresh read-only owner/config/route-or-idle validation, followed by publishing
 * the current USB state. No PCM open, audio injection, parameter/resource
 * changes or copying of an expired public snapshot. Used while awaiting a USB
 * source, so settings can distinguish armed USB from unavailable ownership. */
int sony_receiver_session_observe(struct sony_receiver_session *);
/* Serialized volume-control primitives for a participating USB controller.
 * Fresh getter checks owner, protected config and actual active/parked/idle
 * route. Setter binds this generation and revision, applies the original AVLS
 * limit, notifies the current native DSP without flushing source, adjusts and
 * reads the real DAC volume, then CAS-saves/readbacks global/private snapshots.
 * Idle changes are saved for the next real stream without powering the DAC.
 * No output/gain/effect change, channel creation or UI capability is implied.
 * Caller must coordinate its authenticated request/receipt server separately.
 * Same value is a verified no-op. Out parameters stay unchanged on rejection.
 * A mutation/save uncertainty seals the session and mutes; never retry it. */
int sony_receiver_session_get_controls(struct sony_receiver_session *,struct sony_controls *,unsigned *revision);
int sony_receiver_session_volume(struct sony_receiver_session *,uint64_t generation,unsigned expected_revision,
    unsigned volume,unsigned *applied_revision);
/* Optional root-peer command channel enabled by LIVE_VOLUME at launch. One
 * serialized controller calls poll between source batches and while idle.
 * STAGE replies old+pending before a subsequent poll performs the setter once.
 * Return1 pauses source until the staged hardware update starts. Return2
 * means a one-shot SCHED_OTHER writer is persisting it: feed the current
 * stream, but admit no next source. STATUS remains old+pending until saved.
 * A disconnected client does not replay/cancel that staged operation. Stop
 * closes this channel before output/owner teardown. No flag => verified no-op. */
int sony_receiver_session_poll_controls(struct sony_receiver_session *);
/* Borrowed command-readiness and save-completion fds for the same serialized
 * controller. Observe with poll only; never read/close them. The usual poll
 * function accepts/reads and joins/publishes. ENOTSUP means no live channel.
 * No ownership transfer or independent session thread is created. */
int sony_receiver_session_control_descriptors(struct sony_receiver_session *,int descriptors[2]);
/* Explicit BT startup only, before any source/route/async worker. Promote the
 * two exact, still-owned ready children to RR2 after SCHED_OTHER setup. The
 * existing500000/1000000us watchdog must be inherited and remains unchanged.
 * No caller-supplied PID/policy, no reap, and no retry after a partial error. */
int sony_receiver_session_promote_audio(struct sony_receiver_session *);
/* An admitted writer still owns this session. Stop closes PCM first, returns
 * EAGAIN while the writer runs, and must be called again before releasing the
 * borrowed global owner or destroying any session/config storage. */
int sony_receiver_session_cleanup_pending(const struct sony_receiver_session *);
/* Stop owned downstreams and restore only their exact resources. Retain this
 * object/backends if a child or resource receipt is unresolved. Global owner
 * stays borrowed/alive even on success. Caller still stops and verifies input,
 * restores USB gadget and independently checks final state before global CLEAN.
 * Serialized controller only; no concurrent clients/waitpid or callback reentry.
 */
int sony_receiver_session_stop(struct sony_receiver_session **,int timeout_ms,struct sony_receiver_session_cleanup *);
#endif
