#ifndef SONY_AUDIO_DSD_ASSEMBLER_H
#define SONY_AUDIO_DSD_ASSEMBLER_H
#include <stddef.h>
#include <stdint.h>

#define SONY_DSD_PACKET_BYTES 32768u
struct sony_dsd_assembler;
struct sony_dsd_assembly_status {
    uint64_t source_admitted, packet_bytes_acknowledged, padding_bytes_acknowledged;
    size_t buffered_bytes;
    int finished, faulted;
};
/* Synchronous all-or-error admission. Borrowed packet is exactly 32768 bytes
 * of canonical MSB-first, interleaved stereo. source_bytes identifies original
 * content in this packet; the remainder is Sony 0x69 silence. Only finish emits
 * terminal=1. Callback success means admitted by the next layer, NOT hardware
 * consumed. A failure may have partly consumed data; never retry that session.
 * Callbacks must not reenter/destroy this assembler or retain packet pointers.
 */
typedef int (*sony_dsd_packet_sink)(void *,const uint8_t *,size_t source_bytes,int terminal);
typedef int (*sony_dsd_eos_sink)(void *);

int sony_dsd_assembler_create(struct sony_dsd_assembler **,sony_dsd_packet_sink,sony_dsd_eos_sink,void *);
/* Byte assembly only; caller already parsed/unpacked/applied required Sony fade.
 * Each call accepts <=65536 even bytes. Zero bytes is a no-op, not EOS. Invalid
 * preflight leaves state unchanged. After an internal/callback error the stream
 * is faulted; inspect cumulative source_admitted to diagnose, never resume it.
 * Serialize calls. Source must not overlap this assembler's private storage.
 */
int sony_dsd_assembler_write(struct sony_dsd_assembler *,const uint8_t *,size_t);
/* Normal end: send buffered content padded with 0x69 to 32768, including a full
 * silent packet when exactly aligned/empty, then EOS exactly once. Matches the
 * pinned original WMX stopWrite normal EOS packet ordering. This is not pause,
 * abort, DSP Flush, hardware drain or an end-to-end playback completion signal.
 * 0 newly finished, 1 already finished. Failures fault and are never retried.
 */
int sony_dsd_assembler_finish(struct sony_dsd_assembler *);
int sony_dsd_assembler_status(const struct sony_dsd_assembler *,struct sony_dsd_assembly_status *);
/* Discard pending data, no callbacks. NULL allowed. Reentrant close is refused.
 * Caller must release downstream DSP/output separately, even after a fault. */
int sony_dsd_assembler_abort_free(struct sony_dsd_assembler *);
#endif
