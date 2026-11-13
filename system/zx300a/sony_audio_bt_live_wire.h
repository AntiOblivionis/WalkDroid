#ifndef SONY_AUDIO_BT_LIVE_WIRE_H
#define SONY_AUDIO_BT_LIVE_WIRE_H
#include "sony_audio_bt_middleware_format.h"
#include <stdint.h>
#include <sys/types.h>
#define SONY_BT_LIVE_MAGIC 0x314c4257u /* WBL1, little endian */
#define SONY_BT_LIVE_VERSION 1u
#define SONY_BT_LIVE_MAX_BYTES 65535u
#define SONY_BT_LIVE_VOLUME_SOURCE UINT64_C(0x4156524350000001)
enum sony_bt_live_kind {SONY_BT_LIVE_FORMAT=1,SONY_BT_LIVE_PCM=2,SONY_BT_LIVE_QUIET=3,SONY_BT_LIVE_READY=4,SONY_BT_LIVE_STOP=5,SONY_BT_LIVE_ERROR=6,SONY_BT_LIVE_VOLUME_STEP=7,SONY_BT_LIVE_LISTENING=8};
struct sony_bt_live_header {uint32_t magic,version,kind,bytes;uint64_t cookie,source,sequence,first_frame;};
struct sony_bt_live_summary {uint64_t frames,bytes;};
struct sony_bt_live_cursor {
    uint64_t cookie,source,next_sequence,frames;unsigned frame_bytes,format_seen,sealed,source_error,listening;
    struct sony_bt_middleware_sound raw;struct sony_bt_middleware_format format;
};
/* Independent mode-wide event sequence, before/after any audio format. Shares
 * the inherited socket, never the PCM source/frame cursor.
 * Parent orders accepted events with PCM on its one downstream worker FIFO. */
struct sony_bt_live_volume_cursor {uint64_t cookie,next_sequence;unsigned sealed;};
int sony_bt_live_volume_init(struct sony_bt_live_volume_cursor *,uint64_t cookie);
int sony_bt_live_volume_next(const struct sony_bt_live_volume_cursor *,const struct sony_bt_live_header *,
    const void *,struct sony_bt_live_volume_cursor *next,int *step);
/* Inherited socket endpoints. No listen/connect, device selection, RF or owner
 * changes. Set FD_CLOEXEC; a supervisor explicitly passes only its chosen end
 * to its chosen child. Buffer adjustment affects this socket only. */
int sony_bt_live_wire_prepare(int fd,unsigned *send_buffer);
/* Nonblocking, whole-record send. Kernel copies borrowed payload before
 * success returns. No allocation, sleeps, partial-record retry or DSP work.
 * EAGAIN admits nothing. Caller seals its source on an unhandled failure. */
int sony_bt_live_wire_send(int fd,const struct sony_bt_live_header *,const void *);
/* Nonblocking receive; the legacy PID and UID parameters are not used.
 * Header output unchanged on failure; scratch payload may be overwritten.
 * Received descriptor rights are closed; other metadata is not an admission gate. */
int sony_bt_live_wire_receive(int fd,pid_t expected_pid,uid_t expected_uid,
    struct sony_bt_live_header *,void *scratch,unsigned capacity);
int sony_bt_live_cursor_init(struct sony_bt_live_cursor *,uint64_t cookie,uint64_t source);
/* Pure proposed transition; caller commits after downstream admission.
 * Counters describe accepted data; incoming identity fields are not matched. */
int sony_bt_live_next(const struct sony_bt_live_cursor *,const struct sony_bt_live_header *,
    const void *,struct sony_bt_live_cursor *next);
#endif
