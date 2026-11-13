#include "sony_audio_snapshot_eq.h"
#include "sony_audio_snapshot_store.h"
#include <stdint.h>
#include <errno.h>
static uint32_t word(const unsigned char *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
int sony_snapshot_apply_eq(const void *snapshot,unsigned int size,struct sony_chain_settings *settings) {
    if(!snapshot||!settings||size!=SONY_SNAPSHOT_BYTES)return -EINVAL;
    int status=sony_snapshot_validate(snapshot);if(status)return status;
    const unsigned char *node=(const unsigned char *)snapshot+16*1024;
    int bands[10];
    for(unsigned int i=0;i<10;++i) {
        int32_t value=(int32_t)word(node+156+i*4);
        if(value < -20 || value > 20)return -ERANGE;
        bands[i]=value;
    }
    settings->eq_enabled=word(node+148)!=0;
    for(unsigned int i=0;i<10;++i)settings->bands[i]=bands[i];
    return 0;
}
int sony_snapshot_apply_eq_direct(const void *snapshot,unsigned int size,int feature,struct sony_chain_settings *settings) {
    if(!settings||(feature!=0 && feature!=1))return -EINVAL;
    struct sony_chain_settings staged=*settings;
    int status=sony_snapshot_apply_eq(snapshot,size,&staged);if(status)return status;
    const unsigned char *node=(const unsigned char *)snapshot+16*1024;
    staged.direct=feature && word(node+240)!=0;
    *settings=staged;return 0;
}
static int apply_mode(const void *snapshot,unsigned int size,int feature,unsigned int mode,struct sony_chain_settings *settings) {
    if(!settings)return -EINVAL;
    struct sony_chain_settings staged=*settings;
    int status=sony_snapshot_apply_eq_direct(snapshot,size,feature,&staged);if(status)return status;
    const unsigned char *dc=(const unsigned char *)snapshot+16*1024;
    /* Current wire settings cannot represent EqTone. Never silently switch
     * a stored tone profile into EQ10 just to make the reduced adapter work. */
    if(word(dc+224)!=mode)return -ENOTSUP;
    const unsigned char *vinyl=(const unsigned char *)snapshot+17*1024;
    uint32_t dc_mode=word(dc+232),vinyl_mode=word(vinyl+12);
    if(dc_mode>7 || vinyl_mode>7)return -ERANGE;
    staged.dc_enabled=word(dc+228)!=0;staged.dc_mode=(int)dc_mode;
    staged.vinyl_enabled=word(vinyl+8)!=0;staged.vinyl_mode=(int)vinyl_mode;
    *settings=staged;return 0;
}
int sony_snapshot_apply_supported_effects(const void *snapshot,unsigned int size,int feature,struct sony_chain_settings *settings) {
    return apply_mode(snapshot,size,feature,2,settings);
}
int sony_snapshot_apply_tone_base(const void *snapshot,unsigned int size,int feature,struct sony_chain_settings *settings) {
    return apply_mode(snapshot,size,feature,3,settings);
}
static void put(unsigned char *p,uint32_t v) {
    for(unsigned int i=0;i<4;++i)p[i]=(unsigned char)(v>>(8*i));
}
int sony_snapshot_apply_output(const void *snapshot,unsigned int size,struct sony_output_controls *output) {
    if(!snapshot||!output||size!=SONY_SNAPSHOT_BYTES)return -EINVAL;
    int rc=sony_snapshot_validate(snapshot);if(rc)return rc;
    const unsigned char *p=(const unsigned char *)snapshot+16384;
    struct sony_output_controls c=SONY_OUTPUT_DEFAULT;
    c.balance_l=(int)word(p+908);c.balance_r=(int)word(p+912);c.se_gain=(int)word(p+916);c.btl_gain=(int)word(p+920);
    if(!sony_output_controls_valid(&c))return -ERANGE;
    *output=c;return 0;
}
int sony_snapshot_encode_output(const void *snapshot,unsigned int size,const struct sony_output_controls *c,void *output) {
    if(!snapshot||!output||size!=SONY_SNAPSHOT_BYTES)return -EINVAL;
    if(!sony_output_controls_valid(c))return -ERANGE;
    int rc=sony_snapshot_validate(snapshot);if(rc)return rc;
    unsigned char staged[SONY_SNAPSHOT_BYTES];
    for(unsigned int i=0;i<size;++i)staged[i]=((const unsigned char *)snapshot)[i];
    put(staged+16384+908,(uint32_t)c->balance_l);put(staged+16384+912,(uint32_t)c->balance_r);
    put(staged+16384+916,(uint32_t)c->se_gain);put(staged+16384+920,(uint32_t)c->btl_gain);
    uint32_t crc=~0u;
    for(unsigned int i=0;i<size;++i) {
        crc^=(i>=4096&&i<4216)?0:staged[i];
        for(unsigned int bit=0;bit<8;++bit)crc=(crc>>1)^(0xedb88320u&(0u-(crc&1)));
    }
    put(staged+4096,~crc);
    for(unsigned int i=0;i<size;++i)((unsigned char *)output)[i]=staged[i];
    return 0;
}
int sony_snapshot_apply_volume(const void *snapshot,unsigned int size,struct sony_chain_settings *settings) {
    if(!snapshot||!settings||size!=SONY_SNAPSHOT_BYTES)return -EINVAL;
    int rc=sony_snapshot_validate(snapshot);if(rc)return rc;
    uint32_t volume=word((const unsigned char *)snapshot+16384+896);
    if(volume>120)return -ERANGE;
    settings->volume=(int)volume;return 0;
}
int sony_snapshot_apply_dsee(const void *snapshot,unsigned int size,struct sony_dsee_controls *out) {
    if(!snapshot||!out||size!=SONY_SNAPSHOT_BYTES)return -EINVAL;
    int rc=sony_snapshot_validate(snapshot);if(rc)return rc;
    const unsigned char *p=(const unsigned char *)snapshot;
    struct sony_dsee_controls c={(int)word(p+16432),(int)word(p+16436)};
    if(!sony_dsee_controls_valid(&c))return -ERANGE;
    *out=c;return 0;
}
int sony_snapshot_apply_normalizer(const void *snapshot,unsigned int size,int *enabled) {
    if(!snapshot||!enabled||size!=SONY_SNAPSHOT_BYTES)return -EINVAL;
    int rc=sony_snapshot_validate(snapshot);if(rc)return rc;
    uint32_t value=word((const unsigned char *)snapshot+16448);
    if(value>1)return -ERANGE;
    *enabled=(int)value;return 0;
}
int sony_snapshot_encode_normalizer(const void *snapshot,unsigned int size,int enabled,void *output) {
    if(!snapshot||!output||size!=SONY_SNAPSHOT_BYTES)return -EINVAL;
    if(enabled!=0&&enabled!=1)return -ERANGE;
    int rc=sony_snapshot_validate(snapshot);if(rc)return rc;
    unsigned char staged[SONY_SNAPSHOT_BYTES];
    for(unsigned int i=0;i<size;++i)staged[i]=((const unsigned char *)snapshot)[i];
    put(staged+16448,(uint32_t)enabled);
    uint32_t crc=~0u;
    for(unsigned int i=0;i<size;++i){
        crc^=(i>=4096&&i<4216)?0:staged[i];
        for(unsigned int bit=0;bit<8;++bit)crc=(crc>>1)^(0xedb88320u&(0u-(crc&1)));
    }
    put(staged+4096,~crc);
    for(unsigned int i=0;i<size;++i)((unsigned char *)output)[i]=staged[i];
    return 0;
}
int sony_snapshot_encode_dsee(const void *snapshot,unsigned int size,const struct sony_dsee_controls *c,void *output) {
    if(!snapshot||!output||size!=SONY_SNAPSHOT_BYTES)return -EINVAL;
    if(!sony_dsee_controls_valid(c))return -ERANGE;
    int rc=sony_snapshot_validate(snapshot);if(rc)return rc;
    unsigned char staged[SONY_SNAPSHOT_BYTES];
    for(unsigned int i=0;i<size;++i)staged[i]=((const unsigned char *)snapshot)[i];
    put(staged+16432,(uint32_t)c->enabled);put(staged+16436,(uint32_t)c->mode);
    uint32_t crc=~0u;
    for(unsigned int i=0;i<size;++i) {
        crc^=(i>=4096&&i<4216)?0:staged[i];
        for(unsigned int bit=0;bit<8;++bit)crc=(crc>>1)^(0xedb88320u&(0u-(crc&1)));
    }
    put(staged+4096,~crc);
    for(unsigned int i=0;i<size;++i)((unsigned char *)output)[i]=staged[i];
    return 0;
}
int sony_snapshot_encode_volume(const void *snapshot,unsigned int size,int volume,void *output) {
    if(!snapshot||!output||size!=SONY_SNAPSHOT_BYTES)return -EINVAL;
    if(volume<0||volume>120)return -ERANGE;
    int rc=sony_snapshot_validate(snapshot);if(rc)return rc;
    unsigned char staged[SONY_SNAPSHOT_BYTES];
    for(unsigned int i=0;i<size;++i)staged[i]=((const unsigned char *)snapshot)[i];
    put(staged+16384+896,(uint32_t)volume);
    uint32_t crc=~0u;
    for(unsigned int i=0;i<size;++i) {
        crc^=(i>=4096&&i<4216)?0:staged[i];
        for(unsigned int bit=0;bit<8;++bit)crc=(crc>>1)^(0xedb88320u&(0u-(crc&1)));
    }
    put(staged+4096,~crc);
    for(unsigned int i=0;i<size;++i)((unsigned char *)output)[i]=staged[i];
    return 0;
}
int sony_snapshot_apply_volume_memory(const void *snapshot,unsigned int size,struct sony_volume_memory *out) {
    if(!snapshot||!out||size!=SONY_SNAPSHOT_BYTES)return -EINVAL;
    int rc=sony_snapshot_validate(snapshot);if(rc)return rc;
    const unsigned char *p=snapshot;
    struct sony_volume_memory m={(int)word(p+17288),(int)word(p+17308)};
    if(!sony_volume_memory_valid(&m))return -ERANGE;
    *out=m;return 0;
}
int sony_snapshot_encode_volume_memory(const void *snapshot,unsigned int size,const struct sony_volume_memory *m,void *output) {
    if(!snapshot||!output||size!=SONY_SNAPSHOT_BYTES)return -EINVAL;
    if(!sony_volume_memory_valid(m))return -ERANGE;
    int rc=sony_snapshot_validate(snapshot);if(rc)return rc;
    unsigned char staged[SONY_SNAPSHOT_BYTES];
    for(unsigned int i=0;i<size;++i)staged[i]=((const unsigned char *)snapshot)[i];
    put(staged+17288,(uint32_t)m->avls);put(staged+17308,(uint32_t)m->last_headphone);
    uint32_t crc=~0u;
    for(unsigned int i=0;i<size;++i) {
        crc^=(i>=4096&&i<4216)?0:staged[i];
        for(unsigned int bit=0;bit<8;++bit)crc=(crc>>1)^(0xedb88320u&(0u-(crc&1)));
    }
    put(staged+4096,~crc);
    for(unsigned int i=0;i<size;++i)((unsigned char *)output)[i]=staged[i];
    return 0;
}
int sony_snapshot_encode_supported_effects(const void *snapshot,unsigned int size,int feature,const struct sony_chain_settings *s,void *output) {
    if(!snapshot||!s||!output||size!=SONY_SNAPSHOT_BYTES||(feature!=0 && feature!=1))return -EINVAL;
    int status=sony_snapshot_validate(snapshot);if(status)return status;
    if((s->direct!=0 && s->direct!=1)||(!feature && s->direct)||
       (s->eq_enabled!=0 && s->eq_enabled!=1)||(s->dc_enabled!=0 && s->dc_enabled!=1)||
       (s->vinyl_enabled!=0 && s->vinyl_enabled!=1)||s->dc_mode<0||s->dc_mode>7||s->vinyl_mode<0||s->vinyl_mode>7)return -ERANGE;
    for(unsigned int i=0;i<10;++i)if(s->bands[i]<-20||s->bands[i]>20)return -ERANGE;
    unsigned char staged[SONY_SNAPSHOT_BYTES];
    for(unsigned int i=0;i<size;++i)staged[i]=((const unsigned char *)snapshot)[i];
    unsigned char *node=staged+16*1024,*vinyl=staged+17*1024;
    put(node+224,2); /* Settings schema explicitly describes ten-band EQ. */
    put(node+148,(uint32_t)s->eq_enabled);
    for(unsigned int i=0;i<10;++i)put(node+156+i*4,(uint32_t)s->bands[i]);
    put(node+228,(uint32_t)s->dc_enabled);put(node+232,(uint32_t)s->dc_mode);
    if(feature)put(node+240,(uint32_t)s->direct);
    put(vinyl+8,(uint32_t)s->vinyl_enabled);put(vinyl+12,(uint32_t)s->vinyl_mode);
    uint32_t crc=~0u;
    for(unsigned int i=0;i<size;++i) {
        crc^=(i>=4096 && i<4216)?0:staged[i];
        for(unsigned int bit=0;bit<8;++bit)crc=(crc>>1)^(0xedb88320u & (0u-(crc&1)));
    }
    put(staged+4096,~crc);
    for(unsigned int i=0;i<size;++i)((unsigned char *)output)[i]=staged[i];
    return 0;
}
