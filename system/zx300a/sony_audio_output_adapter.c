#include "sony_audio_output_adapter.h"
#include "sony_audio_pcm_format.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
struct wdc_output_adapter {
    struct wdc_client *client;
    struct wdc_adapter_state state;
    unsigned int bits;
    unsigned int quantum;
    int floating;
    int samples[SONY_PCM_MAX_SOURCE_FRAMES*2];
};
int wdc_adapter_open(struct wdc_output_adapter **out,struct wdc_client *client,const struct sony_chain_settings *settings,
    unsigned int rate,unsigned int channels,unsigned int bits) {
    if(!out)return -2;
    *out=NULL;
    if(!client||!settings||channels!=2||
       (bits!=WDC_PCM_S16_LE&&bits!=WDC_PCM_S32_LE&&bits!=WDC_PCM_FLOAT_LE))return -2;
    struct wdc_output_adapter *a=calloc(1,sizeof(*a));if(!a)return -100;
    int status=wdc_client_open_output_rate(client,settings,rate);if(status){free(a);return status;}
    a->client=client;a->floating=bits==WDC_PCM_FLOAT_LE;a->quantum=sony_pcm_source_frames(rate);
    a->bits=a->floating?32:bits;*out=a;return 0;
}
static int32_t native_float(float value) {
    uint32_t word;memcpy(&word,&value,4);
    if((word&0x7fffffffu)>0x7f800000u)return 0;
    if(value>=1.0f)return INT32_MAX;
    if(value<=-1.0f)return INT32_MIN;
    /* Power-of-two scaling changes representation only. The floating source
     * has at most 24 significant bits; no intermediate S16 or software gain. */
    return (int32_t)(value*2147483648.0f);
}
static int submit(struct wdc_output_adapter *a) {
    int status=wdc_client_process_output(a->client,a->samples,a->quantum);
    if(status){a->state.faulted=1;return status;}
    a->state.native_frames_accepted+=a->quantum;a->state.pending_frames=0;return 0;
}
int wdc_adapter_write(struct wdc_output_adapter *a,const void *input,unsigned int bytes,unsigned int *copied_frames) {
    if(!a||!copied_frames||(!input&&bytes)||bytes%(a->bits/4))return -2;
    if(a->state.sealed||a->state.faulted)return -3;
    *copied_frames=0;
    const unsigned char *p=input;unsigned int frames=bytes/(a->bits/4);
    for(unsigned int f=0;f<frames;++f) {
        unsigned int offset=a->state.pending_frames*2;
        for(unsigned int c=0;c<2;++c) {
            if(a->bits==16){int16_t value;memcpy(&value,p,2);a->samples[offset+c]=(int32_t)value*65536;p+=2;}
            else if(a->floating){float value;memcpy(&value,p,4);a->samples[offset+c]=native_float(value);p+=4;}
            else {memcpy(&a->samples[offset+c],p,4);p+=4;}
        }
        ++a->state.pending_frames;++a->state.source_frames_copied;++*copied_frames;
        if(a->state.pending_frames==a->quantum){int status=submit(a);if(status)return status;}
    }
    return 0;
}
int wdc_adapter_finish(struct wdc_output_adapter *a,unsigned int ms) {
    if(!a||(ms!=100&&ms!=250))return -2;
    if(a->state.sealed||a->state.faulted)return -3;
    a->state.sealed=1;
    if(!a->state.source_frames_copied)return 0; /* Empty session opens no output. */
    if(a->state.pending_frames) {
        unsigned int count=a->quantum-a->state.pending_frames;
        memset(a->samples+a->state.pending_frames*2,0,count*8);
        a->state.zero_padding_frames=count;
        int status=submit(a);if(status)return status;
    }
    int status=wdc_client_finish_output(a->client,ms);
    if(status)a->state.faulted=1;
    return status;
}
int wdc_adapter_get_state(struct wdc_output_adapter *a,struct wdc_adapter_state *out) {
    if(!a||!out)return -2;
    *out=a->state;return 0;
}
int wdc_adapter_abort_close(struct wdc_output_adapter **owner) {
    if(!owner||!*owner)return -2;
    struct wdc_output_adapter *a=*owner;int status=wdc_client_close_stream(a->client);
    free(a);*owner=NULL;return status;
}
