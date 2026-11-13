#ifndef WALKDROID_SONY_BT_LINK_CONTROL_H
#define WALKDROID_SONY_BT_LINK_CONTROL_H
#include "sony_audio_bt_store.h"
#include "sony_audio_bt_tx_volume.h"
#include <stdint.h>
#include <sys/types.h>
#define WLC_MAGIC UINT32_C(0x32434c57) /* WLC2; old WLC1 socket is not reused. */
#define WLC_VERSION 2u
enum wlc_operation {WLC_EVENT=0,WLC_STATUS=1,WLC_ARM=2,WLC_COMMIT=3,WLC_DISARM=4,WLC_VOLUME_STEP=5,WLC_VOLUME_MODE=6};
enum wlc_phase {WLC_IDLE=0,WLC_ARMING=1,WLC_READY=2,WLC_ACTIVE=3,WLC_STOPPING=4,WLC_FAILED=5,WLC_UNKNOWN=6};
enum wlc_flags {WLC_RF_CONNECTED=1,WLC_ROUTE_PUBLISHED=2,WLC_NATIVE_QUIESCENT=4,WLC_GUARD_QUIESCENT=8,WLC_RAM_OWNED=16};
struct wlc_request {
    uint32_t magic,version,operation,request;
    uint64_t generation,route;
    unsigned char instance[16],token[16];
    uint64_t volume_revision;
    int32_t volume_value;uint32_t reserved;
};
struct wlc_reply {
    uint32_t magic,version,operation,request;
    int32_t status;uint32_t phase;
    uint64_t generation,route;
    unsigned char instance[16],token[16],peer[6],padding[2];
    uint32_t native_phase,flags,broker_pid;int32_t last_error;
    uint64_t native_epoch;
    struct wva_snapshot volume;
};
static inline int wlc_is_volume(unsigned op){return op==WLC_VOLUME_STEP||op==WLC_VOLUME_MODE;}
static inline int wlc_volume_fields_valid(const struct wlc_request*q){
    if(q->reserved)return 0;
    if(!wlc_is_volume(q->operation))return !q->volume_revision&&!q->volume_value;
    return q->volume_revision && (q->operation==WLC_VOLUME_STEP?(q->volume_value==1||q->volume_value==-1):(q->volume_value==0||q->volume_value==1));
}
/* Private inherited front-end/broker socket and copied immutable snapshot.
 * Broker owns all route/native state and survives front-end death to retire
 * its ORIGINAL handles. No unknown-root adoption or lease reset. */
int sony_bt_link_broker(int socket,pid_t parent,struct sony_bt_snapshot *,const struct wlc_reply *initial);
#endif
