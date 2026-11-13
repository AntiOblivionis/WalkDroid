#ifndef WALKDROID_SONY_PRESET_CONTROLS_H
#define WALKDROID_SONY_PRESET_CONTROLS_H
#include "sony_audio_controls.h"
#include "sony_audio_snapshot_store.h"
#include "sony_audio_sound_presets.h"
/* Local controller state only, never a persisted structure or wire ABI.
 * Retain unchanged from admission through native settling and one commit. */
struct sony_sound_preset_plan {
    unsigned int operation,slot;
    unsigned char before[SONY_SNAPSHOT_BYTES],after[SONY_SNAPSHOT_BYTES];
};
int sony_controls_prepare_preset(const char *request,unsigned int revision,
    const struct sony_controls *base,struct sony_controls *target,struct sony_sound_preset_plan *);
/* Decode just original preset-effect controls; preserve independent volume,
 * output/gain, AVLS and CFW context supplied by the caller. */
int sony_controls_decode_effects(const void *snapshot,struct sony_controls *);
int sony_controls_commit_preset(struct wdc_client *,const struct sony_controls *,const struct sony_sound_preset_plan *);
#endif
