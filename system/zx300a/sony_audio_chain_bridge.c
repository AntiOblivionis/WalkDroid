#include "sony_audio_chain_bridge.h"
#include "sony_audio_pcm_format.h"
#ifdef SONY_PCM_ENTRY_AUDIT
#include "sony_audio_pcm_entry_audit.h"
#endif
extern void *calloc(unsigned int,unsigned int);
extern void free(void *);
extern void *dlsym(void *,const char *);
extern int snprintf(char *,unsigned int,const char *,...);
extern unsigned int sony_audio_native_call3(void *,unsigned int,unsigned int,unsigned int);
#define N1(member,a) sony_audio_native_call3((void *)b->member,(unsigned int)(a),0,0)
#define N2(member,a,c) sony_audio_native_call3((void *)b->member,(unsigned int)(a),(unsigned int)(c),0)
#define N3(member,a,c,d) sony_audio_native_call3((void *)b->member,(unsigned int)(a),(unsigned int)(c),(unsigned int)(d))
typedef void (*ctor_fn)(void *);
typedef void (*string_fn)(void *,const char *,unsigned int);
typedef int (*init_fn)(void *,void *,const void *);
typedef int (*unary_fn)(void *);
typedef void *(*input_fn)(void *,const void *);
typedef int (*param_fn)(void *,const void *);
typedef int (*query_fn)(void *,const void *,void *);
typedef void *(*allocate_fn)(void *,unsigned int);
typedef void (*release_fn)(void *,void *);
typedef void (*info_fn)(void *,const void *);
typedef void (*size_fn)(void *,unsigned int);
typedef unsigned int (*written_fn)(void *);
typedef void *(*payload_fn)(void *);
typedef int (*process_fn)(void *,void *);

struct sony_chain_bridge {
#ifdef SONY_PCM_ENTRY_AUDIT
    struct sony_pcm_entry_audit *entry_audit;
    int entry_audit_checked;
#endif
    void *pool,*manager,*input;
    int pool_constructed,manager_constructed,initialized,faulted,input_ended;
    unsigned int input_rate;
    int receiver_pcm,receiver_busy,receiver_started,receiver_stopped,receiver_bluetooth;
    unsigned int receiver_volume;
    int receiver_service,receiver_service_sealed;
    void *receiver_processor,*receiver_attenuate,*receiver_fade_in,*receiver_fade_out,*receiver_do_fade_in,*receiver_append_info;
    unsigned int receiver_track[25];
    int dsd_input;
    int dsd_started,dsd_padding_used,dsd_finishing,dsd_finished;
    unsigned int dsd_finish_latency;
    struct sony_dsd_finish_result dsd_finish;
    struct sony_dsd_result dsd_expected;
    struct sony_output_controls output;
    struct sony_dsd_live_controls live,staged;
    unsigned int live_available,live_revision,live_pending,live_packets,live_min_packets;
    /* Opaque constructor storage for the fixed audited library. This is not
     * a recovered public class layout, hence experimental-only status. */
    unsigned int callbacks[512];
    ctor_fn pool_ctor,pool_destroy,manager_ctor,manager_destroy,string_destroy;
    string_fn string_init;
    init_fn init;
    unary_fn exit;
    input_fn get_input;
    param_fn param;
    query_fn query;
    allocate_fn allocate;
    release_fn release;
    info_fn info,valid_bits,original_info;
    size_fn size;
    written_fn written;
    payload_fn payload,audio_info,internal_info;
    process_fn write;
    void *processor_ctor,*processor_destroy,*create_silence,*vector_delete;
    void *header_size,*packet_ctrl,*packet_type,*audio_ctrl_command;
};

static int valid(const struct sony_chain_settings *s) {
    return s!=0;
}

static int command(struct sony_chain_bridge *b,const char *text) {
    unsigned int object[16]={0},length=0;while(text[length]) ++length;
    N3(string_init,object,text,length);
    int result=(int)N2(param,b->input,object);
    N1(string_destroy,object);return result;
}

int sony_chain_get_parameter(struct sony_chain_bridge *b,const char *key,char *output,unsigned int capacity) {
    if(!b || !b->initialized || b->faulted || b->dsd_finishing || b->receiver_busy || !key || !output || !capacity) return -1;
    unsigned int length=0;while(length<128 && key[length]) ++length;
    if(!length || length==128) return -1;
    unsigned int request[16]={0},reply[16]={0};
    N3(string_init,request,key,length);N3(string_init,reply,"",0);
    int status=(int)N3(query,b->input,request,reply);
    const unsigned char *bytes=(const unsigned char *)reply;
    unsigned int count=bytes[0]&1 ? reply[1] : bytes[0]>>1;
    const char *text=bytes[0]&1 ? (const char *)reply[2] : (const char *)bytes+1;
    if(status || !text || !count || count>4096) status=-3;
    else if(count>=capacity) status=-2;
    else {
        for(unsigned int i=0;i<count;++i) output[i]=text[i];
        output[count]=0;status=0;
    }
    N1(string_destroy,reply);N1(string_destroy,request);return status;
}

int sony_chain_update(struct sony_chain_bridge *b,const struct sony_chain_settings *s) {
    if(!b || !b->initialized || b->faulted || b->input_ended || b->dsd_input || b->receiver_busy || !valid(s)) return -1;
    b->faulted=1;
    char text[96];
    snprintf(text,sizeof(text),"output_device=%d",b->output.device);
    if(command(b,text)||command(b,"path=0"))return -2;
    snprintf(text,sizeof(text),"volume=%d,%d",s->volume,s->volume);
    if(command(b,text))return -2;
    snprintf(text,sizeof(text),"segainmode=%d",b->output.se_gain);if(command(b,text))return -2;
    snprintf(text,sizeof(text),"btlgainmode=%d",b->output.btl_gain);if(command(b,text))return -2;
    /* Enabling EQ alone does not select it: inherited SS_EQ_USE=3 leaves
     * EqTone active. This bridge's settings explicitly describe ten-band EQ. */
    if(command(b,"selectusingeq,type=2"))return -3;
    for(unsigned int i=0;i<10;++i) {
        snprintf(text,sizeof(text),"eq10band,band=%u,value=%d",i,s->bands[i]);
        if(command(b,text)) return -3;
    }
    if(command(b,s->eq_enabled ? "eq10band=on" : "eq10band=off")) return -3;
    snprintf(text,sizeof(text),"dcphaselinear,filtertype=%d",s->dc_mode);
    if(command(b,text) || command(b,s->dc_enabled ? "dcphaselinear=on" : "dcphaselinear=off")) return -3;
    snprintf(text,sizeof(text),"vinylizer,type=%d",s->vinyl_mode);
    if(command(b,text) || command(b,s->vinyl_enabled ? "vinylizer=on" : "vinylizer=off")) return -3;
    if(command(b,s->direct ? "sourcedirect=on" : "sourcedirect=off")) return -3;
    if(b->receiver_pcm)b->receiver_volume=(unsigned int)s->volume;
    b->faulted=0;return 0;
}

