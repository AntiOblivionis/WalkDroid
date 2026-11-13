#ifndef SONY_AUDIO_RECEIVER_LEGACY_POLICY_H
#define SONY_AUDIO_RECEIVER_LEGACY_POLICY_H
#include "sony_audio_dsd_power_platform.h"
#include "sony_audio_receiver_legacy_output.h"
/* Resource plan used by the current receiver implementation. */
#define SONY_RECEIVER_LEGACY_RESOURCE_PLAN {0,0,1,1,{0,0,0}}
struct sony_receiver_legacy_policy_platform;
struct sony_receiver_legacy_policy_state {
    uint64_t token;
    unsigned used,acquired,write_attempted,release_attempted,restore_attempted,restored;
    unsigned previous_latency,previous_deep_idle;
    int first_error;
};
/* Reads previous latency and requests restoration on release. Only playback
 * latency is mutated; no resource readback or process identity check is made. */
int sony_receiver_legacy_policy_create(struct sony_receiver_legacy_policy_platform **,
    const struct sony_dsd_power_platform_io *,uint64_t exact_reserved_receipt);
int sony_receiver_legacy_policy_backend(struct sony_receiver_legacy_policy_platform *,struct sony_dsd_power_backend *);
/* Compatibility entry point; success is not evidence of hardware state. */
int sony_receiver_legacy_policy_assure(struct sony_receiver_legacy_policy_platform *,enum sony_receiver_legacy_policy_operation);
int sony_receiver_legacy_policy_state(struct sony_receiver_legacy_policy_platform *,struct sony_receiver_legacy_policy_state *);
/* Attempts to restore the saved latency before freeing; errors can be retried. */
int sony_receiver_legacy_policy_destroy(struct sony_receiver_legacy_policy_platform **);
#endif
