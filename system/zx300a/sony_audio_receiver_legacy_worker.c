/* Dedicated source-specific Sony PCM receiver endpoint. WDP5 data, WKP7
 * checked parent resources on fd3; no sysfs/route/unmute/capture authority. */
#include "sony_audio_pcm_sink_protocol.h"
#include "sony_audio_receiver_legacy_output.h"
#include "sony_audio_receiver_legacy_policy.h"
#include "sony_audio_dsd_power_wire.h"
#include "sony_audio_snapshot_store.h"
#include "sony_audio_snapshot_config.h"
#include "sony_audio_cfw_profile.h"
#include <dlfcn.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
static int data[WDP_MAX_FRAMES*2];
static unsigned char snapshot[SONY_SNAPSHOT_BYTES];
static int transfer(int fd,void *p,unsigned n,int sending){unsigned at=0;while(at<n){ssize_t got=sending?write(fd,(char *)p+at,n-at):read(fd,(char *)p+at,n-at);if(got<0&&errno==EINTR)continue;if(got<=0)return -1;at+=(unsigned)got;}return 0;}
static int number(const char *p,uint64_t *out){if(!p||*p<'0'||*p>'9')return -EINVAL;uint64_t n=0;while(*p>='0'&&*p<='9'){unsigned d=(unsigned)(*p++-'0');if(n>(UINT64_MAX-d)/10)return -ERANGE;n=n*10+d;}if(*p)return -EINVAL;*out=n;return 0;}
static int assure(void *v,enum sony_receiver_legacy_policy_operation op){return sony_dsd_power_wire_client_assure(v,(unsigned)op);}
static int refresh(struct sony_receiver_legacy_output *output,struct wdp_state *out){
    struct sony_receiver_legacy_state s;int rc=sony_receiver_legacy_state(output,&s);if(rc)return rc;
    *out=(struct wdp_state){s.timing.sample_rate,(unsigned)s.accepted_frames,(unsigned)(s.accepted_frames>>32),
        (s.sealed?WDP_SEALED:0)|(s.failed?WDP_FAULTED:0)|(s.drained?WDP_DRAIN_ACCEPTED:0),s.timing.period_frames,s.timing.buffer_frames};return 0;
}
static int close_output(struct sony_receiver_legacy_output **output){
    struct sony_receiver_legacy_cleanup c={0};int rc=sony_receiver_legacy_close(output,&c);
    fprintf(stderr,"LEGACY_CLOSE status=%d retained=%d accepted=%llu alsa=%llu pending=%u padding=%u discarded=%u drop=%d close=%d recovery_refused=%u blocked=%u first_error=%d\n",rc,c.retained,
        (unsigned long long)c.state.accepted_frames,(unsigned long long)c.state.alsa_confirmed_frames,c.state.buffered_frames,c.state.drain_padding_frames,c.state.discarded_buffered_frames,c.drop_status,c.close_status,c.state.recovery_refused,c.state.io_blocked_after_error,c.state.first_error);
    return rc;
}
static int release_power(struct sony_dsd_power_backend *b,uint64_t *token){
    if(!*token)return 0;int rc=b->output_closed(b->context);if(!rc)rc=b->release(b->context,*token);if(!rc)*token=0;
    fprintf(stderr,"LEGACY_RESOURCE_RELEASE status=%d retained=%u\n",rc,!!*token);return rc;
}
int main(int argc,char **argv){
    uint64_t parent,session_id,generation,receipt,profile,mode;
    if(argc!=8||strcmp(argv[1],"--receiver-pcm")||number(argv[2],&parent)||parent>INT_MAX||
       number(argv[3],&session_id)||number(argv[4],&generation)||number(argv[5],&receipt)||
       number(argv[6],&profile)||number(argv[7],&mode))return 64;
    if(signal(SIGPIPE,SIG_IGN)==SIG_ERR)return 65;
    int reply=dup(1);if(reply<0||dup2(2,1)<0)return 65;
    struct sony_dsd_power_wire_binding binding={session_id,generation,receipt,(pid_t)parent,0,0};
    struct sony_dsd_power_wire_client *power=NULL;struct sony_dsd_power_backend backend;
    if(sony_dsd_power_wire_client_attach(&power,3,&binding,3000)||sony_dsd_power_wire_client_backend(power,&backend))return 65;
    void *alsa=dlopen("libasound.so",RTLD_NOW|RTLD_GLOBAL),*library=dlopen(sony_cfw_profile_library((unsigned)profile),RTLD_NOW);
    if(!alsa||!library){fprintf(stderr,"RECEIVER_LEGACY_LOAD_ERROR %s\n",dlerror());return 66;}
    struct sony_receiver_legacy_output *output=NULL;struct wdp_state state={0};uint64_t token=0;
    uint32_t session=0,next=0;int poisoned=0,close_attempted=0,power_attempted=0,release_attempted=0;
    for(;;){
        struct wdp_header h;if(transfer(0,&h,sizeof(h),0))break;
        if(h.payload_bytes>sizeof(data))break;
        if(transfer(0,data,h.payload_bytes,0))break;
        unsigned command=h.command,bytes=0;int status=0,quit=0;
        {
            if(command==WDP_OPEN){
                if(output||poisoned)status=WDP_STATE_ERROR;
                else if(h.payload_bytes||h.frames)status=WDP_FORMAT;
                else if(sony_snapshot_read("/data/config",snapshot)||sony_snapshot_config_bind(snapshot))status=WDP_STORAGE;
                else{
                    int rc=0;if(!power_attempted){power_attempted=1;const struct sony_dsd_power_plan plan=SONY_RECEIVER_LEGACY_RESOURCE_PLAN;rc=backend.acquire(backend.context,&plan,&token);}
                    if(!rc&&!token)rc=-EPIPE;struct sony_receiver_legacy_policy policy={assure,power};
                    if(!rc){close_attempted=0;rc=sony_receiver_legacy_open(&output,library,alsa,(unsigned)profile,(unsigned)mode,h.rate,&policy);}
                    if(!rc)rc=refresh(output,&state);if(!rc&&sony_snapshot_config_fault())rc=-EIO;
                    if(rc){
                        poisoned=1;status=WDP_BACKEND;
                        if(output){close_attempted=1;int closed=close_output(&output);if(closed||output)status=WDP_CLEANUP_UNCERTAIN;}
                        if(output){session=++next;state=(struct wdp_state){h.rate,0,0,WDP_FAULTED,0,0};}
                        else sony_snapshot_config_unbind();
                    }else{session=++next;bytes=sizeof(state);fprintf(stderr,"LEGACY_OPEN profile=%u mode=%u rate=%u period=%u buffer=%u receipt=%llu\n",(unsigned)profile,(unsigned)mode,h.rate,state.period_frames,state.buffer_frames,(unsigned long long)token);}
                }
            }else if(command==WDP_READY||command==WDP_SHUTDOWN){
                if(output||h.payload_bytes||h.rate||h.frames)status=WDP_STATE_ERROR;
                else if(command==WDP_SHUTDOWN){release_attempted=1;int rc=release_power(&backend,&token);if(rc)status=WDP_CLEANUP_UNCERTAIN;quit=1;}
                else if(poisoned)status=WDP_STATE_ERROR;
            }else if(!output)status=WDP_STATE_ERROR;
            else if(command==WDP_WRITE){
                if(!h.frames||h.frames>WDP_MAX_FRAMES||h.payload_bytes!=h.frames*8||h.rate!=state.rate)status=WDP_FORMAT;
                else if(state.flags||close_attempted)status=WDP_STATE_ERROR;
                else{int rc=sony_receiver_legacy_write(output,data,h.frames);if(!rc)rc=refresh(output,&state);if(!rc&&sony_snapshot_config_fault())rc=-EIO;
                    if(rc){state.flags|=WDP_FAULTED;poisoned=1;status=WDP_BACKEND;}else bytes=sizeof(state);}
            }else if(h.payload_bytes||h.rate||h.frames)status=WDP_FORMAT;
            else if(command==WDP_DRAIN){
                if(state.flags||close_attempted||(!state.accepted_low&&!state.accepted_high))status=WDP_STATE_ERROR;
                else{state.flags|=WDP_SEALED;int rc=sony_receiver_legacy_drain(output);if(!rc)rc=refresh(output,&state);if(!rc&&sony_snapshot_config_fault())rc=-EIO;
                    if(rc){state.flags|=WDP_FAULTED;poisoned=1;status=WDP_BACKEND;}else bytes=sizeof(state);}
            }else if(command==WDP_STATE)bytes=sizeof(state);
            else if(command==WDP_CLOSE){
                if(close_attempted)status=WDP_STATE_ERROR;
                else{close_attempted=1;int rc=close_output(&output);if(rc||output){poisoned=1;state.flags|=WDP_FAULTED;status=WDP_CLEANUP_UNCERTAIN;}
                    if(!output){session=0;memset(&state,0,sizeof(state));sony_snapshot_config_unbind();}}
            }else status=WDP_PROTOCOL;
        }
        h.command=command|WDP_REPLY;h.status=status;h.session=session;h.payload_bytes=status?0:bytes;
        if(transfer(reply,&h,sizeof(h),1)||(h.payload_bytes&&transfer(reply,&state,bytes,1)))break;
        if(quit){sony_dsd_power_wire_client_destroy(&power);return status?74:0;}
    }
    /* No data retry or repeated uncertain Close/Release on EOF. Exact parent
     * reaps and independently compensates a known receipt if necessary. */
    if(output&&!close_attempted){close_attempted=1;close_output(&output);}
    if(!output&&!release_attempted){release_attempted=1;release_power(&backend,&token);}
    sony_dsd_power_wire_client_destroy(&power);return 72;
}
