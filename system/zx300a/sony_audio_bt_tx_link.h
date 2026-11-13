#ifndef WALKDROID_SONY_BT_TX_LINK_H
#define WALKDROID_SONY_BT_TX_LINK_H
#include "sony_audio_bt_tx_session.h"

/* WBL3 is deliberately incompatible with WBT2. A link owns one peer for its
 * entire lifetime; PCM epochs may finish and restart without releasing RF.
 * Native-close acknowledgement is NOT supervisor/lease cleanup evidence. */
#define WBL_MAGIC UINT32_C(0x334c4257)
#define WBL_VERSION 3u
enum wbl_command { WBL_CONNECT=1,WBL_RESUME=2,WBL_WRITE=3,WBL_PAUSE=4,WBL_CLOSE=5,WBL_CANCEL=6,WBL_DROP=7 };
enum wbl_phase { WBL_NEW=0,WBL_CONNECTED=1,WBL_STREAMING=2,WBL_CLOSED=3,WBL_FAILED=4 };
enum wbl_flags { WBL_NATIVE_SUBMISSION_ONLY=1,WBL_LINK_CONNECTED=2,
                 WBL_PROCESSING_STATS=4,WBL_NATIVE_CLOSED=8 };
struct wbl_request {
    uint32_t magic,version,command,request;
    uint64_t session,epoch,frame_position;
    uint32_t payload_bytes,rate,channels,bits;
    uint8_t peer[6],reserved[10];
};
struct wbl_reply {
    uint32_t magic,version,command,request;
    uint64_t session,epoch,accepted_frames,submitted_frames;
    int32_t status;
    uint32_t phase,queued_bytes,flags;
    uint64_t uncertain_frames;
    struct sony_tx_pcm_stats processing;
};
struct wbl_ops {
    int (*connect)(void *,const uint8_t peer[6]);
    /* epoch.close stops streaming without disconnecting or powering off. */
    int (*live)(void *,unsigned phase);
    struct wbt_ops epoch;
    int (*close_link)(void *);
};
struct wbl_session {
    uint64_t nonce,epoch;
    uint32_t next_request,phase;
    uint8_t peer[6];
    int connect_attempted,close_attempted,native_closed,close_status,epoch_active,epoch_stop_status;
    struct wbt_session pcm;
    const struct wbl_ops *ops;
    void *context;
};
int wbl_init(struct wbl_session *,uint64_t,const struct wbl_ops *,void *);
int wbl_handle(struct wbl_session *,const struct wbl_request *,const void *,struct wbl_reply *);
int wbl_consume_payload(struct wbl_session *,struct wbl_request *,struct wbl_reply *,
                        wbt_read_payload,void *,int *operation);
/* EOF, owner death and idle native-disconnect never flush/replay old PCM. */
int wbl_abort(struct wbl_session *);
int wbl_check_live(struct wbl_session *);
#endif