int sony_chain_update_output(struct sony_chain_bridge *b,const struct sony_output_controls *s) {
    if(!b||!b->initialized||b->faulted||b->input_ended||b->dsd_input||b->receiver_busy||!s)return -1;
    b->faulted=1;char text[64];
    snprintf(text,sizeof(text),"output_device=%d",s->device);if(command(b,text))return -2;
    snprintf(text,sizeof(text),"segainmode=%d",s->se_gain);if(command(b,text))return -2;
    snprintf(text,sizeof(text),"btlgainmode=%d",s->btl_gain);if(command(b,text))return -2;
    /* Balance belongs to the original hardware volume controller, not PCM. */
    b->output=*s;b->faulted=0;return 0;
}

int sony_chain_update_dsee(struct sony_chain_bridge *b,const struct sony_dsee_controls *s) {
    if(!b||!b->initialized||b->faulted||b->input_ended||b->dsd_input||b->receiver_busy||!s)return -1;
    b->faulted=1;char text[64];
    snprintf(text,sizeof(text),"dseehxcustom,mode=%d",s->mode);
    if(command(b,text)||command(b,s->enabled?"dseehxcustom=on":"dseehxcustom=off"))return -3;
    b->faulted=0;return 0;
}

int sony_chain_update_normalizer(struct sony_chain_bridge *b,int enabled) {
    if(!b||!b->initialized||b->faulted||b->input_ended||b->dsd_input||b->receiver_busy||(enabled!=0&&enabled!=1))return -1;
    b->faulted=1;
    if(command(b,enabled?"dynamicnormalizer=on":"dynamicnormalizer=off"))return -3;
    b->faulted=0;return 0;
}

int sony_chain_abort_close(struct sony_chain_bridge *b) {
    if(!b) return 0;
    if(b->dsd_finishing||b->receiver_busy)return -1;
    int status=0;
    if(b->initialized && N1(exit,b->manager)) status=-1;
    if(b->manager_constructed) N1(manager_destroy,b->manager);
    if(b->receiver_processor){N1(processor_destroy,b->receiver_processor);free(b->receiver_processor);}
    if(b->pool_constructed) N1(pool_destroy,b->pool);
#ifdef SONY_PCM_ENTRY_AUDIT
    sony_pcm_entry_audit_close(b->entry_audit);
#endif
    free(b->manager);free(b->pool);free(b);return status;
}

int sony_chain_update_tone(struct sony_chain_bridge *b,const struct sony_tone_settings *s) {
    if(!b||!s||!b->initialized||b->faulted||b->input_ended||b->dsd_input||b->receiver_busy)return -1;
    b->faulted=1;char text[96];
    for(unsigned int i=0;i<3;++i) {
        snprintf(text,sizeof(text),"eqtone,type=%u,value=%d",i,s->gains[i]);
        if(command(b,text))return -3;
        snprintf(text,sizeof(text),"eqtone,type=%u,centerfreq=%d",i,s->centerfreq[i]);
        if(command(b,text))return -3;
    }
    if(command(b,s->enabled?"eqtone=on":"eqtone=off")||command(b,"selectusingeq,type=3"))return -3;
    b->faulted=0;return 0;
}

int sony_chain_open(struct sony_chain_bridge **out,void *library,const struct sony_chain_settings *settings) {
    if(!out) return -1;
    *out=(void *)0;
    if(!library || !valid(settings)) return -1;
#ifdef SONY_CHAIN_AI_BOUNDARY
    if(sony_pcm_boundary_bind(library))return -3;
#endif
    struct sony_chain_bridge *b=calloc(1,sizeof(*b));if(!b) return -2;
#define RESOLVE(type,member,symbol) do { b->member=(type)dlsym(library,symbol); if(!b->member) goto missing; } while(0)
    RESOLVE(ctor_fn,pool_ctor,"_ZN3pst8services5sound22DynamicAllocPacketPoolC1Ev");
    RESOLVE(ctor_fn,pool_destroy,"_ZN3pst8services5sound22DynamicAllocPacketPoolD1Ev");
    RESOLVE(ctor_fn,manager_ctor,"_ZN3pst8services5sound17FilterChainMgrDmpC1Ev");
    RESOLVE(ctor_fn,manager_destroy,"_ZN3pst8services5sound17FilterChainMgrDmpD1Ev");
    RESOLVE(string_fn,string_init,"_ZNSt3__112basic_stringIcNS_11char_traitsIcEENS_9allocatorIcEEE6__initEPKcj");
    RESOLVE(ctor_fn,string_destroy,"_ZNSt3__112basic_stringIcNS_11char_traitsIcEENS_9allocatorIcEEED1Ev");
    RESOLVE(init_fn,init,"_ZN3pst8services5sound17FilterChainMgrDmp4InitEPNS1_22DynamicAllocPacketPoolERKNS1_15ModuleCallbacksE");
    RESOLVE(unary_fn,exit,"_ZN3pst8services5sound17FilterChainMgrDmp4ExitEv");
    RESOLVE(input_fn,get_input,"_ZN3pst8services5sound17FilterChainMgrDmp11GetFilterInERKNSt3__112basic_stringIcNS3_11char_traitsIcEENS3_9allocatorIcEEEE");
    RESOLVE(param_fn,param,"_ZN3pst8services5sound11FilterInDmp8SetParamERKNSt3__112basic_stringIcNS3_11char_traitsIcEENS3_9allocatorIcEEEE");
    RESOLVE(query_fn,query,"_ZN3pst8services5sound11FilterInDmp8GetParamERKNSt3__112basic_stringIcNS3_11char_traitsIcEENS3_9allocatorIcEEEERS9_");
    RESOLVE(allocate_fn,allocate,"_ZN3pst8services5sound22DynamicAllocPacketPool8AllocateEj");
    RESOLVE(release_fn,release,"_ZN3pst8services5sound22DynamicAllocPacketPool7ReleaseEPNS1_7IPacketE");
    RESOLVE(info_fn,info,"_ZN3pst8services5sound10DataPacket12SetAudioInfoERKNS1_10TrackParamE");
    RESOLVE(info_fn,valid_bits,"_ZN3pst8services5sound7profile15InternalInfoDmp19SetValidBitsPerSampERKNS1_11BitsPerSampE");
    RESOLVE(info_fn,original_info,"_ZN3pst8services5sound7profile15InternalInfoDmp20SetOriginalAudioInfoERKNS1_10TrackParamE");
    RESOLVE(size_fn,size,"_ZN3pst8services5sound10DataPacket21SetWrittenPayloadSizeEj");
    RESOLVE(written_fn,written,"_ZNK3pst8services5sound10DataPacket21GetWrittenPayloadSizeEv");
    RESOLVE(payload_fn,payload,"_ZNK3pst8services5sound10DataPacket15GetAudioPayloadEv");
    RESOLVE(payload_fn,audio_info,"_ZNK3pst8services5sound10DataPacket12GetAudioInfoEv");
    RESOLVE(payload_fn,internal_info,"_ZNK3pst8services5sound10DataPacket15GetInternalInfoEv");
    RESOLVE(process_fn,write,"_ZN3pst8services5sound11FilterInDmp5WriteEPNS1_10DataPacketE");
    RESOLVE(void *,processor_ctor,"_ZN3pst8services5sound21SoundServiceProcessorC1EPNS1_22DynamicAllocPacketPoolE");
    RESOLVE(void *,processor_destroy,"_ZN3pst8services5sound21SoundServiceProcessorD1Ev");
    RESOLVE(void *,create_silence,"_ZN3pst8services5sound21SoundServiceProcessor17CreateSilentAudioERKNS1_22ISoundServiceProcessor15SilentAudioArgsERNSt3__16vectorIPNS1_10DataPacketENS7_9allocatorISA_EEEE");
    RESOLVE(void *,vector_delete,"_ZdlPv");
    RESOLVE(void *,header_size,"_ZN3pst8services5sound10DataPacket13GetHeaderSizeEv");
    RESOLVE(void *,packet_ctrl,"_ZN3pst8services5sound10DataPacket13SetPacketCtrlERKNSt3__112basic_stringIcNS3_11char_traitsIcEENS3_9allocatorIcEEEE");
    RESOLVE(void *,packet_type,"_ZNK3pst8services5sound10DataPacket7GetTypeEv");
    RESOLVE(void *,audio_ctrl_command,"_ZNK3pst8services5sound10DataPacket15GetAudioCtrlCmdEv");
#undef RESOLVE
    b->pool=calloc(1,4096);b->manager=calloc(1,16384);
    if(!b->pool || !b->manager) { sony_chain_abort_close(b);return -2; }
    N1(pool_ctor,b->pool);b->pool_constructed=1;
    N1(manager_ctor,b->manager);b->manager_constructed=1;
    if(N3(init,b->manager,b->pool,b->callbacks)) { sony_chain_abort_close(b);return -4; }
    b->initialized=1;b->input_rate=48000;
    struct sony_output_controls output=SONY_OUTPUT_DEFAULT;b->output=output;
    unsigned int name[16]={0};N3(string_init,name,"master",6);
    b->input=(void *)N2(get_input,b->manager,name);N1(string_destroy,name);
    if(!b->input || sony_chain_update(b,settings)) { sony_chain_abort_close(b);return -5; }
    *out=b;return 0;
missing:
    sony_chain_abort_close(b);return -3;
}

