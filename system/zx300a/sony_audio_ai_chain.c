#define _GNU_SOURCE 1
#include "sony_audio_ai_chain.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
struct sony_ai_chain {
    struct sony_chain_bridge *native;
    struct sony_ai_client *client;
    struct sony_ai_chain_state state;
    int busy,finishing,final_block;
    unsigned final_valid;
    const float *tail;unsigned tail_frames;
    sony_ai_chain_sink sink;void *sink_context;
    float queue[8192],ai[4096];
    int pcm[4096],silence[1024];
};
static unsigned occupied;
static int valid_settings(const struct sony_chain_settings *s){
    if(!s||s->volume<0||s->volume>120||(s->direct!=0&&s->direct!=1)||
       (s->eq_enabled!=0&&s->eq_enabled!=1)||(s->dc_enabled!=0&&s->dc_enabled!=1)||
       s->dc_mode<0||s->dc_mode>7||(s->vinyl_enabled!=0&&s->vinyl_enabled!=1)||s->vinyl_mode<0||s->vinyl_mode>7)return 0;
    for(unsigned i=0;i<10;++i)if(s->bands[i]<-20||s->bands[i]>20)return 0;return 1;
}
static int permitted(struct sony_ai_chain *b){
    if(!b)return -EINVAL;
    return b->busy?-EBUSY:0;
}
static int healthy(struct sony_ai_chain *b){
    int r=permitted(b);if(r)return r;
    if(b->state.phase==SONY_AI_CHAIN_FAILED)return -EIO;
    return b->state.phase==SONY_AI_CHAIN_FINISHED?-EPIPE:0;
}
static int terminate(struct sony_ai_chain *b,int reason){
    b->state.phase=SONY_AI_CHAIN_FAILED;b->state.failure=reason?reason:-EIO;
    /* Called only after the synchronous client operation/callback unwound. */
    if(b->client)sony_ai_client_close(&b->client,1);
    return b->state.failure;
}
static int transform(void *context,const float *source,unsigned source_frames,float *output,
    unsigned capacity,unsigned *frames,unsigned *rate){
    struct sony_ai_chain *b=context;*frames=0;*rate=b->state.output_rate;
    if(!b->busy||source_frames!=512||capacity<2048)return -EPROTO;
    unsigned buffered=b->state.buffered_frames;
    if(b->final_block){
        if(!buffered||buffered>=2048)return -EPROTO;
        b->final_valid=buffered;memcpy(output,b->queue,buffered*8);
        memset(output+buffered*2,0,(2048-buffered)*8);
        b->state.buffered_frames=0;*frames=2048;return 0;
    }
    unsigned count=0,actual_rate=0;
    if(b->tail){
        count=b->tail_frames;actual_rate=b->state.output_rate;
        if(!b->finishing||count!=2048)return -EPROTO;
        memcpy(b->ai,b->tail,count*8);
    }else{
        if(b->finishing)return -EPROTO;
        int status=sony_ai_client_process(b->client,source,source_frames,b->ai,2048,&count,&actual_rate);
        if(status)return status;
        b->state.source_frames+=source_frames;
    }
    if(count>2048||actual_rate!=b->state.output_rate||buffered+count>4096)return -EPROTO;
    b->state.ai_output_frames+=count;
    memcpy(b->queue+buffered*2,b->ai,count*8);buffered+=count;
    if(buffered>=2048){*frames=2048;memcpy(output,b->queue,16384);buffered-=2048;memmove(b->queue,b->queue+4096,buffered*8);}
    b->state.buffered_frames=buffered;return 0;
}
static int native_block(struct sony_ai_chain *b,const int *source,unsigned *frames,unsigned *rate){
    b->final_valid=0;
    int r=sony_chain_process_transform(b->native,source,512,transform,b,b->pcm,2048,frames,rate);
    if(r)return -EIO;
    if(*rate!=b->state.output_rate||(*frames!=0&&*frames!=2048))return -EPROTO;
    if(b->final_valid){
        if(*frames!=2048||b->final_valid>=2048)return -EPROTO;
        b->state.trimmed_padding_frames=2048-b->final_valid;*frames=b->final_valid;
    }
    ++b->state.native_packets;if(!*frames)++b->state.zero_packets;return 0;
}
int sony_ai_chain_open(struct sony_ai_chain **out,void *library,const struct sony_chain_settings *settings,
    unsigned rate,struct sony_ai_client **owner){
    if(!out||*out||!library||!valid_settings(settings)||settings->direct||!owner||!*owner||(rate!=44100&&rate!=48000))return -EINVAL;
    unsigned expected=0;
    if(!__atomic_compare_exchange_n(&occupied,&expected,1,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))return -EBUSY;
    int r=0;
    struct sony_ai_chain *b=NULL;
    if(!r){b=calloc(1,sizeof(*b));if(!b)r=-ENOMEM;}
    if(!r)r=sony_chain_open_rate(&b->native,library,settings,rate);
    struct sony_dsee_controls gate={1,1};
    if(!r)r=sony_chain_update_dsee(b->native,&gate);
    if(!r)r=sony_chain_update_normalizer(b->native,0);
    if(r){if(b){sony_chain_abort_close(b->native);free(b);}__atomic_store_n(&occupied,0,__ATOMIC_RELEASE);return r;}
    b->client=*owner;*owner=NULL;
    b->state.phase=SONY_AI_CHAIN_OPEN;b->state.input_rate=rate;b->state.output_rate=rate*4;*out=b;return 0;
}
int sony_ai_chain_process(struct sony_ai_chain *b,const int *source,unsigned frames,int *output,unsigned capacity,
    unsigned *produced,unsigned *rate){
    if(produced)*produced=0;if(rate)*rate=0;
    int r=healthy(b);if(r)return r;
    if(!source||!output||!produced||!rate||frames!=512||capacity<2048)return -EINVAL;
    b->busy=1;unsigned count=0,actual_rate=0;r=native_block(b,source,&count,&actual_rate);b->busy=0;
    if(r)return terminate(b,r);
    memcpy(output,b->pcm,count*8);*produced=count;*rate=actual_rate;
    b->state.returned_frames+=count;b->state.phase=SONY_AI_CHAIN_STREAMING;return 0;
}
static int tail_sink(void *context,const float *pcm,unsigned frames,unsigned rate){
    struct sony_ai_chain *b=context;
    if(!b->busy||!b->finishing||b->tail||!pcm||frames!=2048||rate!=b->state.output_rate)return -EPROTO;
    b->tail=pcm;b->tail_frames=frames;unsigned count=0,actual_rate=0;
    int r=native_block(b,b->silence,&count,&actual_rate);b->tail=NULL;b->tail_frames=0;
    if(!r&&count)r=b->sink(b->sink_context,b->pcm,count,actual_rate);
    if(!r){b->state.returned_frames+=count;b->state.tail_sink_frames+=count;}return r;
}
int sony_ai_chain_finish(struct sony_ai_chain *b,unsigned ms,sony_ai_chain_sink sink,void *context){
    int r=permitted(b);if(r)return r;
    if((ms!=100&&ms!=250)||!sink)return -EINVAL;
    if(b->state.phase==SONY_AI_CHAIN_FINISHED)return ms==b->state.finish_ms?1:-EINVAL;
    if(b->state.phase==SONY_AI_CHAIN_FAILED)return -EIO;
    b->busy=1;b->finishing=1;b->sink=sink;b->sink_context=context;
    r=sony_ai_client_finish(b->client,ms,tail_sink,b);
    struct wai_state state={0};
    if(!r)r=sony_ai_client_state(b->client,&state);
    if(!r&&(state.phase!=WAI_ENDED||wai_join(state.source_lo,state.source_hi)!=b->state.source_frames||
       wai_join(state.produced_lo,state.produced_hi)!=b->state.ai_output_frames))r=-EPROTO;
    if(!r){b->state.padding_source_frames=wai_join(state.padding_lo,state.padding_hi);b->state.ai_tail_ack_frames=wai_join(state.tail_lo,state.tail_hi);}
    if(!r&&b->state.buffered_frames){
        b->final_block=1;unsigned count=0,rate=0;r=native_block(b,b->silence,&count,&rate);b->final_block=0;
        if(!r)r=sink(context,b->pcm,count,rate);
        if(!r){b->state.returned_frames+=count;b->state.tail_sink_frames+=count;}
    }
    if(!r&&(b->state.buffered_frames||b->state.returned_frames!=b->state.ai_output_frames))r=-EPROTO;
    if(!r&&sony_chain_send_eos(b->native))r=-EIO;
    b->sink=NULL;b->sink_context=NULL;b->finishing=0;b->busy=0;
    if(r)return terminate(b,r);
    b->state.phase=SONY_AI_CHAIN_FINISHED;b->state.finish_ms=ms;return 0;
}
int sony_ai_chain_state(struct sony_ai_chain *b,struct sony_ai_chain_state *out){
    int r=permitted(b);if(r)return r;if(!out)return -EINVAL;*out=b->state;return 0;
}
int sony_ai_chain_update(struct sony_ai_chain *b,const struct sony_chain_settings *settings){
    int r=healthy(b);if(r)return r;if(!valid_settings(settings))return -EINVAL;if(settings->direct)return -EAGAIN;
    b->busy=1;r=sony_chain_update(b->native,settings);
    struct sony_dsee_controls gate={1,1};if(!r)r=sony_chain_update_dsee(b->native,&gate);b->busy=0;
    return r?terminate(b,-EIO):0;
}
int sony_ai_chain_update_tone(struct sony_ai_chain *b,const struct sony_tone_settings *settings){
    int r=healthy(b);if(r)return r;if(!settings||(settings->enabled!=0&&settings->enabled!=1))return -EINVAL;
    for(unsigned i=0;i<3;++i)if(settings->gains[i]<-20||settings->gains[i]>20||settings->centerfreq[i]!=2)return -EINVAL;
    b->busy=1;r=sony_chain_update_tone(b->native,settings);b->busy=0;
    return r?terminate(b,-EIO):0;
}
int sony_ai_chain_update_output(struct sony_ai_chain *b,const struct sony_output_controls *settings){
    int r=healthy(b);if(r)return r;if(!sony_output_controls_valid(settings))return -EINVAL;
    b->busy=1;r=sony_chain_update_output(b->native,settings);b->busy=0;
    return r?terminate(b,-EIO):0;
}
int sony_ai_chain_update_normalizer(struct sony_ai_chain *b,int enabled){
    int r=healthy(b);if(r)return r;if(enabled!=0&&enabled!=1)return -EINVAL;
    b->busy=1;r=sony_chain_update_normalizer(b->native,enabled);b->busy=0;return r?terminate(b,-EIO):0;
}
int sony_ai_chain_get_parameter(struct sony_ai_chain *b,const char *key,char *output,unsigned capacity){
    int r=healthy(b);if(r)return r;if(!key||!output||!capacity)return -EINVAL;
    b->busy=1;r=sony_chain_get_parameter(b->native,key,output,capacity);b->busy=0;return r;
}
int sony_ai_chain_close(struct sony_ai_chain **owner,int discard){
    if(!owner||(discard!=0&&discard!=1))return -EINVAL;
    struct sony_ai_chain *b=*owner;if(!b)return 0;
    int r=permitted(b);if(r)return r;
    int failed=b->state.phase==SONY_AI_CHAIN_FAILED;
    if(!discard&&!failed&&b->state.source_frames&&b->state.phase!=SONY_AI_CHAIN_FINISHED)return -EBUSY;
    b->busy=1;
    if(b->client)r=sony_ai_client_close(&b->client,discard||failed);
    if(sony_chain_abort_close(b->native)&&!r)r=-EIO;
    free(b);*owner=NULL;__atomic_store_n(&occupied,0,__ATOMIC_RELEASE);return failed?-EIO:r;
}
