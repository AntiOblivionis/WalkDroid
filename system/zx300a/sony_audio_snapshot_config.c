/* Experimental glibc worker facade for the mapped PCM-chain keys. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "sony_audio_snapshot_config.h"
#include "sony_audio_snapshot_store.h"
#include "sony_audio_dmpconfig_forward.h"
#include <stdint.h>
struct config_mapping { unsigned int key,offset; };
#include "sony_audio_snapshot_config_map.h"
#define CONFIG_COUNT (sizeof(config_map)/sizeof(config_map[0]))
static int values[CONFIG_COUNT];
static int bound;
static int index_of(unsigned int key) {
    for(unsigned int i=0;i<CONFIG_COUNT;++i)if(config_map[i].key==key)return (int)i;
    return -1;
}
void sony_snapshot_config_unbind(void) {
    bound=0;
    for(unsigned int i=0;i<CONFIG_COUNT;++i)values[i]=0;
}
int sony_snapshot_config_bind(const void *snapshot) {
    sony_snapshot_config_unbind();
    if(!snapshot||sony_snapshot_validate(snapshot))return -1;
    const unsigned char *p=snapshot;
    for(unsigned int i=0;i<CONFIG_COUNT;++i) {
        unsigned int o=config_map[i].offset;
        uint32_t v=(uint32_t)p[o]|(uint32_t)p[o+1]<<8|(uint32_t)p[o+2]<<16|(uint32_t)p[o+3]<<24;
        values[i]=(int32_t)v;
    }
    bound=1;return 0;
}
int sony_snapshot_config_fault(void) {return 0;}
int snapshot_config_get(void *,const unsigned int *,int *)
    __asm__("_ZN3pst9dmpconfig9DmpConfig3GetERKNS0_3KeyERi");
int snapshot_config_get(void *object,const unsigned int *key,int *value) {
    int i=key?index_of(*key):-1;
    if(bound&&value&&i>=0){*value=values[i];return 0;}
    typedef int (*get_fn)(void *,const unsigned int *,int *);
    get_fn original=(get_fn)sony_dmpconfig_original("_ZN3pst9dmpconfig9DmpConfig3GetERKNS0_3KeyERi");
    return original?original(object,key,value):1;
}
int snapshot_config_set(void *,const unsigned int *,const int *,unsigned char)
    __asm__("_ZN3pst9dmpconfig9DmpConfig3SetERKNS0_3KeyERKib");
int snapshot_config_set(void *object,const unsigned int *key,const int *value,unsigned char persist) {
    int i=key?index_of(*key):-1;
    if(bound&&value&&i>=0&&!persist){values[i]=*value;return 0;}
    /* Never claim a durable native write without calling its provider. */
    typedef int (*set_fn)(void *,const unsigned int *,const int *,unsigned char);
    set_fn original=(set_fn)sony_dmpconfig_original("_ZN3pst9dmpconfig9DmpConfig3SetERKNS0_3KeyERKib");
    int result=original?original(object,key,value,persist):1;
    if(!result&&bound&&value&&i>=0)values[i]=*value;
    return result;
}
