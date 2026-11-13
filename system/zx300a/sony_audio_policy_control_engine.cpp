#include "sony_audio_policy_control.h"
#include <errno.h>
#include <string.h>
namespace android {
SonyPolicyControlEngine::SonyPolicyControlEngine(SonyPolicyAdapter *p,SonyPolicyTickets::Clock clock,
 SonyPolicyTickets::Entropy entropy,void *context):tickets(clock,entropy,context),mAdapter(p){}
SonyPolicyControlEngine::~SonyPolicyControlEngine(){}
static SonyControlSnapshot snapshot(const SonyPolicyAdapterStateV3& v,int kind){
    const auto&s=v.previous;
    return {s.base.generation,s.base.selected_uid,s.base.references,s.retired_references,
        s.base.open_outputs,s.base.duplicate_outputs,s.base.direct_volume_unity_calls,
        s.base.effects_refused,s.base.adapter_error,s.base.observer_error,s.base.last_release_error,s.core_error,
        (int)v.held,v.held?(kind?kind:-1):0};
}
status_t SonyPolicyControlEngine::dispatch(const char *ticket){
    SonyTicketCommand command={};int rc=tickets.begin(ticket,&command);if(rc)return rc;
    SonyPolicyAdapterStateV3 before={},after={};bool valid=false;
    if(!mAdapter)rc=NO_INIT;
    else {
        rc=sony_policy_adapter_state_v3(mAdapter,&before);
        if(!rc&&command.operation==SONY_TICKET_SELECT){
            rc=sony_policy_adapter_reconfigure_uid(mAdapter,command.selected_uid);
        }else if(!rc&&command.operation==SONY_TICKET_HOLD_ENTER){
            if(!before.held){
                rc=sony_policy_adapter_reconfigure_hold(mAdapter,true);
            }
            if(!rc){mHoldKind=command.selected_uid;memcpy(mHoldIdentity,command.identity,65);}
        }else if(!rc&&command.operation==SONY_TICKET_HOLD_EXIT){
            if(before.held){
                rc=sony_policy_adapter_reconfigure_hold(mAdapter,false);
            }
            if(!rc){mHoldKind=SONY_HOLD_NONE;memset(mHoldIdentity,0,sizeof(mHoldIdentity));}
        }else if(!rc&&(command.operation==SONY_TICKET_BT_CONNECT||command.operation==SONY_TICKET_BT_DISCONNECT)){
            SonyBtPolicyIdentity identity={};rc=SonyBtPolicyRoute::decode(command.identity,&identity);
            if(!rc)rc=sony_policy_adapter_bt_change(mAdapter,command.operation==SONY_TICKET_BT_CONNECT,command.expected_generation,identity);
        }
        status_t observed=sony_policy_adapter_state_v3(mAdapter,&after);
        valid=observed==NO_ERROR;
    }
    SonyControlSnapshot state={};if(valid){state=snapshot(after,mHoldKind);
        if(command.operation>=SONY_TICKET_BT_CONNECT){int observed=sony_policy_adapter_bt_state(mAdapter,&state.bt);state.bt_valid=!observed;}}
    int committed=tickets.finish(ticket,rc,valid?&state:nullptr);
    /* Dispatch success means an outcome was durably retained in this process,
     * not that selection succeeded. The caller must peek the operation status. */
    return committed;
}
int SonyPolicyControlEngine::stop(){int rc=tickets.stop();if(!rc)mAdapter=nullptr;return rc;}
}
