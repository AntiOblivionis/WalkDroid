#ifndef WALKDROID_SONY_CFW_SIGNATURE_H
#define WALKDROID_SONY_CFW_SIGNATURE_H
#include "sony_audio_feature_config.h"
/* Software sound-profile selection only, not device identity or a complete CFW
 * sound preset. Unknown values defer the model lookup to the original provider;
 * no EQ coefficients or non-audio conf_a/b/c settings are invented. External
 * LK/NVRAM effects and physical equivalence remain unproven. */
enum sony_cfw_signature {
    SONY_CFW_NEUTRAL=0, SONY_CFW_WARM=1,
    SONY_CFW_BRIGHT=2, SONY_CFW_WM1Z=3
};
int sony_cfw_signature_context(unsigned int signature,unsigned int ship,
    struct sony_audio_feature_context *out);
#endif
