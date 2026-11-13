#include "sony_audio_usb_boundary.h"
#ifdef __KERNEL__
#include <linux/errno.h>
#include <linux/string.h>
#else
#include <errno.h>
#include <string.h>
#endif
#define U64_MAX_VALUE ((sony_ub_u64)~(sony_ub_u64)0)
#ifndef SONY_USB_BOUNDARY_READER_ONLY
static void sony_ub_put32(unsigned char *p,sony_ub_u32 value){unsigned i;for(i=0;i<4;++i)p[i]=(unsigned char)(value>>(8*i));}
static void sony_ub_put64(unsigned char *p,sony_ub_u64 value){unsigned i;for(i=0;i<8;++i)p[i]=(unsigned char)(value>>(8*i));}
int sony_usb_boundary_encode(const struct sony_usb_boundary *s,char *out,unsigned cap){
    unsigned n,at=0;unsigned char raw[SONY_USB_BOUNDARY_BYTES];
    if(!s||!out)return -EINVAL;if(cap<SONY_USB_BOUNDARY_TEXT_BYTES)return -EMSGSIZE;
#define PUT32(value) do{sony_ub_put32(raw+at,(sony_ub_u32)(value));at+=4;}while(0)
#define PUT64(value) do{sony_ub_put64(raw+at,(sony_ub_u64)(value));at+=8;}while(0)
    PUT32(s->magic);PUT32(s->version);PUT32(s->bytes);PUT32(s->flags);
    PUT64(s->instance);PUT64(s->source_epoch);PUT64(s->capture_id);PUT64(s->capture_epoch);
    PUT64(s->written_frames);PUT64(s->final_frames);
    PUT32(s->format);PUT32(s->rate);PUT32(s->bits);PUT32(s->capture_format);PUT32(s->capture_rate);PUT32(s->capture_bits);
    PUT32(s->play_sequence);PUT32(s->last_sequence);PUT32(s->final_sequence);PUT32(s->final_reason);
    PUT32(s->error);PUT32(s->capture_sequence);
#undef PUT32
#undef PUT64
    out[0]='W';out[1]='D';out[2]='B';out[3]='1';out[4]=' ';
    for(n=0;n<SONY_USB_BOUNDARY_BYTES;++n){unsigned a=raw[n]>>4,b=raw[n]&15;out[5+n*2]=(char)(a+(a<10?'0':'a'-10));out[6+n*2]=(char)(b+(b<10?'0':'a'-10));}
    out[SONY_USB_BOUNDARY_TEXT_BYTES-1]='\n';return SONY_USB_BOUNDARY_TEXT_BYTES;
}
static void sony_ub_freeze(struct sony_usb_boundary *s,unsigned reason,sony_ub_u32 seq){
    if((s->flags&SONY_USB_BOUNDARY_CAPTURE_LIVE)&&!(s->flags&SONY_USB_BOUNDARY_FINAL)){
        s->flags|=SONY_USB_BOUNDARY_FINAL;s->final_frames=s->written_frames;s->final_sequence=seq;s->final_reason=reason;
    }
}
int sony_usb_boundary_bind(struct sony_usb_boundary *s){
    sony_ub_u64 instance;if(!s)return -EINVAL;
    instance=s->instance+1;memset(s,0,sizeof(*s));s->magic=SONY_USB_BOUNDARY_MAGIC;s->version=1;s->bytes=SONY_USB_BOUNDARY_BYTES;s->flags=SONY_USB_BOUNDARY_BOUND;s->instance=instance;return 0;
}
int sony_usb_boundary_unbind(struct sony_usb_boundary *s){if(!s)return -EINVAL;sony_ub_freeze(s,SONY_USB_BOUNDARY_DISCONNECT,s->last_sequence);s->flags&=~(SONY_USB_BOUNDARY_BOUND|SONY_USB_BOUNDARY_SOURCE_LIVE|SONY_USB_BOUNDARY_CAPTURE_LIVE);return 0;}
int sony_usb_boundary_play(struct sony_usb_boundary *s,sony_ub_u32 seq,sony_ub_u32 f,sony_ub_u32 r,sony_ub_u32 b){
    if(!s)return -EINVAL;
    sony_ub_freeze(s,SONY_USB_BOUNDARY_CHANGE,seq);++s->source_epoch;s->flags|=SONY_USB_BOUNDARY_SOURCE_LIVE|SONY_USB_BOUNDARY_SEQUENCE_SEEN;s->format=f;s->rate=r;s->bits=b;s->play_sequence=s->last_sequence=seq;return 0;
}
int sony_usb_boundary_stop(struct sony_usb_boundary *s,sony_ub_u32 seq,unsigned reason,int error){
    if(!s)return -EINVAL;
    sony_ub_freeze(s,reason,seq);s->flags=(s->flags&~SONY_USB_BOUNDARY_SOURCE_LIVE)|SONY_USB_BOUNDARY_SEQUENCE_SEEN;s->last_sequence=seq;if(error&&!s->error)s->error=error;return 0;
}
int sony_usb_boundary_capture_start(struct sony_usb_boundary *s,sony_ub_u64 epoch){
    if(!s)return -EINVAL;
    ++s->capture_id;s->capture_epoch=epoch;s->capture_sequence=s->play_sequence;s->capture_format=s->format;s->capture_rate=s->rate;s->capture_bits=s->bits;s->written_frames=s->final_frames=0;s->final_sequence=s->final_reason=0;
    s->flags=(s->flags|SONY_USB_BOUNDARY_CAPTURE_LIVE)&~SONY_USB_BOUNDARY_FINAL;return 0;
}
int sony_usb_boundary_capture_stop(struct sony_usb_boundary *s,sony_ub_u64 id){(void)id;if(!s)return -EINVAL;s->flags&=~SONY_USB_BOUNDARY_CAPTURE_LIVE;return 0;}
int sony_usb_boundary_copy(struct sony_usb_boundary *s,sony_ub_u64 epoch,sony_ub_u32 frames){
    (void)epoch;if(!s)return -EINVAL;s->written_frames+=frames;return 0;
}
#endif
#ifndef __KERNEL__
static int sony_ub_digit(char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;}
static sony_ub_u32 sony_ub_get32(const unsigned char *p){sony_ub_u32 value=0;unsigned i;for(i=0;i<4;++i)value|=(sony_ub_u32)p[i]<<(8*i);return value;}
static sony_ub_u64 sony_ub_get64(const unsigned char *p){sony_ub_u64 value=0;unsigned i;for(i=0;i<8;++i)value|=(sony_ub_u64)p[i]<<(8*i);return value;}
int sony_usb_boundary_parse(const char *text,unsigned size,struct sony_usb_boundary *out){
    struct sony_usb_boundary s={0};unsigned char raw[SONY_USB_BOUNDARY_BYTES];unsigned n,at=0;
    if(!text||!out)return -EINVAL;if(size<SONY_USB_BOUNDARY_TEXT_BYTES)return -EMSGSIZE;
    for(n=0;n<SONY_USB_BOUNDARY_BYTES;++n){int a=sony_ub_digit(text[5+n*2]),b=sony_ub_digit(text[6+n*2]);if(a<0||b<0)return -EPROTO;raw[n]=(unsigned char)(a*16+b);}
#define GET32(field) do{s.field=sony_ub_get32(raw+at);at+=4;}while(0)
#define GET64(field) do{s.field=sony_ub_get64(raw+at);at+=8;}while(0)
    GET32(magic);GET32(version);GET32(bytes);GET32(flags);
    GET64(instance);GET64(source_epoch);GET64(capture_id);GET64(capture_epoch);
    GET64(written_frames);GET64(final_frames);
    GET32(format);GET32(rate);GET32(bits);GET32(capture_format);GET32(capture_rate);GET32(capture_bits);
    GET32(play_sequence);GET32(last_sequence);GET32(final_sequence);GET32(final_reason);
    s.error=(sony_ub_s32)sony_ub_get32(raw+at);at+=4;GET32(capture_sequence);
#undef GET32
#undef GET64
    *out=s;return 0;
}
int sony_usb_boundary_reader_start(struct sony_usb_boundary_reader *r,const struct sony_usb_boundary *s,sony_ub_u32 seq,sony_ub_u32 hz,sony_ub_u32 bits,sony_ub_u32 ring){
    (void)seq;(void)hz;(void)bits;if(!r||!s||!ring)return -EINVAL;
    *r=(struct sony_usb_boundary_reader){s->instance,s->capture_id,s->capture_epoch,0,s->capture_format,s->capture_rate,s->capture_bits,s->capture_sequence,ring,0,0};return 0;
}
int sony_usb_boundary_reader_limit(struct sony_usb_boundary_reader *r,const struct sony_usb_boundary *s,unsigned maximum,unsigned *frames){
    sony_ub_u64 available;if(!r||!s||!frames||!maximum||!r->ring_frames)return -EINVAL;
    if(s->written_frames<r->consumed_frames)r->consumed_frames=0;
    available=s->written_frames-r->consumed_frames;
    if(available>r->ring_frames){available=r->ring_frames;r->consumed_frames=s->written_frames-available;}
    if(!available)return s->flags&SONY_USB_BOUNDARY_FINAL?1:-EAGAIN;
    *frames=available<maximum?(unsigned)available:maximum;return 0;
}
int sony_usb_boundary_reader_commit(struct sony_usb_boundary_reader *r,const struct sony_usb_boundary *before,const struct sony_usb_boundary *after,unsigned requested,unsigned count){
    (void)before;(void)after;if(!r)return -EINVAL;
    if(count>requested)count=requested;
    if(r->consumed_frames>U64_MAX_VALUE-count)r->consumed_frames=U64_MAX_VALUE;else r->consumed_frames+=count;
    return 0;
}
#endif
