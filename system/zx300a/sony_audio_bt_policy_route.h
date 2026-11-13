#ifndef WALKDROID_SONY_BT_POLICY_ROUTE_H
#define WALKDROID_SONY_BT_POLICY_ROUTE_H
#include <stdint.h>
namespace android {
/* Serialized by the AudioPolicyService lock. The port calls the original APM
 * directly; external media-force/device changes can be reported for state. */
struct SonyBtPolicyIdentity {uint64_t route;uint8_t token[16],peer[6];};
struct SonyBtPolicySnapshot {
    uint64_t generation,route;
    uint8_t token[16],peer[6],reserved[2];
    int32_t phase,flags,previous_force,current_force,error;
};
enum SonyBtPolicyPhase {SONY_BT_POLICY_OFF=0,SONY_BT_POLICY_ACTIVE=1,SONY_BT_POLICY_PARTIAL=2,SONY_BT_POLICY_LOST=3};
enum SonyBtPolicyFlags {SONY_BT_POLICY_OWNS_FORCE=1,SONY_BT_POLICY_OWNS_DEVICE=2,SONY_BT_POLICY_DEVICE_PRESENT=4,SONY_BT_POLICY_FOREIGN=8};
class SonyBtPolicyPort {
public:
    virtual ~SonyBtPolicyPort(){}
    virtual int mediaForce()=0;
    virtual void mediaForce(int)=0;
    virtual int connected(const uint8_t[6])=0;
    virtual int connection(const uint8_t[6],bool)=0;
};
class SonyBtPolicyRoute {
public:
    explicit SonyBtPolicyRoute(SonyBtPolicyPort&);
    int connect(uint64_t,const SonyBtPolicyIdentity&);
    int disconnect(uint64_t,const SonyBtPolicyIdentity&);
    int snapshot(SonyBtPolicySnapshot*);
    void externalMediaForce();
    void externalDevice(const uint8_t[6]);
    static int decode(const char*,SonyBtPolicyIdentity*);
private:
    SonyBtPolicyPort& port;
    SonyBtPolicyIdentity identity={};
    uint64_t generation=1;
    int phase=SONY_BT_POLICY_OFF,previousForce=0,error=0;
    bool forceOwned=false,deviceOwned=false,foreign=false;
    bool advance();
    int observe(int*,int*);
};
}
#endif
