#define _GNU_SOURCE
#include "sony_audio_receiver_legacy_output.h"
#include "sony_audio_pcm_sink_protocol.h"
#include "sony_audio_receiver_legacy_io.h"
#include <dlfcn.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
extern unsigned sony_audio_native_call3(void *,unsigned,unsigned,unsigned);
#define P(p) ((unsigned)(p))
#define CALL(fn,a,b,c) ((int)sony_audio_native_call3((fn),P(a),P(b),P(c)))
struct sony_receiver_legacy_output {
    void *library,*alsa,*factory,*destroy,*open,*configure,*write,*drain,*drop,*close;
    unsigned *object;struct sony_receiver_legacy_policy policy;struct sony_receiver_legacy_state state;
    int pid,busy,opening,closing,close_attempted,drop_attempted,drop_status,close_error,close_status,unsafe_object;
};
static __thread struct sony_receiver_legacy_output *active;
static int error(struct sony_receiver_legacy_output *s,int rc){if(rc>=0)rc=-EPROTO;if(!s->state.first_error)s->state.first_error=rc;if(s->closing&&!s->close_error)s->close_error=rc;s->state.failed=1;return rc;}
static int enter(struct sony_receiver_legacy_output *s){if(!s)return -EINVAL;if(s->busy)return -EBUSY;active=s;s->busy=1;return 0;}
static int leave(struct sony_receiver_legacy_output *s,int rc){s->busy=0;return rc;}
static int policy(struct sony_receiver_legacy_output *s,enum sony_receiver_legacy_policy_operation op){(void)s;(void)op;return 0;}
static void *pcm(struct sony_receiver_legacy_output *s){return s->object?(void *)s->object[7]:NULL;}
int sony_receiver_legacy_amixer(void *,const unsigned *,const unsigned *) __asm__("_ZN3pst8services8funcarch11AmixerUtils6SetStrERKNSt3__112basic_stringIcNS3_11char_traitsIcEENS3_9allocatorIcEEEESB_");
int sony_receiver_legacy_amixer(void *self,const unsigned *key,const unsigned *value){
    struct sony_receiver_legacy_output *s=active;if(s)++s->state.low_requests;
    const char *symbol="_ZN3pst8services8funcarch11AmixerUtils6SetStrERKNSt3__112basic_stringIcNS3_11char_traitsIcEENS3_9allocatorIcEEEESB_";
    int (*fn)(void *,const unsigned *,const unsigned *)=s?dlsym(s->library,symbol):NULL;
    if(!fn)fn=dlsym(RTLD_NEXT,symbol);
    return fn?fn(self,key,value):1;
}
int sony_receiver_legacy_sysfs(void *,const unsigned *,const unsigned *) __asm__("_ZN3pst8services8funcarch10SysFsUtils5WriteERKNSt3__112basic_stringIcNS3_11char_traitsIcEENS3_9allocatorIcEEEESB_");
int sony_receiver_legacy_sysfs(void *self,const unsigned *key,const unsigned *value){
    struct sony_receiver_legacy_output *s=active;if(s){if(s->opening)++s->state.idle_block_requests;else if(s->closing)++s->state.idle_release_requests;}
    const char *symbol="_ZN3pst8services8funcarch10SysFsUtils5WriteERKNSt3__112basic_stringIcNS3_11char_traitsIcEENS3_9allocatorIcEEEESB_";
    int (*fn)(void *,const unsigned *,const unsigned *)=s?dlsym(s->library,symbol):NULL;
    if(!fn)fn=dlsym(RTLD_NEXT,symbol);
    return fn?fn(self,key,value):1;
}
int snd_pcm_open(void **out,const char *name,int stream,int flags){
    struct sony_receiver_legacy_output *s=active;
    int (*fn)(void **,const char *,int,int)=dlsym(RTLD_NEXT,"snd_pcm_open");if(!fn)return -ENOSYS;
    if(s)++s->state.pcm_open_forwarded;int rc=fn(out,name,stream,flags);return s&&rc<0?error(s,rc):rc;
}
int sony_receiver_legacy_no_recovery(void *,long) __asm__("_ZN3pst8audiohal20AudioHalOutAdlerAlsa18RecoverWriteBufferEl");
int sony_receiver_legacy_no_recovery(void *self,long failure){
    struct sony_receiver_legacy_output *s=active;
    int (*fn)(void *,long)=s?dlsym(s->library,"_ZN3pst8audiohal20AudioHalOutAdlerAlsa18RecoverWriteBufferEl"):NULL;
    return fn?fn(self,failure):1;
}
static int geometry(struct sony_receiver_legacy_output *s,unsigned rate){
    void *alsa=s->alsa,*alloc=dlsym(alsa,"snd_pcm_hw_params_malloc"),*current=dlsym(alsa,"snd_pcm_hw_params_current"),*freep=dlsym(alsa,"snd_pcm_hw_params_free"),
      *get_rate=dlsym(alsa,"snd_pcm_hw_params_get_rate"),*get_ch=dlsym(alsa,"snd_pcm_hw_params_get_channels"),*get_fmt=dlsym(alsa,"snd_pcm_hw_params_get_format"),
      *get_period=dlsym(alsa,"snd_pcm_hw_params_get_period_size"),*get_buffer=dlsym(alsa,"snd_pcm_hw_params_get_buffer_size"),*get_access=dlsym(alsa,"snd_pcm_hw_params_get_access");
    if(!alloc||!current||!freep||!get_rate||!get_ch||!get_fmt||!get_period||!get_buffer||!get_access)return -ENOSYS;
    void *p=NULL;if(CALL(alloc,&p,0,0)||!p)return -ENOMEM;
    unsigned hz=0,ch=0,fmt=0,access=0,period=0,buffer=0;int direction=0;
    int rc=CALL(current,pcm(s),p,0)||CALL(get_rate,p,&hz,&direction)||CALL(get_ch,p,&ch,0)||CALL(get_fmt,p,&fmt,0)||CALL(get_access,p,&access,0)||CALL(get_period,p,&period,&direction)||CALL(get_buffer,p,&buffer,0);
    CALL(freep,p,0,0);
    (void)rate;if(rc)return -EIO;
    s->state.timing=(struct sony_output_timing){hz,period,buffer,1};return 0;
}
int sony_receiver_legacy_open(struct sony_receiver_legacy_output **out,void *adler,void *alsa,unsigned profile,unsigned mode,unsigned rate,const struct sony_receiver_legacy_policy *backend){
    if(!out||*out||!adler||!alsa||profile>2||(mode!=1&&mode!=2)||!backend||!backend->assure)return -EINVAL;
    if(active)return -EBUSY;struct sony_receiver_legacy_output *s=calloc(1,sizeof(*s));if(!s)return -ENOMEM;
    s->library=adler;s->alsa=alsa;s->policy=*backend;s->state.profile=profile;s->state.mode=mode;s->pid=(int)getpid();*out=s;active=s;s->busy=1;
#define GET(member,name) do{s->member=dlsym(adler,name);if(!s->member)return leave(s,error(s,-ENOSYS));}while(0)
    GET(factory,"_ZN3pst8audiohal9adleralsa17CreateAudioHalOutERKNS1_13AudioHalOutIdE");GET(destroy,"_ZN3pst8audiohal20AudioHalOutAdlerAlsaD0Ev");
    GET(open,"_ZN3pst8audiohal20AudioHalOutAdlerAlsa4OpenEv");GET(configure,"_ZN3pst8audiohal20AudioHalOutAdlerAlsa14SetOutputParamERKNS0_13AudioHalParamE");
    GET(write,"_ZN3pst8audiohal20AudioHalOutAdlerAlsa5WriteERKNS0_14AudioHalBufferEPj");GET(drain,"_ZN3pst8audiohal20AudioHalOutAdlerAlsa5DrainEv");
    GET(drop,"_ZN3pst8audiohal20AudioHalOutAdlerAlsa4DropEv");GET(close,"_ZN3pst8audiohal20AudioHalOutAdlerAlsa5CloseEv");
#undef GET
    int rc=policy(s,SONY_RECEIVER_LEGACY_REQUIRE_LOW);if(rc)return leave(s,rc);
    unsigned id=mode;CALL(s->factory,&s->object,&id,0);if(!s->object)return leave(s,error(s,-ENODEV));
    s->opening=1;int native=CALL(s->open,s->object,0,0);s->opening=0;
    if(native||s->state.failed)return leave(s,error(s,s->state.first_error?s->state.first_error:-EIO));
    s->state.opened=1;unsigned param[25]={0};param[0]=rate;param[1]=2;param[4]=4;
    rc=CALL(s->configure,s->object,param,0);if(!rc)rc=geometry(s,rate);if(!rc)rc=policy(s,SONY_RECEIVER_LEGACY_STREAM_CHECK);
    if(rc)return leave(s,error(s,rc));s->state.configured=1;return leave(s,0);
}
int sony_receiver_legacy_write(struct sony_receiver_legacy_output *s,const int *data,unsigned frames){
    if(!data||!frames||frames>8192||(uintptr_t)data>UINTPTR_MAX-frames*8)return -EINVAL;int rc=enter(s);if(rc)return rc;
    if(!s->state.configured||s->state.failed||s->state.sealed||s->closing)return leave(s,-EPIPE);
    if(s->state.accepted_frames>UINT64_MAX-frames||s->state.alsa_confirmed_frames>UINT64_MAX-frames)return leave(s,error(s,-EOVERFLOW));
    rc=policy(s,SONY_RECEIVER_LEGACY_STREAM_CHECK);if(rc)return leave(s,rc);
    struct sony_receiver_legacy_io_report report;rc=sony_receiver_legacy_io_begin(&report,pcm(s));if(rc)return leave(s,-EBUSY);
    unsigned payload[3]={P(data),frames*8,frames*8},ack=0;int native=CALL(s->write,s->object,payload,&ack);rc=sony_receiver_legacy_io_end(native,1);
    s->state.alsa_confirmed_frames+=report.confirmed_frames;
    s->state.io_blocked_after_error+=report.blocked_after_error;
    if(!rc&&s->state.failed)rc=s->state.first_error;
    if(!rc)rc=policy(s,SONY_RECEIVER_LEGACY_STREAM_CHECK);
    if(rc)return leave(s,error(s,rc));s->state.accepted_frames+=frames;s->state.buffered_frames=s->object[33]/8;return leave(s,0);
}
int sony_receiver_legacy_drain(struct sony_receiver_legacy_output *s){
    int rc=enter(s);if(rc)return rc;if(!s->state.configured||s->state.failed||s->state.sealed||s->closing)return leave(s,-EPIPE);
    rc=policy(s,SONY_RECEIVER_LEGACY_STREAM_CHECK);if(rc)return leave(s,rc);
    unsigned pending=s->object[33],period=s->object[16];
    struct sony_receiver_legacy_io_report report;rc=sony_receiver_legacy_io_begin(&report,pcm(s));if(rc)return leave(s,-EBUSY);s->state.sealed=1;
    int native=CALL(s->drain,s->object,0,0);rc=sony_receiver_legacy_io_end(native,0);
    s->state.alsa_confirmed_frames+=report.confirmed_frames;
    s->state.io_blocked_after_error+=report.blocked_after_error;
    if(!rc)rc=policy(s,SONY_RECEIVER_LEGACY_STREAM_CHECK);if(rc)return leave(s,error(s,rc));s->state.drained=1;s->state.buffered_frames=0;s->state.drain_padding_frames=pending&&period>pending?(period-pending)/8:0;return leave(s,0);
}
int sony_receiver_legacy_state(struct sony_receiver_legacy_output *s,struct sony_receiver_legacy_state *out){
    if(!out)return -EINVAL;int rc=enter(s);if(rc)return rc;*out=s->state;return leave(s,0);
}
int sony_receiver_legacy_close(struct sony_receiver_legacy_output **owner,struct sony_receiver_legacy_cleanup *out){
    if(!owner||!*owner||!out)return -EINVAL;struct sony_receiver_legacy_output *s=*owner;int rc=enter(s);if(rc)return rc;
    *out=(struct sony_receiver_legacy_cleanup){.state=s->state,.drop_status=s->drop_status,.close_status=s->close_status,.retained=1,.output_cleanup_unverified=1};
    if(s->close_attempted)return leave(s,-EALREADY);
    struct sony_receiver_legacy_io_report report;
    if(pcm(s)&&s->state.configured&&(!s->state.drained||s->state.failed)&&!s->drop_attempted){
        rc=sony_receiver_legacy_io_begin(&report,pcm(s));if(rc)return leave(s,-EBUSY);s->drop_attempted=1;
        unsigned pending=s->object[33];int native=CALL(s->drop,s->object,0,0);s->drop_status=sony_receiver_legacy_io_end(native,0);
        if(s->drop_status)error(s,s->drop_status);
        else {s->state.discarded_buffered_frames=pending/8;s->state.buffered_frames=0;}
    }
    if(s->object){
        void *handle=pcm(s);if(handle){rc=sony_receiver_legacy_io_begin(&report,handle);if(rc)return leave(s,-EBUSY);}
        s->closing=1;s->close_attempted=1;int native=CALL(s->close,s->object,0,0);s->closing=0;
        rc=handle?sony_receiver_legacy_io_end(native,0):native?-EIO:0;
        if(!rc&&s->close_error)rc=s->close_error;
        if(rc)error(s,rc);s->close_status=rc;out->close_status=rc;
        CALL(s->destroy,s->object,0,0);s->object=NULL;
    }
    out->state=s->state;out->drop_status=s->drop_status;out->retained=0;
    rc=s->drop_status?s->drop_status:out->close_status;active=NULL;free(s);*owner=NULL;return rc;
}
