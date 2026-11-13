#include "sony_audio_bt_middleware_format.h"
#include <errno.h>
int sony_bt_middleware_format(const struct sony_bt_middleware_sound *s,struct sony_bt_middleware_format *out){
    if(!s||!out)return -EINVAL;
    struct sony_bt_middleware_format f={0};f.pcm.rate=s->rate;f.pcm.channel=s->channel==2?2:1;f.service_state=s->state<3?s->state+1:1;f.native_flag=s->native_flag;
    switch(s->codec){case 0:f.pcm.codec=SONY_BT_AV_NATIVE_PCM;f.service_codec=1;break;case 1:f.pcm.codec=SONY_BT_AV_SBC;f.service_codec=2;break;case 2:f.pcm.codec=SONY_BT_AV_AAC;f.service_codec=3;break;case 5:f.pcm.codec=SONY_BT_AV_LDAC;f.service_codec=4;break;default:return -ENOTSUP;}
    *out=f;return 0;
}
