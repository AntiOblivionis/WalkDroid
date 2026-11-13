#define _GNU_SOURCE
#include "sony_audio_ai_bridge.h"
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
extern unsigned int sony_audio_native_call3(void *,unsigned int,unsigned int,unsigned int);
#define C1(method,a) sony_audio_native_call3(b->method,(unsigned)(a),0,0)
#define C2(method,a,c) sony_audio_native_call3(b->method,(unsigned)(a),(unsigned)(c),0)
#define C3(method,a,c,d) sony_audio_native_call3(b->method,(unsigned)(a),(unsigned)(c),(unsigned)(d))
struct sony_ai_bridge {
    void *filter;
    unsigned packet[128],header[256],track[25],original_track[25];
    unsigned pool[1024],processor[1024];
    float samples[32768],tail_output[16384];
    struct sony_ai_status status;
    unsigned finish_ms;
    int initialized,packet_constructed,pool_constructed,processor_constructed;
    void *factory,*si,*sd,*init,*exiting,*destroy,*set,*config,*process;
    void *frame_length,*output_fs,*drain,*dispatch,*ctor,*pd,*info,*size,*written,*actual;
    void *payload,*internal_info,*valid_bits,*original_info,*pool_ctor,*pool_destroy;
    void *proc_ctor,*proc_destroy,*silence,*vector_delete,*ctrl;
};
static int configure(struct sony_ai_bridge *b,int enabled) {
    unsigned text[16]={0};const char *command=enabled?"dseeai=on":"dseeai=off";
    C3(si,text,command,strlen(command));int result=(int)C2(set,b->filter,text);C1(sd,text);
    if(result)return -EIO;
    unsigned current[25],original[25];memcpy(current,b->track,sizeof(current));
    memcpy(original,b->original_track,sizeof(original));
    if(C3(config,b->filter,current,original))return -EIO;
    unsigned out_rate=enabled?C2(output_fs,b->filter,&b->status.input_rate):b->status.input_rate;
    b->status.requested_enabled=!!enabled;b->status.output_rate=out_rate;
    b->status.processing_expected=out_rate!=b->status.input_rate;return 0;
}
int sony_ai_bridge_open(struct sony_ai_bridge **out,void *library,unsigned rate,int enabled) {
    if(!out)return -EINVAL;*out=NULL;
    if(!library)return -EINVAL;
    int result=0;
    struct sony_ai_bridge *b=calloc(1,sizeof(*b));if(!b)return -ENOMEM;
#define LOOKUP(member,name) do{b->member=dlsym(library,name);if(!b->member){result=-ENOEXEC;goto fail;}}while(0)
    LOOKUP(factory,"_ZN3pst8services5sound6mobile12CreateFilterERKNSt3__112basic_stringIcNS3_11char_traitsIcEENS3_9allocatorIcEEEE");
    LOOKUP(si,"_ZNSt3__112basic_stringIcNS_11char_traitsIcEENS_9allocatorIcEEE6__initEPKcj");
    LOOKUP(sd,"_ZNSt3__112basic_stringIcNS_11char_traitsIcEENS_9allocatorIcEEED1Ev");
    LOOKUP(init,"_ZN3pst8services5sound6mobile6DseeAi4InitEv");
    LOOKUP(exiting,"_ZN3pst8services5sound6mobile6DseeAi4ExitEv");
    LOOKUP(destroy,"_ZN3pst8services5sound6mobile6DseeAiD0Ev");
    LOOKUP(set,"_ZN3pst8services5sound6mobile6DseeAi8SetParamERKNSt3__112basic_stringIcNS4_11char_traitsIcEENS4_9allocatorIcEEEE");
    LOOKUP(config,"_ZN3pst8services5sound6mobile6DseeAi6ConfigERNS1_10TrackParamES5_");
    LOOKUP(process,"_ZN3pst8services5sound6mobile6DseeAi18ProcessAudioPacketEPNS1_10DataPacketE");
    LOOKUP(frame_length,"_ZN3pst8services5sound6mobile6DseeAi20GetFrameLengthDseeAiERKNS1_10TrackParamE");
    LOOKUP(output_fs,"_ZN3pst8services5sound6mobile6DseeAi11GetOutputFsERKi");
    LOOKUP(drain,"_ZN3pst8services5sound6mobile6DseeAi5DrainEPNS1_10DataPacketE");
    LOOKUP(dispatch,"_ZN3pst8services5sound6mobile6Filter7ProcessEPNS1_10DataPacketE");
    LOOKUP(ctor,"_ZN3pst8services5sound10DataPacketC1EPNS1_6HeaderEPvj");
    LOOKUP(pd,"_ZN3pst8services5sound10DataPacketD1Ev");
    LOOKUP(info,"_ZN3pst8services5sound10DataPacket12SetAudioInfoERKNS1_10TrackParamE");
    LOOKUP(size,"_ZN3pst8services5sound10DataPacket21SetWrittenPayloadSizeEj");
    LOOKUP(written,"_ZNK3pst8services5sound10DataPacket21GetWrittenPayloadSizeEv");
    LOOKUP(actual,"_ZNK3pst8services5sound10DataPacket12GetAudioInfoEv");
    LOOKUP(payload,"_ZNK3pst8services5sound10DataPacket15GetAudioPayloadEv");
    LOOKUP(internal_info,"_ZNK3pst8services5sound10DataPacket15GetInternalInfoEv");
    LOOKUP(valid_bits,"_ZN3pst8services5sound7profile15InternalInfoDmp19SetValidBitsPerSampERKNS1_11BitsPerSampE");
    LOOKUP(original_info,"_ZN3pst8services5sound7profile15InternalInfoDmp20SetOriginalAudioInfoERKNS1_10TrackParamE");
    LOOKUP(pool_ctor,"_ZN3pst8services5sound22DynamicAllocPacketPoolC1Ev");
    LOOKUP(pool_destroy,"_ZN3pst8services5sound22DynamicAllocPacketPoolD1Ev");
    LOOKUP(proc_ctor,"_ZN3pst8services5sound21SoundServiceProcessorC1EPNS1_22DynamicAllocPacketPoolE");
    LOOKUP(proc_destroy,"_ZN3pst8services5sound21SoundServiceProcessorD1Ev");
    LOOKUP(silence,"_ZN3pst8services5sound21SoundServiceProcessor17CreateSilentAudioERKNS1_22ISoundServiceProcessor15SilentAudioArgsERNSt3__16vectorIPNS1_10DataPacketENS7_9allocatorISA_EEEE");
    LOOKUP(vector_delete,"_ZdlPv");
    LOOKUP(ctrl,"_ZN3pst8services5sound10DataPacket13SetPacketCtrlERKNSt3__112basic_stringIcNS3_11char_traitsIcEENS3_9allocatorIcEEEE");
#undef LOOKUP
    unsigned name[16]={0},created[16]={0};C3(si,name,"dseeai",6);
    C2(factory,created,name);C1(sd,name);b->filter=(void *)created[0];
    if(!b->filter||created[1]){result=-ENOEXEC;goto fail;}
    if(C1(init,b->filter)){result=-EIO;goto fail;}b->initialized=1;
    b->track[0]=rate;b->track[1]=32;b->track[2]=2;b->track[3]=2;
    b->track[10]=2;b->track[11]=4;b->track[12]=1;
    memcpy(b->original_track,b->track,sizeof(b->track));
    b->status.input_rate=rate;b->status.source_quantum=C2(frame_length,b->filter,b->track);
    if(!b->status.source_quantum||b->status.source_quantum>4096){result=-EPROTO;goto fail;}
    result=configure(b,enabled);if(result)goto fail;
    ((void (*)(void *,void *,void *,unsigned))b->ctor)(b->packet,b->header,b->samples,sizeof(b->samples));
    b->packet_constructed=1;C1(pool_ctor,b->pool);b->pool_constructed=1;
    C2(proc_ctor,b->processor,b->pool);b->processor_constructed=1;
    *out=b;return 0;
fail:
    sony_ai_bridge_close(&b,1);return result;
}
int sony_ai_bridge_set_enabled(struct sony_ai_bridge *b,int enabled) {
    if(!b)return -EINVAL;
    if(b->status.faulted)return -EIO;
    if(b->status.finished||b->status.finishing||b->status.source_frames)return -EBUSY;
    int result=configure(b,enabled);if(result)b->status.faulted=1;return result;
}
int sony_ai_bridge_status(struct sony_ai_bridge *b,struct sony_ai_status *out) {
    if(!b||!out)return -EINVAL;
    *out=b->status;return b->status.faulted?-EIO:0;
}
static int process(struct sony_ai_bridge *b,const float *input,unsigned frames,
                   float *output,unsigned capacity,unsigned *produced,unsigned *rate,int tail) {
    if(produced)*produced=0;if(rate)*rate=0;
    if(!b||!input||!output||!produced||!rate)return -EINVAL;
    if(b->status.faulted)return -EIO;
    if(b->status.finished||(!tail&&b->status.finishing))return -EPIPE;
    if(frames!=b->status.source_quantum)return -EINVAL;
    unsigned ratio=b->status.processing_expected?4u:1u;
    if(capacity<frames*ratio)return -EMSGSIZE;
    memcpy(b->samples,input,frames*8);
    C2(info,b->packet,b->track);C2(size,b->packet,frames*8);
    unsigned internal=C1(internal_info,b->packet),bits=32;
    if(!internal){b->status.faulted=1;return -EIO;}
    C2(valid_bits,internal,&bits);C2(original_info,internal,b->original_track);
    if(C2(process,b->filter,b->packet)){b->status.faulted=1;return -EIO;}
    if(tail)b->status.padding_source_frames+=frames;else b->status.source_frames+=frames;
    unsigned bytes=C1(written,b->packet);
    if(bytes>sizeof(b->samples)||bytes%8||bytes/8>capacity){b->status.faulted=1;return -EPROTO;}
    if(!bytes){*rate=b->status.output_rate;return 0;}
    const unsigned *format=(const unsigned *)C1(actual,b->packet);
    const float *data=(const float *)C1(payload,b->packet);
    if(!data){b->status.faulted=1;return -EIO;}
    memcpy(output,data,bytes);*produced=bytes/8;*rate=format?format[0]:b->status.output_rate;
    b->status.produced_frames+=*produced;
    if(b->status.processing_expected)b->status.processing_observed=1;
    return 0;
}
int sony_ai_bridge_process(struct sony_ai_bridge *b,const float *input,unsigned frames,
                           float *output,unsigned capacity,unsigned *produced,unsigned *rate) {
    return process(b,input,frames,output,capacity,produced,rate,0);
}
int sony_ai_bridge_finish(struct sony_ai_bridge *b,unsigned ms,sony_ai_tail_sink sink,void *context) {
    if(!b||!sink)return -EINVAL;
    if(b->status.faulted)return -EIO;
    if(b->status.finishing)return -EBUSY;
    if(b->status.finished)return 1;
    b->status.finishing=1;b->finish_ms=ms;
    unsigned args[27]={0},vector[3]={0},count=0;
    int result=0;
    if(b->status.source_frames){
        memcpy(args,b->track,sizeof(b->track));args[16]=b->status.input_rate;args[17]=32;
        args[25]=ms;args[26]=b->status.source_quantum*32;
        if(C3(silence,b->processor,args,vector)||vector[1]<vector[0]||
           (vector[1]-vector[0])%4||(!vector[0]&&vector[1])){result=-EPROTO;goto done;}
        count=(vector[1]-vector[0])/4;
        /* Bounds needed for the four sample reads in each generated packet. */
        for(unsigned i=0;i<count;++i){
            void *packet=((void **)vector[0])[i];if(!packet){result=-EPROTO;goto done;}
            const unsigned *data=(const unsigned *)C1(payload,packet);
            if(!data||C1(written,packet)<args[26]){result=-EPROTO;goto done;}
        }
        for(unsigned i=0;i<count;++i){
            void *packet=((void **)vector[0])[i];const float *data=(const float *)C1(payload,packet);
            for(unsigned part=0;part<4;++part){
                unsigned frames=0,rate=0;
                result=process(b,data+part*b->status.source_quantum*2,b->status.source_quantum,
                               b->tail_output,sizeof(b->tail_output)/8,&frames,&rate,1);
                if(result)goto done;
                if(frames){
                    int delivered=sink(context,b->tail_output,frames,rate);
                    if(!delivered)b->status.tail_acknowledged_frames+=frames;
                    if(delivered||b->status.faulted){result=-EIO;goto done;}
                }
            }
        }
    }
    C2(size,b->packet,0);
    if(C2(drain,b->filter,b->packet)){result=-EIO;goto done;}
    unsigned command[16]={0};C3(si,command,"track::eos",10);
    C2(ctrl,b->packet,command);C1(sd,command);
    if(C2(dispatch,b->filter,b->packet)){result=-EIO;goto done;}
    b->status.finished=1;
done:
    if(vector[0])C1(vector_delete,vector[0]);
    b->status.finishing=0;if(result)b->status.faulted=1;return result;
}
int sony_ai_bridge_close(struct sony_ai_bridge **owner,int discard) {
    if(!owner||(discard!=0&&discard!=1))return -EINVAL;
    struct sony_ai_bridge *b=*owner;if(!b)return 0;
    if(b->status.finishing)return -EBUSY;
    if(!discard&&b->status.source_frames&&!b->status.finished)return -EBUSY;
    int result=0;
    if(b->processor_constructed)C1(proc_destroy,b->processor);
    if(b->pool_constructed)C1(pool_destroy,b->pool);
    if(b->packet_constructed)C1(pd,b->packet);
    if(b->initialized&&C1(exiting,b->filter))result=-EIO;
    if(b->filter&&b->destroy)C1(destroy,b->filter);
    free(b);*owner=NULL;return result;
}
