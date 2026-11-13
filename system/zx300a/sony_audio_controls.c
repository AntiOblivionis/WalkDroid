#include "sony_audio_controls.h"
#include "sony_audio_snapshot_store.h"
#include "sony_audio_snapshot_eq.h"
#include "sony_audio_snapshot_tone.h"
#include "sony_audio_volume_policy.h"
#ifdef SONY_SOUND_PRESETS
#include "sony_audio_preset_controls.h"
#endif
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
extern int __system_property_get(const char *,char *);
static struct sony_volume_state volume_state(const struct sony_controls *c) {
    struct sony_volume_state s={c->ship_selector,c->settings.volume,c->output.device,c->volume_memory.last_headphone,
        0,0,c->output.se_gain,c->output.btl_gain,c->volume_memory.avls,c->avls_threshold};
    return s;
}
static void accept_volume_plan(struct sony_controls *c,const struct sony_volume_plan *p) {
    c->settings.volume=p->next.volume;c->volume_memory.avls=p->next.avls;
    c->volume_memory.last_headphone=p->next.last_headphone;c->avls_threshold=p->next.avls_threshold;
}
#ifdef SONY_SOUND_PRESETS
int sony_controls_decode_effects(const void *snapshot,struct sony_controls *out){
    if(!snapshot||!out)return -EINVAL;
    struct sony_controls c=*out;
    int rc=sony_snapshot_apply_dsee(snapshot,SONY_SNAPSHOT_BYTES,&c.dsee);if(rc)return rc;
    rc=sony_snapshot_apply_normalizer(snapshot,SONY_SNAPSHOT_BYTES,&c.normalizer);if(rc)return rc;
    for(unsigned int i=0;i<3;++i)c.tone.tone.centerfreq[i]=2;
    if(!sony_snapshot_decode_tone(snapshot,SONY_SNAPSHOT_BYTES,&c.tone.tone)){
        c.tone.selector=3;rc=sony_snapshot_apply_tone_base(snapshot,SONY_SNAPSHOT_BYTES,1,&c.settings);
    }else{
        c.tone.selector=2;rc=sony_snapshot_apply_supported_effects(snapshot,SONY_SNAPSHOT_BYTES,1,&c.settings);
        if(!rc)rc=sony_snapshot_decode_tone_values(snapshot,SONY_SNAPSHOT_BYTES,&c.tone.tone);
    }
    if(!rc)*out=c;
    return rc;
}
#endif
int sony_controls_output_event(const struct sony_controls *base,int se,int btl,struct sony_controls *out) {
    if(!base||!out||(se!=0&&se!=1)||(btl!=0&&btl!=1))return -EINVAL;
    struct sony_volume_state s=volume_state(base);struct sony_volume_plan p;
    s.se_connected=se;s.btl_connected=btl;s.output=btl?SONY_OUTPUT_BTL:SONY_OUTPUT_SE;
    int rc=sony_volume_plan_reconfigure(&s,SONY_VOLUME_OUTPUT,&p);if(rc)return rc;
    struct sony_controls c=*base;c.output.device=s.output;accept_volume_plan(&c,&p);*out=c;return 0;
}
int sony_controls_load(struct sony_controls *out) {
    if(!out)return -EINVAL;
    unsigned char snapshot[SONY_SNAPSHOT_BYTES];int rc=sony_snapshot_read(SONY_AUDIO_CONFIG,snapshot);if(rc)return rc;
    struct sony_controls c={0};
    char selector[128]={0},*end=NULL;__system_property_get("ro.walkdroid.sony_ship",selector);
    errno=0;unsigned long ship=strtoul(selector,&end,10);
    if(errno||end==selector||*end||ship>UINT_MAX)ship=0;
    c.ship_selector=(unsigned int)ship;
    rc=sony_cfw_profile_read(SONY_AUDIO_CONFIG,&c.cfw_profile);if(rc)return rc;
    rc=sony_cfw_signature_read(SONY_AUDIO_CONFIG,&c.cfw_signature);if(rc)return rc;
    rc=sony_cfw_gain_read(SONY_AUDIO_CONFIG,&c.cfw_gain);if(rc)return rc;
    char applied[128]={0};__system_property_get("sys.walkdroid.cfw_gain",applied);
    c.cfw_gain_applied=applied[0]>='0'&&applied[0]<='2'&&!applied[1]?applied[0]-'0':-1;
    rc=sony_snapshot_apply_volume(snapshot,sizeof(snapshot),&c.settings);if(rc)return rc;
    rc=sony_snapshot_apply_output(snapshot,sizeof(snapshot),&c.output);if(rc)return rc;
    rc=sony_snapshot_apply_volume_memory(snapshot,sizeof(snapshot),&c.volume_memory);if(rc)return rc;
    /* Original Init seeds AVLS from lastHeadphone, not the instantaneous jack. */
    c.avls_threshold=sony_volume_initial(c.ship_selector,c.volume_memory.last_headphone==1?1:2,
        c.volume_memory.last_headphone==1?c.output.btl_gain:c.output.se_gain);
    rc=sony_snapshot_apply_dsee(snapshot,sizeof(snapshot),&c.dsee);if(rc)return rc;
    rc=sony_snapshot_apply_normalizer(snapshot,sizeof(snapshot),&c.normalizer);if(rc)return rc;
    for(unsigned int i=0;i<3;++i)c.tone.tone.centerfreq[i]=2;
    if(!sony_snapshot_decode_tone(snapshot,sizeof(snapshot),&c.tone.tone)) {
        c.tone.selector=3;rc=sony_snapshot_apply_tone_base(snapshot,sizeof(snapshot),1,&c.settings);
    }else {c.tone.selector=2;rc=sony_snapshot_apply_supported_effects(snapshot,sizeof(snapshot),1,&c.settings);
        if(!rc)rc=sony_snapshot_decode_tone_values(snapshot,sizeof(snapshot),&c.tone.tone);}
    if(!rc)*out=c;
    return rc;
}
static int number(const char *text,int *out) {
    if(!text||!*text||(*text!='-'&&(*text<'0'||*text>'9')))return -EINVAL;
    errno=0;char *end;long n=strtol(text,&end,10);
    if(errno||*end||n<INT_MIN||n>INT_MAX)return -EINVAL;
    *out=(int)n;return 0;
}
int sony_controls_parse(const char *text,unsigned int revision,const struct sony_controls *base,struct sony_controls *out) {
    (void)revision;
    if(!text||!base||!out||strlen(text)>=1024)return -EINVAL;
    struct sony_controls c=*base;char copy[1024];strcpy(copy,text);char *save=NULL;uint64_t seen=0;
    for(char *p=strtok_r(copy,";",&save);p;p=strtok_r(NULL,";",&save)) {
        char *v=strchr(p,'=');if(!v)return -EINVAL;*v++=0;int n;if(number(v,&n))return -EINVAL;
        unsigned int bit;int *dest=NULL,lo=0,hi=1;
        if(!strcmp(p,"sony_revision")){bit=0;}
        else if(!strcmp(p,"sony_direct")){bit=1;dest=&c.settings.direct;}
        else if(!strcmp(p,"sony_eq")){bit=2;dest=&c.settings.eq_enabled;}
        else if(!strcmp(p,"sony_dc")){bit=3;dest=&c.settings.dc_enabled;}
        else if(!strcmp(p,"sony_dc_mode")){bit=4;dest=&c.settings.dc_mode;hi=7;}
        else if(!strcmp(p,"sony_vinyl")){bit=5;dest=&c.settings.vinyl_enabled;}
        else if(!strcmp(p,"sony_vinyl_mode")){bit=6;dest=&c.settings.vinyl_mode;hi=7;}
        else if(!strcmp(p,"sony_selector")){bit=7;lo=2;hi=3;if(n<lo||n>hi)return -EINVAL;c.tone.selector=(unsigned int)n;}
        else if(!strcmp(p,"sony_tone")){bit=8;dest=&c.tone.tone.enabled;}
        else if(!strcmp(p,"sony_low")){bit=9;dest=&c.tone.tone.gains[0];lo=-20;hi=20;}
        else if(!strcmp(p,"sony_mid")){bit=10;dest=&c.tone.tone.gains[1];lo=-20;hi=20;}
        else if(!strcmp(p,"sony_high")){bit=11;dest=&c.tone.tone.gains[2];lo=-20;hi=20;}
        else if(!strcmp(p,"sony_volume")){bit=22;dest=&c.settings.volume;hi=120;}
        else if(!strcmp(p,"sony_se_gain")){bit=23;dest=&c.output.se_gain;}
        else if(!strcmp(p,"sony_btl_gain")){bit=31;dest=&c.output.btl_gain;}
        else if(!strcmp(p,"sony_balance_l")){bit=24;dest=&c.output.balance_l;hi=88;}
        else if(!strcmp(p,"sony_balance_r")){bit=25;dest=&c.output.balance_r;hi=88;}
        else if(!strcmp(p,"sony_dsee")){bit=26;dest=&c.dsee.enabled;}
        else if(!strcmp(p,"sony_dsee_mode")){bit=27;dest=&c.dsee.mode;lo=1;hi=5;}
        else if(!strcmp(p,"sony_cfw_profile")){bit=28;dest=&c.cfw_profile;hi=2;}
        else if(!strcmp(p,"sony_cfw_signature")){bit=33;dest=&c.cfw_signature;lo=-1;hi=3;}
        else if(!strcmp(p,"sony_cfw_gain")){bit=29;dest=&c.cfw_gain;hi=2;}
        else if(!strcmp(p,"sony_avls")){bit=30;dest=&c.volume_memory.avls;}
        else if(!strcmp(p,"sony_normalizer")){bit=32;dest=&c.normalizer;}
        else if(!strncmp(p,"sony_band",9)&&p[9]>='0'&&p[9]<='9'&&!p[10]){bit=12+(unsigned int)(p[9]-'0');dest=&c.settings.bands[p[9]-'0'];lo=-20;hi=20;}
        else return -ENOSYS;
        if(seen&(1ull<<bit))return -EINVAL;
        seen|=1ull<<bit;if(dest){if(n<lo||n>hi)return -EINVAL;*dest=n;}
    }
    if(!(seen&~1ull))return -EINVAL;
    /* A profile is a different output-library instance. Do not combine its
     * separate durable commit with native audio-snapshot edits. */
    if((seen&(1u<<28))&&(seen&~(1ull|(1ull<<28))))return -EINVAL;
    if((seen&(1u<<29))&&(seen&~(1ull|(1ull<<29))))return -EINVAL;
    if((seen&(1ull<<33))&&(seen&~(1ull|(1ull<<33))))return -EINVAL;
    /* An AVLS toggle has its own native ordering. Do not invent a combined
     * multi-setting operation with ambiguous enable/volume/gain sequencing. */
    if((seen&(1u<<30))&&(seen&~(1ull|(1ull<<30))))return -EINVAL;
    /* Do not invent ordering for two independent native gain events. */
    if((seen&(1u<<23))&&(seen&(1u<<31)))return -EINVAL;
    /* The worker persists tone fields only with the tone selector. Never ACK
     * an inactive bank edit which would disappear on the next reload. */
    if((seen&(15u<<8))&&c.tone.selector!=3)return -EINVAL;
    struct sony_volume_plan plan;struct sony_volume_state state;
    if(seen&(1u<<30)) {
        state=volume_state(base);int rc=sony_volume_plan_avls(&state,c.volume_memory.avls,&plan);if(rc)return rc;
        accept_volume_plan(&c,&plan);
    }else {
        if(seen&(1u<<22)) {
            state=volume_state(base);int rc=sony_volume_plan_set(&state,c.settings.volume,&plan);if(rc)return rc;
            c.settings.volume=plan.next.volume;
        }
        if(c.output.se_gain!=base->output.se_gain) {
            state=volume_state(&c);int rc=sony_volume_plan_reconfigure(&state,SONY_VOLUME_SE_GAIN,&plan);if(rc)return rc;
            accept_volume_plan(&c,&plan);
        }
        if(c.output.btl_gain!=base->output.btl_gain) {
            state=volume_state(&c);int rc=sony_volume_plan_reconfigure(&state,SONY_VOLUME_BTL_GAIN,&plan);if(rc)return rc;
            accept_volume_plan(&c,&plan);
        }
    }
    *out=c;return 0;
}
char *sony_controls_text(const struct sony_controls *c,unsigned int revision,int failed,int active) {
    char text[1024];int at=snprintf(text,sizeof(text),"sony_revision=%u;sony_failed=%d;sony_active=%d;sony_direct=%d;sony_eq=%d;sony_dc=%d;sony_dc_mode=%d;sony_vinyl=%d;sony_vinyl_mode=%d;sony_selector=%u;sony_tone=%d;sony_low=%d;sony_mid=%d;sony_high=%d",
        revision,failed,active,c->settings.direct,c->settings.eq_enabled,c->settings.dc_enabled,c->settings.dc_mode,c->settings.vinyl_enabled,c->settings.vinyl_mode,
        c->tone.selector,c->tone.tone.enabled,c->tone.tone.gains[0],c->tone.tone.gains[1],c->tone.tone.gains[2]);
    for(unsigned int i=0;i<10;++i)at+=snprintf(text+at,sizeof(text)-(unsigned int)at,";sony_band%u=%d",i,c->settings.bands[i]);
    at+=snprintf(text+at,sizeof(text)-(unsigned int)at,";sony_volume=%d",c->settings.volume);
    at+=snprintf(text+at,sizeof(text)-(unsigned int)at,";sony_cfw_signature=%d",c->cfw_signature);
    at+=snprintf(text+at,sizeof(text)-(unsigned int)at,";sony_se_gain=%d;sony_btl_gain=%d;sony_balance_l=%d;sony_balance_r=%d;sony_output=%d",
        c->output.se_gain,c->output.btl_gain,c->output.balance_l,c->output.balance_r,c->output.device);
    at+=snprintf(text+at,sizeof(text)-(unsigned int)at,";sony_dsee=%d;sony_dsee_mode=%d;sony_cfw_profile=%d;sony_cfw_gain=%d;sony_cfw_gain_applied=%d",c->dsee.enabled,c->dsee.mode,c->cfw_profile,c->cfw_gain,c->cfw_gain_applied);
    snprintf(text+at,sizeof(text)-(unsigned int)at,";sony_avls=%d;sony_avls_limit=%d;sony_last_headphone=%d;sony_normalizer=%d",c->volume_memory.avls,c->avls_threshold,c->volume_memory.last_headphone,c->normalizer);
#ifdef SONY_SOUND_PRESETS
    at=(int)strlen(text);snprintf(text+at,sizeof(text)-(unsigned int)at,";sony_preset_slots=3");
#endif
    return strdup(text);
}
#define CONTROL_CALL(expr) do { int code=(expr);if(code){fprintf(stderr,"SONY_CONTROL_FAILURE stage=%s rc=%d\n",#expr,code);return code;} } while(0)
int sony_controls_stage(struct wdc_client *client,const struct sony_controls *c) {
    CONTROL_CALL(wdc_client_update_output(client,&c->output));
    CONTROL_CALL(wdc_client_update(client,&c->settings));
    if(c->tone.selector==3)CONTROL_CALL(wdc_client_update_tone(client,&c->tone.tone));
    CONTROL_CALL(wdc_client_update_dsee(client,&c->dsee));
    CONTROL_CALL(wdc_client_update_normalizer(client,c->normalizer));
    CONTROL_CALL(wdc_client_update_volume_memory(client,&c->volume_memory));
    return 0;
}
static int commit_controls(struct wdc_client *client,const struct sony_controls *c
#ifdef SONY_SOUND_PRESETS
    ,const struct sony_sound_preset_plan *preset
#endif
    ) {
    (void)c;
#ifdef SONY_SOUND_PRESETS
    if(preset){
        CONTROL_CALL(wdc_client_save_preset(client,preset->operation,preset->slot,preset->before));
    }else
#endif
    CONTROL_CALL(wdc_client_save(client));
    return 0;
}
int sony_controls_commit(struct wdc_client *client,const struct sony_controls *c){
    return commit_controls(client,c
#ifdef SONY_SOUND_PRESETS
        ,NULL
#endif
    );
}
#ifdef SONY_SOUND_PRESETS
int sony_controls_commit_preset(struct wdc_client *client,const struct sony_controls *c,const struct sony_sound_preset_plan *preset){
    if(!preset||preset->slot<1||preset->slot>3||
       (preset->operation!=SONY_SOUND_PRESET_SAVE&&preset->operation!=SONY_SOUND_PRESET_LOAD))return -EINVAL;
    return commit_controls(client,c,preset);
}
#endif
int sony_controls_apply(struct wdc_client *client,const struct sony_controls *c) {
    int rc=sony_controls_stage(client,c);return rc?rc:sony_controls_commit(client,c);
}

int sony_controls_feature_ship(const struct sony_controls *c){
    static const unsigned words[]={0,1,2,0x103,4,6,0x306,0x203};
    if(!c)return -1;for(unsigned i=0;i<8;++i)if(c->ship_selector==words[i])return (int)i;return -1;
}
