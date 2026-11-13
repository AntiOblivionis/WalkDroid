#ifndef WALKDROID_SONY_SNAPSHOT_EQ_H
#define WALKDROID_SONY_SNAPSHOT_EQ_H
#include "sony_audio_chain_bridge.h"
#include "sony_audio_volume_memory.h"
/* Partial adapter: only EQ enable and ten bands. Other settings unchanged.
 * Caller pins the audited Sony config layout; not a full profile decoder.
 * Requires a complete 28672-byte snapshot, signed gains within bridge range.
 */
int sony_snapshot_apply_eq(const void *,unsigned int,struct sony_chain_settings *);
/* SourceDirect feature gate must be supplied from verified model capabilities.
 * 0 disables direct regardless of stored value, matching FilterInConfig::Load.
 */
int sony_snapshot_apply_eq_direct(const void *,unsigned int,int source_direct_feature,struct sony_chain_settings *);
/* Adds raw DC phase and vinyl modes supported by the current bridge (0..7).
 * Requires stored EQ_USE=2; EqTone profiles need a future expanded interface.
 * Raw persisted modes only: does not assign Sony GUI labels or presets.
 */
int sony_snapshot_apply_supported_effects(const void *,unsigned int,int source_direct_feature,struct sony_chain_settings *);
/* Only for callers that also decode/apply tone controls before processing PCM. */
int sony_snapshot_apply_tone_base(const void *,unsigned int,int,struct sony_chain_settings *);
/* Encode only supported effect fields plus EQ_USE=2, preserve others and CRC.
 * volume is deliberately not persisted. Output unchanged on validation error.
 * Direct field is preserved when the model does not support SourceDirect.
 */
int sony_snapshot_encode_supported_effects(const void *,unsigned int,int,const struct sony_chain_settings *,void *output);
/* Explicit WDS3 output control; callers must use a migrated private store.
 * Never load historical VOL_MASTERVOL=120 as a new hardware default. */
int sony_snapshot_apply_volume(const void *,unsigned int,struct sony_chain_settings *);
int sony_snapshot_encode_volume(const void *,unsigned int,int volume,void *output);
int sony_snapshot_apply_output(const void *,unsigned int,struct sony_output_controls *);
int sony_snapshot_encode_output(const void *,unsigned int,const struct sony_output_controls *,void *output);
int sony_snapshot_apply_dsee(const void *,unsigned int,struct sony_dsee_controls *);
int sony_snapshot_encode_dsee(const void *,unsigned int,const struct sony_dsee_controls *,void *output);
/* Original SS_DN_ONOFF (key16), separate from Android audio effects. */
int sony_snapshot_apply_normalizer(const void *,unsigned int,int *enabled);
int sony_snapshot_encode_normalizer(const void *,unsigned int,int enabled,void *output);
int sony_snapshot_apply_volume_memory(const void *,unsigned int,struct sony_volume_memory *);
int sony_snapshot_encode_volume_memory(const void *,unsigned int,const struct sony_volume_memory *,void *output);
#endif
