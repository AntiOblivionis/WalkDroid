#ifndef WALKDROID_SONY_BT_TX_SESSION_H
#define WALKDROID_SONY_BT_TX_SESSION_H
#include <stddef.h>
#include <stdint.h>
#include "sony_audio_bt_tx_pcm.h"

/* Private, inherited-socket ABI. All integers are little endian. A process
 * owns ONE session, selected peer and format. No reconnect or replay. The
 * Android broker must own BOTH audio and radio leases before starting it.
 * These messages are not an authorization mechanism or hardware ACKs. */
#define WBT_MAGIC UINT32_C(0x32544257) /* WBT2; never interpret a WBT1 ACK as this. */
#define WBT_VERSION 2u
#define WBT_MAX_PCM 8192u
enum wbt_command { WBT_OPEN=1, WBT_WRITE=2, WBT_FINISH=3, WBT_CANCEL=4 };
enum wbt_phase { WBT_NEW=0, WBT_STREAMING=1, WBT_CLOSED=2, WBT_FAILED=3 };
enum wbt_reply_flags { WBT_NATIVE_SUBMISSION_ONLY=1, WBT_CLEANUP_VERIFIED=2,WBT_PROCESSING_STATS=4 };
struct wbt_request {
    uint32_t magic,version,command,request;
    uint64_t session,frame_position;
    uint32_t payload_bytes,rate,channels,bits;
    uint8_t peer[6],reserved[10];
};
struct wbt_reply {
    uint32_t magic,version,command,request;
    uint64_t session,accepted_frames,submitted_frames;
    int32_t status;
    uint32_t phase,queued_bytes,flags;
    uint64_t uncertain_frames;
    /* submitted_frames counts INPUT frames accepted by the native processing
     * stage, including its bounded queue. Codec output has separate units. */
    struct sony_tx_pcm_stats processing;
};

/* begin starts the selected stream and supplies a preferred block size.
 * A submit failure is ambiguous, so the engine does not retry that block.
 * close is called once after any attempted begin, including a partial failure. */
struct wbt_ops {
    int (*begin)(void *,const struct wbt_request *,unsigned *preferred);
    int (*submit)(void *,const void *,unsigned bytes);
    int (*close)(void *);
    /* Optional explicit processing stage. finish runs before close; abort,
     * CANCEL and lost-response cleanup never call it. stats is read-only. */
    int (*finish)(void *);
    int (*stats)(void *,struct sony_tx_pcm_stats *);
};
struct wbt_session {
    uint64_t nonce,accepted,submitted,uncertain;
    uint32_t next_request,phase,preferred,queued,accepted_tail;
    int begin_attempted,close_attempted,cleanup_verified,close_status;
    uint8_t pcm[WBT_MAX_PCM];
    const struct wbt_ops *ops;
    void *context;
};
int wbt_init(struct wbt_session *,uint64_t,const struct wbt_ops *,void *);
int wbt_handle(struct wbt_session *,const struct wbt_request *,const void *,struct wbt_reply *);
typedef int (*wbt_read_payload)(void *,void *,size_t);
int wbt_consume_payload(struct wbt_session *,struct wbt_request *,struct wbt_reply *,
                        wbt_read_payload,void *,int *operation);
/* EOF, truncated input, timeout and lost response all abort the same exact
 * session. Discards queued bytes; never sends a tail during cancellation. */
int wbt_abort(struct wbt_session *);
#endif
