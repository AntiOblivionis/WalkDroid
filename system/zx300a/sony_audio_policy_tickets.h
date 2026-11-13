#ifndef WALKDROID_SONY_AUDIO_POLICY_TICKETS_H
#define WALKDROID_SONY_AUDIO_POLICY_TICKETS_H
#include <stdint.h>
#include "sony_audio_bt_policy_route.h"
namespace android {
enum SonyTicketPhase {SONY_TICKET_EMPTY=0,SONY_TICKET_STAGED=1,SONY_TICKET_RUNNING=2,SONY_TICKET_DONE=3};
enum SonyTicketOperation {SONY_TICKET_QUERY=0,SONY_TICKET_SELECT=1,SONY_TICKET_HOLD_ENTER=2,SONY_TICKET_HOLD_EXIT=3,SONY_TICKET_BT_CONNECT=4,SONY_TICKET_BT_DISCONNECT=5,SONY_TICKET_BT_QUERY=6};
enum SonyReceiverHoldKind {SONY_HOLD_NONE=0,SONY_HOLD_USB=1,SONY_HOLD_BT_RX=2};
struct SonyControlSnapshot {
    uint64_t generation;
    int32_t selected_uid;
    uint32_t references,retired_references,outputs,duplicates,unity_calls,effects_refused;
    int32_t adapter_error,observer_error,release_error,core_error;
    int32_t held,hold_kind;
    int32_t bt_valid;
    SonyBtPolicySnapshot bt;
};
struct SonyTicketCommand {
    int32_t operation,selected_uid; // HOLD operations use selected_uid as hold-kind operand.
    uint64_t expected_generation;
    char identity[65]; // Optional request metadata; not used to authorize callers.
};
struct SonyTicketResult {
    SonyTicketPhase phase;
    bool executed,owner_dead,snapshot_valid;
    int32_t operation_status;
    SonyTicketCommand command;
    SonyControlSnapshot snapshot;
    char ticket[49]; // Request identifier for the existing transport format.
};
/* One operation slot, no polling thread. Request identifiers and caller IDs are
 * informational; DONE is retained until acknowledgement, including lost replies.
 */
class __attribute__((visibility("default"))) SonyPolicyTickets {
public:
    typedef uint64_t (*Clock)(void *);
    typedef int (*Entropy)(void *,unsigned char[16]);
    SonyPolicyTickets(Clock,Entropy,void *);
    ~SonyPolicyTickets();
    int stage(int uid,int pid,const SonyTicketCommand&,char out[49]);
    int begin(const char *,SonyTicketCommand *);
    int finish(const char *,int status,const SonyControlSnapshot *);
    int peek(int uid,int pid,const char *,SonyTicketResult *);
    int acknowledge(int uid,int pid,const char *);
    int cancel(int uid,int pid,const char *);
    int recover(int uid,int pid,SonyTicketResult *); // Historical, never a fresh native query.
    void ownerDied(const char *); // Notify the current operation of owner death.
    int stop(); // Rejects new work; never pretends a RUNNING operation was undone.
    SonyPolicyTickets(const SonyPolicyTickets&)=delete;
    SonyPolicyTickets& operator=(const SonyPolicyTickets&)=delete;
private:
    struct Impl;Impl *m;
};
}
#endif
