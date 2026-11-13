#include "sony_audio_player_route_policy.h"
#include <errno.h>
int sony_player_route_select(const struct sony_dsd_controls *saved,unsigned se,unsigned btl,
    struct sony_dsd_controls *session,unsigned *expected,int *native){
    if(!sony_dsd_controls_valid(saved)||se>1||btl>1||!session||!expected||!native)return -EINVAL;
    if(!se&&!btl)return -ENXIO;
    struct sony_dsd_controls selected=*saved;int raw=btl&&saved->mode==1;
    if(!raw)selected.mode=2;
    *session=selected;*expected=btl?SONY_OUTPUT_BTL:SONY_OUTPUT_SE;*native=raw;return 0;
}
