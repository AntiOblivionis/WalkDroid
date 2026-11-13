#ifndef SONY_AUDIO_DSF_READER_H
#define SONY_AUDIO_DSF_READER_H
#include <stddef.h>
#include <stdint.h>
struct sony_dsf_reader;
struct sony_dsf_info {
    uint64_t sample_bits_per_channel, source_bytes, physical_data_bytes, metadata_offset;
    unsigned rate, channels;
};
struct sony_dsf_block {
    size_t bytes;
    uint64_t first_byte_per_channel;
    int first, last;
};
/* Caller owns a stable, read-only file snapshot and its size for this lifetime.
 * Synchronous read: return 0 and actual==requested, or an error. A partial read
 * is fatal; the reader never silently substitutes zeros or retries a live file.
 * Callback may not reenter or destroy this reader. No seek/write API provided.
 */
typedef int (*sony_dsf_read_at)(void *,uint64_t offset,uint8_t *,size_t requested,size_t *actual);
/* Reads the 92-byte DSD/fmt/data layout as uncompressed LSB-first mono/stereo
 * with 4096-byte channel blocks and a positive byte-aligned sample count.
 * Format-version and format-ID words do not select a decoder or block opening;
 * other layouts are not adapted. The source rate is returned as provided and
 * may still be rejected by the downstream DSP. Optional trailing metadata is
 * bounded/skipped. Open failures leave *out unchanged; no Sony fade/DSP here.
 */
int sony_dsf_reader_open(struct sony_dsf_reader **out,uint64_t file_bytes,sony_dsf_read_at,void *);
int sony_dsf_reader_info(const struct sony_dsf_reader *,struct sony_dsf_info *);
/* Returns 0 with one canonical MSB-first byte-interleaved stereo block, 1 at
 * confirmed end, or negative error. At most 32768 physical bytes are read and
 * 65536 canonical bytes produced (mono duplicates channels). Final on-disk
 * padding is removed before output. Metadata/output unchanged on any error/EOF.
 * first/last are file boundaries, not claimed original OMX fade flag policy.
 * Argument errors do not consume; I/O failure faults this lifetime, no retry.
 */
int sony_dsf_reader_next(struct sony_dsf_reader *,uint8_t *,size_t capacity,struct sony_dsf_block *);
/* NULL allowed, reentrant destruction refused. Caller closes its own file. */
int sony_dsf_reader_close(struct sony_dsf_reader *);
#endif
