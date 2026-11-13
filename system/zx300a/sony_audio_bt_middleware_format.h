#ifndef SONY_AUDIO_BT_MIDDLEWARE_FORMAT_H
#define SONY_AUDIO_BT_MIDDLEWARE_FORMAT_H
#include "sony_audio_receiver_pcm.h"
/* Raw callback values, before libBtCompIf and BtPlayerService conversion. */
struct sony_bt_middleware_sound {uint32_t state,codec,rate,channel,native_flag;};
struct sony_bt_middleware_format {struct sony_bt_pcm_stream pcm;uint32_t service_state,service_codec,native_flag;};
/* Executed against both original conversions and the real track factory.
 * Raw0/1/2/5 -> service1/2/3/4 -> PreAudioInfo10/2/5/11. Receiver IDs1/3/4
 * remain stable; ID2 carries raw0's native16 PCM tag. Actual SBC44100/16
 * reception was observed as raw0, not the previously assumed raw1. Preserve
 * its original full track metadata. Raw channel2 stays2, others normalize to1
 * like Sony. Raw state0/1/2 -> service1/2/3; other raw states normalize to1.
 * Other unknown codecs are NOT admitted as SBC just because Sony's enum
 * defaults to a 16-bit track. Out stays unchanged on all rejections.
 * Pure metadata only; no claim of RF, stream Open/Start or actual PCM delivery.
 */
int sony_bt_middleware_format(const struct sony_bt_middleware_sound *,struct sony_bt_middleware_format *);
#endif
