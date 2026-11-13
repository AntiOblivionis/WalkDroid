#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "sony_audio_ai_config.h"
#include "sony_audio_dmpconfig_forward.h"
static int bound,requested;
static unsigned int reads,mirrors;
int sony_ai_config_bind(int enabled){
    bound=1;requested=enabled;return 0;
}
int sony_ai_config_fault(void){return 0;}
unsigned int sony_ai_config_reads(void){return reads;}
unsigned int sony_ai_config_mirrors(void){return mirrors;}
int sony_ai_private_get(void *,const unsigned int *,int *)
 __asm__("_ZN3pst9dmpconfig9DmpConfig3GetERKNS0_3KeyERi");
int sony_ai_private_get(void *object,const unsigned int *key,int *value){
    if(bound&&key&&*key==251&&value){*value=requested;++reads;return 0;}
    typedef int (*get_fn)(void *,const unsigned int *,int *);
    get_fn original=(get_fn)sony_dmpconfig_original("_ZN3pst9dmpconfig9DmpConfig3GetERKNS0_3KeyERi");
    return original?original(object,key,value):1;
}
int sony_ai_private_set(void *,const unsigned int *,const int *,unsigned char)
 __asm__("_ZN3pst9dmpconfig9DmpConfig3SetERKNS0_3KeyERKib");
int sony_ai_private_set(void *object,const unsigned int *key,const int *value,unsigned char persist){
    int local=bound&&key&&*key==251&&value;
    if(local&&!persist){if(*value==requested)++mirrors;requested=*value;return 0;}
    typedef int (*set_fn)(void *,const unsigned int *,const int *,unsigned char);
    set_fn original=(set_fn)sony_dmpconfig_original("_ZN3pst9dmpconfig9DmpConfig3SetERKNS0_3KeyERKib");
    int result=original?original(object,key,value,persist):1;
    if(!result&&local)requested=*value;
    return result;
}
