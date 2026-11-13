/* Dedicated ARM/glibc native-DSD endpoint. No route/unmute/device selection.
 * The parent provides its runtime root and the WKP7 resource transport.
 * stdin/stdout=WDN1 pipes, fd3=WKP7 seqpacket. Ordinary logs go to stderr. */
#include "sony_audio_dsd_sink_protocol.h"
#include "sony_audio_dsd_native_sink.h"
#include "sony_audio_dsd_power_wire.h"
#include "sony_audio_cfw_profile.h"
#include <dlfcn.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static unsigned char data[WDN_MAX_BYTES];
static int transfer(int fd,void *p,unsigned n,int sending){
 unsigned at=0;while(at<n){ssize_t got=sending?write(fd,(char *)p+at,n-at):read(fd,(char *)p+at,n-at);
  if(got<0&&errno==EINTR)continue;if(got<=0)return -1;at+=(unsigned)got;}return 0;
}
static int refresh(struct sony_dsd_native_sink *sink,struct wdn_state *out){
 struct sony_dsd_native_sink_state s;int rc=sony_dsd_native_sink_state(sink,&s);if(rc)return rc;
 const struct sony_dsd_native_geometry *g=&s.geometry;
 *out=(struct wdn_state){g->bit_rate,(unsigned)s.writer.completed_bytes,(unsigned)(s.writer.completed_bytes>>32),
  (s.sealed?WDN_SEALED:0)|(s.failed?WDN_FAULTED:0)|(s.drain_accepted?WDN_DRAIN_ACCEPTED:0),
  g->alsa_rate,g->period_frames,g->buffer_frames,g->channels,g->format,g->access,g->bytes_per_write,g->physical_width};
 return 0;
}
int main(int argc,char **argv){
 if(argc<8)return 64;
 uint64_t parent=strtoull(argv[2],NULL,10),session_id=strtoull(argv[3],NULL,10),
  generation=strtoull(argv[4],NULL,10),receipt=strtoull(argv[5],NULL,10),
  profile=strtoull(argv[6],NULL,10),low=strtoull(argv[7],NULL,10);
 if(signal(SIGPIPE,SIG_IGN)==SIG_ERR)return 65;
 int reply=dup(1);if(reply<0||dup2(2,1)<0)return 65;
 struct sony_dsd_power_wire_binding binding={session_id,generation,receipt,(pid_t)parent,0,0};
 struct sony_dsd_power_wire_client *power=NULL;struct sony_dsd_power_backend backend;
 if(sony_dsd_power_wire_client_attach(&power,3,&binding,3000)||sony_dsd_power_wire_client_backend(power,&backend))return 65;
 void *alsa=dlopen("libasound.so",RTLD_NOW|RTLD_GLOBAL),*library=dlopen(sony_cfw_profile_library((unsigned)profile),RTLD_NOW);
 if(!alsa||!library){fprintf(stderr,"NATIVE_DSD_LOAD_ERROR %s\n",dlerror());return 66;}
 struct sony_dsd_native_sink *sink=NULL;struct wdn_state state={0};
 uint32_t session=0;int close_attempted=0;
 for(;;){
  struct wdn_header h;if(transfer(0,&h,sizeof(h),0))break;
  if(h.payload_bytes>sizeof(data))break;
  if(transfer(0,data,h.payload_bytes,0))break;
  unsigned command=h.command,bytes=0;int status=0,quit=0;
  {
   if(command==WDN_OPEN){
    if(sink){struct sony_dsd_native_sink_state cleanup;int closed=sony_dsd_native_sink_close(&sink,&cleanup);
     if(closed||sink)status=WDN_CLEANUP_UNCERTAIN;}
    if(!status){
     close_attempted=0;int rc=sony_dsd_native_sink_open(&sink,library,alsa,(unsigned)profile,h.bit_rate,(unsigned)low,&backend);
     if(!rc)(void)refresh(sink,&state);
     if(rc){
      status=WDN_BACKEND;
      if(sink){struct sony_dsd_native_sink_state cleanup;close_attempted=1;
       int closed=sony_dsd_native_sink_close(&sink,&cleanup);
       if(closed||sink)status=WDN_CLEANUP_UNCERTAIN;
      }
      if(sink){session=1;state=(struct wdn_state){.bit_rate=h.bit_rate,.flags=WDN_FAULTED};}
     }else{session=1;bytes=sizeof(state);}
    }
   }else if(command==WDN_READY||command==WDN_SHUTDOWN){
    if(command==WDN_SHUTDOWN){
     if(sink){struct sony_dsd_native_sink_state cleanup;close_attempted=1;
      int closed=sony_dsd_native_sink_close(&sink,&cleanup);if(closed||sink)status=WDN_CLEANUP_UNCERTAIN;}
     quit=1;
    }
   }else if(command==WDN_CLOSE){
    if(sink){close_attempted=1;struct sony_dsd_native_sink_state cleanup;
     int rc=sony_dsd_native_sink_close(&sink,&cleanup);if(rc||sink){state.flags|=WDN_FAULTED;status=WDN_CLEANUP_UNCERTAIN;}}
    if(!sink){session=0;memset(&state,0,sizeof(state));}
   }else if(!sink)status=WDN_STATE_ERROR;
   else if(command==WDN_WRITE){
    unsigned actual=h.bytes<h.payload_bytes?h.bytes:h.payload_bytes;
    int rc=sony_dsd_native_sink_write(sink,data,actual);
    if(!rc)(void)refresh(sink,&state);
    if(rc){state.flags|=WDN_FAULTED;status=WDN_BACKEND;}else bytes=sizeof(state);
   }
   else if(command==WDN_DRAIN){
    state.flags|=WDN_SEALED;int rc=sony_dsd_native_sink_drain(sink);
    if(!rc)(void)refresh(sink,&state);
    if(rc){state.flags|=WDN_FAULTED;status=WDN_BACKEND;}else bytes=sizeof(state);
   }else if(command==WDN_STATE)bytes=sizeof(state);
   else status=WDN_PROTOCOL;
  }
  h.command=command|WDN_REPLY;h.status=status;h.session=session;h.payload_bytes=status?0:bytes;
  if(transfer(reply,&h,sizeof(h),1)||(h.payload_bytes&&transfer(reply,&state,bytes,1)))break;
  if(quit){int rc=sony_dsd_power_wire_client_destroy(&power);return rc?74:0;}
 }
 /* EOF/transport loss: one native close only. Parent independently reaps and
  * compensates a known held receipt; do not retry an uncertain release here. */
 if(sink&&!close_attempted){struct sony_dsd_native_sink_state cleanup;int rc=sony_dsd_native_sink_close(&sink,&cleanup);(void)rc;}
 int rc=sony_dsd_power_wire_client_destroy(&power);(void)rc;return 72;
}
