#include "sony_audio_policy_control.h"
#include <binder/Binder.h>
#include <binder/IServiceManager.h>
#include <binder/IPCThreadState.h>
#include <binder/Parcel.h>
#include <utils/Mutex.h>
#include <cutils/properties.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <string.h>
#include <utility>
namespace android {
static uint64_t monotonic(void *){timespec t={};if(clock_gettime(CLOCK_MONOTONIC,&t))return 0;return(uint64_t)t.tv_sec*1000000000ull+t.tv_nsec;}
static int entropy(void *,unsigned char bytes[16]){
    int fd=open("/dev/urandom",O_RDONLY|O_CLOEXEC|O_NOFOLLOW);if(fd<0)return-errno;
    int rc=0;
    unsigned at=0;while(!rc&&at<16){ssize_t n=read(fd,bytes+at,16-at);if(n<0&&errno==EINTR)continue;if(n<=0){rc=n<0?-errno:-EIO;break;}at+=(unsigned)n;}close(fd);return rc;
}
static void hex(const uint8_t*raw,unsigned bytes,char*out){const char*d="0123456789abcdef";for(unsigned n=0;n<bytes;n++){out[2*n]=d[raw[n]>>4];out[2*n+1]=d[raw[n]&15];}out[2*bytes]=0;}
static void writeResult(Parcel *reply,const SonyTicketResult& r){
    reply->writeInt32(r.phase);reply->writeInt32(r.executed);reply->writeInt32(r.owner_dead);reply->writeInt32(r.snapshot_valid);reply->writeInt32(r.operation_status);
    reply->writeInt32(r.command.operation);reply->writeInt32(r.command.selected_uid);reply->writeUint64(r.command.expected_generation);
    reply->writeString16(String16(r.command.identity));reply->writeString16(String16(r.ticket));
    if(r.snapshot_valid){const auto&s=r.snapshot;reply->writeUint64(s.generation);reply->writeInt32(s.selected_uid);
        for(uint32_t v:{s.references,s.retired_references,s.outputs,s.duplicates,s.unity_calls,s.effects_refused})reply->writeUint32(v);
        for(int32_t v:{s.adapter_error,s.observer_error,s.release_error,s.core_error,s.held,s.hold_kind})reply->writeInt32(v);
        if(r.command.operation>=SONY_TICKET_BT_CONNECT){reply->writeInt32(s.bt_valid);if(s.bt_valid){const auto&b=s.bt;char token[33],peer[13];hex(b.token,16,token);hex(b.peer,6,peer);reply->writeUint64(b.generation);reply->writeUint64(b.route);reply->writeString16(String16(token));reply->writeString16(String16(peer));reply->writeInt32(b.phase);reply->writeInt32(b.flags);reply->writeInt32(b.previous_force);reply->writeInt32(b.current_force);reply->writeInt32(b.error);}}}
}
class PolicyControlBinder;
class ControlOwnerDeath:public IBinder::DeathRecipient {
    wp<PolicyControlBinder> service;char ticket[49];
public:
    ControlOwnerDeath(const sp<PolicyControlBinder>& s,const char *t):service(s){memcpy(ticket,t,49);}
    void binderDied(const wp<IBinder>&) override;
};
class PolicyControlBinder final:public BBinder {
    Mutex ownerLock;
    sp<IBinder> owner;sp<ControlOwnerDeath> death;
    sp<IBinder> btOwner;
    char ownedTicket[49]={};
    SonyPolicyAdapter *adapter;
public:
    SonyPolicyControlEngine engine;
    explicit PolicyControlBinder(SonyPolicyAdapter *p):adapter(p),engine(p,monotonic,entropy,nullptr){}
    void died(const char *ticket){engine.tickets.ownerDied(ticket);}
    const String16& getInterfaceDescriptor()const override {static const String16 name(SONY_POLICY_CONTROL_DESCRIPTOR);return name;}
    status_t disable(){
        Mutex::Autolock lock(ownerLock);int rc=engine.stop();if(rc)return rc;
        sony_policy_adapter_bt_notify(adapter,nullptr,nullptr);btOwner.clear();
        status_t detached=sony_policy_adapter_detach_control(adapter,this);adapter=nullptr;return detached;
    }
    static status_t hook(void *context,audio_devices_t device,audio_policy_dev_state_t state,const char *address,const char *name){
        (void)device;(void)state;(void)address;(void)name;
        auto self=(PolicyControlBinder *)context;char ticket[49]={};
        {Mutex::Autolock lock(self->ownerLock);memcpy(ticket,self->ownedTicket,sizeof(ticket));}
        status_t rc=self->engine.dispatch(ticket);
        sp<IBinder> notify;
        {Mutex::Autolock lock(self->ownerLock);
            notify=self->owner;SonyTicketResult r={};if(!self->engine.tickets.peek(0,getpid(),ticket,&r)&&r.snapshot_valid&&r.snapshot.bt_valid){
                const auto& b=r.snapshot.bt;
                if(r.command.operation==SONY_TICKET_BT_CONNECT&&(b.flags&3))self->btOwner=self->owner;
                else if(r.command.operation==SONY_TICKET_BT_DISCONNECT&&b.phase==SONY_BT_POLICY_OFF)self->btOwner.clear();
                else if(r.command.operation==SONY_TICKET_BT_QUERY&&b.phase!=SONY_BT_POLICY_OFF&&(!self->btOwner||!self->btOwner->isBinderAlive()))self->btOwner=self->owner;
            }
        }
        if(notify){Parcel message;message.writeInterfaceToken(String16("org.walkdroid.audio.policy.completion.v2"));message.writeString16(String16(ticket));
            (void)notify->transact(IBinder::FIRST_CALL_TRANSACTION,message,nullptr,IBinder::FLAG_ONEWAY);}
        return rc;
    }
    static void btChanged(void*context){
        auto self=(PolicyControlBinder*)context;sp<IBinder> target;{Mutex::Autolock lock(self->ownerLock);target=self->btOwner;}
        if(target){Parcel p;p.writeInterfaceToken(String16("org.walkdroid.audio.policy.completion.v2"));(void)target->transact(IBinder::FIRST_CALL_TRANSACTION+1,p,nullptr,IBinder::FLAG_ONEWAY);}
    }
protected:
    status_t onTransact(uint32_t code,const Parcel& data,Parcel *reply,uint32_t flags)override {
        if(code<1||code>6)return BBinder::onTransact(code,data,reply,flags);
        if(!reply)return BAD_VALUE;
        auto ipc=IPCThreadState::self();int uid=ipc->getCallingUid(),pid=ipc->getCallingPid();
        reply->writeInt32(0); // Java readException(). Protocol errors remain explicit status codes.
        int32_t strictPolicy=data.readInt32();(void)data.readString16();
        ipc->setStrictModePolicy((flags&IBinder::FLAG_ONEWAY)?0:strictPolicy);
        Mutex::Autolock lock(ownerLock);
        if(code==1){
            SonyTicketCommand command={};int64_t expected=0;
            int rc=data.readInt32(&command.operation);if(!rc)rc=data.readInt32(&command.selected_uid);if(!rc)rc=data.readInt64(&expected);command.expected_generation=(uint64_t)expected;
            String8 identity(data.readString16());sp<IBinder> token=data.readStrongBinder();
            if(rc||data.errorCheck()){reply->writeInt32(BAD_VALUE);return NO_ERROR;}
            size_t count=identity.size()<64?identity.size():64;memcpy(command.identity,identity.string(),count);
            char ticket[49]={};rc=engine.tickets.stage(uid,pid,command,ticket);
            if(!rc){
                if(token){sp<ControlOwnerDeath> next=new ControlOwnerDeath(this,ticket);(void)token->linkToDeath(next);owner=token;death=next;}
                memcpy(ownedTicket,ticket,49);
            }
            reply->writeInt32(rc);if(!rc)reply->writeString16(String16(ticket));return NO_ERROR;
        }
        String8 ticket;if(code!=6)ticket=String8(data.readString16());
        if(data.errorCheck()){reply->writeInt32(BAD_VALUE);return NO_ERROR;}
        SonyTicketResult result={};int rc=BAD_VALUE;
        if(code==2)rc=engine.tickets.peek(uid,pid,ticket.string(),&result);
        if(code==3)rc=engine.tickets.acknowledge(uid,pid,ticket.string());
        if(code==4)rc=engine.tickets.cancel(uid,pid,ticket.string());
        if(code==5){engine.tickets.ownerDied(ticket.string());rc=0;}
        if(code==6)rc=engine.tickets.recover(uid,pid,&result);
        if(!rc&&code==3){if(owner&&death)owner->unlinkToDeath(death);owner.clear();death.clear();ownedTicket[0]=0;}
        reply->writeInt32(rc);if(!rc&&(code==2||code==6))writeResult(reply,result);return NO_ERROR;
    }
};
void ControlOwnerDeath::binderDied(const wp<IBinder>&){auto p=service.promote();if(p)p->died(ticket);}
static sp<PolicyControlBinder> published;
status_t sony_policy_control_publish(SonyPolicyAdapter *adapter){
    if(published||!adapter)return INVALID_OPERATION;
    sp<PolicyControlBinder> service=new PolicyControlBinder(adapter);
    status_t rc=sony_policy_adapter_install_control(adapter,PolicyControlBinder::hook,service.get());if(rc)return rc;
    rc=sony_policy_adapter_bt_notify(adapter,PolicyControlBinder::btChanged,service.get());if(rc){service->disable();return rc;}
    rc=defaultServiceManager()->addService(String16(SONY_POLICY_CONTROL_SERVICE),service,false);
    if(rc){service->disable();return rc;}published=service;return NO_ERROR;
}
status_t sony_policy_control_disable(SonyPolicyAdapter *adapter){
    (void)adapter;return published?published->disable():NO_ERROR;
}
}
