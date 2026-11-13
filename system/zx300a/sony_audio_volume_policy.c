#include "sony_audio_volume_policy.h"
#include <errno.h>

int sony_volume_initial(unsigned int ship,int output,int gain) {
    if((output!=1&&output!=2)||(gain!=0&&gain!=1))return -EINVAL;
    if(ship==0x103||ship==0x203)return 75;
    return gain?(output==1?46:58):68;
}
static int boolean(int v) {return v==0||v==1;}
static int valid(const struct sony_volume_state *s) {
    return s&&sony_volume_initial(s->ship_selector,2,0)>=0&&s->volume>=0&&s->volume<=120&&
        s->output>=0&&s->output<=6&&(s->last_headphone==1||s->last_headphone==2||s->last_headphone==6)&&
        boolean(s->se_connected)&&boolean(s->btl_connected)&&boolean(s->se_gain)&&boolean(s->btl_gain)&&
        boolean(s->avls)&&s->avls_threshold>=0&&s->avls_threshold<=120;
}
static void request(struct sony_volume_plan *p,int value) {
    p->set_volume=1;p->requested_volume=value;
    /* Original SetVolume uses the CURRENT AVLS threshold, before a subsequent
     * ReconfigureVolumes updates it. Output 3 is exempt in VolumeAdlerOut. */
    p->next.volume=p->next.avls&&p->next.output!=3&&value>p->next.avls_threshold?p->next.avls_threshold:value;
}
int sony_volume_plan_set(const struct sony_volume_state *s,int volume,struct sony_volume_plan *out) {
    if(!valid(s)||!out||volume<0||volume>120)return -EINVAL;
    struct sony_volume_plan p={.next=*s};request(&p,volume);*out=p;return 0;
}
int sony_volume_plan_avls(const struct sony_volume_state *s,int enabled,struct sony_volume_plan *out) {
    if(!valid(s)||!out||!boolean(enabled))return -EINVAL;
    struct sony_volume_plan p={.next=*s};
    if(enabled!=s->avls) {
        p.next.avls=enabled;
        if(enabled&&s->output!=3&&s->volume>s->avls_threshold)request(&p,s->avls_threshold);
    }
    *out=p;return 0;
}
int sony_volume_plan_reconfigure(const struct sony_volume_state *s,int cmd,struct sony_volume_plan *out) {
    if(!valid(s)||!out||cmd<SONY_VOLUME_OUTPUT||cmd>SONY_VOLUME_BTL_GAIN)return -EINVAL;
    struct sony_volume_plan p={.next=*s};int target=0;
    if(cmd==SONY_VOLUME_OUTPUT&&(s->output==1||s->output==2)&&
        (s->output==1?s->btl_connected:s->se_connected)&&s->last_headphone!=s->output)target=s->output;
    else if(cmd==SONY_VOLUME_SE_PLUG&&s->se_connected&&s->output==2&&s->last_headphone!=2)target=2;
    else if(cmd==SONY_VOLUME_SE_GAIN&&s->output!=1&&s->last_headphone!=1)target=2;
    else if(cmd==SONY_VOLUME_BTL_GAIN&&(s->output==1||s->last_headphone==1))target=1;
    if(target) {
        int limit=sony_volume_initial(s->ship_selector,target,target==1?s->btl_gain:s->se_gain);
        p.adjust_target=target;
        if(s->volume>limit)request(&p,limit);
        /* The independently executed DmpFeature AVLS tables have the same
         * numeric values for this exact model/region set; not a general rule. */
        p.next.avls_threshold=limit;p.update_avls=1;
        if(cmd==SONY_VOLUME_OUTPUT||cmd==SONY_VOLUME_SE_PLUG)p.next.last_headphone=target;
    }
    *out=p;return 0;
}
