#ifndef SONY_AUDIO_RECEIVER_LEGACY_OUTPUT_H
#define SONY_AUDIO_RECEIVER_LEGACY_OUTPUT_H
#include "sony_audio_output_timing.h"
#include <stdint.h>
struct sony_receiver_legacy_output;
enum sony_receiver_legacy_policy_operation {
    SONY_RECEIVER_LEGACY_REQUIRE_LOW=1,
    SONY_RECEIVER_LEGACY_REQUIRE_IDLE_BLOCKED=2,
    SONY_RECEIVER_LEGACY_KEEP_IDLE_BLOCKED_ON_CLOSE=3,
    SONY_RECEIVER_LEGACY_BEFORE_PCM_OPEN=4,
    SONY_RECEIVER_LEGACY_STREAM_CHECK=5
};
struct sony_receiver_legacy_policy {
    /* Real caller retains global receiver owner, prepared muted physical route,
     * exact CFW/config and parent resource transaction. Check/confirm actual
     * low latency and paused deep-idle policy, including on every request.
     * Child has no sysfs/Amixer mutation authority. Closing's native request to
     * enable deep idle is acknowledged as deferred; guard stays held. Callback
     * success is never hardware cleanup or authority to release global owner. */
    int (*assure)(void *,enum sony_receiver_legacy_policy_operation);
    void *context;
};
struct sony_receiver_legacy_state {
    struct sony_output_timing timing;
    uint64_t accepted_frames,alsa_confirmed_frames;
    unsigned buffered_frames,drain_padding_frames,discarded_buffered_frames;
    unsigned profile,mode,opened,configured,sealed,drained,failed;
    unsigned pcm_open_forwarded,low_requests,idle_block_requests,idle_release_requests,recovery_refused;
    unsigned io_blocked_after_error;
    int first_error;
};
struct sony_receiver_legacy_cleanup {
    struct sony_receiver_legacy_state state;
    int drop_status,close_status,retained,output_cleanup_unverified;
};
/* ARM32/glibc Sony legacy factories1(USB)/2(BT receiver). The backend
 * receives the requested rate; actual format support depends on that backend.
 * Original untouched libraries, source-specific buffering and actual S32 PCM.
 * Requires receiver-legacy-io before this DSO in global/preload order. Native Sony
 * control requests are intercepted only for the exact bound object/thread;
 * unexpected requests fail and block PCM Open, never execute a shell/sysfs.
 * This module cannot acquire/spawn/route/unmute or implement a policy backend.
 * *out startsNULL; failures after allocation retain it for mandatory close. */
int sony_receiver_legacy_open(struct sony_receiver_legacy_output **,void *adler,void *alsa,
    unsigned profile,unsigned mode,unsigned rate,const struct sony_receiver_legacy_policy *);
/* Same creating process/thread; serialized, no callback reentry. Native
 * admission and complete ALSA periods are counted separately, checking native
 * pending bytes and actual IO against the exact period geometry. Any failed/partial
 * operation seals; counters never authorize replay. Original auto-recovery is
 * refused before it can reopen an output or replay uncertain data. */
int sony_receiver_legacy_write(struct sony_receiver_legacy_output *,const int *,unsigned frames);
int sony_receiver_legacy_drain(struct sony_receiver_legacy_output *);
int sony_receiver_legacy_state(struct sony_receiver_legacy_output *,struct sony_receiver_legacy_state *);
/* Caller mutes before close. Undrained/faulted output uses original Drop then
 * Close. A busy IO guard before action retains a retryable handle; an attempted
 * uncertain Close with PCM still held retains a sealed handle, no close retry.
 * Child/supervisor and independent hardware/resource checks still mandatory. */
int sony_receiver_legacy_close(struct sony_receiver_legacy_output **,struct sony_receiver_legacy_cleanup *);
#endif
