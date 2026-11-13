#define _GNU_SOURCE
#include "sony_audio_settings_relay.h"
#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
struct field {const char *name;unsigned offset;int lo,hi;};
#define FIELD(n,p,l,h) {n,offsetof(struct sony_dsd_live_controls,p),l,h}
static const struct field fields[]={
    FIELD("sony_eq",settings.eq_enabled,0,1),
    FIELD("sony_band0",settings.bands[0],-20,20),FIELD("sony_band1",settings.bands[1],-20,20),
    FIELD("sony_band2",settings.bands[2],-20,20),FIELD("sony_band3",settings.bands[3],-20,20),
    FIELD("sony_band4",settings.bands[4],-20,20),FIELD("sony_band5",settings.bands[5],-20,20),
    FIELD("sony_band6",settings.bands[6],-20,20),FIELD("sony_band7",settings.bands[7],-20,20),
    FIELD("sony_band8",settings.bands[8],-20,20),FIELD("sony_band9",settings.bands[9],-20,20),
    FIELD("sony_selector",selector,2,3),FIELD("sony_tone",tone.enabled,0,1),
    FIELD("sony_low",tone.gains[0],-20,20),FIELD("sony_mid",tone.gains[1],-20,20),FIELD("sony_high",tone.gains[2],-20,20),
    FIELD("sony_dc",settings.dc_enabled,0,1),FIELD("sony_dc_mode",settings.dc_mode,1,6),
    FIELD("sony_vinyl",settings.vinyl_enabled,0,1),FIELD("sony_vinyl_mode",settings.vinyl_mode,1,4),
    FIELD("sony_dsee",dsee.enabled,0,1),FIELD("sony_dsee_mode",dsee.mode,1,5),
    FIELD("sony_dsd_filter",dsd.filter,1,2),FIELD("sony_dsd_gain",dsd.gain,1,2),
    FIELD("sony_volume",settings.volume,0,120)
};
#define COUNT ((unsigned)(sizeof(fields)/sizeof(fields[0])))
static int field_value(const struct sony_dsd_live_controls *c,unsigned n){int v;memcpy(&v,(const char *)c+fields[n].offset,sizeof(v));return v;}
static void field_put(struct sony_dsd_live_controls *c,unsigned n,int v){memcpy((char *)c+fields[n].offset,&v,sizeof(v));}
static int hex_id(const char *s){
    if(!s||strlen(s)!=32)return 0;
    for(unsigned n=0;n<32;++n)if(!((s[n]>='0'&&s[n]<='9')||(s[n]>='a'&&s[n]<='f')))return 0;
    return 1;
}
static int positive(const char *s,uint64_t maximum,uint64_t *out){
    if(!s||s[0]<'1'||s[0]>'9')return -EINVAL;
    uint64_t v=0;for(;*s;++s){if(*s<'0'||*s>'9'||v>(maximum-(unsigned)(*s-'0'))/10)return -EINVAL;v=v*10+(unsigned)(*s-'0');}
    *out=v;return 0;
}
int sony_settings_patch_parse(const char *text,struct sony_settings_patch *out){
    /* Android AudioParameter/HIDL reorders keys. Validate the entire unordered
     * set below, including all mandatory headers and duplicate detection. */
    if(!text||!out||!*text||strnlen(text,1024)>=1024)return -EINVAL;
    char copy[1024];strcpy(copy,text);struct sony_settings_patch patch={0};unsigned header=0;
    char *next=copy;
    while(next){
        char *p=next;next=strchr(p,';');if(next)*next++=0;
        if(!*p)return -EINVAL;
        char *v=strchr(p,'=');if(!v||v==p||!v[1]||strchr(v+1,'='))return -EINVAL;*v++=0;
        if(!strcmp(p,"sony_owner_generation")){if(header&1||positive(v,UINT64_MAX,&patch.generation))return -EINVAL;header|=1;}
        else if(!strcmp(p,"sony_revision")){uint64_t revision;if(header&2||positive(v,UINT32_MAX,&revision))return -EINVAL;patch.revision=(uint32_t)revision;header|=2;}
        else if(!strcmp(p,"sony_request_id")){if(header&4||!hex_id(v))return -EINVAL;strcpy(patch.id,v);header|=4;}
        else{
            unsigned n;for(n=0;n<COUNT&&strcmp(p,fields[n].name);++n){}
            if(n==COUNT)return -ENOTSUP;
            if(patch.mask&(1u<<n))return -EINVAL;
            const char *digit=v;if(*digit=='-')++digit;if(!*digit)return -EINVAL;
            for(;*digit;++digit)if(*digit<'0'||*digit>'9')return -EINVAL;
            errno=0;char *end;long value=strtol(v,&end,10);
            if(errno||*end||value<fields[n].lo||value>fields[n].hi)return -EINVAL;
            patch.mask|=1u<<n;field_put(&patch.values,n,(int)value);
        }
    }
    if(header!=7||!patch.mask)return -EINVAL;
    /* Native mode choices enable their effect. Conflicting explicit disable
     * is invalid; other fields remain untouched. */
    for(unsigned n=17;n<=21;n+=2)if(patch.mask&(1u<<n)){
        if((patch.mask&(1u<<(n-1)))&&!field_value(&patch.values,n-1))return -EINVAL;
        patch.mask|=1u<<(n-1);field_put(&patch.values,n-1,1);
    }
    *out=patch;return 0;
}
int sony_settings_patch_apply(const struct sony_settings_patch *p,const struct wdf_controls *base,struct sony_dsd_live_controls *out){
    if(!p||!base||!out||!p->mask||(p->mask>>COUNT))return -EINVAL;
    if(base->pending)return -EBUSY;
    struct sony_dsd_live_controls target=base->controls;
    for(unsigned n=0;n<COUNT;++n)if(p->mask&(1u<<n)){int v=field_value(&p->values,n);if(v<fields[n].lo||v>fields[n].hi)return -EINVAL;field_put(&target,n,v);}
    if((p->mask&0x7ffu)&&target.selector!=2)return -EINVAL;
    if((p->mask&0xf000u)&&target.selector!=3)return -EINVAL;
    /* The inactive native tone cache does not represent the saved tone bank.
     * Selector transition requires all saved values from the same UI snapshot.
     * Never silently replace that bank with disabled/native-default zeros. */
    if(base->controls.selector==2&&target.selector==3&&(p->mask&0xf000u)!=0xf000u)return -EINVAL;
    if((p->mask&(3u<<22))&&base->controls.dsd.mode!=2)return -ENOTSUP;
    *out=target;return 0;
}
struct sony_settings_relay {
    struct sony_settings_patch patch;struct sony_command_client *client;
    struct sony_dsd_live_controls target;struct wdf_controls actual;
    uint64_t deadline,next_status;unsigned waiting;int phase,error,changed,stage_sent;
};
static int time_ms(uint64_t *out){struct timespec t;if(clock_gettime(CLOCK_BOOTTIME,&t))return -errno;
    if(t.tv_sec<0||t.tv_nsec<0||t.tv_nsec>=1000000000)return -EIO;
    *out=(uint64_t)t.tv_sec*1000u+(unsigned long)t.tv_nsec/1000000u;return 0;}