int sony_chain_open_rate(struct sony_chain_bridge **out,void *library,
    const struct sony_chain_settings *settings,unsigned int input_rate) {
    if(!out)return -1;
    *out=(void *)0;
    int status=sony_chain_open(out,library,settings);
    if(!status)(*out)->input_rate=input_rate;
    return status;
}

static int open_receiver_pcm(struct sony_chain_bridge **out,void *library,
    const struct sony_chain_settings *settings,const unsigned int track[25],int bluetooth) {
    if(!out||*out||!track)return -1;
    struct sony_chain_bridge *b=(void *)0;int r=sony_chain_open(&b,library,settings);if(r)return r;
    for(unsigned int i=0;i<25;++i)b->receiver_track[i]=track[i];
    b->receiver_pcm=1;b->receiver_bluetooth=bluetooth;b->receiver_volume=(unsigned int)settings->volume;b->input_rate=track[0];*out=b;return 0;
}
int sony_chain_open_receiver_usb_pcm(struct sony_chain_bridge **out,void *library,
    const struct sony_chain_settings *settings,const unsigned int track[25]) {
    return open_receiver_pcm(out,library,settings,track,0);
}
int sony_chain_receiver_volume(struct sony_chain_bridge *b,unsigned int expected,unsigned int volume){
    (void)expected;
    if(!b||!b->initialized||b->receiver_busy)return -1;
    if(volume==b->receiver_volume)return 0;
    char text[96];snprintf(text,sizeof(text),"volume=%u,%u",volume,volume);
    b->faulted=1;
    if(command(b,text))return -2;
    b->receiver_volume=volume;b->faulted=0;return 0;
}
static int open_receiver_service(struct sony_chain_bridge **out,void *library,
    const struct sony_chain_settings *settings,const unsigned int track[25],int bluetooth) {
    if(!out||*out)return -1;
    struct sony_chain_bridge *b=(void *)0;int r=open_receiver_pcm(&b,library,settings,track,bluetooth);if(r)return r;
    b->receiver_attenuate=dlsym(library,"_ZN3pst8services5sound21SoundServiceProcessor9AttenuateEPNS1_10DataPacketE");
    b->receiver_fade_in=dlsym(library,"_ZN3pst8services5sound21SoundServiceProcessor6FadeInEPNS1_10DataPacketE");
    b->receiver_fade_out=dlsym(library,"_ZN3pst8services5sound21SoundServiceProcessor7FadeOutEPNS1_10DataPacketE");
    b->receiver_do_fade_in=dlsym(library,"_ZN3pst8services5sound7profile8DoFadeInEPNS1_10DataPacketE");
    b->receiver_append_info=dlsym(library,"_ZN3pst8services5sound10DataPacket15AppendExtraInfoERKNSt3__112basic_stringIcNS3_11char_traitsIcEENS3_9allocatorIcEEEE");
    if(!b->receiver_attenuate||!b->receiver_fade_in||!b->receiver_fade_out||!b->receiver_do_fade_in||!b->receiver_append_info){sony_chain_abort_close(b);return -3;}
    b->receiver_processor=calloc(1,4096);if(!b->receiver_processor){sony_chain_abort_close(b);return -2;}
    N2(processor_ctor,b->receiver_processor,b->pool);b->receiver_service=1;*out=b;return 0;
}
int sony_chain_open_receiver_usb_service(struct sony_chain_bridge **out,void *library,
    const struct sony_chain_settings *settings,const unsigned int track[25]) {
    return open_receiver_service(out,library,settings,track,0);
}
int sony_chain_open_receiver_bt_service(struct sony_chain_bridge **out,void *library,
    const struct sony_chain_settings *settings,const unsigned int track[25]) {
    return open_receiver_service(out,library,settings,track,1);
}
static int receiver_service_mark(struct sony_chain_bridge *b,void *packet,const char *mark) {
    unsigned int text[16]={0},n=0;while(mark[n])++n;N3(string_init,text,mark,n);
    N2(receiver_append_info,packet,text);N1(string_destroy,text);return 0;
}
static int process_receiver_pcm(struct sony_chain_bridge *b,const void *input,unsigned int frames,
    void *output,unsigned int capacity,struct sony_receiver_chain_result *result,int real_input,unsigned int service_flags) {
    if(!b||!b->initialized||!b->receiver_pcm||b->dsd_input||b->faulted||b->input_ended||
       !input||!output||!result)return -1;
    unsigned int width=b->receiver_track[11];
    if(width&&(width>131072u/2||frames>131072u/(width*2)))return -1;
    unsigned int bytes=frames*width*2;
    const unsigned char *src=input;
    void *packet=(void *)N2(allocate,b->pool,131072);if(!packet){b->faulted=1;return -2;}
    N2(info,packet,b->receiver_track);N2(size,packet,bytes);
    void *internal=(void *)N1(internal_info,packet);unsigned char *p=(void *)N1(payload,packet);int status=-3;
    if(!internal||!p)goto receiver_done;
    unsigned int bits=b->receiver_track[1];N2(valid_bits,internal,&bits);N2(original_info,internal,b->receiver_track);
    for(unsigned int i=0;i<bytes;++i)p[i]=src[i];
    if(b->receiver_service&&real_input){
        if(service_flags&SONY_RECEIVER_HEAD)receiver_service_mark(b,packet,"audio_head");
        if(service_flags&SONY_RECEIVER_STOP_TAIL)receiver_service_mark(b,packet,"audio_tail");
        if(service_flags&SONY_RECEIVER_END_TAIL)receiver_service_mark(b,packet,"audio_eos");
        if(N2(receiver_attenuate,b->receiver_processor,packet))goto receiver_done;
    }
    if(N2(write,b->input,packet))goto receiver_done;
    /* Original service order is Attenuate, FilterIn, conditional FadeIn,
     * conditional FadeOut, Renderer. Generated silence bypasses WriteAudio
     * and enters modules directly in the original service. */
    if(b->receiver_service&&real_input){
        if(N1(receiver_do_fade_in,packet)&&N2(receiver_fade_in,b->receiver_processor,packet))goto receiver_done;
        if((service_flags&SONY_RECEIVER_STOP_TAIL)&&N2(receiver_fade_out,b->receiver_processor,packet))goto receiver_done;
    }
    unsigned int count=N1(written,packet);const unsigned int *actual=(const void *)N1(audio_info,packet);p=(void *)N1(payload,packet);
    if(!actual||!p||count>131072||count>capacity)goto receiver_done;
    struct sony_receiver_chain_result r={0};for(unsigned int i=0;i<25;++i)r.track[i]=actual[i];
    unsigned long long frame_bytes=(unsigned long long)actual[11]*2;
    r.bytes=count;r.frames=frame_bytes?(unsigned int)(count/frame_bytes):0;
    unsigned char *dst=output;for(unsigned int i=0;i<count;++i)dst[i]=p[i];*result=r;
    if(real_input)b->receiver_started=1;
    if(b->receiver_service&&real_input&&(service_flags&6u))b->receiver_service_sealed=1;
    status=0;
receiver_done:
    N2(release,b->pool,packet);if(status)b->faulted=1;return status;
}
int sony_chain_process_receiver_pcm(struct sony_chain_bridge *b,const void *input,unsigned int frames,
    void *output,unsigned int capacity,struct sony_receiver_chain_result *result) {
    if(b&&(b->receiver_busy||b->receiver_service))return -1;
    return process_receiver_pcm(b,input,frames,output,capacity,result,1,0);
}
int sony_chain_process_receiver_service(struct sony_chain_bridge *b,const void *input,unsigned int frames,unsigned int flags,
    void *output,unsigned int capacity,struct sony_receiver_chain_result *result) {
    if(!b||!b->receiver_service||b->receiver_busy)return -1;
    return process_receiver_pcm(b,input,frames,output,capacity,result,1,flags);
}
/* Preserve the original silence packet-size selection for known source rates. */
static unsigned int receiver_silent_packet_bytes(const struct sony_chain_bridge *b) {
    unsigned int bytes=16384;
    switch(b->input_rate){
    case 88200:case 96000:bytes=32768;break;
    case 176400:case 192000:bytes=65536;break;
    case 352800:case 384000:bytes=131072;break;
    default:break;
    }
    return b->receiver_track[11]==2?bytes>>1:bytes;
}
int sony_chain_render_silence_receiver_pcm(struct sony_chain_bridge *b,unsigned int ms,
    sony_receiver_chain_sink sink,void *context,unsigned int *acknowledged) {
    if(!b||!b->initialized||!b->receiver_pcm||b->receiver_busy||!sink||!acknowledged)return -1;
    *acknowledged=0;b->receiver_busy=1;
    void *processor=calloc(1,4096);unsigned char *output=calloc(1,131072);
    if(!processor||!output){free(processor);free(output);b->receiver_busy=0;b->faulted=1;return -2;}
    unsigned int packet_bytes=receiver_silent_packet_bytes(b);
    unsigned int args[27]={0},vector[3]={0};for(unsigned int i=0;i<25;++i)args[i]=b->receiver_track[i];args[25]=ms;args[26]=packet_bytes;
    N2(processor_ctor,processor,b->pool);int status=(int)N3(create_silence,processor,args,vector);
    if(status)status=-3;
    unsigned int end=vector[1];
    if(end<vector[0])end=vector[0];
    if(vector[2]<end)end=vector[2]>=vector[0]?vector[2]:vector[0];
    unsigned int count=status?0:(end-vector[0])/4;
    /* Processor owns the packets; copied vector owns only pointer storage. */
    for(unsigned int i=0;!status&&i<count;++i){
        void *packet=((void **)vector[0])[i];if(!packet){status=-3;break;}
        const unsigned char *data=(void *)N1(payload,packet);if(!data){status=-3;break;}
        unsigned int bytes=N1(written,packet);if(bytes>packet_bytes)bytes=packet_bytes;
        unsigned long long frame_bytes=(unsigned long long)b->receiver_track[11]*2;
        unsigned int frames=frame_bytes?(unsigned int)(bytes/frame_bytes):0;
        if(!frames)continue;
        if(b->receiver_service){
            /* Preserve original generated packet boundaries into FilterIn.
             * Splitting into512 changes native partial-block state after an
             * unaligned short source (e.g. one-frame EQ/DC/vinyl tail). */
            struct sony_receiver_chain_result r;
            if(process_receiver_pcm(b,data,frames,output,131072,&r,0,0))status=-4;
            else if(sink(context,output,&r))status=-5;
            else *acknowledged+=r.bytes;
            continue;
        }
        for(unsigned int at=0;!status&&at<frames;at+=512){
            struct sony_receiver_chain_result r;unsigned int part=frames-at<512?frames-at:512;
            if(process_receiver_pcm(b,data+at*frame_bytes,part,output,131072,&r,0,0)){status=-4;break;}
            if(sink(context,output,&r)){status=-5;break;}*acknowledged+=r.bytes;
        }
    }
    if(vector[0])N1(vector_delete,(void *)vector[0]);
    N1(processor_destroy,processor);free(processor);free(output);
    b->receiver_busy=0;if(status)b->faulted=1;return status;
}

