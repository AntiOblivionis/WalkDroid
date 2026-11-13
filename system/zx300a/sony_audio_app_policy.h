#ifndef WALKDROID_SONY_AUDIO_APP_POLICY_H
#define WALKDROID_SONY_AUDIO_APP_POLICY_H
#include <stdint.h>
#include "AudioPolicyInterface.h"

namespace android {
/* Routing component serialized by AudioPolicyService's policy lock.
 * UID is the requested playback-routing selection; this class is not a
 * package resolver, Binder service or Sony lease.
 *
 * The observer must describe the actual AudioFlinger openOutput callback, not
 * echo this component's requested flags. A claimed DIRECT flag in getOutput's
 * return value alone cannot distinguish an early policy-mix return.
 *
 * selectUid remains the strict no-outstanding-reference entry. The explicit
 * reconfiguration entry needs an owning adapter which closes old physical
 * outputs through original APM, verifies native idle and classifies each old
 * handle. This is not a package resolver, Binder endpoint or USB/RX handoff. */
struct SonyAppOutput {
    audio_output_flags_t flags;
    audio_devices_t device;
    audio_config_t config;
};
typedef bool (*SonyAppOutputObserver)(void *,audio_io_handle_t,SonyAppOutput *);
enum SonyAppRetiredOutput { SONY_APP_RETIRED_CLOSED=1, SONY_APP_RETIRED_BUS=2 };
/* Handles and streams describe the current framework references. A callback
 * failure is returned even if it has already changed routing; the caller must
 * account for those changes. Unknown retirement kinds are retained without
 * being reclassified as closed or software outputs. */
typedef status_t (*SonyAppRetireOutputs)(void *,const audio_io_handle_t *,unsigned,
    const audio_stream_type_t *,unsigned,SonyAppRetiredOutput *,bool *changed);
class SonyAppPolicy {
public:
    SonyAppPolicy(AudioPolicyInterface *,SonyAppOutputObserver,void *);
    ~SonyAppPolicy();
    SonyAppPolicy(const SonyAppPolicy&)=delete;
    SonyAppPolicy& operator=(const SonyAppPolicy&)=delete;
    status_t selectUid(int32_t uid); /* -1 = none */
    status_t reconfigureUid(int32_t uid,SonyAppRetireOutputs,void *);
    /* A receiver HOLD preserves the preferred application UID but sends all
     * Android playback UIDs to the clocked software BUS. Releasing it uses the
     * same explicit original-APM retirement/native-idle contract. */
    status_t reconfigureHold(bool held,SonyAppRetireOutputs,void *);
    status_t getOutputForAttr(const audio_attributes_t *,audio_io_handle_t *,
        audio_session_t,audio_stream_type_t *,uid_t,const audio_config_t *,
        audio_output_flags_t *,audio_port_handle_t *,audio_port_handle_t *);
    status_t startOutput(audio_io_handle_t,audio_stream_type_t,audio_session_t);
    status_t stopOutput(audio_io_handle_t,audio_stream_type_t,audio_session_t);
    status_t releaseOutput(audio_io_handle_t,audio_stream_type_t,audio_session_t);
    uint64_t generation() const;
    int32_t selectedUid() const;
    unsigned references() const;
    unsigned retiredReferences() const;
    bool failed() const;
    bool held() const;
private:
    struct Impl;
    Impl *m;
    status_t applySelection(int32_t uid,bool allowRetired);
    status_t applyMode(int32_t uid,bool held,bool allowRetired);
    status_t reconfigureMode(int32_t uid,bool held,SonyAppRetireOutputs,void *);
};
}
#endif
