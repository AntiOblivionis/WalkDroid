#ifndef SONY_AUDIO_BT_STREAM_EPOCHS_H
#define SONY_AUDIO_BT_STREAM_EPOCHS_H
#include "sony_audio_bt_live_wire.h"

/* WBE2 is explicitly separate from the fixed-format WBL1 receiver. This
 * parser does no I/O, RF, allocation, owner reset, sample conversion or replay.
 * The existing WBL1 runtime remains unchanged until a whole new controller,
 * upstream producer and authenticated transport are built together. */
#define SONY_BT_EPOCH_MAGIC UINT32_C(0x32454257)
#define SONY_BT_EPOCH_VERSION 2u
enum sony_bt_epoch_message {
    SONY_BT_EPOCH_LISTENING=1,SONY_BT_EPOCH_FORMAT=2,SONY_BT_EPOCH_PCM=3,
    SONY_BT_EPOCH_END=4,SONY_BT_EPOCH_QUIET=5,
    SONY_BT_EPOCH_READY=6,SONY_BT_EPOCH_CLOSED=7,SONY_BT_EPOCH_STOP=8,
    SONY_BT_EPOCH_ERROR=9,SONY_BT_EPOCH_FLUSH_READY=10
};
enum sony_bt_epoch_phase {
    SONY_BT_EPOCH_NEW=0,SONY_BT_EPOCH_WAITING=1,
    SONY_BT_EPOCH_OPENING=2,SONY_BT_EPOCH_ACTIVE=3,
    SONY_BT_EPOCH_FINISHING=4,SONY_BT_EPOCH_PAUSED=5,
    SONY_BT_EPOCH_QUIESCED=6
};
struct sony_bt_epoch_summary {uint64_t frames,bytes;};
struct sony_bt_epochs {
    uint64_t cookie,epoch,next_sequence,control_sequence;
    uint64_t epoch_frames,epoch_bytes,total_frames,total_bytes,closed_epochs;
    unsigned phase,frame_bytes,listening,stop_requested,quiet_seen;
    struct sony_bt_middleware_sound raw;
    struct sony_bt_middleware_format format;
};
int sony_bt_epochs_init(struct sony_bt_epochs *,uint64_t cookie);
/* Exact proposed transition. Commit *next ONLY after the corresponding
 * original backend OPEN/DATA/FINISH admission succeeds. EAGAIN admits nothing;
 * any uncertain submission is sealed by the caller rather than replayed.
 * FORMAT advances a local source epoch. Incoming identity and accounting
 * fields are not used to admit a transition. */
int sony_bt_epochs_next(const struct sony_bt_epochs *,
    const struct sony_bt_live_header *,const void *,unsigned payload_bytes,
    struct sony_bt_epochs *next);
/* Construct a control packet AND its proposed cursor. Caller commits only on
 * successful whole-record send. READY requires a successful real DSP/sink
 * OPEN. CLOSED follows the caller's FINISH handling. These pure functions do
 * not prove hardware state. */
int sony_bt_epochs_ready(const struct sony_bt_epochs *,uint64_t backend_epoch,
    struct sony_bt_epochs *next,struct sony_bt_live_header *control);
int sony_bt_epochs_closed(const struct sony_bt_epochs *,uint64_t backend_epoch,
    struct sony_bt_epochs *next,struct sony_bt_live_header *control);
int sony_bt_epochs_stop(const struct sony_bt_epochs *,
    struct sony_bt_epochs *next,struct sony_bt_live_header *control);
/* STOP has sealed native admission, but a prefetched accepted prefix may be
 * waiting for this exact successful backend OPEN. This authorizes only that
 * prefix to drain; it must never enable the native decoder/player. */
int sony_bt_epochs_flush_ready(const struct sony_bt_epochs *,uint64_t backend_epoch,
    struct sony_bt_epochs *next,struct sony_bt_live_header *control);
#endif
