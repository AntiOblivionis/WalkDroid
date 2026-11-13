#define _POSIX_C_SOURCE 200809L
#include "sony_audio_preset_controls.h"
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
static int number(const char *text,unsigned int *value){
    if(!text||!*text)return -EINVAL;
    for(const char *p=text;*p;++p)if(*p<'0'||*p>'9')return -EINVAL;
    errno=0;char *end=NULL;unsigned long result=strtoul(text,&end,10);
    if(errno||*end||result>UINT_MAX)return -EINVAL;
    *value=(unsigned int)result;return 0;
}
int sony_controls_prepare_preset(const char *request,unsigned int revision,
    const struct sony_controls *base,struct sony_controls *target,struct sony_sound_preset_plan *output){
    (void)revision;
    if(!request||!base||!target||!output)return -EINVAL;
    unsigned int size=(unsigned int)strlen(request);
    if(!size||size>=128||request[0]==';'||request[size-1]==';'||strstr(request,";;"))return -EINVAL;
    char text[128];memcpy(text,request,size+1);char *state=NULL;unsigned int seen=0,operation=0,slot=0;
    for(char *p=strtok_r(text,";",&state);p;p=strtok_r(NULL,";",&state)){
        char *value=strchr(p,'=');if(!value)return -EINVAL;*value++=0;
        unsigned int n=0,bit;if(number(value,&n))return -EINVAL;
        if(!strcmp(p,"sony_revision")){bit=1;}
        else if(!strcmp(p,"sony_preset_save")||!strcmp(p,"sony_preset_load")){
            bit=2;if(n<1||n>3)return -EINVAL;slot=n;
            operation=!strcmp(p,"sony_preset_save")?SONY_SOUND_PRESET_SAVE:SONY_SOUND_PRESET_LOAD;
        }else return -ENOSYS;
        if(seen&bit)return -EINVAL;seen|=bit;
    }
    if(!(seen&2))return -EINVAL;
    struct sony_sound_preset_plan plan={0};plan.operation=operation;plan.slot=slot;
    int rc=sony_snapshot_read(SONY_AUDIO_CONFIG,plan.before);if(rc)return rc;
    rc=sony_sound_preset_project(plan.before,sizeof(plan.before),operation,slot,plan.after);if(rc)return rc;
    struct sony_controls next=*base;
    if(operation==SONY_SOUND_PRESET_LOAD){rc=sony_controls_decode_effects(plan.after,&next);if(rc)return rc;}
    *target=next;*output=plan;return 0;
}
