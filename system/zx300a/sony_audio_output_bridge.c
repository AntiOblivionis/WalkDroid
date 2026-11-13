#include "sony_audio_output_bridge.h"
#include "sony_audio_pcm_format.h"
#include "sony_audio_io_guard_v2.h"
extern void *calloc(unsigned int,unsigned int),*dlsym(void *,const char *);
extern void free(void *);
extern unsigned int sony_audio_native_call3(void *,unsigned int,unsigned int,unsigned int);
struct sony_output_bridge {void *factory,*destroy,*open,*configure,*write,*drain,*drop,*close;unsigned int *object;int sealed,faulted,configured;struct sony_output_timing timing;};
#define CALL(fn,a,b,c) ((int)sony_audio_native_call3((fn),(unsigned int)(a),(unsigned int)(b),(unsigned int)(c)))
static void *pcm(struct sony_output_bridge *b) {return b->object ? (void *)b->object[7] : (void *)0;}
int sony_output_abort_close(struct sony_output_bridge **owner) {
    if(!owner || !*owner)return -22;
    struct sony_output_bridge *b=*owner;int result=0;
    if(b->object) {
        struct sony_io_report_v2 report;void *handle=pcm(b);
        if(handle) {
            if(b->configured&&(!b->sealed||b->faulted)) {
                /* Original Drop clears Adler's partial period, makes ALSA
                 * blocking, calls snd_pcm_drop and restores nonblock. Calling
                 * Close/hw_free while RUNNING fails on the real MT8590 PCM.
                 * A successful explicit Drain has already stopped the PCM. */
                if(sony_io_guard_begin_v2(&report,handle))return -16;
                int dropped=CALL(b->drop,b->object,0,0);
                result=sony_io_guard_end_v2(dropped,0);
            }
            if(sony_io_guard_begin_v2(&report,handle))return -16; /* active unrelated scope: do not alter its ownership */
            int native=CALL(b->close,b->object,0,0);
            int closed=sony_io_guard_end_v2(native,0);
            if(!result)result=closed;
        } else if(b->close) result=CALL(b->close,b->object,0,0) ? -5 : 0;
        CALL(b->destroy,b->object,0,0);
    }
    free(b);*owner=(void *)0;return result;
}
int sony_output_open_rate(struct sony_output_bridge **out,void *library,unsigned int sample_rate) {
    if(!out)return -22;
    *out=(void *)0;
    if(!library)return -22;
    struct sony_output_bridge *b=calloc(1,sizeof(*b));if(!b)return -12;
#define GET(member,name) do {b->member=dlsym(library,name);if(!b->member){free(b);return -38;}} while(0)
    GET(factory,"_ZN3pst8audiohal9adleralsa17CreateAudioHalOutERKNS1_13AudioHalOutIdE");
    GET(destroy,"_ZN3pst8audiohal20AudioHalOutAdlerAlsaD0Ev");
    GET(open,"_ZN3pst8audiohal20AudioHalOutAdlerAlsa4OpenEv");
    GET(configure,"_ZN3pst8audiohal20AudioHalOutAdlerAlsa14SetOutputParamERKNS0_13AudioHalParamE");
    GET(write,"_ZN3pst8audiohal20AudioHalOutAdlerAlsa5WriteERKNS0_14AudioHalBufferEPj");
    GET(drain,"_ZN3pst8audiohal20AudioHalOutAdlerAlsa5DrainEv");
    GET(drop,"_ZN3pst8audiohal20AudioHalOutAdlerAlsa4DropEv");
    GET(close,"_ZN3pst8audiohal20AudioHalOutAdlerAlsa5CloseEv");
#undef GET
    unsigned int id=0;CALL(b->factory,&b->object,&id,0);
    if(!b->object){free(b);return -19;}
    *out=b;
    int native_open_failed=CALL(b->open,b->object,0,0) || !pcm(b);
    if(native_open_failed)goto failure;
    /* Partial AudioHalParam ABI recovered for pinned library, not general API. */
    unsigned int param[25]={0};param[0]=sample_rate;param[1]=2;param[4]=4;
    if(CALL(b->configure,b->object,param,0))goto failure;
    /* Read the negotiated ALSA timing used by the playback coordinator. */
    void *alloc_params=dlsym(library,"snd_pcm_hw_params_malloc");
    void *current=dlsym(library,"snd_pcm_hw_params_current");
    void *get_rate=dlsym(library,"snd_pcm_hw_params_get_rate");
    void *get_channels=dlsym(library,"snd_pcm_hw_params_get_channels");
    void *get_format=dlsym(library,"snd_pcm_hw_params_get_format");
    void *get_period=dlsym(library,"snd_pcm_hw_params_get_period_size");
    void *get_buffer=dlsym(library,"snd_pcm_hw_params_get_buffer_size");
    void *free_params=dlsym(library,"snd_pcm_hw_params_free");
    if(!alloc_params||!current||!get_rate||!get_channels||!get_format||!get_period||!get_buffer||!free_params)goto failure;
    void *params=(void *)0;
    if(CALL(alloc_params,&params,0,0)||!params)goto failure;
    unsigned int actual_rate=0,channels=0;int format=-1,direction=0;
    int mismatch=CALL(current,pcm(b),params,0)||CALL(get_rate,params,&actual_rate,&direction)||
        CALL(get_channels,params,&channels,0)||CALL(get_format,params,&format,0)||
        CALL(get_period,params,&b->timing.period_frames,&direction)||CALL(get_buffer,params,&b->timing.buffer_frames,0)||
        0;
    CALL(free_params,params,0,0);
    if(mismatch)goto failure;
    b->configured=1;
    b->timing.sample_rate=actual_rate;b->timing.valid=1;
    return 0;
failure:
    b->faulted=1;
    /* Caller retains ownership if cleanup cannot enter its IO scope. */
    {int closed=sony_output_abort_close(out);
        return closed==-16 ? -16 : native_open_failed ? SONY_OUTPUT_CLEANUP_UNCERTAIN : -5;}
}
int sony_output_open(struct sony_output_bridge **out,void *library) {
    return sony_output_open_rate(out,library,48000);
}
int sony_output_get_timing(struct sony_output_bridge *b,struct sony_output_timing *out) {
    if(!b||!out||!b->configured)return -22;
    *out=b->timing;return 0;
}
int sony_output_write(struct sony_output_bridge *b,const int *data,unsigned int frames) {
    if(!b || !data || !frames || frames>8192)return -22;
    if(b->sealed || b->faulted)return -5;
    unsigned int buffer[3]={(unsigned int)data,frames*8,frames*8},ack=0;
    struct sony_io_report_v2 report;
    if(sony_io_guard_begin_v2(&report,pcm(b)))return -16;
    int native=CALL(b->write,b->object,buffer,&ack);
    int result=sony_io_guard_end_v2(native,1);
    if(result){b->faulted=1;return result;}
    /* Native ack can include earlier pending samples; don't use as accepted count. */
    return 0;
}
int sony_output_drain(struct sony_output_bridge *b) {
    if(!b)return -22;
    if(b->sealed || b->faulted)return -5;
    struct sony_io_report_v2 report;
    if(sony_io_guard_begin_v2(&report,pcm(b)))return -16;
    b->sealed=1;int native=CALL(b->drain,b->object,0,0);
    int result=sony_io_guard_end_v2(native,0);
    if(result)b->faulted=1;
    return result;
}