static int finish(struct sony_settings_relay *r,int phase,int error){
    r->phase=phase;r->error=error;r->waiting=0;if(r->client)sony_command_client_close(&r->client);return 0;
}
static int start(struct sony_settings_relay **out,int dir,enum sony_audio_owner_kind kind,const char *text,unsigned timeout){
    if(!out||*out||timeout<10||timeout>10000||(kind!=SONY_OWNER_DSF&&kind!=SONY_OWNER_USB_DAC&&kind!=SONY_OWNER_BT_RX))return -EINVAL;
    struct sony_settings_patch patch;int rc=sony_settings_patch_parse(text,&patch);if(rc)return rc;
    if((kind==SONY_OWNER_USB_DAC||kind==SONY_OWNER_BT_RX)&&patch.mask!=(1u<<24))return -ENOTSUP;
    struct sony_settings_relay *r=calloc(1,sizeof(*r));if(!r)return -ENOMEM;r->patch=patch;r->phase=SONY_SETTINGS_CHECKING;
    uint64_t now=0;rc=time_ms(&now);if(!rc)r->deadline=now+timeout;
    if(!rc)rc=dir<0?sony_command_client_open_kind(&r->client,patch.generation,kind):sony_command_client_open_at_kind(&r->client,dir,patch.generation,kind);
    if(!rc)rc=sony_command_client_send(r->client,SONY_COMMAND_GET,0,NULL);
    if(rc){if(r->client)sony_command_client_close(&r->client);free(r);return rc;}
    r->waiting=SONY_COMMAND_GET;*out=r;return 0;
}
int sony_settings_relay_start(struct sony_settings_relay **o,const char *p,unsigned t){return start(o,-1,SONY_OWNER_DSF,p,t);}
int sony_settings_relay_start_at(struct sony_settings_relay **o,int d,const char *p,unsigned t){return d<0?-EINVAL:start(o,d,SONY_OWNER_DSF,p,t);}
int sony_settings_relay_start_kind(struct sony_settings_relay **o,enum sony_audio_owner_kind k,const char *p,unsigned t){return start(o,-1,k,p,t);}
int sony_settings_relay_start_at_kind(struct sony_settings_relay **o,int d,enum sony_audio_owner_kind k,const char *p,unsigned t){return d<0?-EINVAL:start(o,d,k,p,t);}
int sony_settings_relay_busy(const struct sony_settings_relay *r){return r&&(r->phase==SONY_SETTINGS_CHECKING||r->phase==SONY_SETTINGS_APPLYING);}
int sony_settings_relay_matches(const struct sony_settings_relay *r,const char *id){return r&&hex_id(id)&&!strcmp(id,r->patch.id);}
int sony_settings_relay_poll(struct sony_settings_relay *r){
    if(!r)return -EINVAL;
    if(!sony_settings_relay_busy(r))return 0;
    uint64_t now=0;int rc=time_ms(&now);
    if(rc||now>=r->deadline)return finish(r,r->stage_sent?SONY_SETTINGS_UNKNOWN:SONY_SETTINGS_REJECTED,rc?rc:-ETIMEDOUT);
    if(!r->waiting){
        if(now<r->next_status)return 0;
        rc=sony_command_client_send(r->client,SONY_COMMAND_STATUS,0,NULL);
        if(rc)return finish(r,SONY_SETTINGS_UNKNOWN,rc);
        r->waiting=SONY_COMMAND_STATUS;return 0;
    }
    struct sony_command_reply reply;rc=sony_command_client_receive(r->client,&reply);
    if(rc==-EAGAIN)return 0;
    if(rc)return finish(r,r->stage_sent?SONY_SETTINGS_UNKNOWN:SONY_SETTINGS_REJECTED,rc);
    unsigned command=r->waiting;r->waiting=0;
    if(reply.header.error)return finish(r,reply.header.phase==SONY_COMMAND_REJECTED?SONY_SETTINGS_REJECTED:SONY_SETTINGS_FAILED,reply.header.error);
    if(command==SONY_COMMAND_GET){
        rc=sony_settings_patch_apply(&r->patch,&reply.state,&r->target);
        if(rc)return finish(r,SONY_SETTINGS_REJECTED,rc);
        r->changed=memcmp(&r->target,&reply.state.controls,sizeof(r->target))!=0;
        /* Mark uncertain BEFORE attempting a write, including partial transport
         * failure. A subsequent negative send cannot prove no remote mutation. */
        r->stage_sent=1;rc=sony_command_client_send(r->client,SONY_COMMAND_STAGE,reply.state.revision,&r->target);
        if(rc)return finish(r,SONY_SETTINGS_UNKNOWN,rc);
        r->waiting=SONY_COMMAND_STAGE;r->phase=SONY_SETTINGS_APPLYING;return 0;
    }
    if(reply.header.phase==SONY_COMMAND_APPLYING){r->next_status=now+20;return 0;}
    if(reply.header.phase!=SONY_COMMAND_APPLIED||reply.state.pending)
        return finish(r,SONY_SETTINGS_UNKNOWN,-EPROTO);
    r->actual=reply.state;return finish(r,SONY_SETTINGS_APPLIED,0);
}
char *sony_settings_relay_result(const struct sony_settings_relay *r,const char *id){
    if(!hex_id(id))return NULL;
    char *text=NULL;
    if(!r||strcmp(id,r->patch.id)){
        if(asprintf(&text,"sony_request_id=%s;sony_control_phase=6;sony_control_error=%d;sony_owner_generation=0;sony_revision=0;sony_control_changed=0",id,-ENOENT)<0)return NULL;
        return text;
    }
    char result[2048];int n=snprintf(result,sizeof(result),"sony_request_id=%s;sony_control_phase=%d;sony_control_error=%d;sony_owner_generation=%llu;sony_revision=%u;sony_control_changed=%d",
        id,r->phase,r->error,(unsigned long long)r->patch.generation,r->phase==SONY_SETTINGS_APPLIED?r->actual.revision:r->patch.revision,r->changed);
    if(n<0||(unsigned)n>=sizeof(result))return NULL;
    if(r->phase==SONY_SETTINGS_APPLIED)for(unsigned i=0;i<COUNT;++i){
        int wrote=snprintf(result+n,sizeof(result)-(unsigned)n,";%s=%d",fields[i].name,field_value(&r->actual.controls,i));
        if(wrote<0||(unsigned)wrote>=sizeof(result)-(unsigned)n)return NULL;
        n+=wrote;
    }
    return strdup(result);
}
void sony_settings_relay_close(struct sony_settings_relay **out){if(out&&*out){struct sony_settings_relay *r=*out;if(r->client)sony_command_client_close(&r->client);free(r);*out=NULL;}}
