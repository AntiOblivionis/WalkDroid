/* Experimental original-output endpoint. PCM requests MAY ACCESS HARDWARE. */
#include "sony_audio_stream.h"
#include "sony_audio_pcm_format.h"
#include "sony_audio_stream_protocol.h"
#include "sony_audio_chain_worker_protocol.h"
#include "sony_audio_snapshot_store.h"
#include "sony_audio_snapshot_eq.h"
#include "sony_audio_snapshot_tone.h"
#include "sony_audio_snapshot_config.h"
#include "sony_audio_cfw_profile.h"
#ifdef SONY_CFW_SIGNATURE_CONTEXT
#include "sony_audio_cfw_worker_context.h"
#include "sony_audio_feature_config.h"
#endif
#ifdef SONY_SOUND_PRESETS
#include "sony_audio_sound_presets.h"
extern int memcmp(const void *,const void *,unsigned int);
#endif
#ifdef SONY_AI_PARENT_RESOURCES
#include "sony_audio_ai_paused_power.h"
#define WORKER_FAULT() (sony_snapshot_config_fault()||sony_ai_paused_power_fault())
#else
#define WORKER_FAULT() sony_snapshot_config_fault()
#endif
#ifdef SONY_CFW_SIGNATURE_CONTEXT
#ifdef SONY_AI_PARENT_RESOURCES
#undef WORKER_FAULT
#define WORKER_FAULT() (sony_snapshot_config_fault()||sony_ai_paused_power_fault()||sony_audio_feature_config_fault())
#else
#undef WORKER_FAULT
#define WORKER_FAULT() (sony_snapshot_config_fault()||sony_audio_feature_config_fault())
#endif
#endif
#ifdef SONY_STREAM_AI
#include <stddef.h>
#include "sony_audio_ai_client.h"
#include "sony_audio_ai_chain.h"
static int decimal32(const char *s,uint32_t *out) {
    uint32_t value=0;if(!s||!*s)return -1;
    for(;*s;++s){if(*s<'0'||*s>'9'||value>429496729u||(value==429496729u&&*s>'5'))return -1;value=value*10+(unsigned)(*s-'0');}
    *out=value;return 0;
}
#endif
extern int read(int,void *,unsigned int),write(int,const void *,unsigned int),dup(int),dup2(int,int);
extern int *__errno_location(void);
extern int strcmp(const char *,const char *);
extern void *dlopen(const char *,int);
static unsigned int payload[SONY_PCM_MAX_SOURCE_FRAMES*2];
static unsigned char snapshot[SONY_SNAPSHOT_BYTES];
static unsigned char snapshot_staged[SONY_SNAPSHOT_BYTES];
#ifdef SONY_SOUND_PRESETS
static unsigned char snapshot_preset[SONY_SNAPSHOT_BYTES];
#endif
static int prepare_save(unsigned int command,unsigned int bytes){
    if(command==CHAIN_SAVE){if(bytes)return WDA_FORMAT;}
#ifdef SONY_SOUND_PRESETS
    else if(command==STREAM_PRESET_COMMIT){
        if(bytes!=SONY_SNAPSHOT_BYTES+8u||payload[1]<1||payload[1]>3||
           (payload[0]!=SONY_SOUND_PRESET_SAVE&&payload[0]!=SONY_SOUND_PRESET_LOAD))return WDA_FORMAT;
        if(sony_sound_preset_project(snapshot,sizeof(snapshot),payload[0],payload[1],snapshot_preset))return WDA_STORAGE;
    }
#endif
    else return WDA_PROTOCOL;
    for(unsigned int i=0;i<sizeof(snapshot);++i)snapshot_staged[i]=snapshot[i];
#ifdef SONY_SOUND_PRESETS
    if(command==STREAM_PRESET_COMMIT&&payload[0]==SONY_SOUND_PRESET_LOAD)
        for(unsigned int i=0;i<sizeof(snapshot);++i)snapshot_staged[i]=snapshot_preset[i];
#endif
    return 0;
}
static int finish_save(unsigned int command){
#ifdef SONY_SOUND_PRESETS
    if(command==STREAM_PRESET_COMMIT){
        if(payload[0]==SONY_SOUND_PRESET_SAVE&&sony_sound_preset_project(snapshot_staged,sizeof(snapshot_staged),
            payload[0],payload[1],snapshot_staged))return WDA_STORAGE;
    }
#else
    (void)command;
#endif
    return 0;
}
static int transfer(int fd,void *data,unsigned int size,int sending) {
    unsigned int done=0;
    while(done<size){int n=sending?write(fd,(char *)data+done,size-done):read(fd,(char *)data+done,size-done);
        if(n<0&&*__errno_location()==4)continue;
        if(n<=0)return -1;
        done+=(unsigned int)n;
    }return 0;
}
int main(int argc,char **argv) {
#ifdef SONY_CFW_SIGNATURE_CONTEXT
    if(sony_cfw_worker_context_args(&argc,argv)<0)return 64;
#endif
#ifdef SONY_STREAM_AI
    uint32_t ai_lo=0,ai_hi=0;
#ifdef SONY_AI_PARENT_RESOURCES
    if(argc!=10||strcmp(argv[9],"--parent-paused-resources")||
#else
    if(argc!=9||
#endif
       strcmp(argv[5],"--ai-fd")||strcmp(argv[6],"4")||decimal32(argv[7],&ai_lo)||decimal32(argv[8],&ai_hi)||
       !(ai_lo||ai_hi)||strcmp(argv[1],"--snapshot-dir")||(strcmp(argv[3],"0")&&strcmp(argv[3],"1")))return 64;
#else
    if((argc!=4&&argc!=5)||strcmp(argv[1],"--snapshot-dir")||(strcmp(argv[3],"0")&&strcmp(argv[3],"1")))return 64;
#endif
    int profile=2;
    if(argc>=5) {
        if(!argv[4][0]||argv[4][1]||argv[4][0]<'0'||argv[4][0]>'2')return 64;
        profile=argv[4][0]-'0';
    }
    int feature=argv[3][0]-'0',reply_fd=dup(1);if(reply_fd<0||dup2(2,1)<0)return 65;
    /* IO guard's RTLD_NEXT lookup must see the real ALSA implementation.
     * Do not rely on a HOST snd_pcm_open fixture to promote its scope. */
    void *alsa=dlopen("libasound.so",2|256);
    void *sound=dlopen("libSoundServiceFw.so",2),*adler=dlopen(argc>=5?sony_cfw_profile_library(profile):"libaudiohal-adleralsa.so",2);
    if(!alsa||!sound||!adler)return 66;
#ifdef SONY_STREAM_AI
    struct sony_ai_client *ai=NULL;int ai_used=0;
    if(sony_ai_client_create(&ai,4,((uint64_t)ai_hi<<32)|ai_lo,3000))return 65;
#ifdef SONY_AI_PARENT_RESOURCES
    if(sony_ai_paused_power_bind(((uint64_t)ai_hi<<32)|ai_lo))return 65;
#endif
#endif
    struct sony_stream *stream=0;unsigned int session=0,next=0,input_rate=48000,revision=0;int endpoint_fault=0,storage_uncertain=0;
    struct sony_chain_settings accepted={0};
    struct sony_output_controls accepted_output=SONY_OUTPUT_DEFAULT;
    struct sony_dsee_controls accepted_dsee=SONY_DSEE_DEFAULT;
    int accepted_normalizer=0;
    struct sony_volume_memory accepted_volume_memory={0,6};
    struct {unsigned int selector;struct sony_tone_settings tone;} accepted_tone={2,{0,{0,0,0},{2,2,2}}};
    for(;;){
        struct wda_header h;
        if(transfer(0,&h,sizeof(h),0))break;
        if(h.payload_bytes>sizeof(payload))break;
        if(transfer(0,payload,h.payload_bytes,0))break;
        unsigned int command=h.command,bytes=h.payload_bytes;int status=0,quit=0;
        struct sony_stream_state state={0};unsigned int response_bytes=0;
        struct sony_output_timing timing={0};void *response=&state;
        char text[1024];
        {
            if(command==WDA_OPEN){
                if(stream
#ifdef SONY_STREAM_AI
                   ||ai_used
#endif
                )status=WDA_STATE_ERROR;
                else if(bytes!=sizeof(struct sony_chain_settings)||h.frames||h.channels!=2)status=WDA_FORMAT;
                else {
                    struct sony_chain_settings settings=*(const struct sony_chain_settings *)payload;
                    struct sony_output_controls output=SONY_OUTPUT_DEFAULT;
                    struct sony_dsee_controls dsee=SONY_DSEE_DEFAULT;
                    int normalizer=0;
                    struct sony_volume_memory volume_memory={0,6};
                    struct sony_tone_settings tone={0,{0,0,0},{2,2,2}};int use_tone=0;
                    if(sony_snapshot_read(argv[2],snapshot)||sony_snapshot_config_bind(snapshot))status=WDA_STORAGE;
                    else {
                        use_tone=!sony_snapshot_decode_tone(snapshot,sizeof(snapshot),&tone);
                        if(use_tone?sony_snapshot_apply_tone_base(snapshot,sizeof(snapshot),feature,&settings):
                            sony_snapshot_apply_supported_effects(snapshot,sizeof(snapshot),feature,&settings))status=WDA_STORAGE;
                    }
                    if(!status){
                        if(sony_snapshot_apply_output(snapshot,sizeof(snapshot),&output))status=WDA_STORAGE;
                        if(!status&&sony_snapshot_apply_dsee(snapshot,sizeof(snapshot),&dsee))status=WDA_STORAGE;
                        if(!status&&sony_snapshot_apply_normalizer(snapshot,sizeof(snapshot),&normalizer))status=WDA_STORAGE;
                        if(!status&&sony_snapshot_apply_volume_memory(snapshot,sizeof(snapshot),&volume_memory))status=WDA_STORAGE;
                    }
                    if(!status){
                        int native;
#ifdef SONY_STREAM_AI
                        if(settings.direct||!dsee.enabled||dsee.mode!=1||(h.sample_rate!=44100&&h.sample_rate!=48000))native=-11;
                        else {
                            ai_used=1;native=sony_ai_client_open(ai,h.sample_rate,1);
                            if(!native)native=sony_stream_open_ai(&stream,sound,adler,&settings,h.sample_rate,&ai);
                            if(native&&ai)sony_ai_client_close(&ai,1);
                        }
#else
                        native=sony_stream_open_rate(&stream,sound,adler,&settings,h.sample_rate);
#endif
                        if(!native)native=sony_stream_update_output(stream,&output);
                        if(!native)native=sony_stream_update_dsee(stream,&dsee);
                        if(!native)native=sony_stream_update_normalizer(stream,normalizer);
                        if(!native&&use_tone)native=sony_stream_update_tone(stream,&tone);
                        if(native||WORKER_FAULT()) {
                            if(stream)sony_stream_abort_close(&stream);
#ifdef SONY_STREAM_AI
                            status=native==-11?WDA_STATE_ERROR:WDA_BACKEND;
#else
                            status=WDA_BACKEND;
#endif
                        }else{session=++next;if(!session)session=++next;revision=1;input_rate=h.sample_rate;endpoint_fault=0;storage_uncertain=0;
                            accepted=settings;accepted_output=output;accepted_dsee=dsee;accepted_normalizer=normalizer;accepted_volume_memory=volume_memory;accepted_tone.selector=use_tone?3:2;
                            if(!use_tone)sony_snapshot_decode_tone_values(snapshot,sizeof(snapshot),&tone);
                            accepted_tone.tone=tone;}
                    }
                }
            }else if(command==STREAM_READY){
                if(stream)status=WDA_STATE_ERROR;
            }else if(command==WDA_SHUTDOWN){
                if(stream)status=WDA_STATE_ERROR;
                else quit=1;
            }else if(!stream)status=WDA_STATE_ERROR;
            else if(command==STREAM_PROCESS){
                if(bytes!=sony_pcm_source_frames(input_rate)*8||h.frames!=sony_pcm_source_frames(input_rate)||h.sample_rate!=input_rate||h.channels!=2)status=WDA_FORMAT;
                else if(endpoint_fault||(!sony_stream_get_state(stream,&state)&&(state.sealed||state.faulted)))status=WDA_STATE_ERROR;
                else {
                    int native=sony_stream_process(stream,(const int *)payload,h.frames);
#ifdef SONY_AI_PARENT_RESOURCES
                    if(native==STREAM_CLEANUP_UNCERTAIN)sony_ai_paused_power_failed_open();
#endif
                    if(native){status=native==STREAM_CLEANUP_UNCERTAIN?native:WDA_BACKEND;endpoint_fault=1;}
                }
            }else if(h.frames||h.sample_rate||h.channels)status=WDA_FORMAT;
            else if(command==STREAM_FINISH){
                if(bytes!=4||(payload[0]!=100&&payload[0]!=250))status=WDA_FORMAT;
                else if(endpoint_fault||(!sony_stream_get_state(stream,&state)&&(state.sealed||state.faulted)))status=WDA_STATE_ERROR;
                else {int native=sony_stream_finish(stream,payload[0]);if(native){status=WDA_BACKEND;endpoint_fault=1;}}
            }else if(command==STREAM_STATE){
                if(bytes)status=WDA_FORMAT;
                else if(sony_stream_get_state(stream,&state))status=WDA_BACKEND;
                else {if(endpoint_fault)state.faulted=1;response_bytes=sizeof(state);}
            }else if(command==STREAM_TIMING){
                if(bytes)status=WDA_FORMAT;
                else if(sony_stream_get_timing(stream,&timing))status=WDA_BACKEND;
                else {response=&timing;response_bytes=sizeof(timing);}
            }else if(command==STREAM_OUTPUT_UPDATE){
                if(bytes!=sizeof(accepted_output)||!sony_output_controls_valid((const struct sony_output_controls *)payload))status=WDA_FORMAT;
                else if(endpoint_fault||revision==~0u||sony_stream_get_state(stream,&state)||state.sealed||state.faulted)status=WDA_STATE_ERROR;
                else if(sony_stream_update_output(stream,(const struct sony_output_controls *)payload)||WORKER_FAULT()){endpoint_fault=1;status=WDA_BACKEND;}
                else {accepted_output=*(const struct sony_output_controls *)payload;++revision;}
            }else if(command==STREAM_VOLUME_MEMORY_UPDATE){
                if(bytes!=sizeof(accepted_volume_memory)||!sony_volume_memory_valid((const struct sony_volume_memory *)payload))status=WDA_FORMAT;
                else if(endpoint_fault||revision==~0u||sony_stream_get_state(stream,&state)||state.sealed||state.faulted)status=WDA_STATE_ERROR;
                else {accepted_volume_memory=*(const struct sony_volume_memory *)payload;++revision;}
            }else if(command==STREAM_VOLUME_MEMORY_STATE){
                if(bytes)status=WDA_FORMAT;
                else {response=&accepted_volume_memory;response_bytes=sizeof(accepted_volume_memory);}
            }else if(command==STREAM_NORMALIZER_UPDATE){
                if(bytes!=sizeof(int)||payload[0]>1)status=WDA_FORMAT;
                else if(endpoint_fault||revision==~0u||sony_stream_get_state(stream,&state)||state.sealed||state.faulted)status=WDA_STATE_ERROR;
                else if(sony_stream_update_normalizer(stream,(int)payload[0])||WORKER_FAULT()){endpoint_fault=1;status=WDA_BACKEND;}
                else {accepted_normalizer=(int)payload[0];++revision;}
            }else if(command==STREAM_NORMALIZER_STATE){
                if(bytes)status=WDA_FORMAT;
                else {response=&accepted_normalizer;response_bytes=sizeof(accepted_normalizer);}
            }else if(command==STREAM_DSEE_UPDATE){
                if(bytes!=sizeof(accepted_dsee)||!sony_dsee_controls_valid((const struct sony_dsee_controls *)payload))status=WDA_FORMAT;
                else if(endpoint_fault||revision==~0u||sony_stream_get_state(stream,&state)||state.sealed||state.faulted)status=WDA_STATE_ERROR;
                else {
                    int native=sony_stream_update_dsee(stream,(const struct sony_dsee_controls *)payload);
#ifdef SONY_STREAM_AI
                    if(native==-11)status=WDA_STATE_ERROR;
                    else
#endif
                    if(native||WORKER_FAULT()){endpoint_fault=1;status=WDA_BACKEND;}
                    else {accepted_dsee=*(const struct sony_dsee_controls *)payload;++revision;}
                }
            }else if(command==STREAM_DSEE_STATE){
                if(bytes)status=WDA_FORMAT;
                else {response=&accepted_dsee;response_bytes=sizeof(accepted_dsee);}
            }else if(command==STREAM_OUTPUT_STATE){
                if(bytes)status=WDA_FORMAT;
                else {response=&accepted_output;response_bytes=sizeof(accepted_output);}
            }else if(command==WDA_UPDATE||command==CHAIN_TONE_UPDATE){
                if(bytes!=(command==WDA_UPDATE?sizeof(accepted):sizeof(accepted_tone.tone)))status=WDA_FORMAT;
                else if(endpoint_fault||revision==~0u||sony_stream_get_state(stream,&state)||state.sealed||state.faulted)status=WDA_STATE_ERROR;
                else {
                    int native=command==WDA_UPDATE?sony_stream_update(stream,(const struct sony_chain_settings *)payload):
                        sony_stream_update_tone(stream,(const struct sony_tone_settings *)payload);
#ifdef SONY_STREAM_AI
                    if(native==-11)status=WDA_STATE_ERROR;
                    else
#endif
                    if(native||WORKER_FAULT()){endpoint_fault=1;status=WDA_BACKEND;}
                    else {
                        ++revision;
                        if(command==WDA_UPDATE){accepted=*(const struct sony_chain_settings *)payload;accepted_tone.selector=2;}
                        else {accepted_tone.tone=*(const struct sony_tone_settings *)payload;accepted_tone.selector=3;}
                    }
                }
            }else if(command==WDA_STATE||command==CHAIN_TONE_STATE){
                if(bytes)status=WDA_FORMAT;
                else if(command==WDA_STATE){response=&accepted;response_bytes=sizeof(accepted);}
                else {response=&accepted_tone;response_bytes=sizeof(accepted_tone);}
            }else if(command==WDA_CONTROLS){
                unsigned int length=0;while(length<bytes&&((char *)payload)[length])++length;
                if(bytes<2||bytes>128||length!=bytes-1)status=WDA_FORMAT;
                else if(sony_stream_query(stream,(const char *)payload,text,sizeof(text)))status=WDA_BACKEND;
                else {response=text;while(text[response_bytes])++response_bytes;++response_bytes;}
            }else if(command==CHAIN_SAVE
#ifdef SONY_SOUND_PRESETS
                ||command==STREAM_PRESET_COMMIT
#endif
                ){
                if((status=prepare_save(command,bytes))){/* Rejected before any durable mutation. */}
                else if(endpoint_fault||sony_stream_get_state(stream,&state)||state.sealed||state.faulted)status=WDA_STATE_ERROR;
                else if(storage_uncertain)status=CHAIN_STORAGE_UNCERTAIN;
                else if(sony_snapshot_encode_supported_effects(snapshot_staged,sizeof(snapshot_staged),feature,&accepted,snapshot_staged))status=WDA_STORAGE;
                else if(accepted_tone.selector==3&&sony_snapshot_encode_tone(snapshot_staged,sizeof(snapshot_staged),&accepted_tone.tone,snapshot_staged))status=WDA_STORAGE;
                else if(sony_snapshot_encode_volume(snapshot_staged,sizeof(snapshot_staged),accepted.volume,snapshot_staged))status=WDA_STORAGE;
                else if(sony_snapshot_encode_output(snapshot_staged,sizeof(snapshot_staged),&accepted_output,snapshot_staged))status=WDA_STORAGE;
                else if(sony_snapshot_encode_dsee(snapshot_staged,sizeof(snapshot_staged),&accepted_dsee,snapshot_staged))status=WDA_STORAGE;
                else if(sony_snapshot_encode_normalizer(snapshot_staged,sizeof(snapshot_staged),accepted_normalizer,snapshot_staged))status=WDA_STORAGE;
                else if(sony_snapshot_encode_volume_memory(snapshot_staged,sizeof(snapshot_staged),&accepted_volume_memory,snapshot_staged))status=WDA_STORAGE;
                else if((status=finish_save(command))){/* Preset preparation failed. */}
                else {
                    int saved=sony_snapshot_commit(argv[2],snapshot,snapshot_staged);
                    if(saved==-117){storage_uncertain=1;status=CHAIN_STORAGE_UNCERTAIN;}
                    else if(saved)status=WDA_STORAGE;
                    else for(unsigned int i=0;i<sizeof(snapshot);++i)snapshot[i]=snapshot_staged[i];
                }
            }else if(command==WDA_CLOSE){
                if(bytes)status=WDA_FORMAT;
                else {int native=sony_stream_abort_close(&stream);if(native)status=WDA_BACKEND;if(!stream){session=0;revision=0;endpoint_fault=0;}}
            }else status=WDA_PROTOCOL;
        }
        if(stream&&WORKER_FAULT()){endpoint_fault=1;status=WDA_BACKEND;}
        if(status)response_bytes=0;
        h.command=command|WDA_REPLY;h.status=status;h.payload_bytes=response_bytes;
        h.session=session;h.revision=revision;
        if(transfer(reply_fd,&h,sizeof(h),1)||(response_bytes&&transfer(reply_fd,response,response_bytes,1)))break;
        if(quit){
#ifdef SONY_STREAM_AI
            if(ai&&sony_ai_client_close(&ai,1))return 72;
#endif
#ifdef SONY_AI_PARENT_RESOURCES
            if(sony_ai_paused_power_finish())return 72;
#endif
            return 0;
        }
    }
    if(stream)sony_stream_abort_close(&stream);
#ifdef SONY_STREAM_AI
    if(ai)sony_ai_client_close(&ai,1);
#endif
#ifdef SONY_AI_PARENT_RESOURCES
    (void)sony_ai_paused_power_finish();
#endif
    return 72;
}
