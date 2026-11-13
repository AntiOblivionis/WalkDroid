#ifndef SONY_AUDIO_DSD_NATIVE_WRITER_H
#define SONY_AUDIO_DSD_NATIVE_WRITER_H
#include <stdint.h>
/* Borrowed, already-open original Sony DsfPlayer object. This API does NOT
 * open/negotiate an ALSA device, power it, select a jack or own its lifetime.
 * The caller supplies a library and object with the expected ARM32 ABI;
 * this interface does not verify library bytes, actual DSD_U8 negotiation,
 * routing or hardware ownership. Caller serializes object use. */
struct sony_dsd_native_writer;
struct sony_dsd_native_writer_state {
    unsigned int bit_rate,completed_packets,failed;
    int first_error;
    uint64_t completed_bytes,observed_admitted_bytes;
};
int sony_dsd_native_writer_bind(struct sony_dsd_native_writer **,void *library,
                                void *borrowed_native_object,unsigned int bit_rate);
/* Exactly 8192/16384/24576/32768 bytes, with private writable scratch. Kernel
 * DSD pre-copy may mutate that scratch; the caller's original remains intact.
 * An error seals this writer. No automatic retry/reopen/recovery. */
int sony_dsd_native_writer_write(struct sony_dsd_native_writer *,const void *,unsigned int bytes);
int sony_dsd_native_writer_state(struct sony_dsd_native_writer *,struct sony_dsd_native_writer_state *);
/* Releases scratch only. No Drain/Drop/Close or power/owner release is implied. */
int sony_dsd_native_writer_destroy(struct sony_dsd_native_writer **);
#endif
