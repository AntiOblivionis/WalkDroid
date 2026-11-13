#ifndef WALKDROID_SONY_CHAIN_CLIENT_H
#define WALKDROID_SONY_CHAIN_CLIENT_H
#include "sony_audio_stream.h"
#include "sony_audio_chain_bridge.h" /* Only plain settings cross IPC. */
#include "sony_audio_volume_memory.h"
#define WDA_CLIENT_IO -100
#define WDA_CLIENT_TIMEOUT -101
#define WDA_CLIENT_BAD_REPLY -102
#define WDC_STORAGE_UNCERTAIN -7
struct wdc_client;
struct wdc_tone_state {unsigned int selector;struct sony_tone_settings tone;};
/* Descriptors transferred on successful attach, not on failure. A single
 * full-duplex descriptor may be supplied for both and is closed once.
 * Serializes calls; timeout/invalid reply poisons channel, no automatic retry.
 * Join all users before destroy. No device/process selection or hardware ACK. */
int wdc_client_attach(struct wdc_client **,int tx,int rx,int timeout_ms);
/* Explicit WDS3 output endpoint; incompatible with WDC2 PCM-return workers.
 * Use open_stream/close_stream plus output methods; native ACK is not playback. */
int wdc_client_attach_output(struct wdc_client **,int tx,int rx,int timeout_ms);
/* WDS3 closed-session handshake/shutdown only. Never opens DSP or output. */
int wdc_client_output_ready(struct wdc_client *);
int wdc_client_shutdown_output(struct wdc_client *);
int wdc_client_open_output(struct wdc_client *,const struct sony_chain_settings *);
/* Explicit input rate for WDS3; the source quantum follows the rate band in
 * sony_audio_pcm_format.h. Backend support is determined by actual operations. */
int wdc_client_open_output_rate(struct wdc_client *,const struct sony_chain_settings *,unsigned int input_rate);
int wdc_client_process_output(struct wdc_client *,const int *,unsigned int frames);
int wdc_client_finish_output(struct wdc_client *,unsigned int padding_ms);
int wdc_client_get_output_state(struct wdc_client *,struct sony_stream_state *);
/* Negotiated ALSA geometry only; never interprets submitted frames as played. */
int wdc_client_get_output_timing(struct wdc_client *,struct sony_output_timing *);
int wdc_client_open_stream(struct wdc_client *,const struct sony_chain_settings *);
int wdc_client_update(struct wdc_client *,const struct sony_chain_settings *);
/* Return the controls supplied by the current worker, without interpreting
 * their values. This is not DSP settling or hardware readback. Output remains
 * unchanged on communication failure. */
int wdc_client_get_settings(struct wdc_client *,struct sony_chain_settings *);
int wdc_client_update_tone(struct wdc_client *,const struct sony_tone_settings *);
int wdc_client_update_output(struct wdc_client *,const struct sony_output_controls *);
int wdc_client_get_output_controls(struct wdc_client *,struct sony_output_controls *);
int wdc_client_update_dsee(struct wdc_client *,const struct sony_dsee_controls *);
int wdc_client_get_dsee(struct wdc_client *,struct sony_dsee_controls *);
int wdc_client_update_normalizer(struct wdc_client *,int enabled);
int wdc_client_get_normalizer(struct wdc_client *,int *enabled);
int wdc_client_update_volume_memory(struct wdc_client *,const struct sony_volume_memory *);
int wdc_client_get_volume_memory(struct wdc_client *,struct sony_volume_memory *);
/* Accepted selector/tone in one revision. Output unchanged on failure. */
int wdc_client_get_tone(struct wdc_client *,struct wdc_tone_state *);
/* Explicit save on snapshot workers. 0 acknowledges complete file publication,
 * not hardware DSP settling. -6 storage failure; -7 durability uncertain.
 * No retry. A timeout/bad reply can leave outcome unknown and poisons channel.
 */
int wdc_client_save(struct wdc_client *);
/* Opt-in WDS3 preset operation. The legacy snapshot parameter is retained for
 * wire compatibility; the current snapshot is used. Does not stage DSP controls. */
int wdc_client_save_preset(struct wdc_client *,unsigned int operation,unsigned int slot,const void *expected_snapshot);
int wdc_client_process(struct wdc_client *,const int *,int *,unsigned int frames);
/* 512-frame/48k input, variable 48k/192k output; capacity 1..10240 frames.
 * Insufficient capacity faults the worker stream after consumption; no retry.
 * Output/metadata unchanged on all errors. Existing process remains fixed. */
int wdc_client_process_expanded(struct wdc_client *,const int *,int *,unsigned int input_frames,
    unsigned int output_capacity_frames,unsigned int *output_frames,unsigned int *output_rate);
int wdc_client_query(struct wdc_client *,const char *,char *,unsigned int capacity);
/* Native 100/250ms padding, actual 48k/192k metadata. Capacity <=40960 frames.
 * Padding delivery only; caller sends EOS and separately drains output backend. */
int wdc_client_tail_expanded(struct wdc_client *,unsigned int ms,int *,unsigned int capacity_frames,
    unsigned int *output_frames,unsigned int *output_rate);
int wdc_client_tail(struct wdc_client *,unsigned int ms,int *,unsigned int capacity_frames,unsigned int *frames);
int wdc_client_eos(struct wdc_client *);
int wdc_client_close_stream(struct wdc_client *);
int wdc_client_failed(struct wdc_client *);
void wdc_client_destroy(struct wdc_client *);
#endif
