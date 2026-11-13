#include "sony_audio_snapshot_tone.h"
#include "sony_audio_snapshot_store.h"
#include <stdint.h>
#include <errno.h>
static uint32_t word(const unsigned char *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static void put(unsigned char *p,uint32_t v) {
    for(unsigned int i=0;i<4;++i)p[i]=(unsigned char)(v>>(i*8));
}
static int valid(const struct sony_tone_settings *s) {
    if(!s||(s->enabled!=0&&s->enabled!=1))return 0;
    for(unsigned int i=0;i<3;++i)if(s->gains[i]<-20||s->gains[i]>20||s->centerfreq[i]!=2)return 0;
    return 1;
}
static int decode_tone(const void *snapshot,unsigned int size,struct sony_tone_settings *output,int selected) {
    if(!snapshot||!output||size!=SONY_SNAPSHOT_BYTES)return -EINVAL;
    int status=sony_snapshot_validate(snapshot);if(status)return status;
    const unsigned char *node=(const unsigned char *)snapshot+16*1024;
    if(selected&&word(node+224)!=3)return -ENOTSUP;
    struct sony_tone_settings staged;staged.enabled=word(node+196)!=0;
    for(unsigned int i=0;i<3;++i) {
        staged.gains[i]=(int32_t)word(node+200+i*4);
        staged.centerfreq[i]=(int32_t)word(node+212+i*4);
    }
    if(!valid(&staged))return -ERANGE;
    *output=staged;return 0;
}
int sony_snapshot_decode_tone(const void *snapshot,unsigned int size,struct sony_tone_settings *output) {
    return decode_tone(snapshot,size,output,1);
}
int sony_snapshot_decode_tone_values(const void *snapshot,unsigned int size,struct sony_tone_settings *output) {
    return decode_tone(snapshot,size,output,0);
}
int sony_snapshot_encode_tone(const void *snapshot,unsigned int size,const struct sony_tone_settings *s,void *output) {
    if(!snapshot||!output||size!=SONY_SNAPSHOT_BYTES)return -EINVAL;
    int status=sony_snapshot_validate(snapshot);if(status)return status;
    if(!valid(s))return -ERANGE;
    unsigned char staged[SONY_SNAPSHOT_BYTES];
    for(unsigned int i=0;i<size;++i)staged[i]=((const unsigned char *)snapshot)[i];
    unsigned char *node=staged+16*1024;
    put(node+224,3);put(node+196,(uint32_t)s->enabled);
    for(unsigned int i=0;i<3;++i) {
        put(node+200+i*4,(uint32_t)s->gains[i]);
        put(node+212+i*4,(uint32_t)s->centerfreq[i]);
    }
    uint32_t crc=~0u;
    for(unsigned int i=0;i<size;++i) {
        crc^=(i>=4096&&i<4216)?0:staged[i];
        for(unsigned int b=0;b<8;++b)crc=(crc>>1)^(0xedb88320u&(0u-(crc&1)));
    }
    put(staged+4096,~crc);
    for(unsigned int i=0;i<size;++i)((unsigned char *)output)[i]=staged[i];
    return 0;
}
