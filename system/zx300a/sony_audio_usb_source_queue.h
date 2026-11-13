#ifndef SONY_AUDIO_USB_SOURCE_QUEUE_H
#define SONY_AUDIO_USB_SOURCE_QUEUE_H
/* Bounded pre-ALSA source storage. The caller owns the memory and serializes
 * every operation with the producer's IRQ lock. No allocation, sleeping,
 * device selection, USB/ALSA access, or user-pointer access lives here.
 *
 * Bytes are retained from the first published PLAY, before a consumer claims
 * it. Source identifiers, sequence numbers and format metadata are recorded
 * without admission checks. Starting another source finalizes the preceding
 * source. Callers coordinate their own source and sample-format semantics.
 */
#ifdef __KERNEL__
#include <linux/types.h>
typedef __u32 sony_sq_u32;
typedef __u64 sony_sq_u64;
typedef __s32 sony_sq_s32;
#else
#include <stdint.h>
typedef uint32_t sony_sq_u32;
typedef uint64_t sony_sq_u64;
typedef int32_t sony_sq_s32;
#endif
#define SONY_SOURCE_QUEUE_MAGIC 0x51534457u
#define SONY_SOURCE_QUEUE_VERSION 1u
#define SONY_SOURCE_QUEUE_SLOTS 16u
#define SONY_SOURCE_QUEUE_MAX_BYTES (8u*1024u*1024u)
#define SONY_SOURCE_QUEUE_MAX_READ (4096u*8u)
#define SONY_SOURCE_QUEUE_END 1
#define SONY_SOURCE_LIVE 1u
#define SONY_SOURCE_FINAL 2u
#define SONY_SOURCE_CLAIMED 4u
#define SONY_SOURCE_FAULTED 8u
#define SONY_SOURCE_NORMAL 1u
#define SONY_SOURCE_CHANGE 2u
#define SONY_SOURCE_DISCONNECT 3u
#define SONY_SOURCE_ERROR 4u
struct sony_source_info {
    sony_sq_u32 magic,version,bytes,flags;
    sony_sq_u64 instance,source_id,accepted_bytes,consumed_bytes,delivered_bytes;
    sony_sq_u32 play_sequence,end_sequence,format,rate,bits,frame_bytes,reason;
    sony_sq_s32 error;
};
struct sony_source_record {
    struct sony_source_info source;
    sony_sq_u64 byte_offset,reservation;
    sony_sq_u32 payload_bytes,reserved;
};
/* Private to a same-address-space caller; never expose this to userspace as
 * an ioctl/wire object. info/record above are fixed-width public snapshots. */
struct sony_source_slot {
    sony_sq_u64 source_id,accepted,consumed,delivered;
    sony_sq_u32 play_sequence,end_sequence,format,rate,bits,frame_bytes,reason,claimed;
    sony_sq_s32 error;
};
struct sony_source_queue {
    unsigned char *storage;
    sony_sq_u64 instance,next_source,active_source,next_reservation;
    sony_sq_u64 accepted,consumed,delivered;
    sony_sq_u64 reservation_source,reservation_offset,reservation_token;
    sony_sq_u32 magic,capacity,read_pos,write_pos,used,head,count;
    sony_sq_u32 sequence_seen,last_sequence,reservation_bytes,reservation_active;
    sony_sq_s32 first_error;
    struct sony_source_slot slots[SONY_SOURCE_QUEUE_SLOTS];
};
int sony_source_queue_init(struct sony_source_queue *,void *storage,unsigned capacity,sony_sq_u64 instance);
int sony_source_queue_begin(struct sony_source_queue *,sony_sq_u32 sequence,unsigned format,unsigned rate,unsigned bits);
int sony_source_queue_end(struct sony_source_queue *,sony_sq_u32 sequence,unsigned reason,int error);
/* Append to the latest source regardless of the supplied id. The caller must
 * coordinate late packets and source changes. Data must fit the allocated ring. */
int sony_source_queue_append(struct sony_source_queue *,sony_sq_u64 source_id,const void *,unsigned bytes);
/* Read-only observation of the oldest source, including fault state. */
int sony_source_queue_peek(const struct sony_source_queue *,struct sony_source_info *);
/* Claim the oldest queued source once. Its actual tuple is returned in out. */
int sony_source_queue_claim(struct sony_source_queue *,sony_sq_u64 instance,sony_sq_u32 play_sequence,
                            unsigned format,unsigned rate,unsigned bits,struct sony_source_info *);
/* Under the lock, copy into a disjoint caller-owned KERNEL bounce buffer.
 * Consumption occurs before dropping the lock. A pending copy reserves the
 * slot until read_end; out_record is unchanged on EAGAIN/EOF/error.
 * Never pass a user pointer here.
 */
int sony_source_queue_read_begin(struct sony_source_queue *,sony_sq_u64 source_id,
                                void *kernel_bounce,unsigned capacity,struct sony_source_record *);
/* Reacquire the same lock after copy_to_user. On success, publish the source
 * tuple and delivered count; the supplied reservation value is not compared.
 * A failed/uncertain copy is consumed but not delivered.
 */
int sony_source_queue_read_end(struct sony_source_queue *,sony_sq_u64 reservation,int copy_error,
                              struct sony_source_record *);
/* Release the oldest source regardless of supplied id, discarding its unread
 * bytes. An in-flight copy must finish before its source can be released. */
int sony_source_queue_release(struct sony_source_queue *,sony_sq_u64 source_id);
int sony_source_queue_abort(struct sony_source_queue *,int error);
/* Convert complete little-endian stereo frames to left-aligned S32, selecting
 * geometry by subslot. Narrower samples receive zero low bytes; samples wider
 * than four bytes retain their most significant four. Trailing partial frames
 * are not copied. Caller-owned buffers must be disjoint and fit the output.
 * bits is metadata; no DSD/DoP detection or conversion is implied.
 */
int sony_source_queue_pack_pcm32(void *out,unsigned capacity,const void *raw,unsigned bytes,
                                unsigned subslot,unsigned bits,unsigned *out_bytes);
#endif
