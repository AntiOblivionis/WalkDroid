#ifndef SONY_AUDIO_RECEIVER_IDLE_GUARD_H
#define SONY_AUDIO_RECEIVER_IDLE_GUARD_H
#include <stdint.h>
#include "sony_audio_receiver_legacy_policy.h"
/* Temporary LP PCM idle-timer value used by this receiver. The effect depends
 * on the running kernel; this module does not verify its implementation. */
#define SONY_RECEIVER_IDLE_GUARD_TIMER UINT32_MAX
struct sony_receiver_idle_observation {unsigned raw_switch,timer;};
struct sony_receiver_idle_guard_io {
    int (*check)(void *);int (*closed)(void *);
    int (*inspect)(void *,struct sony_receiver_idle_observation *);
    int (*write_timer)(void *,unsigned);void *context;
};
struct sony_receiver_idle_guard;
struct sony_receiver_idle_guard_state {
    uint64_t token;
    unsigned used,acquired,write_attempted,release_attempted,restore_attempted,restored,previous_timer;
    int first_error;
};
/* Acquire reads the previous timer, writes a temporary value, and release
 * requests restoration. Output and switch states are not independently checked. */
int sony_receiver_idle_guard_create(struct sony_receiver_idle_guard **,const struct sony_receiver_idle_guard_io *,uint64_t receipt);
int sony_receiver_idle_guard_acquire(struct sony_receiver_idle_guard *);
/* Compatibility entry point; success is not evidence of hardware state. */
int sony_receiver_idle_guard_assure(struct sony_receiver_idle_guard *,unsigned raw,unsigned operation);
int sony_receiver_idle_guard_release(struct sony_receiver_idle_guard *);
int sony_receiver_idle_guard_state(struct sony_receiver_idle_guard *,struct sony_receiver_idle_guard_state *);
int sony_receiver_idle_guard_destroy(struct sony_receiver_idle_guard **);
/* Composition with the existing latency-only transaction. */
struct sony_receiver_mode_policy {
    struct sony_dsd_power_platform_io io;
    struct sony_receiver_legacy_policy_platform *latency;
    struct sony_receiver_idle_guard *idle;
    int first_error,busy;
    /* Retained for source compatibility; assure no longer invokes these. */
    int (*observation_begin)(void *);
    int (*observation_end)(void *);
    void *observation_context;
};
int sony_receiver_mode_policy_assure(void *,unsigned operation);
#endif
