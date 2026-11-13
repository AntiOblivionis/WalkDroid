/* EXPLICIT hardware-capable worker. Never launch without a verified target,
 * private root, IO guard and external route ownership. No automatic service. */
#include "sony_audio_pcm_sink_protocol.h"
#include "sony_audio_output_bridge.h"
#include "sony_audio_snapshot_store.h"
#include "sony_audio_snapshot_config.h"
#include "sony_audio_cfw_profile.h"
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <dlfcn.h>
static int pcm_data[WDP_MAX_FRAMES*2];
static unsigned char snapshot[SONY_SNAPSHOT_BYTES];
static int transfer(int fd,void *p,unsigned n,int sending){
    unsigned at=0;while(at<n){ssize_t rc=sending?write(fd,(char *)p+at,n-at):read(fd,(char *)p+at,n-at);
        if(rc<0&&errno==EINTR)continue;if(rc<=0)return -1;at+=(unsigned)rc;}return 0;
}
int main(int argc,char **argv){
    if((argc!=4&&argc!=6)||strcmp(argv[1],"--snapshot-dir")||strcmp(argv[3],"--allow-pcm-output"))return 64;
    const char *library="libaudiohal-adleralsa.so"; /* Frozen legacy entry. */
    if(argc==6){
        if(strcmp(argv[4],"--cfw-profile")||argv[5][0]<'0'||argv[5][0]>'2'||argv[5][1])return 64;
        library=sony_cfw_profile_library(argv[5][0]-'0');
    }
    /* Only this dedicated child changes SIGPIPE. Parent APIs retain disposition. */
    if(signal(SIGPIPE,SIG_IGN)==SIG_ERR)return 65;
    int reply=dup(1);if(reply<0||dup2(2,1)<0)return 65;
    void *alsa=dlopen("libasound.so",RTLD_NOW|RTLD_GLOBAL);
    void *adler=dlopen(library,RTLD_NOW);if(!alsa||!adler)return 66;
    struct sony_output_bridge *output=NULL;struct wdp_state state={0};
    uint32_t session=0,next=0;uint64_t accepted=0;int poisoned=0;
    for(;;){
        struct wdp_header h;if(transfer(0,&h,sizeof(h),0))break;
        if(h.payload_bytes>sizeof(pcm_data))break;
        if(transfer(0,pcm_data,h.payload_bytes,0))break;
        uint32_t command=h.command;int status=0,quit=0;unsigned bytes=0;
        {
            if(command==WDP_OPEN){
                if(output||poisoned)status=WDP_STATE_ERROR;
                else if(h.payload_bytes||h.frames)status=WDP_FORMAT;
                else if(sony_snapshot_read(argv[2],snapshot)||sony_snapshot_config_bind(snapshot))status=WDP_STORAGE;
                else {
                    int rc=sony_output_open_rate(&output,adler,h.rate);struct sony_output_timing timing={0};
                    if(!rc)rc=sony_output_get_timing(output,&timing);
                    if(!rc&&sony_snapshot_config_fault())rc=-5;
                    if(rc){
                        poisoned=1;status=WDP_CLEANUP_UNCERTAIN;
                        if(output)sony_output_abort_close(&output);
                        /* Retained cleanup ownership must remain addressable. */
                        if(output){session=++next;state=(struct wdp_state){h.rate,0,0,WDP_FAULTED,0,0};}
                    }else {
                        session=++next;accepted=0;state=(struct wdp_state){h.rate,0,0,0,timing.period_frames,timing.buffer_frames};bytes=sizeof(state);
                    }
                }
            }else if(command==WDP_READY||command==WDP_SHUTDOWN){
                if(output||h.payload_bytes||h.rate||h.frames)status=WDP_STATE_ERROR;
                else if(command==WDP_SHUTDOWN)quit=1;
                else if(poisoned)status=WDP_STATE_ERROR;
            }else if(!output)status=WDP_STATE_ERROR;
            else if(command==WDP_WRITE){
                if(!h.frames||h.frames>WDP_MAX_FRAMES||h.payload_bytes!=h.frames*8||h.rate!=state.rate)status=WDP_FORMAT;
                else if(state.flags)status=WDP_STATE_ERROR;
                else {
                    int rc=sony_output_write(output,pcm_data,h.frames);
                    if(!rc&&sony_snapshot_config_fault())rc=-5;
                    if(rc){state.flags|=WDP_FAULTED;poisoned=1;status=WDP_BACKEND;}
                    else {accepted+=h.frames;state.accepted_low=(uint32_t)accepted;state.accepted_high=(uint32_t)(accepted>>32);bytes=sizeof(state);}
                }
            }else if(h.payload_bytes||h.rate||h.frames)status=WDP_FORMAT;
            else if(command==WDP_DRAIN){
                if(state.flags||!accepted)status=WDP_STATE_ERROR;
                else {
                    state.flags|=WDP_SEALED;int rc=sony_output_drain(output);
                    if(!rc&&sony_snapshot_config_fault())rc=-5;
                    if(rc){state.flags|=WDP_FAULTED;poisoned=1;status=WDP_BACKEND;}
                    else {state.flags|=WDP_DRAIN_ACCEPTED;bytes=sizeof(state);}
                }
            }else if(command==WDP_STATE)bytes=sizeof(state);
            else if(command==WDP_CLOSE){
                int rc=sony_output_abort_close(&output);
                if(rc){poisoned=1;state.flags|=WDP_FAULTED;status=WDP_CLEANUP_UNCERTAIN;}
                if(!output){session=0;accepted=0;memset(&state,0,sizeof(state));sony_snapshot_config_unbind();}
            }else status=WDP_PROTOCOL;
        }
        h.command=command|WDP_REPLY;h.status=status;h.session=session;h.payload_bytes=status?0:bytes;
        if(transfer(reply,&h,sizeof(h),1)||(h.payload_bytes&&transfer(reply,&state,bytes,1)))break;
        if(quit)return 0;
    }
    if(output)sony_output_abort_close(&output);
    return 72;
}
