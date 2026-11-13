#ifndef WALKDROID_SONY_AUDIO_RECEIVER_PCM_H
#define WALKDROID_SONY_AUDIO_RECEIVER_PCM_H
#include "sony_audio_usb_stream.h"
#include <stdint.h>
/* Pure data adapter; no devices, routes, DSP, owner, threads or resampling.
 * Exact original USB TrackParam factory contract, 27 independently captured
 * cases. Caller must authenticate events and retain receiver-wide exclusivity.
 * The native rate/bit-width/codec metadata must reach Sony's processing entry. */
struct sony_receiver_pcm;
struct sony_receiver_pcm_packet {uint32_t track[25],frames,bytes;uint64_t first_frame;};
#define SONY_RECEIVER_PCM_MAX_PACKET_FRAMES 512u
#define SONY_RECEIVER_PCM_QUEUE_FRAMES 4096u
int sony_receiver_pcm_usb_track(const struct sony_usb_stream *,uint32_t out[25]);
/* WalkDroid receiver codec IDs retained for this adapter's existing ABI;
 * these are neither raw BtMw nor IBtPlayerService codec enum values. Convert
 * raw callbacks with sony_audio_bt_middleware_format.h before admission.
 * The existing families map to original PreAudioInfo2/5/11. Native PCM is
 * PreAudioInfo10: the exact original component/service result for raw codec0,
 * which actual Sony SBC reception emits. Do not relabel it as raw codec1. */
enum sony_bt_av_codec {SONY_BT_AV_SBC=1,SONY_BT_AV_NATIVE_PCM=2,SONY_BT_AV_AAC=3,SONY_BT_AV_LDAC=4};
struct sony_bt_pcm_stream {uint32_t rate,codec,channel;};
int sony_receiver_pcm_bt_track(const struct sony_bt_pcm_stream *,uint32_t out[25]);
/* Original decoded Bluetooth payload: native stereo S16, or LDAC24 in its
 * original four-byte container. Metadata/bytes stay unchanged; no USB
 * narrowing, AudioTrack/software volume, resampling or channel duplication.
 * channel1/2 preserves Sony's ChConfig; native metadata still has two slots.
 * This adapter provides neither radio negotiation nor mode ownership. */
int sony_receiver_pcm_open_bt(struct sony_receiver_pcm **,const struct sony_bt_pcm_stream *);
int sony_receiver_pcm_push_bt(struct sony_receiver_pcm *,const void *,unsigned frames);
/* Single serialized caller. *out must be NULL. Only a validated PLAY/PCM tuple;
 * DSF/DSD/DoP and STOP are not silently converted. The input is Sony UAC2's
 * little-endian, stereo S32 container. Native Sony track is S16 for a 16-bit
 * source; upper halfwords are retained exactly, after verifying lower bits zero.
 * A 24-bit source keeps its S32 container, with lower eight bits required zero.
 * Source bytes are immutable; admission is all-or-nothing on any error/full
 * queue. Capacity exhaustion returns ENOBUFS, never drops/overwrites samples. */
int sony_receiver_pcm_open_usb(struct sony_receiver_pcm **,const struct sony_usb_stream *);
int sony_receiver_pcm_push(struct sony_receiver_pcm *,const void *,unsigned frames);
/* One native 512-frame block, except an explicitly sealed final partial block.
 * EAGAIN means insufficient unsealed data; ENODATA means sealed and emptied.
 * Out packet/data unchanged on failure. Returns the native S16/S32 payload,
 * exact original metadata and contiguous frame position; no gain, dither,
 * limiter, channel swap, rate conversion or invented silence is applied.
 * Input/data and out packet must be disjoint. Maximum native payload4096bytes. */
int sony_receiver_pcm_take(struct sony_receiver_pcm *,void *data,unsigned capacity,
    struct sony_receiver_pcm_packet *out);
/* Seal once after a known real input boundary. Does not send Sony EOS, pad,
 * drain, stop a PCM or release exclusivity. take drains the exact remaining
 * samples; a format change requires a separate closed lifetime. */
int sony_receiver_pcm_seal(struct sony_receiver_pcm *);
int sony_receiver_pcm_state(const struct sony_receiver_pcm *,uint64_t *accepted,
    uint64_t *taken,unsigned *queued,int *sealed);
void sony_receiver_pcm_destroy(struct sony_receiver_pcm **);
#endif