/* The original DSEE stage expands the packet in place. Its native bound is
 * 131072 bytes; input payload size is not a safe allocation capacity. */
#ifdef SONY_CHAIN_AI_BOUNDARY
int sony_chain_process_transform(struct sony_chain_bridge *b,const int *input,unsigned int frames,
    sony_pcm_float_transform transform,void *context,int *output,unsigned int capacity,
    unsigned int *produced,unsigned int *rate){
    if(produced)*produced=0;if(rate)*rate=0;
    if(!b||!b->initialized||b->faulted||b->input_ended||b->dsd_input||b->receiver_pcm||b->receiver_busy||
       !input||!output||!transform||!produced||!rate||frames!=512||capacity<2048||capacity>16384||
       (b->input_rate!=44100&&b->input_rate!=48000))return -1;
    struct sony_pcm_boundary g={0};
    void *packet=(void *)N2(allocate,b->pool,131072);if(!packet){b->faulted=1;return -2;}
    unsigned track[25]={0};track[0]=b->input_rate;track[1]=32;track[2]=2;track[3]=2;
    track[10]=1;track[11]=4;track[12]=1;track[16]=b->input_rate;track[17]=32;
    N2(info,packet,track);N2(size,packet,4096);
    void *internal=(void *)N1(internal_info,packet);int *samples=(int *)N1(payload,packet);
    int status=-3;if(!internal||!samples)goto transformed_done;
    unsigned bits=32;N2(valid_bits,internal,&bits);N2(original_info,internal,track);
    for(unsigned i=0;i<1024;++i)samples[i]=input[i];
    g.packet=packet;g.context=context;g.transform=transform;g.payload=b->payload;g.audio_info=b->audio_info;
    g.written=b->written;g.size=b->size;g.source_rate=b->input_rate;g.source_frames=512;g.output_rate=b->input_rate*4;
    if(sony_pcm_boundary_enter(&g))goto transformed_done;
    b->receiver_busy=1;
    int native_status=(int)N2(write,b->input,packet);
    int boundary_status=sony_pcm_boundary_leave(&g);b->receiver_busy=0;
    if(native_status||boundary_status)goto transformed_done;
    unsigned count=N1(written,packet);const unsigned *actual=(const unsigned *)N1(audio_info,packet);
    samples=(int *)N1(payload,packet);
    if(!actual||!samples||count!=g.produced*8||count/8>capacity||actual[0]!=b->input_rate*4||
       actual[1]!=32||actual[2]!=2||actual[3]!=2||actual[12]!=1||actual[10]!=1||actual[11]!=4)goto transformed_done;
    for(unsigned i=0;i<count/4;++i)output[i]=samples[i];
    *produced=count/8;*rate=actual[0];status=0;
transformed_done:
    N2(release,b->pool,packet);if(status)b->faulted=1;return status;
}
#endif
static int process_packet(struct sony_chain_bridge *b,const int *input,int *output,
    unsigned int frames,unsigned int capacity,unsigned int *produced,unsigned int *rate,int fixed) {
    if(!b||!b->initialized||b->faulted||b->input_ended||b->dsd_input||b->receiver_pcm||b->receiver_busy||!input||!output||frames!=sony_pcm_source_frames(b->input_rate)||
       !produced||!rate||!capacity||capacity>16384)return -1;
    void *packet=(void *)N2(allocate,b->pool,131072);
    if(!packet){b->faulted=1;return -2;}
    unsigned int track[25]={0};track[0]=b->input_rate;track[1]=32;track[2]=2;track[3]=2;
    track[10]=1;track[11]=4;track[12]=1;track[17]=32;
    track[16]=b->input_rate;
    N2(info,packet,track);N2(size,packet,frames*8);
    void *internal=(void *)N1(internal_info,packet);int *p=(int *)N1(payload,packet);
    int status=-3;
    if(!internal||!p)goto done;
    unsigned int bits=32;N2(valid_bits,internal,&bits);N2(original_info,internal,track);
    for(unsigned int i=0;i<frames*2;++i)p[i]=input[i];
#ifdef SONY_PCM_ENTRY_AUDIT
    if(!b->entry_audit_checked){
        b->entry_audit_checked=1;
        b->entry_audit=sony_pcm_entry_audit_begin(b->input_rate);
    }
    sony_pcm_entry_audit_push_frames(b->entry_audit,p,frames);
    int write_status=(int)N2(write,b->input,packet);
    sony_pcm_entry_audit_result(b->entry_audit,write_status);
    if(write_status)goto done;
#else
    if(N2(write,b->input,packet))goto done;
#endif
    unsigned int count=N1(written,packet);
    const unsigned int *actual=(const unsigned int *)N1(audio_info,packet);p=(int *)N1(payload,packet);
    if(!actual||!p||!count||count>131072||count%8||count/8>capacity||
       actual[1]!=32||actual[2]!=2||
       actual[3]!=2||actual[10]!=1||actual[11]!=4||actual[12]!=1)goto done;
    if(fixed&&count!=4096)goto done;
    for(unsigned int i=0;i<count/4;++i)output[i]=p[i];
    *produced=count/8;*rate=actual[0];status=0;
done:
    N2(release,b->pool,packet);
    if(status)b->faulted=1;
    return status;
}
int sony_chain_process(struct sony_chain_bridge *b,const int *input,int *output,unsigned int frames) {
    if(!b||b->input_rate!=48000)return -1;
    unsigned int produced=0,rate=0;
    return process_packet(b,input,output,frames,512,&produced,&rate,1);
}
int sony_chain_process_expanded(struct sony_chain_bridge *b,const int *input,int *output,
    unsigned int input_frames,unsigned int output_capacity_frames,
    unsigned int *output_frames,unsigned int *output_sample_rate) {
    return process_packet(b,input,output,input_frames,output_capacity_frames,output_frames,output_sample_rate,0);
}

