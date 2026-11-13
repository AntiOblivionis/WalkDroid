#ifndef SONY_AUDIO_DSD_IO_GUARD_H
#define SONY_AUDIO_DSD_IO_GUARD_H
/* Audited ARM32 Sony DsfPlayer WRITE guard. Separate from the deployed PCM
 * guard: never enable this globally as a replacement for ordinary PCM.
 * A serialized owner supplies its exact PCM and private writable DSD buffer.
 * The pinned original method divides each aligned 8192-byte block into one
 * 4096-frame ALSA write. Any short/error/foreign/out-of-order call seals the
 * scope before another underlying write can occur. No retry or bit conversion.
 * This verifies synchronous admission only, NOT physical DSD timing/consumption.
 * Open/power/route ownership and compensating cleanup belong to the caller. */
#define SONY_DSD_IO_BLOCK_BYTES 8192u
#define SONY_DSD_IO_MAX_BYTES 32768u
struct sony_dsd_io_report {
    unsigned int expected_calls,calls,forwarded_calls,blocked_calls;
    unsigned int accepted_frames,foreign_handle_calls,recovery_blocked;
    int first_error,native_result;
};
int sony_dsd_io_begin(struct sony_dsd_io_report *,void *pcm,void *buffer,unsigned int bytes);
/* native_ack is the original method's frame count; it is independently checked
 * against all observed complete writes. End consumes the scope even on failure.
 * Failure is not rollback: a positive short may already have admitted a prefix. */
int sony_dsd_io_end(int native_result,unsigned int native_ack);
#endif
