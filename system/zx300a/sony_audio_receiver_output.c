#include "sony_audio_receiver_output.h"
#include "sony_audio_receiver_pcm.h"
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
static int output_s32(const uint32_t usb[25],const struct sony_receiver_chain_result *native,
    const void *payload,unsigned bytes,void *destination,unsigned capacity,struct sony_receiver_s32_info *info,int bluetooth) {
    (void)bluetooth;
    if(!usb||!native||!info||!payload||!destination)return -EINVAL;
    unsigned width=native->track[11]?native->track[11]:4;
    unsigned rate=native->track[0]?native->track[0]:usb[0];
    uint64_t frames=(uint64_t)bytes/((uint64_t)width*2),needed=frames*8;
    if(needed>capacity)return -ENOSPC;
    uint8_t *staged=needed?malloc((size_t)needed):NULL;
    if(needed&&!staged)return -ENOMEM;
    const uint8_t *from=payload;
    for(uint64_t i=0;i<frames*2;++i){
        const uint8_t *sample=from+i*width;uint8_t *to=staged+i*4;
        unsigned copy=width<4?width:4;
        memset(to,0,4-copy);memcpy(to+4-copy,sample+width-copy,copy);
    }
    if(needed)memcpy(destination,staged,(size_t)needed);free(staged);
    *info=(struct sony_receiver_s32_info){rate,2,4,(uint32_t)frames,(uint32_t)needed};return 0;
}
int sony_receiver_output_s32(const uint32_t source[25],const struct sony_receiver_chain_result *native,
    const void *payload,unsigned bytes,void *destination,unsigned capacity,struct sony_receiver_s32_info *info){
    return output_s32(source,native,payload,bytes,destination,capacity,info,0);
}
int sony_receiver_output_bt_s32(const uint32_t source[25],const struct sony_receiver_chain_result *native,
    const void *payload,unsigned bytes,void *destination,unsigned capacity,struct sony_receiver_s32_info *info){
    return output_s32(source,native,payload,bytes,destination,capacity,info,1);
}