static void dsd_track(unsigned int *track,unsigned int rate) {
    for(unsigned int i=0;i<25;++i)track[i]=0;
    track[0]=rate;track[1]=1;track[2]=2;track[3]=2;
    track[10]=3;track[11]=1;track[12]=1;track[17]=1;
}

static int open_dsd(struct sony_chain_bridge **out,void *library,
    const struct sony_chain_settings *settings,const struct sony_dsd_controls *dsd,
    const struct sony_output_controls *output,unsigned int input_rate,
    const struct sony_tone_settings *tone,const struct sony_dsee_controls *dsee) {
    if(!out)return -1;
    *out=(void *)0;
    if(!library||!valid(settings)||!sony_dsd_controls_valid(dsd)||
       !sony_output_controls_valid(output))return -1;
    if(dsee&&!sony_dsee_controls_valid(dsee))return -1;
    if(tone){
        if(tone->enabled!=0&&tone->enabled!=1)return -1;
        for(unsigned i=0;i<3;++i)if(tone->gains[i]<-20||tone->gains[i]>20||tone->centerfreq[i]!=2)return -1;
    }
    void *resolve=dlsym(library,"_ZN3pst8services5sound6mobile15dsdprocessrules15ResolveProcCondERKNS3_6ConfigERNS3_8ProcCondE");
    if(!resolve)return -3;
    struct {unsigned int mode;unsigned char direct,se_native,pad[2];unsigned int device,rate;} cfg;
    struct {unsigned char convert,pad[3];unsigned int rate;} proc={0};
    cfg.mode=(unsigned int)dsd->mode;cfg.direct=(unsigned char)settings->direct;cfg.se_native=0;
    cfg.pad[0]=cfg.pad[1]=0;cfg.device=(unsigned int)output->device;cfg.rate=input_rate;
    if(sony_audio_native_call3(resolve,(unsigned int)&cfg,(unsigned int)&proc,0))return -3;
    struct sony_chain_bridge *b=(void *)0;int status=sony_chain_open(&b,library,settings);
    if(status)return status;
    if((tone&&sony_chain_update_tone(b,tone))||(dsee&&sony_chain_update_dsee(b,dsee))){sony_chain_abort_close(b);return -5;}
    if(sony_chain_update_output(b,output)){sony_chain_abort_close(b);return -5;}
    char text[96],actual[32];
    const char *keys[]={"builtin_dsd_process_mode","dsd_conv_filter_type","dsd_conv_gain_mode"};
    int values[]={dsd->mode,dsd->filter,dsd->gain};
    for(unsigned int i=0;i<3;++i) {
        snprintf(text,sizeof(text),"%s=%d",keys[i],values[i]);
        if(command(b,text)||sony_chain_get_parameter(b,keys[i],actual,sizeof(actual))||
           actual[0]!='0'+values[i]||actual[1]) {sony_chain_abort_close(b);return -5;}
    }
    /* Capability key has no useful original Get reply. Its native Set and
     * exact FilterInConfig byte mapping have separate original-code evidence. */
    if(command(b,"se_hp_dsd_native_enabled=0")){sony_chain_abort_close(b);return -5;}
    b->input_rate=input_rate;b->dsd_input=1;
    if(dsee){
        b->live_available=1;b->live_revision=1;b->live.settings=*settings;b->live.dsd=*dsd;b->live.dsee=*dsee;
        b->live.selector=tone?3:2;
        if(tone)b->live.tone=*tone;
        else for(unsigned i=0;i<3;++i)b->live.tone.centerfreq[i]=2;
    }
    b->dsd_expected.rate=proc.rate;b->dsd_expected.format=proc.convert?SONY_DSD_OUTPUT_PCM_S32LE:SONY_DSD_OUTPUT_NATIVE;
    unsigned int rate_shift=input_rate==2822400?0u:input_rate==5644800?1u:2u;
    b->dsd_expected.bytes=proc.convert?((proc.rate==352800?131072u:65536u)>>rate_shift):SONY_DSD_PACKET_BYTES;
    *out=b;return 0;
}

