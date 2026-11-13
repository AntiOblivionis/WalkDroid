#include "sony_audio_app_policy_adapter.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <vector>
#include <media/AudioParameter.h>
#include <utils/Errors.h>

namespace android {
class ObservedClient final:public AudioPolicyClientInterface {
public:
    AudioPolicyClientInterface *mTarget;
    struct Output {audio_io_handle_t handle;SonyAppOutput value;bool live,known,duplicate;audio_patch_handle_t patch;audio_devices_t lastRoutedDevice;};
    std::vector<Output> outputs;
    int error=0;
    bool selected=false;
    unsigned unityCalls=0;
    char retirement[320]="not-requested";
    void retireDiagnostic(const char *stage,int rc,audio_io_handle_t handle=0,const SonyAppOutput *out=nullptr){
        auto o=find(handle);
        snprintf(retirement,sizeof(retirement),"stage=%s status=%d output=%d flags=%#x device=%#x last_routed=%#x",stage,rc,handle,out?(unsigned)out->flags:0,out?(unsigned)out->device:0,o?(unsigned)o->lastRoutedDevice:0);
    }
    explicit ObservedClient(AudioPolicyClientInterface *target):mTarget(target){}
    void fail(int rc){if(!error)error=rc?rc:INVALID_OPERATION;}
    Output *find(audio_io_handle_t h){for(auto &o:outputs)if(o.handle==h&&o.live)return &o;return nullptr;}
    void record(audio_io_handle_t h,const SonyAppOutput& value,bool duplicate){
        if(h==AUDIO_IO_HANDLE_NONE||find(h)){fail(INVALID_OPERATION);return;}
        for(auto &o:outputs)if(!o.live){o={h,value,true,true,duplicate,AUDIO_PATCH_HANDLE_NONE,value.device};return;}
        if(outputs.size()>=64){fail(NO_MEMORY);return;}
        outputs.push_back({h,value,true,true,duplicate,AUDIO_PATCH_HANDLE_NONE,value.device});
    }
    static bool observe(void *ctx,audio_io_handle_t h,SonyAppOutput *out){
        auto c=(ObservedClient *)ctx;if(c->error||!out)return false;
        auto o=c->find(h);if(!o||!o->known)return false;*out=o->value;return true;
    }
    unsigned live(bool duplicated=false)const{unsigned n=0;for(const auto&o:outputs)if(o.live&&(!duplicated||o.duplicate))++n;return n;}
    status_t openOutput(audio_module_handle_t module,audio_io_handle_t *output,
      audio_config_t *config,audio_devices_t *devices,const String8& address,
      uint32_t *latencyMs,audio_output_flags_t flags) override {
        status_t rc=mTarget->openOutput(module,output,config,devices,address,latencyMs,flags);
        if(!rc){if(!output||!config||!devices)fail(BAD_VALUE);else record(*output,{flags,*devices,*config},false);}
        return rc;
    }
    audio_io_handle_t openDuplicateOutput(audio_io_handle_t first,audio_io_handle_t second) override {
        audio_io_handle_t h=mTarget->openDuplicateOutput(first,second);
        if(h!=AUDIO_IO_HANDLE_NONE){
            SonyAppOutput a={},b={};
            if(!observe(this,first,&a)||!observe(this,second,&b))fail(INVALID_OPERATION);
            a.flags=AUDIO_OUTPUT_FLAG_NONE;a.device=(audio_devices_t)(a.device|b.device);record(h,a,true);
        }
        return h;
    }
    status_t closeOutput(audio_io_handle_t h) override {
        auto o=find(h);status_t rc=mTarget->closeOutput(h);
        if(!o||rc){fail(rc);if(o)o->known=false;}
        else {o->live=false;o->known=false;}
        return rc;
    }
    status_t createAudioPatch(const audio_patch *patch,audio_patch_handle_t *handle,int delayMs) override {
        status_t rc=mTarget->createAudioPatch(patch,handle,delayMs);
        if(!patch||patch->num_sources!=1||patch->sources[0].type!=AUDIO_PORT_TYPE_MIX)return rc;
        auto o=find(patch->sources[0].ext.mix.handle);
        if(!o){fail(INVALID_OPERATION);return rc;}
        if(rc||delayMs||!handle||*handle==AUDIO_PATCH_HANDLE_NONE||patch->num_sinks!=1||patch->sinks[0].type!=AUDIO_PORT_TYPE_DEVICE){o->known=false;if(rc)fail(rc);return rc;}
        o->value.device=patch->sinks[0].ext.device.type;o->patch=*handle;o->known=true;
        if(o->value.device!=AUDIO_DEVICE_NONE)o->lastRoutedDevice=o->value.device;
        return rc;
    }
    status_t releaseAudioPatch(audio_patch_handle_t h,int delayMs) override {
        status_t rc=mTarget->releaseAudioPatch(h,delayMs);
        for(auto&o:outputs)if(o.live&&o.patch==h){o.known=false;if(rc)fail(rc);else if(!delayMs){o.patch=AUDIO_PATCH_HANDLE_NONE;o.value.device=AUDIO_DEVICE_NONE;o.known=true;}}
        return rc;
    }
    void setParameters(audio_io_handle_t h,const String8& parameters,int delayMs=0) override {
        AudioParameter parsed(parameters);int route=0;
        if(parsed.getInt(String8(AUDIO_PARAMETER_STREAM_ROUTING),route)==NO_ERROR){
            for(auto&o:outputs)if(o.live&&(h==AUDIO_IO_HANDLE_NONE||o.handle==h))o.known=false;
        }
        mTarget->setParameters(h,parameters,delayMs);
    }
    status_t setStreamVolume(audio_stream_type_t stream,float volume,audio_io_handle_t h,int delayMs=0) override {
        auto o=find(h);
        if(selected&&o&&o->known&&o->value.flags==AUDIO_OUTPUT_FLAG_DIRECT&&o->value.device==AUDIO_DEVICE_OUT_WIRED_HEADPHONE){volume=1.0f;++unityCalls;}
        return mTarget->setStreamVolume(stream,volume,h,delayMs);
    }
#include "sony_audio_app_policy_client_forward.inc"
};

class PolicyFacade final:public AudioPolicyInterface {
public:
    AudioPolicyInterface *mBase;
    SonyAppPolicy *mCore;
    ObservedClient *mObserved;
    int error=0,lastRelease=0;
    unsigned effectsRefused=0;
    SonyPolicyControlHook control=nullptr;
    void *controlContext=nullptr;
    SonyPolicyBtNotify btNotify=nullptr;void *btNotifyContext=nullptr;
    class BtPort final:public SonyBtPolicyPort {
        PolicyFacade& owner;
        static void address(const uint8_t peer[6],char text[18]){snprintf(text,18,"%02X:%02X:%02X:%02X:%02X:%02X",peer[0],peer[1],peer[2],peer[3],peer[4],peer[5]);}
    public:
        explicit BtPort(PolicyFacade&p):owner(p){}
        int mediaForce()override{return owner.mBase?(int)owner.mBase->getForceUse(AUDIO_POLICY_FORCE_FOR_MEDIA):-ENODEV;}
        void mediaForce(int value)override{if(owner.mBase)owner.mBase->setForceUse(AUDIO_POLICY_FORCE_FOR_MEDIA,(audio_policy_forced_cfg_t)value);}
        int connected(const uint8_t peer[6])override{if(!owner.mBase)return -ENODEV;char text[18];address(peer,text);return owner.mBase->getDeviceConnectionState(AUDIO_DEVICE_OUT_BLUETOOTH_A2DP,text);}
        int connection(const uint8_t peer[6],bool on)override{if(!owner.mBase)return -ENODEV;char text[18];address(peer,text);return owner.mBase->setDeviceConnectionState(AUDIO_DEVICE_OUT_BLUETOOTH_A2DP,on?AUDIO_POLICY_DEVICE_STATE_AVAILABLE:AUDIO_POLICY_DEVICE_STATE_UNAVAILABLE,text,"Sony Walkman ZX300A");}
    } btPort;
    SonyBtPolicyRoute btRoute;
    PolicyFacade(AudioPolicyInterface *base,SonyAppPolicy *core,ObservedClient *observed):mBase(base),mCore(core),mObserved(observed),btPort(*this),btRoute(btPort){}
    bool btBusy(){SonyBtPolicySnapshot s={};return btRoute.snapshot(&s)||s.phase!=SONY_BT_POLICY_OFF||(s.flags&(SONY_BT_POLICY_OWNS_DEVICE|SONY_BT_POLICY_OWNS_FORCE));}
    bool restricted()const{return mCore->held()||mCore->selectedUid()!=-1;}
    status_t initCheck() override {return !mBase||error||mObserved->error||mCore->failed()?NO_INIT:mBase->initCheck();}
    status_t setDeviceConnectionState(audio_devices_t device,audio_policy_dev_state_t state,const char *address,const char *name) override {
        static const char family[]="walkdroid.policy.ticket.";
        if(address&&!strncmp(address,family,sizeof(family)-1)){
            if(!control)return NO_INIT;
            return control(controlContext,device,state,address,name);
        }
        SonyBtPolicySnapshot before={};btRoute.snapshot(&before);
        if(device==AUDIO_DEVICE_OUT_BLUETOOTH_A2DP&&address){
            unsigned a[6];int used=0;if(sscanf(address,"%2x:%2x:%2x:%2x:%2x:%2x%n",&a[0],&a[1],&a[2],&a[3],&a[4],&a[5],&used)==6&&used==17&&!address[17]){uint8_t peer[6];for(unsigned n=0;n<6;n++)peer[n]=(uint8_t)a[n];btRoute.externalDevice(peer);}
        }
        status_t rc=mBase?mBase->setDeviceConnectionState(device,state,address,name):NO_INIT;
        SonyBtPolicySnapshot after={};btRoute.snapshot(&after);if(before.generation!=after.generation&&btNotify)btNotify(btNotifyContext);return rc;
    }
    void setForceUse(audio_policy_force_use_t usage,audio_policy_forced_cfg_t config)override{
        SonyBtPolicySnapshot before={};btRoute.snapshot(&before);
        if(usage==AUDIO_POLICY_FORCE_FOR_MEDIA)btRoute.externalMediaForce();
        if(mBase)mBase->setForceUse(usage,config);
        SonyBtPolicySnapshot after={};btRoute.snapshot(&after);if(before.generation!=after.generation&&btNotify)btNotify(btNotifyContext);
    }
    status_t getOutputForAttr(const audio_attributes_t *attr,audio_io_handle_t *out,audio_session_t session,
       audio_stream_type_t *stream,uid_t uid,const audio_config_t *config,audio_output_flags_t *flags,
       audio_port_handle_t *device,audio_port_handle_t *port) override {
        if(initCheck()!=NO_ERROR)return NO_INIT;
        return mCore->getOutputForAttr(attr,out,session,stream,uid,config,flags,device,port);
    }
    status_t startOutput(audio_io_handle_t o,audio_stream_type_t t,audio_session_t s) override {
        if(initCheck()!=NO_ERROR)return NO_INIT;return mCore->startOutput(o,t,s);
    }
    status_t stopOutput(audio_io_handle_t o,audio_stream_type_t t,audio_session_t s) override {return !mBase?NO_INIT:mCore->stopOutput(o,t,s);}
    void releaseOutput(audio_io_handle_t o,audio_stream_type_t t,audio_session_t s) override {
        lastRelease=mBase?mCore->releaseOutput(o,t,s):NO_INIT;if(lastRelease&&!error)error=lastRelease;
    }
    status_t dump(int fd) override {
        dprintf(fd,"WalkDroid app policy: selected_uid=%d generation=%llu references=%u adapter_error=%d observer_error=%d release_error=%d outputs=%u duplicates=%u unity_calls=%u effects_refused=%u\n",mCore->selectedUid(),(unsigned long long)mCore->generation(),mCore->references(),error,mObserved->error,lastRelease,mObserved->live(),mObserved->live(true),mObserved->unityCalls,effectsRefused);
        dprintf(fd,"WalkDroid app retirement: retired_references=%u core_error=%d\n",mCore->retiredReferences(),mCore->failed()?NO_INIT:0);
        dprintf(fd,"WalkDroid retirement observation: %s\n",mObserved->retirement);
        dprintf(fd,"WalkDroid receiver hold: held=%d preferred_uid=%d\n",mCore->held(),mCore->selectedUid());
        SonyBtPolicySnapshot b={};int bt=btRoute.snapshot(&b);dprintf(fd,"WalkDroid BT policy: generation=%llu route=%llu phase=%d flags=%d force_before=%d force_current=%d error=%d query=%d\n",(unsigned long long)b.generation,(unsigned long long)b.route,b.phase,b.flags,b.previous_force,b.current_force,b.error,bt);
        return mBase?mBase->dump(fd):NO_INIT;
    }
    status_t registerPolicyMixes(const Vector<AudioMix>& mixes) override {
        return !mBase?NO_INIT:restricted()?INVALID_OPERATION:mBase->registerPolicyMixes(mixes);
    }
    status_t unregisterPolicyMixes(Vector<AudioMix> mixes) override {
        return !mBase?NO_INIT:restricted()?INVALID_OPERATION:mBase->unregisterPolicyMixes(mixes);
    }
    audio_io_handle_t getOutputForEffect(const effect_descriptor_t *desc) override {
        if(!mBase)return AUDIO_IO_HANDLE_NONE;
        if(restricted()){++effectsRefused;return AUDIO_IO_HANDLE_NONE;}return mBase->getOutputForEffect(desc);
    }
    status_t registerEffect(const effect_descriptor_t *desc,audio_io_handle_t io,uint32_t strategy,int session,int id) override {
        if(!mBase)return NO_INIT;
        SonyAppOutput o={};
        if(restricted()&&(io==AUDIO_IO_HANDLE_NONE||!ObservedClient::observe(mObserved,io,&o)||o.flags==AUDIO_OUTPUT_FLAG_DIRECT)){
            ++effectsRefused;return INVALID_OPERATION;
        }
        return mBase->registerEffect(desc,io,strategy,session,id);
    }
    status_t setMasterMono(bool mono) override {
        if(!mBase)return NO_INIT;
        if(mono&&restricted())return INVALID_OPERATION;return mBase->setMasterMono(mono);
    }
    status_t createAudioPatch(const audio_patch *patch,audio_patch_handle_t *handle,uid_t uid) override {
        return !mBase?NO_INIT:restricted()?INVALID_OPERATION:mBase->createAudioPatch(patch,handle,uid);
    }
    status_t releaseAudioPatch(audio_patch_handle_t handle,uid_t uid) override {
        return !mBase?NO_INIT:restricted()?INVALID_OPERATION:mBase->releaseAudioPatch(handle,uid);
    }
#include "sony_audio_app_policy_forward.inc"
};

struct SonyPolicyAdapter {
    ObservedClient client;
    AudioPolicyInterface *base=nullptr;
    SonyAppPolicy *core=nullptr;
    PolicyFacade *facade=nullptr;
    SonyPolicyDestroy destroy;
    explicit SonyPolicyAdapter(AudioPolicyClientInterface *c,SonyPolicyDestroy d):client(c),destroy(d){}
};
status_t sony_policy_adapter_create(SonyPolicyCreate create,SonyPolicyDestroy destroy,
 AudioPolicyClientInterface *client,SonyPolicyAdapter **out,AudioPolicyInterface **iface){
    if(!create||!destroy||!client||!out||!iface||*out||*iface)return BAD_VALUE;
    auto p=new SonyPolicyAdapter(client,destroy);p->base=create(&p->client);
    if(!p->base){delete p;return NO_INIT;}
    p->core=new SonyAppPolicy(p->base,ObservedClient::observe,&p->client);
    p->facade=new PolicyFacade(p->base,p->core,&p->client);
    /* Failed initialization is not silently destroyed: the caller receives
     * the exact object for explicit retirement, including callback failures. */
    *out=p;*iface=p->facade;return p->facade->initCheck();
}
status_t sony_policy_adapter_select_uid(SonyPolicyAdapter *p,int32_t uid){
    if(!p)return BAD_VALUE;if(p->facade->initCheck()!=NO_ERROR)return NO_INIT;
    if(p->facade->btBusy())return WOULD_BLOCK;
    status_t rc=p->core->selectUid(uid);if(!rc)p->client.selected=p->core->held()||p->core->selectedUid()!=-1;return rc;
}
status_t sony_policy_adapter_bt_state(SonyPolicyAdapter*p,SonyBtPolicySnapshot*out){
    if(!p||!out)return BAD_VALUE;return p->facade->btRoute.snapshot(out);
}
status_t sony_policy_adapter_bt_change(SonyPolicyAdapter*p,bool on,uint64_t expected,const SonyBtPolicyIdentity&identity){
    if(!p)return BAD_VALUE;if(p->facade->initCheck()!=NO_ERROR)return NO_INIT;
    if(on&&(p->core->held()||p->core->selectedUid()!=-1))return WOULD_BLOCK;
    return on?p->facade->btRoute.connect(expected,identity):p->facade->btRoute.disconnect(expected,identity);
}
status_t sony_policy_adapter_bt_notify(SonyPolicyAdapter*p,SonyPolicyBtNotify hook,void*context){
    if(!p||(!hook&&context)||(hook&&!context))return BAD_VALUE;
    if(hook&&p->facade->btNotify)return -EBUSY;
    p->facade->btNotify=hook;p->facade->btNotifyContext=context;return NO_ERROR;
}
static status_t retireSelectionOutputs(void *context,const audio_io_handle_t *handles,unsigned count,
 const audio_stream_type_t *streams,unsigned streamCount,SonyAppRetiredOutput *kinds,bool *changed){
    auto p=(SonyPolicyAdapter *)context;auto& c=p->client;
    if(!changed||count>256||streamCount>AUDIO_STREAM_CNT||c.error)return NO_INIT;
    *changed=false;c.retireDiagnostic("retiring",0);status_t rc=NO_ERROR;
    for(unsigned i=0;i<count;i++){
        auto o=c.find(handles[i]);if(!o||!o->known){c.retireDiagnostic("unknown-reference",NO_INIT,handles[i],o?&o->value:nullptr);return NO_INIT;}
        bool bus=(o->value.flags&AUDIO_OUTPUT_FLAG_PRIMARY)&&
            (o->value.device==AUDIO_DEVICE_NONE||o->value.device==AUDIO_DEVICE_OUT_BUS);
        // An idle cached shared output can retain APM references after its
        // patch is released. Remember only this live descriptor's confirmed
        // route; the NONE observation alone is never proof of wired ownership.
        bool idleWired=!o->duplicate&&o->value.flags==AUDIO_OUTPUT_FLAG_NONE&&
            o->value.device==AUDIO_DEVICE_NONE&&o->lastRoutedDevice==AUDIO_DEVICE_OUT_WIRED_HEADPHONE;
        bool wired=idleWired||o->value.device==AUDIO_DEVICE_OUT_WIRED_HEADPHONE||
            o->duplicate&&(o->value.device&~(AUDIO_DEVICE_OUT_WIRED_HEADPHONE|AUDIO_DEVICE_OUT_BUS))==0;
        if(!bus&&!wired){c.retireDiagnostic("unsupported-reference",WOULD_BLOCK,handles[i],&o->value);return WOULD_BLOCK;} // A2DP/output ownership is not silently stolen.
    }
    for(unsigned i=0;i<streamCount;i++)if((unsigned)streams[i]>=AUDIO_STREAM_CNT)return BAD_VALUE;
    auto original=p->base->getDeviceConnectionState(AUDIO_DEVICE_OUT_WIRED_HEADPHONE,"");
    if(original==AUDIO_POLICY_DEVICE_STATE_AVAILABLE){
        /* Use APM's standard configuration rebuild, not a GPIO update or a
         * Java wired-device notification. Original closeOutput retires the
         * physical/DUP threads; original profiles reopen with fresh handles. */
        *changed=true;
        rc=p->base->handleDeviceConfigChange(AUDIO_DEVICE_OUT_WIRED_HEADPHONE,"","Sony Walkman ZX300A");
        if(rc)return rc;
    }
    for(unsigned i=0;i<streamCount;i++){
        *changed=true;rc=c.mTarget->invalidateStream(streams[i]);if(rc)return rc;
    }
    if(c.error)return c.error;
    for(unsigned i=0;i<count;i++){
        auto o=c.find(handles[i]);
        if(!o){kinds[i]=SONY_APP_RETIRED_CLOSED;continue;}
        if(!o->known||o->duplicate||!(o->value.flags&AUDIO_OUTPUT_FLAG_PRIMARY)||
            o->value.flags&AUDIO_OUTPUT_FLAG_DIRECT||
            (o->value.device!=AUDIO_DEVICE_NONE&&o->value.device!=AUDIO_DEVICE_OUT_BUS))return INVALID_OPERATION;
        kinds[i]=SONY_APP_RETIRED_BUS;
    }
    c.retireDiagnostic("retired",NO_ERROR);
    return NO_ERROR;
}
status_t sony_policy_adapter_reconfigure_uid(SonyPolicyAdapter *p,int32_t uid){
    if(!p)return BAD_VALUE;if(p->facade->initCheck()!=NO_ERROR)return NO_INIT;
    if(p->facade->btBusy())return WOULD_BLOCK;
    status_t rc=p->core->reconfigureUid(uid,retireSelectionOutputs,p);
    if(!rc)p->client.selected=p->core->held()||p->core->selectedUid()!=-1;return rc;
}
status_t sony_policy_adapter_reconfigure_hold(SonyPolicyAdapter *p,bool held){
    if(!p)return BAD_VALUE;if(p->facade->initCheck()!=NO_ERROR)return NO_INIT;
    if(p->facade->btBusy()){p->client.retireDiagnostic("bt-route-owned",WOULD_BLOCK);return WOULD_BLOCK;}
    status_t rc=p->core->reconfigureHold(held,retireSelectionOutputs,p);
    if(!rc)p->client.selected=p->core->held()||p->core->selectedUid()!=-1;return rc;
}
status_t sony_policy_adapter_install_control(SonyPolicyAdapter *p,SonyPolicyControlHook hook,void *context){
    if(!p||!hook||!context||p->facade->control)return BAD_VALUE;
    p->facade->control=hook;p->facade->controlContext=context;return NO_ERROR;
}
status_t sony_policy_adapter_detach_control(SonyPolicyAdapter *p,void *context){
    if(!p||!context||p->facade->controlContext!=context)return BAD_VALUE;
    p->facade->control=nullptr;p->facade->controlContext=nullptr;return NO_ERROR;
}
status_t sony_policy_adapter_state(const SonyPolicyAdapter *p,SonyPolicyAdapterState *s){
    if(!p||!s)return BAD_VALUE;
    *s={p->core->generation(),p->core->selectedUid(),p->core->references(),p->client.live(),p->client.live(true),p->facade->error,p->client.error,p->facade->lastRelease,p->client.unityCalls,p->facade->effectsRefused};return NO_ERROR;
}
status_t sony_policy_adapter_state_v2(const SonyPolicyAdapter *p,SonyPolicyAdapterStateV2 *s){
    if(!p||!s)return BAD_VALUE;
    *s={};s->version=2;s->bytes=sizeof(*s);status_t rc=sony_policy_adapter_state(p,&s->base);
    if(rc)return rc;s->retired_references=p->core->retiredReferences();s->core_error=p->core->failed()?NO_INIT:0;return NO_ERROR;
}
status_t sony_policy_adapter_state_v3(const SonyPolicyAdapter *p,SonyPolicyAdapterStateV3 *s){
    if(!p||!s)return BAD_VALUE;
    *s={};s->version=3;s->bytes=sizeof(*s);status_t rc=sony_policy_adapter_state_v2(p,&s->previous);
    if(!rc)s->held=p->core->held();return rc;
}
status_t sony_policy_adapter_destroy(SonyPolicyAdapter **p,AudioPolicyInterface **iface){
    if(!p||!*p||!iface||*iface!=(*p)->facade)return BAD_VALUE;
    auto a=*p;if(a->facade->initCheck()!=NO_ERROR)return NO_INIT;
    if(a->facade->btBusy()||a->core->held()||a->core->references()||a->client.live(true))return WOULD_BLOCK;
    status_t rc=a->core->selectUid(-1);if(rc)return rc;
    a->client.selected=false;
    a->destroy(a->base);a->base=nullptr;a->facade->mBase=nullptr;
    if(a->client.error||a->client.live()){if(!a->facade->error)a->facade->error=INVALID_OPERATION;return INVALID_OPERATION;}
    delete a->facade;delete a->core;delete a;*p=nullptr;*iface=nullptr;return NO_ERROR;
}
}
