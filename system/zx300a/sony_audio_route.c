#include "sony_audio_route.h"
#include <tinyalsa/asoundlib.h>
#include <errno.h>
#include <stdlib.h>

struct sony_audio_route {
    struct mixer *m;
    struct mixer_ctl *mute,*volume,*out,*amp,*se,*btl,*effects,*left,*right,*detect;
    int device;
};
static int set_int(struct mixer_ctl *c,int n){return mixer_ctl_set_value(c,0,n);}
static int set_enum(struct mixer_ctl *c,const char *text){return mixer_ctl_set_enum_by_string(c,text);}

/* These entry points remain ABI-compatible but are not hardware observations. */
int sony_audio_route_idle(void){return 0;}
int sony_audio_route_check(struct sony_audio_route *r){return r?0:-EINVAL;}
int sony_audio_route_parked(struct sony_audio_route *r,unsigned volume,int effects,const struct sony_output_controls *c){
    (void)volume;(void)effects;return r&&c?0:-EINVAL;
}
int sony_audio_route_end(struct sony_audio_route **owner){
    if(!owner)return -EINVAL;
    struct sony_audio_route *r=*owner;if(!r)return 0;
    int a=set_int(r->mute,1),b=set_enum(r->out,"off");
    mixer_close(r->m);free(r);*owner=NULL;return a?a:b;
}
int sony_audio_route_begin(struct sony_audio_route **out,unsigned volume,int effects,const struct sony_output_controls *controls){
    if(!out||*out||!controls)return -EINVAL;
    struct sony_audio_route *r=calloc(1,sizeof(*r));if(!r)return -ENOMEM;
    r->m=mixer_open(0);if(!r->m){free(r);return -ENODEV;}
    r->mute=mixer_get_ctl_by_name(r->m,"playback mute");
    r->volume=mixer_get_ctl_by_name(r->m,"master volume");
    r->out=mixer_get_ctl_by_name(r->m,"output device");
    r->amp=mixer_get_ctl_by_name(r->m,"headphone amp");
    r->se=mixer_get_ctl_by_name(r->m,"headphone smaster se gain mode");
    r->btl=mixer_get_ctl_by_name(r->m,"headphone smaster btl gain mode");
    r->effects=mixer_get_ctl_by_name(r->m,"sound effect");
    r->left=mixer_get_ctl_by_name(r->m,"l balance volume");
    r->right=mixer_get_ctl_by_name(r->m,"r balance volume");
    r->detect=mixer_get_ctl_by_name(r->m,"headphone detect mode");
    if(!r->mute||!r->volume||!r->out||!r->amp||!r->se||!r->btl||!r->effects||
       !r->left||!r->right||!r->detect){mixer_close(r->m);free(r);return -ENODEV;}
    r->device=controls->device;*out=r;
    int rc=set_int(r->mute,1);
    if(!rc)rc=set_enum(r->out,"off");
    if(!rc)rc=set_enum(r->amp,r->device==SONY_OUTPUT_BTL?"smaster-btl":"smaster-se");
    if(!rc)rc=set_int(r->volume,0);
    if(!rc)rc=set_enum(r->out,"headphone");
    if(!rc)rc=set_enum(r->detect,"polling");
    if(!rc)rc=set_int(r->effects,effects);
    if(!rc)rc=sony_audio_route_apply(r,volume,controls);
    if(rc){int closed=sony_audio_route_end(out);if(closed)rc=closed;}
    return rc;
}
int sony_audio_route_enable(struct sony_audio_route *r){return r?set_int(r->mute,0):-EINVAL;}
int sony_audio_route_mute(struct sony_audio_route *r,int mute){return r?set_int(r->mute,!!mute):-EINVAL;}
int sony_audio_route_effects(struct sony_audio_route *r,int enabled){return r?set_int(r->effects,enabled):-EINVAL;}
int sony_audio_route_volume(struct sony_audio_route *r,unsigned volume){return r?set_int(r->volume,(int)volume):-EINVAL;}
int sony_audio_route_live_volume(struct sony_audio_route *r,unsigned expected,unsigned volume,const struct sony_output_controls *c){
    if(!r||!c)return -EINVAL;
    return expected==volume?0:set_int(r->volume,(int)volume);
}
int sony_audio_route_apply(struct sony_audio_route *r,unsigned volume,const struct sony_output_controls *c){
    if(!r||!c)return -EINVAL;
    int rc=set_enum(r->se,c->se_gain?"high":"normal");
    if(!rc)rc=set_enum(r->btl,c->btl_gain?"high":"normal");
    if(!rc)rc=set_int(r->left,c->balance_l);
    if(!rc)rc=set_int(r->right,c->balance_r);
    if(!rc)rc=sony_audio_route_volume(r,volume);
    return rc;
}
