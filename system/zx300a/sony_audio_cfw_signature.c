#include "sony_audio_cfw_signature.h"
#include <limits.h>
int sony_cfw_signature_context(unsigned int signature,unsigned int ship,
    struct sony_audio_feature_context *out){
    if(!out)return -1;
    /* Unknown signatures have no invented model: ask the real provider. */
    unsigned int model=signature>SONY_CFW_WM1Z?UINT_MAX:(signature==SONY_CFW_BRIGHT?7u:1u);
    struct sony_audio_feature_context context={model,ship};
    *out=context;return 0;
}
