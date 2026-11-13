#ifndef WALKDROID_SONY_CONTROLS_H
#define WALKDROID_SONY_CONTROLS_H
#include "sony_audio_chain_client.h"
#include "sony_audio_cfw_profile.h"
#define SONY_AUDIO_CONFIG "/vendor/sony-audio/rootfs/data/config"
struct sony_controls {
    struct sony_chain_settings settings;
    struct wdc_tone_state tone;
    struct sony_output_controls output;
    struct sony_dsee_controls dsee;
    struct sony_volume_memory volume_memory;
    int avls_threshold;
    unsigned int ship_selector;
    int cfw_profile;
    int cfw_signature;
    int cfw_gain,cfw_gain_applied;
    int normalizer;
};
int sony_controls_load(struct sony_controls *);
int sony_controls_feature_ship(const struct sony_controls *);
/* The optional revision is informational; requested controls apply to the
 * currently loaded state rather than being rejected for a stale revision. */
int sony_controls_parse(const char *,unsigned int,const struct sony_controls *,struct sony_controls *);
/* Exact physical output event. Pure plan only; caller owns commit/hardware. */
int sony_controls_output_event(const struct sony_controls *,int se,int btl,struct sony_controls *);
char *sony_controls_text(const struct sony_controls *,unsigned int,int failed,int active);
/* Forward original setters and request an explicit save. Caller serializes on
 * the native owner and manages the active route. */
int sony_controls_apply(struct wdc_client *,const struct sony_controls *);
/* Playing filters defer commands until real PCM crosses the native chain.
 * Stage once, then commit after a subsequent real packet; never insert audio. */
int sony_controls_stage(struct wdc_client *,const struct sony_controls *);
int sony_controls_commit(struct wdc_client *,const struct sony_controls *);
#endif
