#ifndef WALKDROID_SONY_AUDIO_POLICY_CONTROL_H
#define WALKDROID_SONY_AUDIO_POLICY_CONTROL_H
#include "sony_audio_app_policy_adapter.h"
#include "sony_audio_policy_tickets.h"
namespace android {
#define SONY_POLICY_CONTROL_SERVICE "walkdroid.audio.policy"
#define SONY_POLICY_CONTROL_DESCRIPTOR "org.walkdroid.audio.policy.control.v2"
#define SONY_POLICY_TICKET_PREFIX "walkdroid.policy.ticket.v2:"
#define SONY_POLICY_TICKET_DEVICE_NAME "WalkDroid policy ticket v2"
/* Only dispatch() reads or modifies APM. Its caller must hold the owning APS
 * lock; staging/peek/ack never touch an adapter or infer state from a reply
 * timeout. Caller and request identifiers are informational. */
class __attribute__((visibility("default"))) SonyPolicyControlEngine {
public:
    SonyPolicyControlEngine(SonyPolicyAdapter *,SonyPolicyTickets::Clock,SonyPolicyTickets::Entropy,void *);
    ~SonyPolicyControlEngine();
    SonyPolicyTickets tickets;
    status_t dispatch(const char *);
    int stop();
private:
    SonyPolicyAdapter *mAdapter;
    int32_t mHoldKind=SONY_HOLD_NONE;
    char mHoldIdentity[65]={};
};
/* Disable detaches the hook before original APS deletes its client.
 * A normal Binder transaction holds APS alive throughout dispatch. */
status_t sony_policy_control_publish(SonyPolicyAdapter *);
status_t sony_policy_control_disable(SonyPolicyAdapter *);
}
#endif
