/* Dedicated original-AI process. No ALSA, USB, Bluetooth or NVP endpoints. */
#define _GNU_SOURCE
#include "sony_audio_ai_bridge.h"
#include "sony_audio_ai_config.h"
#include "sony_audio_ai_protocol.h"
#include "sony_audio_feature_config.h"
#include "sony_audio_ai_process_protocol.h"
#include <dlfcn.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <unistd.h>
struct worker {
    int fd;int64_t timeout;uint64_t nonce;uint32_t last;
    struct sony_ai_bridge *bridge;
    struct wai_packet packet;
    float output[8192];
};
static int fault(void){return sony_ai_config_fault()||sony_audio_feature_config_fault();}
static int decimal(const char *s,uint32_t *out){
    uint32_t v=0;if(!s||!*s)return -1;
    for(unsigned i=0;s[i];++i){
        if(s[i]<'0'||s[i]>'9'||v>UINT32_MAX/10||
           (v==UINT32_MAX/10&&(unsigned)(s[i]-'0')>UINT32_MAX%10))return -1;
        v=v*10+(unsigned)(s[i]-'0');
    }
    *out=v;return 0;
}
static int64_t deadline(struct worker *w){int64_t n=wai_now_ms();return n<0?-1:n+w->timeout;}
static struct wai_header reply(struct worker *w,const struct wai_header *request,int status){
    struct wai_header r={WAI_MAGIC,WAI_VERSION,request->command|WAI_REPLY,request->request,
        (uint32_t)w->nonce,(uint32_t)(w->nonce>>32),0,status,0,0,0,0};return r;
}
static int state(struct worker *w,struct wai_state *out){
    struct sony_ai_status s;int r=sony_ai_bridge_status(w->bridge,&s);if(r||fault())return -EIO;
    memset(out,0,sizeof(*out));out->input_rate=s.input_rate;out->output_rate=s.output_rate;
    out->quantum=s.source_quantum;out->enabled=s.requested_enabled;out->expected=s.processing_expected;
    out->observed=s.processing_observed;out->phase=s.finished?WAI_ENDED:s.source_frames?WAI_STREAMING:WAI_OPENED;
    out->source_lo=(uint32_t)s.source_frames;out->source_hi=(uint32_t)(s.source_frames>>32);
    out->padding_lo=(uint32_t)s.padding_source_frames;out->padding_hi=(uint32_t)(s.padding_source_frames>>32);
    out->produced_lo=(uint32_t)s.produced_frames;out->produced_hi=(uint32_t)(s.produced_frames>>32);
    out->tail_lo=(uint32_t)s.tail_acknowledged_frames;out->tail_hi=(uint32_t)(s.tail_acknowledged_frames>>32);return 0;
}
struct tail_context {struct worker *worker;struct wai_header request;uint32_t index;int64_t deadline;};
static int tail(void *opaque,const float *samples,unsigned frames,unsigned rate){
    struct tail_context *c=opaque;struct worker *w=c->worker;
    if(!frames||frames>WAI_MAX_BYTES/8||c->index==UINT32_MAX||fault())return -EPROTO;
    struct wai_header h=reply(w,&c->request,0);h.bytes=frames*8;h.frames=frames;h.rate=rate;
    h.flags=WAI_MORE;h.index=++c->index;
    int r=wai_send(w->fd,&h,samples,c->deadline);if(r)return r;
    r=wai_receive(w->fd,&w->packet,c->deadline);if(r)return r;
    const struct wai_header *a=&w->packet.h;
    if(a->status)return a->status<0?a->status:-EIO;
    return 0;
}
int main(int argc,char **argv){
    uint32_t lo=0,hi=0,timeout=0,ready_arg=0;
    int ready=argc==6?4:-1;
    if((argc!=4&&argc!=6)||(ready>=0&&(strcmp(argv[4],"--ready-fd")||decimal(argv[5],&ready_arg)))||
       decimal(argv[1],&lo)||decimal(argv[2],&hi)||decimal(argv[3],&timeout)){fprintf(stderr,"SonyAI startup arguments invalid\n");return 64;}
    if(ready>=0)ready=(int)ready_arg;
    (void)prctl(PR_SET_PDEATHSIG,SIGKILL);
    if(dup2(2,1)<0)return 70; /* Original log output must never enter the socket. */
    struct sony_audio_feature_context context={7,5};
    if(sony_audio_feature_config_bind(&context)||sony_ai_config_bind(0))return 70;
    void *library=dlopen("libSoundServiceFw.so",RTLD_NOW);
    if(!library||fault()){fprintf(stderr,"SonyAI library load failed: %s\n",library?"configuration fault":dlerror());return 71;}
    struct worker *w=calloc(1,sizeof(*w));if(!w)return 70;
    w->fd=0;w->timeout=timeout;w->nonce=wai_join(lo,hi);int result=74;
    if(ready>=0){
        struct wai_process_ready r={WAI_PROCESS_READY_MAGIC,1,lo,hi,(uint32_t)getpid(),
            (uint32_t)getuid(),(uint32_t)geteuid(),(uint32_t)getgid(),(uint32_t)getegid(),
            WAI_PROCESS_LIBRARIES_READY,0,0};
        ssize_t sent=send(ready,&r,sizeof(r),MSG_NOSIGNAL|MSG_DONTWAIT);close(ready);
        if(sent!=sizeof(r)){free(w);return 70;}
    }
    for(;;){
        int r=wai_receive(w->fd,&w->packet,-1);if(r)break;
        struct wai_header h=w->packet.h;
        w->last=h.request;struct wai_header out=reply(w,&h,0);struct wai_state snapshot;
        const void *payload=NULL;int status=0,quit=0;int64_t end=deadline(w);
        if(end<0)break;
        if(h.command==WAI_OPEN){
            uint32_t enabled=2;
            if(h.bytes>=4)memcpy(&enabled,w->packet.payload,4);
            if(w->bridge)status=-EBUSY;
            else if(h.bytes<4)status=-EINVAL;
            else{
                if(sony_ai_config_bind((int)enabled))status=-EIO;
                else status=sony_ai_bridge_open(&w->bridge,library,h.rate,(int)enabled);
                if(!status)status=state(w,&snapshot);
                if(!status){out.bytes=sizeof(snapshot);out.rate=snapshot.output_rate;payload=&snapshot;}
            }
        }else if(h.command==WAI_ABORT&&!w->bridge){
            quit=1;
        }else if(!w->bridge)status=-EPIPE;
        else if(h.command==WAI_PROCESS){
            if(h.frames>h.bytes/8)status=-EINVAL;
            else{
                unsigned frames=0,rate=0;
                status=sony_ai_bridge_process(w->bridge,(const float *)w->packet.payload,h.frames,
                    w->output,sizeof(w->output)/8,&frames,&rate);
                if(!status&&!fault()){out.frames=frames;out.rate=rate;out.bytes=frames*8;payload=w->output;}
                else if(!status)status=-EIO;
            }
        }else if(h.command==WAI_STATE){
            status=state(w,&snapshot);if(!status){out.bytes=sizeof(snapshot);out.rate=snapshot.output_rate;payload=&snapshot;}
        }else if(h.command==WAI_SET){
            uint32_t enabled=2;if(h.bytes>=4)memcpy(&enabled,w->packet.payload,4);
            if(h.bytes<4)status=-EINVAL;
            else{
                if(sony_ai_config_bind((int)enabled))status=-EIO;
                else status=sony_ai_bridge_set_enabled(w->bridge,(int)enabled);
                if(!status)status=state(w,&snapshot);
                if(!status){out.bytes=sizeof(snapshot);out.rate=snapshot.output_rate;payload=&snapshot;}
            }
        }else if(h.command==WAI_FINISH){
            uint32_t ms=0;if(h.bytes>=4)memcpy(&ms,w->packet.payload,4);
            if(h.bytes<4)status=-EINVAL;
            else{
                struct tail_context capture={w,h,0,end};
                status=sony_ai_bridge_finish(w->bridge,ms,tail,&capture);
                if(status==1){status=0;out.flags=WAI_ALREADY;}
                if(!status)status=state(w,&snapshot);
                if(!status){out.bytes=sizeof(snapshot);out.rate=snapshot.output_rate;payload=&snapshot;}
            }
        }else if(h.command==WAI_CLOSE||h.command==WAI_ABORT){
            status=sony_ai_bridge_close(&w->bridge,h.command==WAI_ABORT);if(!status)quit=1;
        }else status=-EPROTO;
        if(fault()&&!status)status=-EIO;
        if(status){fprintf(stderr,"SonyAI command=%u failed=%d config_fault=%d feature_fault=%d\n",h.command,status,sony_ai_config_fault(),sony_audio_feature_config_fault());out=reply(w,&h,status);payload=NULL;}
        if(wai_send(w->fd,&out,payload,end)||status)break;
        if(quit){result=0;break;}
    }
    if(w->bridge)sony_ai_bridge_close(&w->bridge,1);
    fprintf(stderr,"SonyAI exit=%d key251_reads=%u request_mirrors=%u config_fault=%d feature_fault=%d\n",
        result,sony_ai_config_reads(),sony_ai_config_mirrors(),sony_ai_config_fault(),sony_audio_feature_config_fault());
    close(w->fd);free(w);return result;
}
