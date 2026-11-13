#include "sony_audio_dsd_native_sink.h"
#include "sony_audio_dsd_io_guard.h"
#include <dlfcn.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
extern unsigned sony_audio_native_call3(void *,unsigned,unsigned,unsigned);
#define CALL(f,a,b,c) ((int)sony_audio_native_call3((f),(unsigned)(a),(unsigned)(b),(unsigned)(c)))
struct sony_dsd_native_sink {
    void *library,*alsa,*factory,*destroy,*open,*configure,*drain,*drop,*close;
    unsigned *object;
    struct sony_dsd_power_bridge *power;
    struct sony_dsd_native_writer *writer;
    struct sony_dsd_native_sink_state state;
    int busy,closing,close_begun,drop_attempted,write_attempted,native_close_result,unsafe_object;
};
static int lock(struct sony_dsd_native_sink *s){return !s?-EINVAL:__atomic_exchange_n(&s->busy,1,__ATOMIC_ACQUIRE)?-EBUSY:0;}
static int unlock(struct sony_dsd_native_sink *s,int rc){__atomic_store_n(&s->busy,0,__ATOMIC_RELEASE);return rc;}
static int failed(struct sony_dsd_native_sink *s,int rc){if(!s->state.first_error)s->state.first_error=rc;s->state.failed=1;return rc;}
static void *pcm(struct sony_dsd_native_sink *s){return s->object?(void *)s->object[7]:NULL;}
static int owner_thread(struct sony_dsd_native_sink *s){
    if(!s->power)return -EINVAL;
    return sony_dsd_power_state(s->power,&s->state.power);
}
static int geometry(struct sony_dsd_native_sink *s,unsigned bit_rate){
    void *alsa=s->alsa,*alloc=dlsym(alsa,"snd_pcm_hw_params_malloc"),*current=dlsym(alsa,"snd_pcm_hw_params_current"),*freep=dlsym(alsa,"snd_pcm_hw_params_free");
    void *get_rate=dlsym(alsa,"snd_pcm_hw_params_get_rate"),*get_channels=dlsym(alsa,"snd_pcm_hw_params_get_channels"),
         *get_format=dlsym(alsa,"snd_pcm_hw_params_get_format"),*get_access=dlsym(alsa,"snd_pcm_hw_params_get_access"),
         *get_period=dlsym(alsa,"snd_pcm_hw_params_get_period_size"),*get_buffer=dlsym(alsa,"snd_pcm_hw_params_get_buffer_size");
    long (*frame_bytes)(void *,long)=dlsym(alsa,"snd_pcm_frames_to_bytes");
    int (*physical)(int)=dlsym(alsa,"snd_pcm_format_physical_width");
    const char *(*name)(int)=dlsym(alsa,"snd_pcm_format_name");
    if(!alloc||!current||!freep||!get_rate||!get_channels||!get_format||!get_access||!get_period||!get_buffer||!frame_bytes||!physical||!name)return -ENOSYS;
    void *params=NULL;int rc=CALL(alloc,&params,0,0);if(rc<0||!params)return rc<0?rc:-EPROTO;
    unsigned rate=0,channels=0,period=0,buffer=0;int format=-1,access=-1,direction=0;
    rc=CALL(current,pcm(s),params,0);
    if(!rc)rc=CALL(get_rate,params,&rate,&direction);
    if(!rc)rc=CALL(get_channels,params,&channels,0);
    if(!rc)rc=CALL(get_format,params,&format,0);
    if(!rc)rc=CALL(get_access,params,&access,0);
    if(!rc)rc=CALL(get_period,params,&period,&direction);
    if(!rc)rc=CALL(get_buffer,params,&buffer,0);
    CALL(freep,params,0,0);if(rc)return rc<0?rc:-EPROTO;
    long bytes=frame_bytes(pcm(s),4096);int width=physical(format);const char *label=name(format);
    (void)label;
    s->state.geometry=(struct sony_dsd_native_geometry){bit_rate,rate,channels,(unsigned)format,(unsigned)access,period,buffer,(unsigned)bytes,(unsigned)width};
    return 0;
}
int sony_dsd_native_sink_open(struct sony_dsd_native_sink **out,void *library,void *alsa,unsigned profile,unsigned rate,unsigned low,const struct sony_dsd_power_backend *backend){
    if(!out||*out||!library||!alsa||profile>2||(rate!=2822400&&rate!=5644800&&rate!=11289600)||low>1||
       !backend||!backend->acquire||!backend->output_closed||!backend->release)return -EINVAL;
    struct sony_dsd_native_sink *s=calloc(1,sizeof(*s));if(!s)return -ENOMEM;s->library=library;s->alsa=alsa;
#define GET(member,symbol) do{s->member=dlsym(library,symbol);if(!s->member){free(s);return -ENOSYS;}}while(0)
    GET(factory,"_ZN3pst8audiohal9adleralsa17CreateAudioHalOutERKNS1_13AudioHalOutIdE");
    GET(destroy,"_ZN3pst8audiohal20AudioHalOutAdlerAlsaD0Ev");
    GET(open,"_ZN3pst8audiohal20AudioHalOutAdlerAlsa4OpenEv");
    GET(configure,"_ZN3pst8audiohal20AudioHalOutAdlerAlsa14SetOutputParamERKNS0_13AudioHalParamE");
    GET(drain,"_ZN3pst8audiohal20AudioHalOutAdlerAlsa5DrainEv");
    GET(drop,"_ZN3pst8audiohal20AudioHalOutAdlerAlsa4DropEv");
    GET(close,"_ZN3pst8audiohal20AudioHalOutAdlerAlsa5CloseEv");
#undef GET
    *out=s;unsigned id=low?5:4;CALL(s->factory,&s->object,&id,0);
    if(!s->object)return failed(s,-ENODEV);
    int rc=sony_dsd_power_bind_profile(&s->power,s->object,backend,profile);if(rc)return failed(s,rc);
    sony_dsd_power_set_library(s->power,library);
    int native=CALL(s->open,s->object,0,0);rc=sony_dsd_power_open_result(s->power,native);
    if(rc)return failed(s,rc);s->state.opened=1;
    unsigned param[25]={0};param[0]=rate;param[1]=2;param[4]=1;
    if(CALL(s->configure,s->object,param,0))return failed(s,-EIO);s->state.configured=1;
    rc=geometry(s,rate);if(!rc)rc=sony_dsd_native_writer_bind(&s->writer,library,s->object,rate);
    return rc?failed(s,rc):0;
}
int sony_dsd_native_sink_write(struct sony_dsd_native_sink *s,const void *data,unsigned bytes){
    if(!data||!bytes||bytes>SONY_DSD_IO_MAX_BYTES||bytes%SONY_DSD_IO_BLOCK_BYTES||(uintptr_t)data>UINTPTR_MAX-bytes)return -EINVAL;
    int rc=lock(s);if(rc)return rc;
    if(s->closing||s->state.sealed||s->state.failed||!s->writer)return unlock(s,-EPIPE);
    rc=owner_thread(s);if(rc)return unlock(s,rc);
    s->write_attempted=1;rc=sony_dsd_native_writer_write(s->writer,data,bytes);
    int saved=sony_dsd_native_writer_state(s->writer,&s->state.writer);
    if(!rc)rc=saved;if(rc&&rc!=-EBUSY)failed(s,rc);
    return unlock(s,rc);
}
int sony_dsd_native_sink_drain(struct sony_dsd_native_sink *s){
    int rc=lock(s);if(rc)return rc;
    if(s->closing||s->state.sealed||s->state.failed||!s->writer||!s->state.writer.completed_bytes)return unlock(s,-EINVAL);
    rc=owner_thread(s);if(rc)return unlock(s,rc);
    rc=sony_dsd_control_begin(&s->state.drain,pcm(s),SONY_DSD_CTL_DRAIN);if(rc)return unlock(s,rc);
    s->state.sealed=1;int native=CALL(s->drain,s->object,0,0);rc=sony_dsd_control_end(native);
    s->state.drain_status=rc;if(rc)failed(s,rc);else s->state.drain_accepted=1;
    return unlock(s,rc);
}
int sony_dsd_native_sink_state(struct sony_dsd_native_sink *s,struct sony_dsd_native_sink_state *out){
    if(!out)return -EINVAL;int rc=lock(s);if(rc)return rc;
    if(s->power)rc=owner_thread(s);
    if(!rc&&s->writer)rc=sony_dsd_native_writer_state(s->writer,&s->state.writer);
    if(!rc)*out=s->state;return unlock(s,rc);
}
static int pending(struct sony_dsd_native_sink *s,struct sony_dsd_native_sink_state *out,int error){s->state.retained=1;*out=s->state;return unlock(s,error);}
int sony_dsd_native_sink_close(struct sony_dsd_native_sink **owner,struct sony_dsd_native_sink_state *out){
    if(!owner||!*owner||!out)return -EINVAL;struct sony_dsd_native_sink *s=*owner;int rc=lock(s);if(rc)return rc;
    if(s->unsafe_object)return pending(s,out,SONY_DSD_POWER_UNCERTAIN);
    if(s->power){rc=owner_thread(s);if(rc)return pending(s,out,rc);}
    if(s->writer){
        rc=sony_dsd_native_writer_state(s->writer,&s->state.writer);if(rc)return pending(s,out,rc);
        rc=sony_dsd_native_writer_destroy(&s->writer);if(rc)return pending(s,out,rc);
    }
    s->closing=1;s->state.sealed=1;
    if(s->power&&!s->close_begun){rc=sony_dsd_power_begin_close(s->power);if(rc)return pending(s,out,rc);s->close_begun=1;}
    if(pcm(s)&&s->write_attempted&&!s->state.drain_accepted&&!s->drop_attempted){
        rc=sony_dsd_control_begin(&s->state.drop,pcm(s),SONY_DSD_CTL_DROP);if(rc)return pending(s,out,rc);
        s->drop_attempted=1;int native=CALL(s->drop,s->object,0,0);rc=sony_dsd_control_end(native);
        s->state.drop_status=rc;
    }
    if(s->object&&!s->state.close_called){
        void *handle=pcm(s);
        if(handle){rc=sony_dsd_control_begin(&s->state.close,handle,SONY_DSD_CTL_CLOSE);if(rc)return pending(s,out,rc);}
        s->state.close_called=1;s->native_close_result=CALL(s->close,s->object,0,0);
        if(handle)s->state.control_close_status=sony_dsd_control_end(s->native_close_result);
        else s->state.control_close_status=s->native_close_result?-EIO:0;
    }
    if(s->power){
        s->state.power_close_status=sony_dsd_power_close_result(s->power,s->native_close_result);
        rc=sony_dsd_power_state(s->power,&s->state.power);if(rc)return pending(s,out,rc);
        if(!s->state.power.completed||s->state.power.token||s->state.power.resource_retained)return pending(s,out,SONY_DSD_POWER_UNCERTAIN);
        s->state.output_closed_confirmed=1;s->state.resources_clean=1;
        rc=sony_dsd_power_destroy(&s->power);if(rc)return pending(s,out,rc);
    }else {s->state.output_closed_confirmed=1;s->state.resources_clean=1;}
    if(s->object)CALL(s->destroy,s->object,0,0);
    s->state.retained=0;*out=s->state;
    rc=s->state.drop_status?s->state.drop_status:s->state.control_close_status?s->state.control_close_status:s->state.power_close_status;
    free(s);*owner=NULL;return rc;
}
