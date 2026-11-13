#include "sony_audio_dsf_reader.h"
#include "sony_audio_dsd_unpack.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>

struct sony_dsf_reader {
    struct sony_dsf_info info;
    sony_dsf_read_at read;
    void *context;
    uint64_t physical_position,per_channel_position;
    int busy,faulted;
    uint8_t physical[32768];
};
static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
static uint64_t le64(const uint8_t *p) {return le32(p)|((uint64_t)le32(p+4)<<32);}
static int overlap(const void *a,size_t na,const void *b,size_t nb) {
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    if(na>UINTPTR_MAX-x||nb>UINTPTR_MAX-y)return 1;
    return na&&nb&&x<y+nb&&y<x+na;
}
int sony_dsf_reader_open(struct sony_dsf_reader **out,uint64_t file_bytes,sony_dsf_read_at read,void *context) {
    uint8_t h[92];size_t actual=0;uint64_t samples,per_channel,padded,physical,data_size,metadata;
    unsigned rate,channels;struct sony_dsf_reader *r;
    if(!out||!read||file_bytes<sizeof(h))return -EINVAL;
    if(read(context,0,h,sizeof(h),&actual)||actual!=sizeof(h))return -EIO;
    if(memcmp(h,"DSD ",4)||le64(h+4)!=28||le64(h+12)!=file_bytes||
       memcmp(h+28,"fmt ",4)||le64(h+32)!=52||
       le32(h+60)!=1||le32(h+72)!=4096||le32(h+76)!=0||memcmp(h+80,"data",4))return -EINVAL;
    channels=le32(h+52);rate=le32(h+56);samples=le64(h+64);metadata=le64(h+20);data_size=le64(h+84);
    if((channels!=1&&channels!=2)||le32(h+48)!=channels||!samples||(samples&7))return -EINVAL;
    per_channel=samples>>3;
    /* samples is 64-bit bits, so ceil(bytes/4096)*4096*2 cannot overflow. */
    padded=(per_channel+4095)&~(uint64_t)4095;
    physical=channels==1?padded:padded*2;
    if(data_size!=physical+12||physical>file_bytes-92)return -EINVAL;
    if(metadata) {if(metadata!=92+physical||metadata>=file_bytes)return -EINVAL;}
    else if(file_bytes!=92+physical)return -EINVAL;
    r=calloc(1,sizeof(*r));if(!r)return -ENOMEM;
    r->info.sample_bits_per_channel=samples;r->info.source_bytes=channels==1?per_channel:per_channel*2;
    r->info.physical_data_bytes=physical;r->info.metadata_offset=metadata;r->info.rate=rate;r->info.channels=channels;
    r->read=read;r->context=context;*out=r;return 0;
}
int sony_dsf_reader_info(const struct sony_dsf_reader *r,struct sony_dsf_info *out) {
    if(!r||!out||overlap(out,sizeof(*out),r,sizeof(*r)))return -EINVAL;
    if(r->busy)return -EBUSY;
    *out=r->info;return 0;
}
int sony_dsf_reader_next(struct sony_dsf_reader *r,uint8_t *out,size_t capacity,struct sony_dsf_block *block) {
    uint64_t remain,physical_remain;size_t n,valid,per_channel,output_bytes,actual=0,written=0;int rc;
    struct sony_dsf_block result={0};
    if(!r||!out||!block)return -EINVAL;
    if(r->busy)return -EBUSY;
    if(r->faulted)return -EPIPE;
    remain=(r->info.sample_bits_per_channel>>3)-r->per_channel_position;
    if(!remain)return 1;
    physical_remain=r->info.physical_data_bytes-r->physical_position;
    n=physical_remain>sizeof(r->physical)?sizeof(r->physical):(size_t)physical_remain;
    per_channel=r->info.channels==1?n:n/2;
    if(remain<per_channel)per_channel=(size_t)remain;
    valid=r->info.channels==1?per_channel:per_channel*2;output_bytes=per_channel*2;
    if(capacity<output_bytes)return -ENOSPC;
    if(overlap(out,output_bytes,r,sizeof(*r))||overlap(block,sizeof(*block),r,sizeof(*r))||
       overlap(out,output_bytes,block,sizeof(*block)))return -EINVAL;
    r->busy=1;
    rc=r->read(r->context,92+r->physical_position,r->physical,n,&actual);
    if(rc||actual!=n) {r->faulted=1;r->busy=0;return -EIO;}
    rc=sony_dsd_unpack(SONY_DSD_DSF_BLOCKS,r->info.channels,r->physical,n,valid,out,capacity,&written);
    if(rc||written!=output_bytes) {r->faulted=1;r->busy=0;return -EIO;}
    result.bytes=written;result.first_byte_per_channel=r->per_channel_position;
    result.first=r->per_channel_position==0;result.last=per_channel==remain;
    r->physical_position+=n;r->per_channel_position+=per_channel;r->busy=0;
    *block=result;return 0;
}
int sony_dsf_reader_close(struct sony_dsf_reader *r) {
    if(r&&r->busy)return -EBUSY;
    free(r);return 0;
}
