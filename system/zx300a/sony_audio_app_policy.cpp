#include "sony_audio_app_policy.h"
#include "sony_audio_pcm_format.h"
#include <errno.h>
#include <limits.h>
#include <string.h>
#include <vector>
#include <utils/Errors.h>

namespace android {
struct SonyAppPolicy::Impl {
    AudioPolicyInterface *policy;
    SonyAppOutputObserver observe;
    void *context;
    int32_t selected=-1;
    bool held=false;
    uint64_t generation=0;
    bool bad=false;
    audio_patch_handle_t busPatch=AUDIO_PATCH_HANDLE_NONE;
    audio_port_handle_t busSource=AUDIO_PORT_HANDLE_NONE,busDevice=AUDIO_PORT_HANDLE_NONE;
    struct Ref {
        audio_io_handle_t output;
        audio_stream_type_t stream;
        audio_session_t session;
        uid_t uid;
        uint64_t generation;
        unsigned count,active;
        bool retired,closed,software;
    };
    std::vector<Ref> refs;
    Vector<AudioMix> mix;
    status_t patches(std::vector<audio_patch>& out) {
        unsigned count=0,generation=0;
        status_t rc=policy->listAudioPatches(&count,nullptr,&generation);if(rc)return rc;
        if(count>256)return INVALID_OPERATION;
        if(!count){out.clear();return NO_ERROR;}
        unsigned expected=generation,capacity=count;out.resize(count);
        rc=policy->listAudioPatches(&capacity,out.data(),&generation);if(rc)return rc;
        return expected==generation&&capacity==count?NO_ERROR:WOULD_BLOCK;
    }
    bool ownShape(const audio_patch& patch) const {
        return patch.num_sources==1&&patch.num_sinks==1&&
          patch.sources[0].type==AUDIO_PORT_TYPE_MIX&&patch.sources[0].id==busSource&&
          patch.sinks[0].type==AUDIO_PORT_TYPE_DEVICE&&patch.sinks[0].id==busDevice&&
          patch.sinks[0].ext.device.type==AUDIO_DEVICE_OUT_BUS;
    }
    status_t releaseBus() {
        if(busPatch==AUDIO_PATCH_HANDLE_NONE)return NO_ERROR;
        std::vector<audio_patch> current;status_t rc=patches(current);if(rc)return rc;
        for(const auto &p:current)if(p.id==busPatch){
            if(!ownShape(p))return INVALID_OPERATION;
            rc=policy->releaseAudioPatch(busPatch,1041);if(rc)return rc;
            busPatch=AUDIO_PATCH_HANDLE_NONE;return NO_ERROR;
        }
        /* APM itself can retire a software BUS patch on a device event. This
         * is not an uncertain Sony/native close: require the still-live output
         * observation and a coherent original-APM patch snapshot before
         * retiring this obsolete framework-only handle. */
        SonyAppOutput actual={};bool found=false;
        for(const auto &r:current)if(r.num_sources&&r.sources[0].id==busSource)return INVALID_OPERATION;
        unsigned count=0,generation=0;rc=policy->listAudioPorts(AUDIO_PORT_ROLE_SOURCE,AUDIO_PORT_TYPE_MIX,&count,nullptr,&generation);
        if(rc||!count||count>256)return rc?rc:INVALID_OPERATION;
        std::vector<audio_port> ports(count);unsigned expected=generation,capacity=count;
        rc=policy->listAudioPorts(AUDIO_PORT_ROLE_SOURCE,AUDIO_PORT_TYPE_MIX,&capacity,ports.data(),&generation);
        if(rc||expected!=generation||capacity!=count)return rc?rc:WOULD_BLOCK;
        for(const auto &p:ports)if(p.id==busSource&&observe(context,p.ext.mix.handle,&actual)&&
            (actual.flags&AUDIO_OUTPUT_FLAG_PRIMARY)&&!(actual.flags&AUDIO_OUTPUT_FLAG_DIRECT)&&
            (actual.device==AUDIO_DEVICE_NONE||actual.device==AUDIO_DEVICE_OUT_BUS))found=true;
        if(!found)return INVALID_OPERATION;
        busPatch=AUDIO_PATCH_HANDLE_NONE;return NO_ERROR;
    }
    status_t bindBus() {
        unsigned count=0,generation=0;
        status_t rc=policy->listAudioPorts(AUDIO_PORT_ROLE_NONE,AUDIO_PORT_TYPE_NONE,&count,nullptr,&generation);
        if(rc)return rc;
        if(!count||count>256)return INVALID_OPERATION;
        std::vector<audio_port> ports(count);unsigned expected=generation,capacity=count;
        rc=policy->listAudioPorts(AUDIO_PORT_ROLE_NONE,AUDIO_PORT_TYPE_NONE,&capacity,ports.data(),&generation);
        if(rc)return rc;
        if(generation!=expected||capacity!=count)return WOULD_BLOCK;
        const audio_port *primary=nullptr,*bus=nullptr;
        for(const auto &p:ports) {
            if(p.type==AUDIO_PORT_TYPE_DEVICE&&p.role==AUDIO_PORT_ROLE_SINK&&
               p.ext.device.type==AUDIO_DEVICE_OUT_BUS&&p.ext.device.address[0]==0){
                if(bus)return INVALID_OPERATION;bus=&p;
            } else if(p.type==AUDIO_PORT_TYPE_MIX&&p.role==AUDIO_PORT_ROLE_SOURCE) {
                SonyAppOutput actual={};
                if(observe(context,p.ext.mix.handle,&actual)&&
                   (actual.flags&AUDIO_OUTPUT_FLAG_PRIMARY)&&!(actual.flags&AUDIO_OUTPUT_FLAG_DIRECT)){
                    if(primary)return INVALID_OPERATION;primary=&p;
                }
            }
        }
        if(!primary||!bus)return INVALID_OPERATION;
        std::vector<audio_patch> current;rc=patches(current);if(rc)return rc;
        audio_patch_handle_t existing=AUDIO_PATCH_HANDLE_NONE;
        for(const auto &p:current)if(p.num_sources==1&&p.sources[0].id==primary->id){
            if(p.num_sinks!=1||p.sinks[0].id!=bus->id||p.sinks[0].ext.device.type!=AUDIO_DEVICE_OUT_BUS||existing!=AUDIO_PATCH_HANDLE_NONE)return INVALID_OPERATION;
            existing=p.id;
        }
        SonyAppOutput observed={};if(!observe(context,primary->ext.mix.handle,&observed))return INVALID_OPERATION;
        if(existing!=AUDIO_PATCH_HANDLE_NONE&&existing==busPatch&&observed.device==AUDIO_DEVICE_OUT_BUS)return NO_ERROR;
        busPatch=existing;busSource=primary->id;busDevice=bus->id;
        audio_patch patch={};patch.num_sources=patch.num_sinks=1;
        auto &source=patch.sources[0];source=primary->active_config;
        source.id=primary->id;source.type=AUDIO_PORT_TYPE_MIX;source.role=AUDIO_PORT_ROLE_SOURCE;
        source.ext.mix.hw_module=primary->ext.mix.hw_module;source.ext.mix.handle=primary->ext.mix.handle;
        source.ext.mix.usecase.stream=AUDIO_STREAM_MUSIC;
        auto &sink=patch.sinks[0];sink.id=bus->id;sink.type=AUDIO_PORT_TYPE_DEVICE;sink.role=AUDIO_PORT_ROLE_SINK;
        sink.ext.device.hw_module=bus->ext.device.hw_module;sink.ext.device.type=AUDIO_DEVICE_OUT_BUS;
        /* RENDER registration requires a live patch to the BUS. A jack change
         * can retire its initial idle patch even while the primary stays open.
         * Establish it through APM's own patch API, never a HAL routing string.
         * This UID is the eventual audioserver caller; no IPC is exposed here. */
        rc=policy->createAudioPatch(&patch,&busPatch,1041);
        if(!rc&&busPatch==AUDIO_PATCH_HANDLE_NONE)return INVALID_OPERATION;
        return rc;
    }
    Ref *find(audio_io_handle_t o,audio_stream_type_t t,audio_session_t s) {
        for(auto &r:refs)if(r.output==o&&r.stream==t&&r.session==s)return &r;
        return nullptr;
    }
};
SonyAppPolicy::SonyAppPolicy(AudioPolicyInterface *p,SonyAppOutputObserver o,void *c)
 :m(new Impl) {m->policy=p;m->observe=o;m->context=c;if(!p||!o)m->bad=true;}
/* Never silently release live framework references, clear a lease or rewrite
 * routing from a destructor. The containing adapter must retire explicitly. */
SonyAppPolicy::~SonyAppPolicy(){delete m;}
uint64_t SonyAppPolicy::generation()const{return m->generation;}
int32_t SonyAppPolicy::selectedUid()const{return m->selected;}
unsigned SonyAppPolicy::references()const{unsigned n=0;for(const auto&r:m->refs)n+=r.count;return n;}
unsigned SonyAppPolicy::retiredReferences()const{unsigned n=0;for(const auto&r:m->refs)if(r.retired)n+=r.count;return n;}
bool SonyAppPolicy::failed()const{return m->bad;}
bool SonyAppPolicy::held()const{return m->held;}
status_t SonyAppPolicy::selectUid(int32_t uid){return applySelection(uid,false);}
status_t SonyAppPolicy::applySelection(int32_t uid,bool allowRetired) {
    return applyMode(uid,m->held,allowRetired);
}
status_t SonyAppPolicy::applyMode(int32_t uid,bool held,bool allowRetired) {
    if(m->bad)return NO_INIT;
    for(const auto&r:m->refs)if(!allowRetired||!r.retired)return WOULD_BLOCK;
    if(uid==m->selected&&held==m->held&&
       (held||uid!=-1||m->busPatch==AUDIO_PATCH_HANDLE_NONE))return NO_ERROR;
    if(held||uid!=-1){status_t rc=m->bindBus();if(rc)return rc;}
    bool removedMix=false;
    if(!m->mix.isEmpty()) {
        status_t rc=m->policy->unregisterPolicyMixes(m->mix);
        if(rc)return rc;
        m->mix.clear();m->selected=-1;m->held=false;removedMix=true;
    }
    if(held||uid!=-1) {
        AudioMix mix;
        mix.mMixType=MIX_TYPE_PLAYERS;
        mix.mRouteFlags=MIX_ROUTE_FLAG_RENDER;
        mix.mDeviceType=AUDIO_DEVICE_OUT_BUS;
        mix.mDeviceAddress=String8("");
        memset(&mix.mFormat,0,sizeof(mix.mFormat));
        mix.mFormat.sample_rate=48000;
        mix.mFormat.format=AUDIO_FORMAT_PCM_16_BIT;
        mix.mFormat.channel_mask=AUDIO_CHANNEL_OUT_STEREO;
        mix.mCbFlags=0;
        AudioMixMatchCriterion criterion;
        criterion.mRule=RULE_EXCLUDE_UID;
        /* UID_INVALID cannot identify a legitimate Android playback client.
         * HOLD therefore excludes no real UID. Admission still independently
         * checks the actual opened output is BUS before returning a handle. */
        criterion.mValue.mUid=held?(uid_t)-1:(uid_t)uid;
        mix.mCriteria.add(criterion);
        Vector<AudioMix> pending;pending.add(mix);
        status_t rc=m->policy->registerPolicyMixes(pending);
        if(rc){if(removedMix)++m->generation;return rc;}
        m->mix=pending;
    }
    if(!held&&uid==-1&&m->busPatch!=AUDIO_PATCH_HANDLE_NONE){
        status_t rc=m->releaseBus();
        if(rc){if(removedMix)++m->generation;return rc;}
    }
    m->selected=uid;m->held=held;++m->generation;return NO_ERROR;
}
status_t SonyAppPolicy::reconfigureUid(int32_t uid,SonyAppRetireOutputs retire,void *context) {
    if(m->bad)return NO_INIT;
    if(!retire||!context)return BAD_VALUE;
    if(uid==m->selected)return NO_ERROR;
    if(m->held){
        /* Package revocation or a new preference during receiver playback
         * changes future ownership only. Never rebuild a physical profile or
         * require the external Sony writer to stop for this metadata change.
         * Existing BUS references keep exactly the same effective rights. */
        m->selected=uid;++m->generation;
        for(auto&r:m->refs)if(!r.retired)r.generation=m->generation;
        return NO_ERROR;
    }
    return reconfigureMode(uid,false,retire,context);
}
status_t SonyAppPolicy::reconfigureHold(bool held,SonyAppRetireOutputs retire,void *context) {
    return reconfigureMode(m->selected,held,retire,context);
}
status_t SonyAppPolicy::reconfigureMode(int32_t uid,bool held,SonyAppRetireOutputs retire,void *context) {
    if(m->bad)return NO_INIT;
    if(!retire||!context)return BAD_VALUE;
    if(uid==m->selected&&held==m->held)return NO_ERROR;
    std::vector<audio_io_handle_t> handles;std::vector<audio_stream_type_t> streams;
    for(const auto&r:m->refs)if(!r.retired){
        bool known=false;for(auto h:handles)known|=h==r.output;if(!known)handles.push_back(r.output);
        known=false;for(auto s:streams)known|=s==r.stream;if(!known)streams.push_back(r.stream);
    }
    if(handles.size()>256||streams.size()>AUDIO_STREAM_CNT)return INVALID_OPERATION;
    std::vector<SonyAppRetiredOutput> kinds(handles.size());bool changed=false;
    status_t rc=retire(context,handles.data(),handles.size(),streams.data(),streams.size(),kinds.data(),&changed);
    if(rc)return rc;
    for(auto&r:m->refs)if(!r.retired){
        size_t i=0;while(i<handles.size()&&handles[i]!=r.output)++i;
        if(i==handles.size())continue;
        r.retired=true;r.closed=kinds[i]==SONY_APP_RETIRED_CLOSED;
        r.software=kinds[i]==SONY_APP_RETIRED_BUS;
    }
    rc=applyMode(uid,held,true);
    return rc;
}
status_t SonyAppPolicy::getOutputForAttr(const audio_attributes_t *attr,
 audio_io_handle_t *output,audio_session_t session,audio_stream_type_t *stream,
 uid_t uid,const audio_config_t *config,audio_output_flags_t *flags,
 audio_port_handle_t *device,audio_port_handle_t *port) {
    if(m->bad)return NO_INIT;
    if(!output||!stream||!config||!flags||!device||!port)return BAD_VALUE;
    *output=AUDIO_IO_HANDLE_NONE;
    if(references()>=256)return NO_MEMORY;
    bool restricted=m->held||m->selected!=-1;
    bool selected=!m->held&&m->selected!=-1&&uid==(uid_t)m->selected;
    if(selected) {
        *flags=AUDIO_OUTPUT_FLAG_DIRECT;
        *device=AUDIO_PORT_HANDLE_NONE;
    } else if(restricted) {
        /* Event-driven repair at a new non-owner track admission: the
         * original APM may have removed its software BUS patch after a jack
         * change. Rebind only that verified port through APM before admission.
         * No physical writer, global lease or background polling is involved. */
        status_t rc=m->bindBus();if(rc)return rc;
        *flags=AUDIO_OUTPUT_FLAG_NONE;
        *device=AUDIO_PORT_HANDLE_NONE;
    }
    /* Caller tags can trigger a policy mix address shortcut. Do not permit
     * such tags to override the UID dimension of this routing decision. */
    audio_attributes_t clean;
    if(attr&&restricted){clean=*attr;memset(clean.tags,0,sizeof(clean.tags));attr=&clean;}
    status_t rc=m->policy->getOutputForAttr(attr,output,session,stream,uid,config,flags,device,port);
    if(rc)return rc;
    bool valid=*output!=AUDIO_IO_HANDLE_NONE;
    Impl::Ref *ref=m->find(*output,*stream,session);
    /* A still-open PRIMARY BUS can retain the same legacy output/session
     * across AudioTrack recreation. Coalesce only same-UID software-sink
     * references. A retired physical handle is never re-authorized this way. */
    bool softwareMerge=ref&&ref->retired&&ref->software&&ref->uid==uid&&!selected;
    if(softwareMerge){SonyAppOutput actual={};softwareMerge=m->observe(m->context,*output,&actual)&&
        !(actual.flags&AUDIO_OUTPUT_FLAG_DIRECT)&&actual.device==AUDIO_DEVICE_OUT_BUS;}
    if(ref&&(ref->uid!=uid||ref->generation!=m->generation&&!softwareMerge))valid=false;
    if(!valid){m->policy->releaseOutput(*output,*stream,session);*output=AUDIO_IO_HANDLE_NONE;return INVALID_OPERATION;}
    if(ref){if(softwareMerge){ref->generation=m->generation;ref->retired=false;}++ref->count;}
    else m->refs.push_back({*output,*stream,session,uid,m->generation,1,0,false,false,false});
    return NO_ERROR;
}
status_t SonyAppPolicy::startOutput(audio_io_handle_t o,audio_stream_type_t t,audio_session_t s) {
    if(m->bad)return NO_INIT;
    Impl::Ref *r=m->find(o,t,s);
    if(r&&r->retired)return DEAD_OBJECT;
    if(!r||r->generation!=m->generation||r->active>=r->count)return INVALID_OPERATION;
    status_t rc=m->policy->startOutput(o,t,s);if(!rc)++r->active;return rc;
}
status_t SonyAppPolicy::stopOutput(audio_io_handle_t o,audio_stream_type_t t,audio_session_t s) {
    if(m->bad)return NO_INIT;
    Impl::Ref *r=m->find(o,t,s);if(!r)return INVALID_OPERATION;
    if(r->retired&&r->closed){if(r->active)--r->active;return NO_ERROR;}
    if(!r->active)return INVALID_OPERATION;
    status_t rc=m->policy->stopOutput(o,t,s);if(!rc)--r->active;return rc;
}
status_t SonyAppPolicy::releaseOutput(audio_io_handle_t o,audio_stream_type_t t,audio_session_t s) {
    if(m->bad)return NO_INIT;
    for(auto i=m->refs.begin();i!=m->refs.end();++i)if(i->output==o&&i->stream==t&&i->session==s){
        if(!(i->retired&&i->closed)){
            if(i->active>=i->count)return WOULD_BLOCK;
            m->policy->releaseOutput(o,t,s);
        }
        if(i->retired&&i->closed&&i->active)i->active=0;
        if(!--i->count)m->refs.erase(i);
        return NO_ERROR;
    }
    return BAD_VALUE;
}
}
