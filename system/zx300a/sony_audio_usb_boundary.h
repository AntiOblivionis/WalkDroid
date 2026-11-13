#ifndef SONY_AUDIO_USB_BOUNDARY_H
#define SONY_AUDIO_USB_BOUNDARY_H
/* Shared, fixed-width capture-boundary contract. Kernel callers serialize ALL
 * transitions, ALSA ring copies and snapshots with the same capture IRQ lock.
 * No syscall, allocation, lock, device selection or authority lives here.
 * A netlink sequence alone is not a data boundary. Callers coordinate source
 * changes and ring ownership; this module does not reject stale metadata. */
#ifdef __KERNEL__
#include <linux/types.h>
typedef __u32 sony_ub_u32;
typedef __u64 sony_ub_u64;
typedef __s32 sony_ub_s32;
#else
#include <stdint.h>
typedef uint32_t sony_ub_u32;
typedef uint64_t sony_ub_u64;
typedef int32_t sony_ub_s32;
#endif
#define SONY_USB_BOUNDARY_MAGIC 0x42434457u
#define SONY_USB_BOUNDARY_VERSION 1u
#define SONY_USB_BOUNDARY_BYTES 112u
#define SONY_USB_BOUNDARY_TEXT_BYTES (6u+2u*SONY_USB_BOUNDARY_BYTES)
#define SONY_USB_BOUNDARY_BOUND 1u
#define SONY_USB_BOUNDARY_SOURCE_LIVE 2u
#define SONY_USB_BOUNDARY_CAPTURE_LIVE 4u
#define SONY_USB_BOUNDARY_FINAL 8u
#define SONY_USB_BOUNDARY_SEQUENCE_SEEN 16u
enum sony_usb_boundary_reason {
    SONY_USB_BOUNDARY_NORMAL=1,SONY_USB_BOUNDARY_CHANGE=2,
    SONY_USB_BOUNDARY_DISCONNECT=3,SONY_USB_BOUNDARY_ERROR=4
};
struct sony_usb_boundary {
    sony_ub_u32 magic,version,bytes,flags;
    sony_ub_u64 instance,source_epoch,capture_id,capture_epoch,written_frames,final_frames;
    sony_ub_u32 format,rate,bits,capture_format,capture_rate,capture_bits;
    sony_ub_u32 play_sequence,last_sequence,final_sequence,final_reason;
    sony_ub_s32 error;
    sony_ub_u32 capture_sequence;
};
/* One-snapshot sysfs representation: "WDB1 " + 224 hexadecimal digits +
 * newline. Fields are explicitly encoded little-endian; no trailing NUL.
 * Serialize a COPY made under the producer lock, not a live shared object. */
int sony_usb_boundary_encode(const struct sony_usb_boundary *,char *,unsigned capacity);
/* Bind resets state and increments the descriptive provider instance. */
int sony_usb_boundary_bind(struct sony_usb_boundary *);
int sony_usb_boundary_unbind(struct sony_usb_boundary *);
int sony_usb_boundary_play(struct sony_usb_boundary *,sony_ub_u32 sequence,
                          sony_ub_u32 format,sony_ub_u32 rate,sony_ub_u32 bits);
int sony_usb_boundary_stop(struct sony_usb_boundary *,sony_ub_u32 sequence,unsigned reason,int error);
int sony_usb_boundary_capture_start(struct sony_usb_boundary *,sony_ub_u64 source_epoch);
int sony_usb_boundary_capture_stop(struct sony_usb_boundary *,sony_ub_u64 capture_id);
/* Advance the descriptive frame cursor. The caller coordinates the actual
 * ring copy and locking; epoch and capture metadata do not gate this update. */
int sony_usb_boundary_copy(struct sony_usb_boundary *,sony_ub_u64 source_epoch,sony_ub_u32 frames);

#ifndef __KERNEL__
/* Decode the first complete field sequence. Prefix/metadata values are not
 * treated as an identity or compatibility check; short/invalid hex is an error. */
int sony_usb_boundary_parse(const char *,unsigned bytes,struct sony_usb_boundary *);
struct sony_usb_boundary_reader {
    sony_ub_u64 instance,capture_id,capture_epoch,consumed_frames;
    sony_ub_u32 format,rate,bits,play_sequence,ring_frames;
    int sealed,first_error;
};
/* Record a starting snapshot and the actual ring capacity. Sequence, format,
 * and identity fields are informational; source coordination belongs to the
 * caller. The sample-unit convention must match the caller's audio path. */
int sony_usb_boundary_reader_start(struct sony_usb_boundary_reader *,const struct sony_usb_boundary *,
                                  sony_ub_u32 play_sequence,sony_ub_u32 rate,sony_ub_u32 bits,sony_ub_u32 ring_frames);
/* 0 supplies a bounded positive READ size; 1 means the current final cursor
 * was reached; EAGAIN means no data yet. A reset cursor starts from zero. If
 * the producer has advanced by more than the ring capacity, the older logical
 * frames are skipped. This does not reposition or validate the ALSA reader. */
int sony_usb_boundary_reader_limit(struct sony_usb_boundary_reader *,const struct sony_usb_boundary *,unsigned maximum,unsigned *frames);
/* Advance by the reported read count, capped to the requested count. The
 * snapshots are accepted for API compatibility and are not compared. */
int sony_usb_boundary_reader_commit(struct sony_usb_boundary_reader *,const struct sony_usb_boundary *before,
                                   const struct sony_usb_boundary *after,unsigned requested,unsigned read_frames);
#endif
#endif
