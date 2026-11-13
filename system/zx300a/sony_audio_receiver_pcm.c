#include "sony_audio_receiver_pcm.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
struct sony_receiver_pcm {
    uint32_t track[25];uint64_t accepted,taken;
    unsigned head,queued,frame_bytes,source_bits;int sealed,bt;
    unsigned char data[SONY_RECEIVER_PCM_QUEUE_FRAMES*8];
};
int sony_receiver_pcm_usb_track(const struct sony_usb_stream *s,uint32_t out[25]) {
    if(!s||!out)return -EINVAL;
    struct sony_usb_capture_plan plan;int r=sony_usb_stream_capture_plan(s,&plan);if(r)return r;
    if(s->format!=SONY_USB_PCM)return -ENOTSUP;
    uint32_t p[25]={0};p[0]=s->rate;p[1]=s->bit_width==16?16:32;p[2]=2;p[3]=2;
    p[10]=1;p[11]=p[1]/8;p[12]=1;p[13]=40;p[14]=3;p[16]=s->rate;p[17]=s->bit_width;
    memcpy(out,p,sizeof(p));return 0;
}
int sony_receiver_pcm_open_usb(struct sony_receiver_pcm **out,const struct sony_usb_stream *s) {
    if(!out||*out)return -EINVAL;uint32_t p[25];int r=sony_receiver_pcm_usb_track(s,p);if(r)return r;
    struct sony_receiver_pcm *c=calloc(1,sizeof(*c));if(!c)return -ENOMEM;
    memcpy(c->track,p,sizeof(p));c->frame_bytes=p[11]*2;c->source_bits=s->bit_width;*out=c;return 0;
}
int sony_receiver_pcm_bt_track(const struct sony_bt_pcm_stream *s,uint32_t out[25]) {
    if(!s||!out||(s->channel!=1&&s->channel!=2))return -EINVAL;
    switch(s->rate){case 32000:case 44100:case 48000:case 88200:case 96000:break;default:return -ENOTSUP;}
    unsigned codec;
    switch(s->codec){case SONY_BT_AV_SBC:codec=2;break;case SONY_BT_AV_NATIVE_PCM:codec=10;break;case SONY_BT_AV_AAC:codec=5;break;case SONY_BT_AV_LDAC:codec=11;break;default:return -ENOTSUP;}
    uint32_t p[25]={0};p[0]=s->rate;p[1]=codec==11?24:16;p[2]=2;p[3]=s->channel;
    p[10]=1;p[11]=codec==11?4:2;p[12]=1;p[13]=codec;p[14]=3;
    memcpy(out,p,sizeof(p));return 0;
}
int sony_receiver_pcm_open_bt(struct sony_receiver_pcm **out,const struct sony_bt_pcm_stream *s) {
    if(!out||*out)return -EINVAL;uint32_t p[25];int rc=sony_receiver_pcm_bt_track(s,p);if(rc)return rc;
    struct sony_receiver_pcm *c=calloc(1,sizeof(*c));if(!c)return -ENOMEM;
    memcpy(c->track,p,sizeof(p));c->frame_bytes=p[11]*2;c->source_bits=p[1];c->bt=1;*out=c;return 0;
}
int sony_receiver_pcm_push_bt(struct sony_receiver_pcm *c,const void *data,unsigned frames) {
    if(!c||!c->bt||!data||!frames||frames>SONY_RECEIVER_PCM_QUEUE_FRAMES)return -EINVAL;
    if(c->sealed)return -EPIPE;
    if(frames>SONY_RECEIVER_PCM_QUEUE_FRAMES-c->queued)return -ENOBUFS;
    if(c->accepted>UINT64_MAX-frames)return -EOVERFLOW;
    unsigned tail=(c->head+c->queued)%SONY_RECEIVER_PCM_QUEUE_FRAMES;
    unsigned first=SONY_RECEIVER_PCM_QUEUE_FRAMES-tail;if(first>frames)first=frames;
    memcpy(c->data+tail*c->frame_bytes,data,first*c->frame_bytes);
    if(first<frames)memcpy(c->data,(const unsigned char *)data+first*c->frame_bytes,(frames-first)*c->frame_bytes);
    c->queued+=frames;c->accepted+=frames;return 0;
}
int sony_receiver_pcm_push(struct sony_receiver_pcm *c,const void *data,unsigned frames) {
    if(!c||!data||!frames||frames>SONY_RECEIVER_PCM_QUEUE_FRAMES)return -EINVAL;
    if(c->bt)return -EINVAL;
    if(c->sealed)return -EPIPE;
    if(frames>SONY_RECEIVER_PCM_QUEUE_FRAMES-c->queued)return -ENOBUFS;
    if(c->accepted>UINT64_MAX-frames)return -EOVERFLOW;
    const unsigned char *p=data;
    /* Validate the complete source before changing queue/counters. Reading
     * bytewise also supports unaligned USB buffers without C aliasing UB. */
    if(c->source_bits!=32)for(unsigned i=0;i<frames*2;++i) {
        if(p[i*4]||(c->source_bits==16&&p[i*4+1]))return -EPROTO;
    }
    for(unsigned i=0;i<frames;++i) {
        unsigned pos=(c->head+c->queued+i)%SONY_RECEIVER_PCM_QUEUE_FRAMES;
        unsigned char *dst=c->data+pos*c->frame_bytes;
        if(c->source_bits==16){dst[0]=p[i*8+2];dst[1]=p[i*8+3];dst[2]=p[i*8+6];dst[3]=p[i*8+7];}
        else memcpy(dst,p+i*8,8);
    }
    c->queued+=frames;c->accepted+=frames;return 0;
}
int sony_receiver_pcm_take(struct sony_receiver_pcm *c,void *data,unsigned capacity,struct sony_receiver_pcm_packet *out) {
    if(!c||!data||!out)return -EINVAL;
    if(!c->queued)return c->sealed?-ENODATA:-EAGAIN;
    if(c->queued<SONY_RECEIVER_PCM_MAX_PACKET_FRAMES&&!c->sealed)return -EAGAIN;
    unsigned frames=c->queued<SONY_RECEIVER_PCM_MAX_PACKET_FRAMES?c->queued:SONY_RECEIVER_PCM_MAX_PACKET_FRAMES;
    unsigned bytes=frames*c->frame_bytes;if(capacity<bytes)return -ENOSPC;
    struct sony_receiver_pcm_packet packet={0};memcpy(packet.track,c->track,sizeof(packet.track));
    packet.frames=frames;packet.bytes=bytes;packet.first_frame=c->taken;
    unsigned char *p=data;
    for(unsigned i=0;i<frames;++i){unsigned pos=(c->head+i)%SONY_RECEIVER_PCM_QUEUE_FRAMES;memcpy(p+i*c->frame_bytes,c->data+pos*c->frame_bytes,c->frame_bytes);}
    *out=packet;c->head=(c->head+frames)%SONY_RECEIVER_PCM_QUEUE_FRAMES;c->queued-=frames;c->taken+=frames;return 0;
}
int sony_receiver_pcm_seal(struct sony_receiver_pcm *c) {if(!c)return -EINVAL;if(c->sealed)return -EALREADY;c->sealed=1;return 0;}
int sony_receiver_pcm_state(const struct sony_receiver_pcm *c,uint64_t *accepted,uint64_t *taken,unsigned *queued,int *sealed) {
    if(!c||!accepted||!taken||!queued||!sealed)return -EINVAL;*accepted=c->accepted;*taken=c->taken;*queued=c->queued;*sealed=c->sealed;return 0;
}
void sony_receiver_pcm_destroy(struct sony_receiver_pcm **p) {if(!p||!*p)return;struct sony_receiver_pcm *c=*p;memset(c,0,sizeof(*c));free(c);*p=NULL;}
