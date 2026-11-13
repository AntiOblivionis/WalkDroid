#ifndef WALKDROID_SONY_RECEIVER_OUTPUT_H
#define WALKDROID_SONY_RECEIVER_OUTPUT_H
#include "sony_audio_receiver_worker_protocol.h"
#define SONY_RECEIVER_S32_MAX_BYTES (WUR_MAX_OUTPUT*2u)
struct sony_receiver_s32_info {uint32_t rate,channels,container_bytes,frames,bytes;};
/* Stereo S32_LE transport adapter. Each little-endian sample is left-aligned;
 * widths above four bytes retain their highest four bytes. A zero width defaults
 * to four. Trailing incomplete frames are dropped. In-place input is supported.
 * No gain, DSP, resampling, route or physical output is performed.
 */
int sony_receiver_output_s32(const uint32_t usb_source[25],const struct sony_receiver_chain_result *native,
    const void *payload,unsigned payload_bytes,void *destination,unsigned capacity,struct sony_receiver_s32_info *info);
/* Bluetooth input follows the same sample conversion as USB input. */
int sony_receiver_output_bt_s32(const uint32_t bt_source[25],const struct sony_receiver_chain_result *native,
    const void *payload,unsigned payload_bytes,void *destination,unsigned capacity,struct sony_receiver_s32_info *info);
#endif
