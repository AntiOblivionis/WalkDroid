#ifndef SONY_AUDIO_DSD_UNPACK_H
#define SONY_AUDIO_DSD_UNPACK_H

#include <stddef.h>
#include <stdint.h>

/* Byte packing only. Does not apply gain, filters, fading, padding or resampling.
 * Sony DSP consumes MSB-first, byte-interleaved stereo. DSF input here excludes
 * the original demux's 16-byte header and consists of complete 4096-byte blocks
 * per channel, including the on-disk padding of the final block. valid_bytes
 * excludes that padding. DFF means uncompressed interleaved DSD, never DST.
 * Mono is duplicated exactly as the original converter does.
 *
 * On failure, output and *written are unchanged. Input and the actually written
 * output range must not overlap. The caller owns these valid memory ranges.
 * Success does NOT imply a complete file was parsed or an EOS/drain occurred.
 * The fixed-packet DSP API needs a separate assembler for partial output.
 */
enum sony_dsd_packing {
    SONY_DSD_DSF_BLOCKS = 0,
    SONY_DSD_DFF_INTERLEAVED = 1
};

int sony_dsd_unpack(enum sony_dsd_packing packing, unsigned channels,
                    const uint8_t *input, size_t input_bytes, size_t valid_bytes,
                    uint8_t *output, size_t capacity, size_t *written);

#endif
