#ifndef SONY_AUDIO_BT_LIVE_SOURCE_H
#define SONY_AUDIO_BT_LIVE_SOURCE_H
#include "sony_audio_bt_live_wire.h"
/* One fixed native-format lifetime. Storage starts zeroed and stays alive until
 * the external native callbacks have independently stopped. No auto reset. */
struct sony_bt_live_source {
    int fd,error;pid_t parent;uid_t uid;uint64_t cookie,source,sequence,frames,bytes,control_sequence;
    unsigned initialized,format_state,started,ready,ready_seen,closing,busy,finished_attempted,sealed,stop_requested,listening;
    unsigned packets,closing_packets;uint64_t closing_bytes;
    struct sony_bt_middleware_sound raw;unsigned frame_bytes,send_buffer;
    void *transport; /* Fixed prefaulted queue, retained through native exit. */
    unsigned queued_peak_frames,offered_packets;uint64_t offered_frames,offered_bytes;
    uint64_t volume_offered,volume_sent,volume_sequence,volume_closing_drops;
};
int sony_bt_live_source_init(struct sony_bt_live_source *,int fd,pid_t parent,uint64_t cookie,uint64_t source);
int sony_bt_live_source_format(struct sony_bt_live_source *,const struct sony_bt_middleware_sound *);
/* Main thread, after native RF/database/listen success, before FORMAT. */
int sony_bt_live_source_listening(struct sony_bt_live_source *);
/* Main thread publishes the format and polls control events. Poll does not
 * authenticate the sender or correlate cookie, source or sequence fields.
 * Poll accepts STOP before the first format and returns1 for READY,2 for STOP,
 * 0 for no message,negative on error. Only then call native SetPlayerState(1).
 * The flag returned by ready() is the GetPlayerState output value. */
int sony_bt_live_source_start(struct sony_bt_live_source *);
int sony_bt_live_source_poll(struct sony_bt_live_source *);
/* Raw relative AVRCP callbacks, without a component-level peer check.
 * Bounded64-event queue; no send/wait/PCM/DSP/DAC work in the callback.
 * Concurrent control callbacks or overflow seal rather than lose a command.
 * Absolute volume and registration remain no-op as in the saved native GUI. */
int sony_bt_live_source_volume_step(struct sony_bt_live_source *,int step);
/* Main controller only, including while waiting for an audio format. Sends
 * at most16 events, and retains whole EAGAIN records for <=2s. Event sequence
 * is separate from PCM. Finish flushes accepted events BEFORE terminal audio. */
int sony_bt_live_source_poll_volume(struct sony_bt_live_source *);
unsigned sony_bt_live_source_ready(const struct sony_bt_live_source *);
/* Callback: one bounded prefaulted SPSC copy (<=2s including sender in-flight).
 * No allocation, blocking wait, retry or native/DSP operation. Concurrent PCM
 * callbacks or changed format seal this lifetime rather than mislabel bytes.
 * A separate sender may poll/retry only a whole packet for which EAGAIN admits
 * nothing. Other send errors and a full source queue seal, never overwrite. */
int sony_bt_live_source_pcm(struct sony_bt_live_source *,unsigned bytes,const void *);
/* Native DROP or other upstream error: seal admission and
 * preserve the first negative errno. No RF/DSP operation in the callback. */
int sony_bt_live_source_error(struct sony_bt_live_source *,int negative_errno);
/* Main: clear readiness, call original SetPlayerState(0), independently quiesce
 * callbacks, then finish. EAGAIN before finish's first send means a callback is
 * still in flight; retain storage. No admission after stop_begin. An empty
 * mode can finish without start/FORMAT; its QUIET summary must be exactly0. */
int sony_bt_live_source_stop_begin(struct sony_bt_live_source *);
int sony_bt_live_source_finish(struct sony_bt_live_source *);
#endif
