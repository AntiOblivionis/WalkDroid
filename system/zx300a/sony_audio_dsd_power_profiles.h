#ifndef SONY_AUDIO_DSD_POWER_PROFILES_H
#define SONY_AUDIO_DSD_POWER_PROFILES_H
#include "sony_audio_dsd_power_bridge.h"
/* Caller pins the original CFW binary selected for this exact object.
 * 0 Normal: native minimum1040000.
 * 1 Plus-v1 / 2 Plus-v2: native minimum1300000.
 * All three request mute80/80/(low?180:80), before PCM Open.
 * Base bind() retains its historical Plus-v2 profile semantics.
 */
int sony_dsd_power_bind_profile(struct sony_dsd_power_bridge **,void *,
    const struct sony_dsd_power_backend *,unsigned cfw_profile);
#endif
