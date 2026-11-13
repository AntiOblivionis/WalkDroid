#ifndef WALKDROID_SONY_APP_POLICY_ADAPTER_H
#define WALKDROID_SONY_APP_POLICY_ADAPTER_H
#include "sony_audio_app_policy.h"
#include "sony_audio_bt_policy_route.h"
namespace android {
typedef AudioPolicyInterface *(*SonyPolicyCreate)(AudioPolicyClientInterface *);
typedef void (*SonyPolicyDestroy)(AudioPolicyInterface *);
struct SonyPolicyAdapter;
typedef status_t (*SonyPolicyControlHook)(void *,audio_devices_t,audio_policy_dev_state_t,const char *,const char *);
typedef void (*SonyPolicyBtNotify)(void *);
/* Factory adapter. All calls, including control and retirement, must
 * be serialized by the owning AudioPolicyService lock. No Binder endpoint or
 * app-supplied configuration is provided by this interface. create/destroy
 * are supplied factory functions; their bytes are not checked here. Output
 * authority stays with the underlying APM. Selection is supplied by the caller.
 * No USB/RX/ALSA ownership, physical drain or PCM fidelity is inferred here. */
__attribute__((visibility("default"))) status_t sony_policy_adapter_create(SonyPolicyCreate,SonyPolicyDestroy,
    AudioPolicyClientInterface *,SonyPolicyAdapter **,AudioPolicyInterface **);
__attribute__((visibility("default"))) status_t sony_policy_adapter_select_uid(SonyPolicyAdapter *,int32_t);
/* The underlying APM reconfigures the wired profile; native Sony idle is not
 * checked before publishing the new generation.
 * Caller holds the real APS lock. Does not supply a public control endpoint. */
__attribute__((visibility("default"))) status_t sony_policy_adapter_reconfigure_uid(SonyPolicyAdapter *,int32_t);
__attribute__((visibility("default"))) status_t sony_policy_adapter_reconfigure_hold(SonyPolicyAdapter *,bool);
__attribute__((visibility("default"))) status_t sony_policy_adapter_install_control(SonyPolicyAdapter *,SonyPolicyControlHook,void *);
__attribute__((visibility("default"))) status_t sony_policy_adapter_detach_control(SonyPolicyAdapter *,void *);
struct SonyPolicyAdapterState {
    uint64_t generation;
    int32_t selected_uid;
    unsigned references,open_outputs,duplicate_outputs;
    int adapter_error,observer_error,last_release_error;
    unsigned direct_volume_unity_calls,effects_refused;
};
__attribute__((visibility("default"))) status_t sony_policy_adapter_state(const SonyPolicyAdapter *,SonyPolicyAdapterState *);
/* Keep the original state ABI intact for frozen B100 binaries. New clients
 * must resolve the distinct v2 symbol; never pass a larger object to v1 and
 * infer fields which the loaded binary did not write. */
struct SonyPolicyAdapterStateV2 {
    unsigned version,bytes;
    SonyPolicyAdapterState base;
    unsigned retired_references;
    int core_error;
};
__attribute__((visibility("default"))) status_t sony_policy_adapter_state_v2(const SonyPolicyAdapter *,SonyPolicyAdapterStateV2 *);
/* A distinct symbol keeps frozen v1/v2 caller storage sizes unchanged. */
struct SonyPolicyAdapterStateV3 {
    unsigned version,bytes;
    SonyPolicyAdapterStateV2 previous;
    unsigned held;
};
__attribute__((visibility("default"))) status_t sony_policy_adapter_state_v3(const SonyPolicyAdapter *,SonyPolicyAdapterStateV3 *);
__attribute__((visibility("default"))) status_t sony_policy_adapter_bt_state(SonyPolicyAdapter *,SonyBtPolicySnapshot *);
__attribute__((visibility("default"))) status_t sony_policy_adapter_bt_change(SonyPolicyAdapter *,bool,uint64_t,const SonyBtPolicyIdentity&);
__attribute__((visibility("default"))) status_t sony_policy_adapter_bt_notify(SonyPolicyAdapter *,SonyPolicyBtNotify,void *);
/* Returns WOULD_BLOCK if framework references/duplicate outputs remain. It
 * never tears down a live duplicated descriptor, retries an uncertain close,
 * clears a Sony lease or calls system device-disconnect on the user's behalf.
 * Any ambiguous native/client failure retains the adapter for explicit recovery.
 * Caller output pointers are cleared only after a successful retirement. */
__attribute__((visibility("default"))) status_t sony_policy_adapter_destroy(SonyPolicyAdapter **,AudioPolicyInterface **);
}
#endif
