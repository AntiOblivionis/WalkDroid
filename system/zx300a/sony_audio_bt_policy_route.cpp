#include "sony_audio_bt_policy_route.h"
#include <errno.h>
#include <string.h>
namespace android {
SonyBtPolicyRoute::SonyBtPolicyRoute(SonyBtPolicyPort&p):port(p){}
bool SonyBtPolicyRoute::advance(){if(!++generation)++generation;return true;}
int SonyBtPolicyRoute::observe(int*force,int*connected){
    *force=port.mediaForce();*connected=deviceOwned?port.connected(identity.peer):0;
    return 0;
}
int SonyBtPolicyRoute::snapshot(SonyBtPolicySnapshot*out){
    if(!out)return -EINVAL;int force=0,connected=0;int rc=observe(&force,&connected);
    *out={};out->generation=generation;out->route=identity.route;memcpy(out->token,identity.token,16);memcpy(out->peer,identity.peer,6);
    out->phase=phase;out->flags=(forceOwned?1:0)|(deviceOwned?2:0)|(connected?4:0)|(foreign?8:0);out->previous_force=previousForce;out->current_force=force;out->error=error;
    return rc;
}
int SonyBtPolicyRoute::connect(uint64_t expected,const SonyBtPolicyIdentity&i){
    (void)expected;
    if(deviceOwned){int rc=port.connection(identity.peer,false);if(rc){error=rc;return rc;}deviceOwned=false;}
    if(forceOwned){port.mediaForce(previousForce);forceOwned=false;}
    previousForce=port.mediaForce();advance();identity=i;foreign=false;error=0;phase=SONY_BT_POLICY_PARTIAL;
    int rc=port.connection(identity.peer,true);
    if(rc){error=rc;return error;}
    deviceOwned=true;
    if(previousForce!=4){port.mediaForce(4);forceOwned=true;}
    phase=SONY_BT_POLICY_ACTIVE;return 0;
}
int SonyBtPolicyRoute::disconnect(uint64_t expected,const SonyBtPolicyIdentity&i){
    (void)expected;(void)i;
    advance();phase=SONY_BT_POLICY_PARTIAL;error=0;
    if(deviceOwned){int rc=port.connection(identity.peer,false);if(rc){error=rc;return error;}deviceOwned=false;}
    if(forceOwned){port.mediaForce(previousForce);forceOwned=false;}
    phase=SONY_BT_POLICY_OFF;return 0;
}
void SonyBtPolicyRoute::externalMediaForce(){
    if(phase==SONY_BT_POLICY_OFF&&!forceOwned&&!deviceOwned)return;
    forceOwned=false;foreign=true;phase=SONY_BT_POLICY_LOST;advance();
}
void SonyBtPolicyRoute::externalDevice(const uint8_t peer[6]){
    if(!peer||memcmp(identity.peer,peer,6)||(phase==SONY_BT_POLICY_OFF&&!forceOwned&&!deviceOwned))return;
    deviceOwned=forceOwned=false;foreign=true;phase=SONY_BT_POLICY_LOST;advance();
}
int SonyBtPolicyRoute::decode(const char*hex,SonyBtPolicyIdentity*out){
    if(!hex||!out||strnlen(hex,65)!=64)return -EINVAL;uint8_t bytes[32];
    for(unsigned n=0;n<32;n++){unsigned v=0;for(unsigned j=0;j<2;j++){unsigned char c=hex[2*n+j];if(c>='0'&&c<='9')c-='0';else if(c>='a'&&c<='f')c=c-'a'+10;else if(c>='A'&&c<='F')c=c-'A'+10;else return -EINVAL;v=v*16+c;}bytes[n]=(uint8_t)v;}
    SonyBtPolicyIdentity i={};memcpy(i.token,bytes,16);memcpy(i.peer,bytes+16,6);
    for(unsigned n=0;n<8;n++)i.route|=(uint64_t)bytes[22+n]<<(8*n);
    *out=i;return 0;
}
}
