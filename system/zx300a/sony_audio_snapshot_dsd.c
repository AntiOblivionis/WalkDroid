#include "sony_audio_snapshot_dsd.h"
#include "sony_audio_snapshot_store.h"
#include <errno.h>
#include <stdint.h>
#include <string.h>
static uint32_t word(const unsigned char *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void put(unsigned char *p,uint32_t v){for(unsigned i=0;i<4;++i)p[i]=(unsigned char)(v>>(i*8));}
int sony_snapshot_apply_dsd(const void *snapshot,unsigned size,struct sony_dsd_controls *out){
    if(!snapshot||!out||size!=SONY_SNAPSHOT_BYTES)return -EINVAL;
    int rc=sony_snapshot_validate(snapshot);if(rc)return rc;
    const unsigned char *p=snapshot;
    struct sony_dsd_controls c={(int32_t)word(p+16396),(int32_t)word(p+16408),(int32_t)word(p+16404)};
    if(!sony_dsd_controls_valid(&c))return -ERANGE;
    *out=c;return 0;
}
int sony_snapshot_encode_dsd(const void *snapshot,unsigned size,const struct sony_dsd_controls *c,void *out){
    if(!snapshot||!out||size!=SONY_SNAPSHOT_BYTES)return -EINVAL;
    if(!sony_dsd_controls_valid(c))return -ERANGE;
    int rc=sony_snapshot_validate(snapshot);if(rc)return rc;
    unsigned char next[SONY_SNAPSHOT_BYTES];memcpy(next,snapshot,sizeof(next));
    put(next+16396,(uint32_t)c->mode);put(next+16404,(uint32_t)c->gain);put(next+16408,(uint32_t)c->filter);
    uint32_t crc=~0u;
    for(unsigned i=0;i<sizeof(next);++i){
        crc^=i>=4096&&i<4216?0:next[i];
        for(unsigned bit=0;bit<8;++bit)crc=(crc>>1)^(0xedb88320u&(0u-(crc&1)));
    }
    put(next+4096,~crc);memcpy(out,next,sizeof(next));return 0;
}
