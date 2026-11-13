#ifndef SONY_AUDIO_DSD_POWER_BRIDGE_H
#define SONY_AUDIO_DSD_POWER_BRIDGE_H
#include <stdint.h>
/* Private native-DSD worker ONLY. Never preload into ordinary PCM/system apps.
 * Original Sony power/latency/sysfs intents are delegated to one exact owned
 * platform transaction. The original service IPC and hard-coded restore writes
 * are not forwarded. Backend must actually acquire this plan, capture previous
 * state, preserve the suspend guard and acknowledge exact-token restoration.
 * This bridge is not itself an Android/sysfs power backend or device selector.
 * Caller pins original ARM32 library/ABI and the native factory4/5 object. */
#define SONY_DSD_POWER_UNCERTAIN -117
struct sony_dsd_power_plan {
    unsigned int minimum_cpus,minimum_khz,block_deep_idle,low_latency,timed_mute_ms[3];
};
struct sony_dsd_power_backend {
    /* On any partial/uncertain acquisition return a nonzero receipt token,
     * even on error. Token zero on failure means proven no remaining effects. */
    int (*acquire)(void *,const struct sony_dsd_power_plan *,uint64_t *token);
    /* Zero only after independently proving all relevant physical PCM closed. */
    int (*output_closed)(void *);
    /* No automatic retry. Zero acknowledges complete exact-token restoration
     * of the captured prior power state, including after a failed native Open. */
    int (*release)(void *,uint64_t token);
    void *context;
};
struct sony_dsd_power_report {
    struct sony_dsd_power_plan plan;
    uint64_t token;
    unsigned int acquire_attempted,acquire_confirmed,release_attempted,release_confirmed;
    unsigned int pcm_open_forwarded,pcm_close_forwarded,pcm_calls_blocked;
    unsigned int cpu_request,idle_block_request,timed_mute_mask,latency_request;
    unsigned int cpu_restore_request,idle_release_request,native_release_request;
    unsigned int completed,resource_retained;
    int first_error,native_open_result,native_close_result,verify_closed_status,release_status;
};
struct sony_dsd_power_bridge;
/* Resolve unhandled Sony calls through the caller's library, including when
 * it was loaded with local symbol visibility. The caller keeps it alive. */
void sony_dsd_power_set_library(struct sony_dsd_power_bridge *,void *library);
/* Binds without power/device access. Remains scoped to this owner thread until
 * destroy. A second bridge is refused. Original object must remain alive. */
int sony_dsd_power_bind(struct sony_dsd_power_bridge **,void *native_object,
                        const struct sony_dsd_power_backend *);
/* Called exactly after the original Open returns. Its return code cannot
 * override a failed backend acquisition or missing intercepted native intent. */
int sony_dsd_power_open_result(struct sony_dsd_power_bridge *,int native_result);
int sony_dsd_power_begin_close(struct sony_dsd_power_bridge *);
/* Caller first invokes original Close (and audited Drop if needed). This then
 * verifies physical closure and releases the backend receipt even when the
 * original Open failed before creating a PCM. Uncertain release is not retried.
 * If physical closure was not yet proven, only its read-only verification may
 * be repeated; release is still attempted at most once. */
int sony_dsd_power_close_result(struct sony_dsd_power_bridge *,int native_result);
int sony_dsd_power_state(struct sony_dsd_power_bridge *,struct sony_dsd_power_report *);
/* Refuses while any unconfirmed receipt/closure remains; no implied cleanup. */
int sony_dsd_power_destroy(struct sony_dsd_power_bridge **);
#endif