int sony_chain_open_dsd(struct sony_chain_bridge **out,void *library,
    const struct sony_chain_settings *settings,const struct sony_dsd_controls *dsd,
    const struct sony_output_controls *output,unsigned int input_rate) {
    return open_dsd(out,library,settings,dsd,output,input_rate,(void *)0,(void *)0);
}
int sony_chain_open_dsd_configured(struct sony_chain_bridge **out,void *library,
    const struct sony_chain_settings *settings,const struct sony_dsd_controls *dsd,
    const struct sony_output_controls *output,unsigned int input_rate,
    const struct sony_tone_settings *tone,const struct sony_dsee_controls *dsee) {
    if(!out)return -1;
    *out=(void *)0;if(!dsee)return -1;
    return open_dsd(out,library,settings,dsd,output,input_rate,tone,dsee);
}

static int live_ready(struct sony_chain_bridge *b){
    return b&&b->initialized&&!b->faulted&&!b->input_ended&&b->dsd_input&&b->dsd_started&&
        !b->dsd_finishing&&!b->dsd_finished&&!b->dsd_padding_used&&b->live_available&&b->dsd_expected.format==SONY_DSD_OUTPUT_PCM_S32LE;
}
static int live_equal(const struct sony_dsd_live_controls *a,const struct sony_dsd_live_controls *b){
    const unsigned char *x=(const unsigned char *)a,*y=(const unsigned char *)b;
    for(unsigned i=0;i<sizeof(*a);++i)if(x[i]!=y[i])return 0;
    return 1;
}
static int set_integer(struct sony_chain_bridge *b,const char *key,int value){
    char text[96];snprintf(text,sizeof(text),"%s=%d",key,value);return command(b,text);
}
static int set_switch(struct sony_chain_bridge *b,const char *key,int value){
    char text[96];snprintf(text,sizeof(text),"%s=%s",key,value?"on":"off");return command(b,text);
}
int sony_chain_dsd_get_controls(struct sony_chain_bridge *b,struct sony_dsd_live_controls *out,unsigned *revision,unsigned *pending){
    if(!live_ready(b)||!out||!revision||!pending)return -1;
    *out=b->live;*revision=b->live_revision;*pending=b->live_pending;return 0;
}
int sony_chain_dsd_stage_controls(struct sony_chain_bridge *b,unsigned revision,const struct sony_dsd_live_controls *request){
    (void)revision;
    if(!live_ready(b)||!request)return -1;
    if(b->live_pending)return -5;
    struct sony_dsd_live_controls next=*request;
    if(next.selector==2)next.tone=b->live.tone;
    if(live_equal(&next,&b->live))return 0;
    b->faulted=1;char key[80];const struct sony_dsd_live_controls *old=&b->live;
    /* This processing notification does not prove the audible DAC level. */
    if(next.settings.volume!=old->settings.volume){
        char value[80];snprintf(value,sizeof(value),"volume=%d,%d",next.settings.volume,next.settings.volume);
        if(command(b,value))return -2;
    }
#define LIVE_CHANGE(field,key) do{if(next.field!=old->field&&set_integer(b,key,next.field))return -2;}while(0)
#define LIVE_SWITCH(field,key) do{if(next.field!=old->field&&set_switch(b,key,next.field))return -2;}while(0)
    for(unsigned i=0;i<10;++i)if(next.settings.bands[i]!=old->settings.bands[i]){
        snprintf(key,sizeof(key),"eq10band,band=%u,value",i);if(set_integer(b,key,next.settings.bands[i]))return -2;
    }
    LIVE_SWITCH(settings.eq_enabled,"eq10band");
    if(next.selector==3){
        for(unsigned i=0;i<3;++i){
            if(old->selector!=3||next.tone.gains[i]!=old->tone.gains[i]){
                snprintf(key,sizeof(key),"eqtone,type=%u,value",i);if(set_integer(b,key,next.tone.gains[i]))return -2;
            }
            if(old->selector!=3){snprintf(key,sizeof(key),"eqtone,type=%u,centerfreq",i);if(set_integer(b,key,next.tone.centerfreq[i]))return -2;}
        }
        if((old->selector!=3||next.tone.enabled!=old->tone.enabled)&&set_switch(b,"eqtone",next.tone.enabled))return -2;
    }
    LIVE_CHANGE(selector,"selectusingeq,type");
    LIVE_SWITCH(settings.direct,"sourcedirect");
    LIVE_CHANGE(settings.dc_mode,"dcphaselinear,filtertype");LIVE_SWITCH(settings.dc_enabled,"dcphaselinear");
    LIVE_CHANGE(settings.vinyl_mode,"vinylizer,type");LIVE_SWITCH(settings.vinyl_enabled,"vinylizer");
    LIVE_CHANGE(dsee.mode,"dseehxcustom,mode");LIVE_SWITCH(dsee.enabled,"dseehxcustom");
    LIVE_CHANGE(dsd.mode,"builtin_dsd_process_mode");LIVE_CHANGE(dsd.filter,"dsd_conv_filter_type");LIVE_CHANGE(dsd.gain,"dsd_conv_gain_mode");
#undef LIVE_CHANGE
#undef LIVE_SWITCH
    b->live_min_packets=(next.dsd.filter!=old->dsd.filter||next.dsd.gain!=old->dsd.gain)?2:1;
    b->staged=next;b->live_pending=1;b->live_packets=0;b->faulted=0;return 0;
}
int sony_chain_dsd_commit_controls(struct sony_chain_bridge *b,unsigned *revision){
    if(!live_ready(b)||!revision)return -1;
    if(!b->live_pending){*revision=b->live_revision;return 0;}
    b->live=b->staged;b->live_pending=0;++b->live_revision;*revision=b->live_revision;return 0;
}

