#ifndef SONY_AUDIO_BT_EPOCH_RECEIVER_H
#define SONY_AUDIO_BT_EPOCH_RECEIVER_H
#include "sony_audio_bt_epoch_transport.h"
#include "sony_audio_receiver_async.h"
struct sony_bt_epoch_receiver;
struct sony_bt_epoch_receiver_state {
    struct sony_bt_epochs cursor;
    struct sony_receiver_async_state backend;
    uint64_t completed_epochs,source_acked_frames,native_acked_bytes,sink_acked_bytes;
    uint64_t output_acked_frames; /* Native renderer frames after exact S16/S32 container accounting. */
    uint64_t volume_events,packets,ready_count;
    unsigned pending_kind,pending_bytes,partial_frames,stop_requested;
    int first_error;
};
/* Serialized calls over an inherited socket. Borrow a BT async session, whose mode owner
 * remains held across all source epochs. No fork, RF, hardware reset, volume
 * conversion, silence injection, uncertain replay or global CLEAN here.
 * The backend remains owned by the outer controller after this is destroyed. */
int sony_bt_epoch_receiver_create(struct sony_bt_epoch_receiver **,int fd,
    pid_t source_pid,uid_t source_uid,uint64_t cookie,unsigned expected_output,
    struct sony_receiver_async *);
/* Nonblocking. A full received packet is retained on EAGAIN; admitted partial
 * PCM chunks are never retried. READY follows the real worker OPEN. CLOSED
 * follows backend FINISH. Mode STOP precedes any still-unsent READY.
 * Zero is progress/wait; one means the input sent QUIET and queued work ended.
 * These events do not independently prove hardware state. */
int sony_bt_epoch_receiver_poll(struct sony_bt_epoch_receiver *);
int sony_bt_epoch_receiver_request_stop(struct sony_bt_epoch_receiver *);
/* Outer error cleanup only: send the mode STOP without further backend/data
 * admission. EAGAIN means no control was sent; caller retains this context.
 * First data error is preserved, and this never manufactures CLOSED/QUIET. */
int sony_bt_epoch_receiver_stop_source(struct sony_bt_epoch_receiver *);
int sony_bt_epoch_receiver_state(struct sony_bt_epoch_receiver *,struct sony_bt_epoch_receiver_state *);
/* The legacy upstream_quiesced argument is retained but not used as a gate;
 * backend ownership remains with the caller. */
int sony_bt_epoch_receiver_destroy(struct sony_bt_epoch_receiver **,unsigned upstream_quiesced);
#endif
