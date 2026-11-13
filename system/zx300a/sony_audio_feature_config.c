#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "sony_audio_feature_config.h"
#include <dlfcn.h>

static const char *const models[]={"NW-WM1A","NW-WM1Z","NW-A30Series","NW-ZX300","NW-ZX300A","NW-A40Series","NW-A50Series","DMP-Z1"};
static const unsigned int ship_words[]={0,1,2,0x103,4,6,0x306,0x203};
static struct sony_audio_feature_context current;
static unsigned int reads;
static int bound;

void sony_audio_feature_config_unbind(void){bound=0;reads=0;current.model=current.ship=0;}
int sony_audio_feature_config_bind(const struct sony_audio_feature_context *context){
    sony_audio_feature_config_unbind();
    if(!context)return -1;
    current=*context;bound=1;return 0;
}
/* Retained for older callers; a previous failed lookup no longer locks them. */
int sony_audio_feature_config_fault(void){return 0;}
unsigned int sony_audio_feature_config_reads(void){return reads;}

static int original_binary(void *object,unsigned int group,unsigned int key,unsigned int length,unsigned char *out){
    typedef int (*get_binary)(void *,unsigned int,unsigned int,unsigned int,unsigned char *);
    static const char symbol[]="_ZN3pst8services13configuration13Configuration9GetBinaryENS1_7GroupIdENS1_5KeyIdEjPh";
    get_binary original=(get_binary)dlsym(RTLD_NEXT,symbol);
    if(!original){
        /* The vendor may have loaded its provider RTLD_LOCAL. Keep one
         * reference when it is already loaded; do not load a new provider. */
        static void *provider;
        if(!provider)provider=dlopen("libConfigurationService.so",RTLD_NOW|RTLD_NOLOAD);
        if(provider)original=(get_binary)dlsym(provider,symbol);
    }
    return original?original(object,group,key,length,out):1;
}

int sony_audio_feature_config_binary(void *object,unsigned int group,unsigned int key,unsigned int length,unsigned char *out){
    if(bound&&out&&group==0&&key==0&&length==4&&current.ship<sizeof(ship_words)/sizeof(ship_words[0])){
        unsigned int value=ship_words[current.ship];
        for(unsigned int i=0;i<4;++i)out[i]=(unsigned char)(value>>(8*i));
        ++reads;return 0;
    }
    if(bound&&out&&group==0&&key==4&&length==64&&current.model<sizeof(models)/sizeof(models[0])){
        for(unsigned int i=0;i<64;++i)out[i]=0;
        const char *model=models[current.model];for(unsigned int i=0;model[i];++i)out[i]=(unsigned char)model[i];
        ++reads;return 0;
    }
    /* Unknown keys, lengths and enum values belong to the selected provider. */
    return original_binary(object,group,key,length,out);
}

int sony_feature_configuration_binary(void *,unsigned int,unsigned int,unsigned int,unsigned char *)
 __asm__("_ZN3pst8services13configuration13Configuration9GetBinaryENS1_7GroupIdENS1_5KeyIdEjPh");
int sony_feature_configuration_binary(void *object,unsigned int group,unsigned int key,unsigned int length,unsigned char *out){
    return sony_audio_feature_config_binary(object,group,key,length,out);
}
