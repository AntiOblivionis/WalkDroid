#ifndef WALKDROID_SONY_DSF_NATIVE_PLAYBACK_H
#define WALKDROID_SONY_DSF_NATIVE_PLAYBACK_H
#include "sony_audio_dsf_client.h"
#include "sony_audio_dsd_sink_client.h"
#include "sony_audio_dsf_source.h"
struct sony_dsf_native_playback;
struct sony_dsf_native_playback_owner {
 int (*check)(void *);
 /* Called after native PCM2 negotiation, before first sample. External route
  * must verify actual geometry/jack and apply original DAC/volume semantics. */
 int (*started)(void *,const struct wdn_state *);
 /* Mute/stop admission once; MUST retain cleanup generation and WKP7 authority
  * until native Close/resource restoration and exact child reaps are proven.
  * This callback does not retire/release the global owner. */
 int (*quiesce)(void *);
 void *context;
};
struct sony_dsf_native_playback_state {
 uint64_t source_bytes,dsd_acked_bytes;
 unsigned bit_rate,source_finished,drain_accepted,failed,cancel_requested;
 int first_error,dsf_status,sink_status,quiesce_status;
};
struct sony_dsf_native_playback_cleanup {
 int dsf_close,sink_close,source_close,quiesce;
 int output_cleanup_unverified; /* Always 1: supervisor must prove actual idle. */
};
/* Inactive integration API, no spawning/mounts/device selection or auto-route.
 * Idle borrowed verified WDF4/WDN1 workers and exclusive generation required.
 * Native DSD mode1 / BTL only. SE stays original conversion; no fallback.
 * Leased source -> original Sony demux/fade/preroll/DSP/EOF -> native bytes.
 * No resampling, byte permutation, software gain or added padding here.
 * Every non-NULL result requires close even after failed start.
 * Native controls remain immutable for this lifetime, matching WDF native mode.
 */
int sony_dsf_native_playback_start(struct sony_dsf_native_playback **,struct sony_dsf_client *,struct sony_dsd_sink_client *,
 int directory,const char *basename,uint64_t budget,const struct sony_chain_settings *,const struct sony_dsd_controls *,
 const struct sony_output_controls *,unsigned latency,const struct sony_dsf_native_playback_owner *);
/* 0 progress, 1 original terminal plus native Drain ACK. Neither is acoustic or
 * complete physical consumption proof. Error seals; never replay samples. */
int sony_dsf_native_playback_step(struct sony_dsf_native_playback *);
int sony_dsf_native_playback_state(struct sony_dsf_native_playback *,struct sony_dsf_native_playback_state *);
void sony_dsf_native_playback_cancel(struct sony_dsf_native_playback *);
/* Same serialized controller as start/step; cancel alone is concurrent-safe.
 * Close commands once, release source lease; uncertain transport/hardware must
 * keep the external recovery barrier, even if this local object is freed. */
int sony_dsf_native_playback_close(struct sony_dsf_native_playback **,struct sony_dsf_native_playback_cleanup *);
#endif
