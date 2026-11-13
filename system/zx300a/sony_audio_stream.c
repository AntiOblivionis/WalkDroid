#include "sony_audio_stream.h"
#include "sony_audio_pcm_format.h"
#include "sony_audio_output_bridge.h"
#ifdef SONY_STREAM_AI
#include "sony_audio_ai_chain.h"
#endif
extern void *calloc(unsigned int,unsigned int);
extern void free(void *);
struct sony_stream {
    struct sony_chain_bridge *chain;
#ifdef SONY_STREAM_AI
    struct sony_ai_chain *ai;
#endif
    struct sony_output_bridge *output;
    void *output_library;
    struct sony_stream_state state;
    int last_error;
    unsigned int input_rate;
    int pcm[32768];
};
static int fail(struct sony_stream *s,int error){s->state.faulted=1;s->last_error=error;return error;}
#ifdef SONY_STREAM_AI
static int ai_owner(struct sony_stream *s) {
    struct sony_ai_chain_state state;
    return s->ai?sony_ai_chain_state(s->ai,&state):0;
}
static int ai_result(struct sony_stream *s,int status) {
    struct sony_ai_chain_state state;
    if(status&&!sony_ai_chain_state(s->ai,&state)&&state.phase==SONY_AI_CHAIN_FAILED)return fail(s,status);
    return status;
}
#endif
static int send(void *context,const int *pcm,unsigned int frames,unsigned int rate) {
    struct sony_stream *s=context;
    if(s->state.faulted)return -5;
#ifdef SONY_STREAM_AI
    /* Startup has real source input but no AI PCM yet. Do not open hardware
     * for zero samples; the first actual block selects its acknowledged rate. */
    if(s->ai&&!frames)return 0;
#endif
    if(s->output&&s->state.sample_rate!=rate) {
        /* The newly processed packet remains in s->pcm until the old PCM has
         * drained and released ownership. Never retry consumed input or Drop
         * a successful drain; any ambiguous failure seals this stream. */
        int status=sony_output_drain(s->output);
        if(status)return fail(s,status);
        status=sony_output_abort_close(&s->output);
        if(status)return fail(s,status);
    }
    if(!s->output) {
        int status=sony_output_open_rate(&s->output,s->output_library,rate);
        if(status)return fail(s,status);
        s->state.sample_rate=rate;
    }
    unsigned int done=0;
    while(done<frames) {
        unsigned int count=frames-done;if(count>8192)count=8192;
        int status=sony_output_write(s->output,pcm+done*2,count);
        if(status)return fail(s,status);
        done+=count;s->state.output_frames+=count;
    }
    return 0;
}
int sony_stream_open_rate(struct sony_stream **out,void *sound,void *output,
    const struct sony_chain_settings *settings,unsigned int input_rate) {
    if(!out)return -22;
    *out=(void *)0;
    if(!sound||!output||!settings)return -22;
    struct sony_stream *s=calloc(1,sizeof(*s));if(!s)return -12;
    int status=sony_chain_open_rate(&s->chain,sound,settings,input_rate);
    if(status){free(s);return status;}
    s->input_rate=input_rate;
    s->output_library=output;*out=s;return 0;
}
int sony_stream_open(struct sony_stream **out,void *sound,void *output,const struct sony_chain_settings *settings) {
    return sony_stream_open_rate(out,sound,output,settings,48000);
}
#ifdef SONY_STREAM_AI
int sony_stream_open_ai(struct sony_stream **out,void *sound,void *output,
    const struct sony_chain_settings *settings,unsigned int rate,struct sony_ai_client **client) {
    if(!out||*out||!sound||!output||!settings||settings->direct||!client||!*client||(rate!=44100&&rate!=48000))return -22;
    struct sony_stream *s=calloc(1,sizeof(*s));if(!s)return -12;
    int status=sony_ai_chain_open(&s->ai,sound,settings,rate,client);
    if(status){free(s);return status;}
    s->input_rate=rate;s->output_library=output;*out=s;return 0;
}
int sony_stream_get_ai_state(struct sony_stream *s,struct sony_ai_chain_state *out) {
    return !s||!s->ai||!out?-22:sony_ai_chain_state(s->ai,out);
}
#endif
int sony_stream_update_tone(struct sony_stream *s,const struct sony_tone_settings *tone) {
    if(!s||!tone)return -22;
#ifdef SONY_STREAM_AI
    {int r=ai_owner(s);if(r)return r;}
    if(s->ai)return s->state.sealed||s->state.faulted?-5:ai_result(s,sony_ai_chain_update_tone(s->ai,tone));
#endif
    if(s->state.sealed||s->state.faulted)return -5;
    int status=sony_chain_update_tone(s->chain,tone);
    return status?fail(s,status):0;
}
int sony_stream_update_output(struct sony_stream *s,const struct sony_output_controls *output) {
    if(!s||!sony_output_controls_valid(output))return -22;
#ifdef SONY_STREAM_AI
    {int r=ai_owner(s);if(r)return r;}
    if(s->ai)return s->state.sealed||s->state.faulted?-5:ai_result(s,sony_ai_chain_update_output(s->ai,output));
#endif
    if(s->state.sealed||s->state.faulted)return -5;
    int status=sony_chain_update_output(s->chain,output);
    return status?fail(s,status):0;
}
int sony_stream_update(struct sony_stream *s,const struct sony_chain_settings *settings) {
    if(!s||!settings)return -22;
#ifdef SONY_STREAM_AI
    {int r=ai_owner(s);if(r)return r;}
    if(s->ai)return s->state.sealed||s->state.faulted?-5:ai_result(s,sony_ai_chain_update(s->ai,settings));
#endif
    if(s->state.sealed||s->state.faulted)return -5;
    int status=sony_chain_update(s->chain,settings);
    return status?fail(s,status):0;
}
int sony_stream_update_dsee(struct sony_stream *s,const struct sony_dsee_controls *dsee) {
    if(!s||!sony_dsee_controls_valid(dsee))return -22;
#ifdef SONY_STREAM_AI
    {int r=ai_owner(s);if(r)return r;}
    if(s->ai){
        if(s->state.sealed||s->state.faulted)return -5;
        return dsee->enabled==1&&dsee->mode==1?0:-11;
    }
#endif
    if(s->state.sealed||s->state.faulted)return -5;
    int status=sony_chain_update_dsee(s->chain,dsee);
    if(status)return fail(s,status);
    return 0;
}
int sony_stream_update_normalizer(struct sony_stream *s,int enabled) {
    if(!s||(enabled!=0&&enabled!=1))return -22;
#ifdef SONY_STREAM_AI
    {int r=ai_owner(s);if(r)return r;}
    if(s->ai)return s->state.sealed||s->state.faulted?-5:ai_result(s,sony_ai_chain_update_normalizer(s->ai,enabled));
#endif
    if(s->state.sealed||s->state.faulted)return -5;
    int status=sony_chain_update_normalizer(s->chain,enabled);
    return status?fail(s,status):0;
}
int sony_stream_query(struct sony_stream *s,const char *key,char *out,unsigned int capacity) {
    if(!s||!key||!out||!capacity)return -22;
#ifdef SONY_STREAM_AI
    {int r=ai_owner(s);if(r)return r;}
    if(s->ai)return s->state.faulted?-5:sony_ai_chain_get_parameter(s->ai,key,out,capacity);
#endif
    if(s->state.faulted)return -5;
    return sony_chain_get_parameter(s->chain,key,out,capacity);
}
int sony_stream_process(struct sony_stream *s,const int *input,unsigned int frames) {
    if(!s||!input||frames!=sony_pcm_source_frames(s->input_rate))return -22;
#ifdef SONY_STREAM_AI
    {int r=ai_owner(s);if(r)return r;}
#endif
    if(s->state.sealed||s->state.faulted)return -5;
    unsigned int produced=0,rate=0;
    int status;
#ifdef SONY_STREAM_AI
    if(s->ai)status=sony_ai_chain_process(s->ai,input,frames,s->pcm,16384,&produced,&rate);
    else
#endif
    status=sony_chain_process_expanded(s->chain,input,s->pcm,frames,16384,&produced,&rate);
    if(status)return fail(s,status);
    status=send(s,s->pcm,produced,rate);
    if(status)return status;
    s->state.input_frames+=frames;return 0;
}
int sony_stream_finish(struct sony_stream *s,unsigned int ms) {
    if(!s||(ms!=100&&ms!=250))return -22;
#ifdef SONY_STREAM_AI
    {int r=ai_owner(s);if(r)return r;}
#endif
    if(s->state.sealed||s->state.faulted)return -5;
#ifdef SONY_STREAM_AI
    if(s->ai){
        s->state.sealed=1;int status=sony_ai_chain_finish(s->ai,ms,send,s);
        if(status)return fail(s,s->last_error?s->last_error:status);
        if(s->output){status=sony_output_drain(s->output);if(status)return fail(s,status);s->state.drain_accepted=1;}
        return 0;
    }
#endif
    if(!s->output)return -22;
    s->state.sealed=1;unsigned int ack=0;
    int status=sony_chain_render_silence_expanded(s->chain,ms,send,s,&ack);
    if(status)return fail(s,s->last_error?s->last_error:status);
    status=sony_chain_send_eos(s->chain);if(status)return fail(s,status);
    status=sony_output_drain(s->output);if(status)return fail(s,status);
    s->state.drain_accepted=1;return 0;
}
int sony_stream_get_state(struct sony_stream *s,struct sony_stream_state *out) {
    if(!s||!out)return -22;
#ifdef SONY_STREAM_AI
    {int r=ai_owner(s);if(r)return r;}
#endif
    *out=s->state;return 0;
}
int sony_stream_get_timing(struct sony_stream *s,struct sony_output_timing *out) {
    if(!s||!out)return -22;
#ifdef SONY_STREAM_AI
    {int r=ai_owner(s);if(r)return r;}
#endif
    if(!s->output){struct sony_output_timing empty={0};*out=empty;return 0;}
    return sony_output_get_timing(s->output,out);
}
int sony_stream_abort_close(struct sony_stream **owner) {
    if(!owner||!*owner)return -22;
    struct sony_stream *s=*owner;int result=0;
#ifdef SONY_STREAM_AI
    {int r=ai_owner(s);if(r)return r;}
#endif
    if(s->output) {
        result=sony_output_abort_close(&s->output);
        if(result==-16)return result;
    }
    int chain;
#ifdef SONY_STREAM_AI
    if(s->ai){
        struct sony_ai_chain_state state;sony_ai_chain_state(s->ai,&state);
        chain=sony_ai_chain_close(&s->ai,result||state.phase!=SONY_AI_CHAIN_FINISHED);
        if(s->ai)return chain;
    }else
#endif
    chain=sony_chain_abort_close(s->chain);
    if(!result)result=chain;
    free(s);*owner=(void *)0;return result;
}