static int process_dsd(struct sony_chain_bridge *b,const unsigned char *input,
    unsigned int input_bytes,unsigned char *output,unsigned int capacity,struct sony_dsd_result *result) {
    if(!b||!b->initialized||b->faulted||b->input_ended||!b->dsd_input||!input||!output||!result||
       input_bytes>SONY_DSD_PACKET_BYTES)return -1;
    void *packet=(void *)N2(allocate,b->pool,SONY_DSD_MAX_OUTPUT_BYTES);
    if(!packet){b->faulted=1;return -2;}
    unsigned int track[25],bits=1;dsd_track(track,b->input_rate);
    N2(info,packet,track);N2(size,packet,input_bytes);
    void *internal=(void *)N1(internal_info,packet);unsigned char *p=(unsigned char *)N1(payload,packet);
    int status=-3;if(!internal||!p)goto done_dsd;
    N2(valid_bits,internal,&bits);N2(original_info,internal,track);
    for(unsigned int i=0;i<input_bytes;++i)p[i]=input[i];
    if(N2(write,b->input,packet))goto done_dsd;
    unsigned int count=N1(written,packet);const unsigned int *actual=(const unsigned int *)N1(audio_info,packet);
    p=(unsigned char *)N1(payload,packet);
    if(!actual||!p||count>SONY_DSD_MAX_OUTPUT_BYTES||count>capacity)goto done_dsd;
    for(unsigned int i=0;i<count;++i)output[i]=p[i];
    result->bytes=count;result->rate=actual[0];result->format=actual[10];b->dsd_started=1;status=0;
done_dsd:
    N2(release,b->pool,packet);if(status)b->faulted=1;return status;
}

int sony_chain_process_dsd(struct sony_chain_bridge *b,const unsigned char *input,
    unsigned int input_bytes,unsigned char *output,unsigned int capacity,struct sony_dsd_result *result) {
    if(b&&b->dsd_finishing)return -1;
    int rc=process_dsd(b,input,input_bytes,output,capacity,result);
    if(!rc&&b->live_pending&&b->live_packets<8)++b->live_packets;
    return rc;
}

int sony_chain_render_silence(struct sony_chain_bridge *b,unsigned int ms,
    sony_chain_tail_sink sink,void *context,unsigned int *acknowledged_frames) {
    if(!b || !b->initialized || b->faulted || b->input_ended || b->dsd_input || b->receiver_pcm || !sink || !acknowledged_frames || (ms!=100 && ms!=250)) return -1;
    if(b->input_rate!=48000)return -1;
    /* Native silence packets have input-sized capacity; DSEE can expand them.
     * Keep the legacy callback contract explicit and never overflow that packet. */
    char dsee[16];
    if(sony_chain_get_parameter(b,"dseehxcustom",dsee,sizeof(dsee))||
       dsee[0]!='o'||dsee[1]!='f'||dsee[2]!='f'||dsee[3])return -1;
    *acknowledged_frames=0;
    void *processor=calloc(1,4096);if(!processor) return -2;
    unsigned int args[27]={0},vector[3]={0};
    args[0]=48000;args[1]=32;args[2]=2;args[3]=2;args[10]=1;args[11]=4;args[12]=1;args[17]=32;
    args[25]=ms;args[26]=16384;
    N2(processor_ctor,processor,b->pool);
    int status=(int)N3(create_silence,processor,args,vector);
    if(status || vector[1]<vector[0] || (vector[1]-vector[0])%4 || (vector[1]-vector[0])/4>64) status=-3;
    if(!status) {
        unsigned int count=(vector[1]-vector[0])/4;
        if(count!=(ms==100 ? 2u : 5u)) status=-3;
        for(unsigned int i=0;!status && i<count;++i) {
            void *packet=((void **)vector[0])[i];
            void *internal=(void *)N1(internal_info,packet);unsigned int bits=32;
            if(!internal) { status=-4;break; }
            N2(valid_bits,internal,&bits);N2(original_info,internal,args);
            if(N2(write,b->input,packet) || N1(written,packet)!=16384) { status=-4;break; }
            const unsigned int *format=(const unsigned int *)N1(audio_info,packet);
            const int *samples=(const int *)N1(payload,packet);
            if(!format || !samples || format[0]!=48000 || format[1]!=32 || format[2]!=2
                || format[10]!=1 || format[11]!=4 || format[12]!=1) { status=-4;break; }
            if(sink(context,samples,2048)) { status=-5;break; }
            *acknowledged_frames+=2048;
        }
    }
    /* Processor owns packets; vector owns only its copied pointer storage. */
    if(vector[0]) N1(vector_delete,(void *)vector[0]);
    N1(processor_destroy,processor);free(processor);
    if(status) b->faulted=1;
    return status;
}

int sony_chain_render_silence_expanded(struct sony_chain_bridge *b,unsigned int ms,
    sony_chain_expanded_tail_sink sink,void *context,unsigned int *acknowledged_frames) {
    if(!b||!b->initialized||b->faulted||b->input_ended||b->dsd_input||b->receiver_pcm||!sink||!acknowledged_frames||
       (ms!=100&&ms!=250))return -1;
    *acknowledged_frames=0;
    void *processor=calloc(1,4096);int *output=calloc(32768,sizeof(int));
    if(!processor||!output){free(processor);free(output);return -2;}
    unsigned int args[27]={0},vector[3]={0};
    args[0]=b->input_rate;args[1]=32;args[2]=2;args[3]=2;args[10]=1;args[11]=4;args[12]=1;args[17]=32;
    args[16]=b->input_rate;
    unsigned int quantum=sony_pcm_source_frames(b->input_rate),packet_bytes=sony_pcm_tail_packet_bytes(b->input_rate);
    args[25]=ms;args[26]=packet_bytes;
    N2(processor_ctor,processor,b->pool);
    int status=(int)N3(create_silence,processor,args,vector);
    if(status||vector[1]<vector[0]||(vector[1]-vector[0])%4||(vector[1]-vector[0])/4>64)status=-3;
    if(!status) {
        unsigned int count=(vector[1]-vector[0])/4;
        if(count!=sony_pcm_tail_packets(b->input_rate,ms))status=-3;
        for(unsigned int i=0;!status&&i<count;++i) {
            void *packet=((void **)vector[0])[i];
            if(!packet){status=-4;break;}
            const unsigned int *format=(const unsigned int *)N1(audio_info,packet);
            const int *samples=(const int *)N1(payload,packet);
            if(!format||!samples||N1(written,packet)!=packet_bytes||format[0]!=b->input_rate||format[1]!=32||
               format[2]!=2||format[3]!=2||format[10]!=1||format[11]!=4||format[12]!=1){status=-4;break;}
            for(unsigned int chunk=0;!status&&chunk<4;++chunk) {
                unsigned int frames=0,rate=0;
                if(sony_chain_process_expanded(b,samples+chunk*quantum*2,output,quantum,16384,&frames,&rate)){status=-4;break;}
                if(sink(context,output,frames,rate)){status=-5;break;}
                *acknowledged_frames+=frames;
            }
        }
    }
    if(vector[0])N1(vector_delete,(void *)vector[0]);
    N1(processor_destroy,processor);free(processor);free(output);
    if(status)b->faulted=1;
    return status;
}

