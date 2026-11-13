#ifndef WALKDROID_SONY_EFFECT_BRIDGE_H
#define WALKDROID_SONY_EFFECT_BRIDGE_H
#define SONY_EFFECT_DC_PHASE 1
#define SONY_EFFECT_VINYL 2
struct sony_effect_bridge;
struct sony_effect_settings {
    int enabled, mode;
    int output_device, primary_volume, secondary_volume, se_gain_mode, btl_gain_mode;
};
struct sony_effect_controls { int enabled, processing, mode; };
/* Original Sony ARM/glibc node adapter. 48k/stereo float32/512 frames initially.
 * Host verification of a node is not full-chain or hardware-route acceptance.
 * Caller supplies audited original library and working configuration transport.
 * Calls must be serialized. Failure requires caller to stop/silence the stream.
 */
int sony_effect_open(struct sony_effect_bridge **, void *library, int effect,
                     const struct sony_effect_settings *);
int sony_effect_update(struct sony_effect_bridge *, const struct sony_effect_settings *);
int sony_effect_get_controls(struct sony_effect_bridge *, struct sony_effect_controls *);
int sony_effect_process(struct sony_effect_bridge *, const float *, float *, unsigned int frames);
int sony_effect_close(struct sony_effect_bridge *);
#endif
