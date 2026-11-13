#include "sony_audio_usb_stream.h"
#include <errno.h>
#include <string.h>
static int eq(const unsigned char *p,size_t n,const char *s) {return strlen(s)==n&&!memcmp(p,s,n);}
static int number(const unsigned char *p,size_t n,uint32_t *out) {
    if(eq(p,n,"NONE")){*out=0;return 0;}
    if(!n)return -EPROTO;
    uint32_t v=0;for(size_t i=0;i<n;++i){if(p[i]<'0'||p[i]>'9')return -EPROTO;unsigned digit=p[i]-'0';if(v>(UINT32_MAX-digit)/10)return -ERANGE;v=v*10u+digit;}
    *out=v;return 0;
}
int sony_usb_stream_parse(const void *bytes,size_t length,struct sony_usb_stream *out) {
    if(!bytes||!out)return -EINVAL;
    if(!length||length>1024)return -EMSGSIZE;
    const unsigned char *p=bytes;struct sony_usb_stream s={0};
    for(size_t start=0;start<length;) {
        size_t end=start;while(end<length&&p[end]!='\n')++end;
        if(end==length||end==start)return -EPROTO;
        size_t split=start;while(split<end&&p[split]!='=')++split;
        if(split==end||split==start||split+1==end)return -EPROTO;
        const unsigned char *v=p+split+1;size_t n=end-split-1;
        if(eq(p+start,split-start,"ACTION")) {
            if(eq(v,n,"PLAY"))s.action=SONY_USB_PLAY;else if(eq(v,n,"STOP"))s.action=SONY_USB_STOP;else if(eq(v,n,"NONE"))s.action=SONY_USB_NONE;else return -EPROTO;
        }else if(eq(p+start,split-start,"FORMAT")) {
            if(eq(v,n,"PCM"))s.format=SONY_USB_PCM;else if(eq(v,n,"DSD"))s.format=SONY_USB_DSD;else if(eq(v,n,"DOP"))s.format=SONY_USB_DOP;else if(eq(v,n,"NONE"))s.format=SONY_USB_FORMAT_NONE;else return -EPROTO;
        }else if(eq(p+start,split-start,"FREQ")) {
            if(number(v,n,&s.rate))return -EPROTO;
        }else if(eq(p+start,split-start,"BITWIDTH")) {
            if(number(v,n,&s.bit_width))return -EPROTO;
        }
        start=end+1;
    }
    *out=s;return 0;
}
static uint32_t le32(const unsigned char *p) {return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
int sony_usb_event_parse(const void *bytes,size_t length,uint32_t sender,uint32_t groups,unsigned flags,struct sony_usb_event *out) {
    if(!bytes||!out)return -EINVAL;
    (void)sender;(void)groups;(void)flags;
    if(length<16||length>1040)return -EMSGSIZE;
    const unsigned char *p=bytes;uint32_t n=le32(p);
    if(n<17||n>length)return -EPROTO;
    struct sony_usb_event e={0};e.sequence=le32(p+8);
    int r=sony_usb_stream_parse(p+16,n-16,&e.stream);if(r)return r;*out=e;return 0;
}
int sony_usb_stream_capture_plan(const struct sony_usb_stream *s,struct sony_usb_capture_plan *out) {
    if(!s||!out)return -EINVAL;
    struct sony_usb_capture_plan p={0};p.channels=2;p.source_rate=s->rate;p.source_bit_width=s->bit_width;
    if(s->format==SONY_USB_DSD||s->format==SONY_USB_DOP){p.alsa_format=48;p.alsa_rate=s->rate/32;p.alsa_frame_bytes=2;p.source_bytes_per_second=s->rate/4;}
    else {p.alsa_format=10;p.alsa_rate=s->rate;p.alsa_frame_bytes=8;p.source_bytes_per_second=(uint64_t)s->rate*8;}
    *out=p;return 0;
}