static int render_silence_dsd(struct sony_chain_bridge *b,unsigned int ms,
    sony_chain_dsd_tail_sink sink,void *context,unsigned int *acknowledged_bytes) {
    if(!b||!b->initialized||b->faulted||b->input_ended||!b->dsd_input||!sink||!acknowledged_bytes)return -1;
    *acknowledged_bytes=0;b->dsd_padding_used=1;
    void *processor=calloc(1,4096);
    unsigned char *output=calloc(1,SONY_DSD_MAX_OUTPUT_BYTES);
    if(!processor||!output){free(processor);free(output);return -2;}
    unsigned int args[27]={0},vector[3]={0};dsd_track(args,b->input_rate);
    args[25]=ms;args[26]=SONY_DSD_PACKET_BYTES;
    N2(processor_ctor,processor,b->pool);
    int status=(int)N3(create_silence,processor,args,vector);
    unsigned int count=0;
    if(status||vector[1]<vector[0]||(vector[1]-vector[0])%4||
       (vector[1]!=vector[0]&&!vector[0]))status=-3;
    else count=(vector[1]-vector[0])/4;
    /* The processor owns packets; the vector owns only pointer storage.  Feed
     * each returned payload in bounded chunks and pass the actual length on. */
    for(unsigned int i=0;!status&&i<count;++i) {
        void *packet=((void **)vector[0])[i];if(!packet){status=-4;break;}
        const unsigned char *data=(const unsigned char *)N1(payload,packet);
        unsigned int bytes=N1(written,packet),offset=0;
        if(bytes&&!data){status=-4;break;}
        while(!status&&offset<bytes) {
            unsigned int chunk=bytes-offset;
            if(chunk>SONY_DSD_PACKET_BYTES)chunk=SONY_DSD_PACKET_BYTES;
            struct sony_dsd_result result;
            if(process_dsd(b,data+offset,chunk,output,SONY_DSD_MAX_OUTPUT_BYTES,&result)){status=-4;break;}
            if(sink(context,output,&result)){status=-5;break;}
            *acknowledged_bytes+=result.bytes;offset+=chunk;
        }
    }
    if(vector[0])N1(vector_delete,(void *)vector[0]);
    N1(processor_destroy,processor);free(processor);free(output);
    if(status)b->faulted=1;
    return status;
}

int sony_chain_render_silence_dsd(struct sony_chain_bridge *b,unsigned int ms,
    sony_chain_dsd_tail_sink sink,void *context,unsigned int *acknowledged_bytes) {
    if(b&&b->dsd_finishing)return -1;
    return render_silence_dsd(b,ms,sink,context,acknowledged_bytes);
}

static int send_terminal_control(struct sony_chain_bridge *b,const char *expected,unsigned int length) {
    unsigned int bytes=N1(header_size,0);
    if(!bytes || bytes>131072) { b->faulted=1;return -2; }
    void *packet=(void *)N2(allocate,b->pool,bytes);
    if(!packet) { b->faulted=1;return -2; }
    unsigned int command_object[16]={0};N3(string_init,command_object,expected,length);
    N2(packet_ctrl,packet,command_object);N1(string_destroy,command_object);
    const char *command=(const char *)N1(audio_ctrl_command,packet);
    int matched=N1(packet_type,packet)==1 && command;
    for(unsigned int i=0;matched && i<=length;++i) if(command[i]!=expected[i]) matched=0;
    int status=-3;
    if(matched && !N2(write,b->input,packet) && !N1(written,packet)) status=0;
    N2(release,b->pool,packet);
    if(status) b->faulted=1;
    return status;
}

static int send_eos(struct sony_chain_bridge *b,int dsd) {
    if(!b || !b->initialized || b->faulted || b->dsd_finishing || b->receiver_busy || b->dsd_input!=dsd) return -1;
    if(b->input_ended) return 1;
    int status=send_terminal_control(b,"track::eos",10);
    if(!status)b->input_ended=1;
    return status;
}

int sony_chain_send_eos(struct sony_chain_bridge *b) {return send_eos(b,0);}
int sony_chain_send_dsd_eos(struct sony_chain_bridge *b) {return send_eos(b,1);}
int sony_chain_stop_receiver_pcm(struct sony_chain_bridge *b) {
    if(!b||!b->initialized||!b->receiver_pcm||b->dsd_input||b->faulted||b->receiver_busy)return -1;
    if(b->receiver_stopped)return 1;
    int r=send_terminal_control(b,"track::stop",11);if(!r){b->receiver_stopped=1;b->input_ended=1;}return r;
}

int sony_chain_finish_dsd(struct sony_chain_bridge *b,unsigned int latency,
    sony_chain_dsd_tail_sink sink,void *context,struct sony_dsd_finish_result *out) {
    if(!b||!b->initialized||!b->dsd_input||b->faulted||b->dsd_finishing||latency>2||!sink||!out)return -1;
    if(b->dsd_finished) {
        if(latency!=b->dsd_finish_latency)return -1;
        *out=b->dsd_finish;return 1;
    }
    if(!b->dsd_started||b->input_ended||b->dsd_padding_used)return -1;
    b->dsd_finishing=1;b->dsd_finish_latency=latency;
    struct sony_dsd_finish_result result={latency?100u:200u,0,0,0};
    int status=render_silence_dsd(b,result.requested_ms,sink,context,&result.acknowledged_bytes);
    if(!status) {
        status=send_terminal_control(b,"track::eos",10);
        if(!status){result.eos_accepted=1;b->input_ended=1;}
    }
    if(!status) {
        status=send_terminal_control(b,"track::stop",11);
        if(!status)result.stop_accepted=1;
    }
    if(status)b->faulted=1;
    else b->dsd_finished=1;
    b->dsd_finish=result;b->dsd_finishing=0;*out=result;return status;
}
