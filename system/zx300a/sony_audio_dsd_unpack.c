#include "sony_audio_dsd_unpack.h"
#include <errno.h>

/* Differentially checked against the pinned original converter's instructions.
 * See research/sony-audio/dsf-converter-host. In particular, never reproduce the
 * original DSF behavior that consumes an entire input when output is too small.
 */
static uint8_t reverse_byte(uint8_t x) {
    x = (uint8_t)(((x & 0x55u) << 1) | ((x >> 1) & 0x55u));
    x = (uint8_t)(((x & 0x33u) << 2) | ((x >> 2) & 0x33u));
    return (uint8_t)((x << 4) | (x >> 4));
}

int sony_dsd_unpack(enum sony_dsd_packing packing, unsigned channels,
                    const uint8_t *input, size_t input_bytes, size_t valid_bytes,
                    uint8_t *output, size_t capacity, size_t *written) {
    size_t output_bytes, per_channel, frame;
    uintptr_t in_addr, out_addr, written_addr;
    if (!input || !output || !written || (channels != 1 && channels != 2))
        return -EINVAL;
    if (packing != SONY_DSD_DSF_BLOCKS && packing != SONY_DSD_DFF_INTERLEAVED)
        return -EINVAL;
    if (!input_bytes || input_bytes > 32768 || !valid_bytes ||
        valid_bytes > input_bytes || valid_bytes % channels)
        return -EINVAL;
    if (packing == SONY_DSD_DSF_BLOCKS) {
        if (input_bytes % (4096u * channels)) return -EINVAL;
    } else if (input_bytes != valid_bytes) {
        return -EINVAL;
    }
    per_channel = valid_bytes / channels;
    output_bytes = per_channel * 2; /* Bounded above by 65536. */
    if (capacity < output_bytes) return -ENOSPC;
    in_addr = (uintptr_t)input; out_addr = (uintptr_t)output;
    written_addr = (uintptr_t)written;
    if (input_bytes > UINTPTR_MAX - in_addr ||
        output_bytes > UINTPTR_MAX - out_addr ||
        sizeof(*written) > UINTPTR_MAX - written_addr)
        return -EINVAL;
    if (in_addr < out_addr + output_bytes && out_addr < in_addr + input_bytes)
        return -EINVAL;
    /* A metadata pointer inside either sample range would corrupt samples. */
    if ((written_addr < in_addr + input_bytes && in_addr < written_addr + sizeof(*written)) ||
        (written_addr < out_addr + output_bytes && out_addr < written_addr + sizeof(*written)))
        return -EINVAL;
    for (frame = 0; frame < per_channel; ++frame) {
        uint8_t left, right;
        if (packing == SONY_DSD_DSF_BLOCKS) {
            size_t base = (frame / 4096u) * (4096u * channels) + frame % 4096u;
            left = reverse_byte(input[base]);
            right = channels == 1 ? left : reverse_byte(input[base + 4096u]);
        } else {
            left = input[frame * channels];
            right = channels == 1 ? left : input[frame * 2 + 1];
        }
        output[frame * 2] = left;
        output[frame * 2 + 1] = right;
    }
    *written = output_bytes;
    return 0;
}
