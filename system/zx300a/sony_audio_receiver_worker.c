/* Original Sony receiver PCM DSP endpoint. No hardware or persistence. */
#include "sony_audio_receiver_worker_protocol.h"
#include <dlfcn.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#ifdef SONY_RECEIVER_PRIVATE_CONFIG
#include "sony_audio_snapshot_store.h"
#include "sony_audio_snapshot_config.h"
#include "sony_audio_snapshot_eq.h"
#include "sony_audio_snapshot_tone.h"
static unsigned char private_snapshot[SONY_SNAPSHOT_BYTES];
#endif
struct endpoint {
    struct sony_chain_bridge *chain;
    uint32_t session,next_session,request,sequence,frame_bytes,flags;
    uint64_t input_processed,output_acked,packets_acked;
    struct sony_receiver_chain_result last_output;
    unsigned padding_ms,padding_acked;
    unsigned service,bluetooth,pending_frames,service_head_sent,volume;
    unsigned char pending[4096];
    int reply_fd,dead,cancelled;
    const char *directory;
    unsigned feature;
    unsigned char samples[WUR_MAX_OUTPUT];
};
static int transfer(int fd,void *p,size_t bytes,int sending) {
    size_t done=0;
    while(done<bytes){
        ssize_t n=sending?write(fd,(char *)p+done,bytes-done):read(fd,(char *)p+done,bytes-done);
        if(n<0&&errno==EINTR)continue;
        if(n<=0)return -1;done+=(size_t)n;
    }
    return 0;
}
static int discard(int fd,uint32_t bytes) {
    unsigned char chunk[4096];
    while(bytes){
        unsigned n=bytes<sizeof(chunk)?bytes:sizeof(chunk);
        if(transfer(fd,chunk,n,0))return -1;bytes-=n;
    }
    return 0;
}
static int config_fault(void) {
#ifdef SONY_RECEIVER_PRIVATE_CONFIG
    return sony_snapshot_config_fault();
#else
    return 0; /* Only explicitly launched --external-config mode. */
#endif
}
static int bind_config(struct endpoint *e,const struct wur_open *o) {
#ifdef SONY_RECEIVER_PRIVATE_CONFIG
    if(sony_snapshot_read(e->directory,private_snapshot))return -1;
    (void)o;
    return sony_snapshot_config_bind(private_snapshot);
#else
    (void)e;(void)o;return 0;
#endif
}
static int release(struct endpoint *e) {
    int bad=config_fault();int rc=e->chain?sony_chain_abort_close(e->chain):0;e->chain=NULL;
#ifdef SONY_RECEIVER_PRIVATE_CONFIG
    if(config_fault())bad=1;sony_snapshot_config_unbind();
#endif
    e->session=e->frame_bytes=e->flags=e->padding_ms=e->padding_acked=0;e->cancelled=0;
    e->service=e->bluetooth=e->pending_frames=e->service_head_sent=e->volume=0;memset(e->pending,0,sizeof(e->pending));
    e->input_processed=e->output_acked=e->packets_acked=0;memset(&e->last_output,0,sizeof(e->last_output));
    return bad||rc?-1:0;
}
static void state(const struct endpoint *e,struct wur_state *s) {
    struct wur_state v={e->flags,(uint32_t)e->input_processed,(uint32_t)(e->input_processed>>32),
        (uint32_t)e->output_acked,(uint32_t)(e->output_acked>>32),(uint32_t)e->packets_acked,(uint32_t)(e->packets_acked>>32),
        e->last_output.track[0],e->last_output.track[1],e->last_output.track[11],e->request,e->sequence,
        e->padding_ms,e->padding_acked,!!(e->flags&WUR_FLAG_STOPPED),e->service?e->pending_frames:0};*s=v;
}
static int deliver(struct endpoint *e,const void *bytes,const struct sony_receiver_chain_result *r,unsigned flags) {
    if(e->dead||config_fault()||!r||!bytes||r->bytes>WUR_MAX_OUTPUT)return -1;
    if(!r->bytes){e->last_output=*r;return 0;}
    struct wur_header h={WUR_MAGIC,WUR_VERSION,WUR_SAMPLES,e->request,e->session,++e->sequence,
        sizeof(*r)+r->bytes,0,r->track[0],r->track[11],flags,0},reply;
    if(transfer(e->reply_fd,&h,sizeof(h),1)||transfer(e->reply_fd,(void *)r,sizeof(*r),1)||
       transfer(e->reply_fd,(void *)bytes,r->bytes,1)||transfer(0,&reply,sizeof(reply),0))goto broken;
    if(discard(0,reply.payload_bytes))goto broken;
    if(reply.status==WUR_CANCELLED){e->cancelled=1;return -1;}
    if(reply.status)goto broken;
    e->last_output=*r;e->output_acked+=r->bytes;++e->packets_acked;return 0;
broken:
    e->dead=1;return -1;
}
static int tail(void *context,const void *bytes,const struct sony_receiver_chain_result *r) {
    return deliver(context,bytes,r,WUR_TAIL);
}
static int service_packet(struct endpoint *e,const void *bytes,unsigned frames,unsigned terminal) {
    struct sony_receiver_chain_result r;
    unsigned flags=terminal|(e->service_head_sent?0:SONY_RECEIVER_HEAD);
    int rc=sony_chain_process_receiver_service(e->chain,bytes,frames,flags,e->samples,sizeof(e->samples),&r);
    if(!rc){e->service_head_sent=1;rc=deliver(e,e->samples,&r,0);}return rc;
}
int main(int argc,char **argv) {
    struct endpoint e={0};
#ifdef SONY_RECEIVER_PRIVATE_CONFIG
    if(argc!=4||strcmp(argv[1],"--snapshot-dir")||(strcmp(argv[3],"0")&&strcmp(argv[3],"1")))return 64;
    e.directory=argv[2];e.feature=(unsigned)(argv[3][0]-'0');
#else
    if(argc!=2||strcmp(argv[1],"--external-config"))return 64;
#endif
    if(signal(SIGPIPE,SIG_IGN)==SIG_ERR)return 70;
    e.reply_fd=dup(1);if(e.reply_fd<0||dup2(2,1)<0)return 70;
    void *library=dlopen("libSoundServiceFw.so",RTLD_NOW);if(!library)return 71;
    uint32_t payload[WUR_MAX_INPUT/4];int exit_code=72;
    for(;;){
        struct wur_header h;struct wur_state result;unsigned reply_bytes=0;int status=0,quit=0;
        if(transfer(0,&h,sizeof(h),0))break;
        h.command&=~WUR_REPLY;
        unsigned received=h.payload_bytes<sizeof(payload)?h.payload_bytes:sizeof(payload);
        memset(payload,0,sizeof(payload));
        if(transfer(0,payload,received,0)||discard(0,h.payload_bytes-received))break;
        h.payload_bytes=received;
        int service_process=h.command==WUR_SERVICE_PROCESS||h.command==WUR_SERVICE_FINAL;
        int process=h.command==WUR_PROCESS||h.command==WUR_PROCESS_FINAL||service_process;
        e.request=h.request;e.cancelled=0;
            if(h.command==WUR_OPEN||h.command==WUR_OPEN_SERVICE||h.command==WUR_OPEN_BT_SERVICE){
                if(e.chain&&release(&e))status=WUR_BACKEND;
                if(!status){
                    const struct wur_open *o=(const void *)payload;
                    if(bind_config(&e,o))status=WUR_STORAGE;
                    else{
                        int rc=h.command==WUR_OPEN_BT_SERVICE?sony_chain_open_receiver_bt_service(&e.chain,library,&o->settings,o->track):
                            h.command==WUR_OPEN_SERVICE?sony_chain_open_receiver_usb_service(&e.chain,library,&o->settings,o->track):
                            sony_chain_open_receiver_usb_pcm(&e.chain,library,&o->settings,o->track);
                        if(!rc)rc=sony_chain_update_output(e.chain,&o->output);
                        if(!rc)rc=sony_chain_update(e.chain,&o->settings);
                        if(!rc&&o->selector==3)rc=sony_chain_update_tone(e.chain,&o->tone);
                        if(!rc)rc=sony_chain_update_dsee(e.chain,&o->dsee);
                        if(rc||config_fault()){release(&e);status=WUR_BACKEND;}
                        else{
                            uint64_t frame_bytes=(uint64_t)o->track[11]*2;
                            e.session=++e.next_session;e.frame_bytes=frame_bytes&&frame_bytes<=UINT32_MAX?(uint32_t)frame_bytes:1;
                            e.volume=(unsigned)o->settings.volume;
                            e.bluetooth=h.command==WUR_OPEN_BT_SERVICE;e.service=h.command!=WUR_OPEN;
                            e.flags=WUR_FLAG_OPEN|(e.service?WUR_FLAG_SERVICE:0)|(e.bluetooth?WUR_FLAG_BT_SERVICE:0);
                            state(&e,&result);reply_bytes=sizeof(result);
                        }
                    }
                }
            }else if(h.command==WUR_READY||h.command==WUR_SHUTDOWN){
                quit=h.command==WUR_SHUTDOWN;
            }else if(!e.chain&&h.command!=WUR_CLOSE&&h.command!=WUR_STATE)status=WUR_STATE_ERROR;
            else if(h.command==WUR_CLOSE){
                if(release(&e))status=WUR_BACKEND;
            }else if(h.command==WUR_STATE){
                state(&e,&result);reply_bytes=sizeof(result);
            }else if(config_fault()){e.flags|=WUR_FLAG_FAULTED;status=WUR_BACKEND;}
            else if(h.command==WUR_VOLUME){
                    int rc=0;
#ifdef SONY_RECEIVER_PRIVATE_CONFIG
                    unsigned char next[SONY_SNAPSHOT_BYTES];
                    rc=sony_snapshot_encode_volume(private_snapshot,sizeof(private_snapshot),(int)payload[1],next);
#endif
                    if(!rc)rc=sony_chain_receiver_volume(e.chain,payload[0],payload[1]);
                    if(!rc&&config_fault())rc=-1;
#ifdef SONY_RECEIVER_PRIVATE_CONFIG
                    if(!rc)rc=sony_snapshot_config_bind(next);
                    if(!rc)memcpy(private_snapshot,next,sizeof(next));
#endif
                    if(rc){e.flags|=WUR_FLAG_FAULTED;status=WUR_BACKEND;}
                    else{e.volume=payload[1];state(&e,&result);reply_bytes=sizeof(result);}
            }else if(process){
                unsigned frames=h.payload_bytes/e.frame_bytes;
                int final=h.command==WUR_PROCESS_FINAL||h.command==WUR_SERVICE_FINAL;
                    int rc=0;
                    if(e.bluetooth){
                        /* Original latency2 queue depth is zero: no delayed
                         * last packet or synthetic terminal marker. Admission
                         * is not a claim of successful native consumption. */
                        e.input_processed+=frames;if(final)e.flags|=WUR_FLAG_INPUT_SEALED;
                        rc=service_packet(&e,payload,frames,0);
                    }else if(e.service){
                        unsigned old_frames=e.pending_frames;unsigned char old[4096];
                        if(old_frames)memcpy(old,e.pending,old_frames*e.frame_bytes);
                        memcpy(e.pending,payload,h.payload_bytes);e.pending_frames=frames;e.input_processed+=frames;
                        if(final)e.flags|=WUR_FLAG_INPUT_SEALED;
                        if(old_frames)rc=service_packet(&e,old,old_frames,0);
                    }else{
                        struct sony_receiver_chain_result r;
                        rc=sony_chain_process_receiver_pcm(e.chain,payload,frames,e.samples,sizeof(e.samples),&r);
                        if(!rc){e.input_processed+=frames;if(final)e.flags|=WUR_FLAG_INPUT_SEALED;rc=deliver(&e,e.samples,&r,0);}
                    }
                    if(rc||config_fault()){e.flags|=WUR_FLAG_FAULTED;status=e.cancelled?WUR_CANCELLED:WUR_BACKEND;}
                    else{state(&e,&result);reply_bytes=sizeof(result);}
            }else if(h.command==WUR_SILENCE){
                    e.padding_ms=payload[0];e.flags|=WUR_FLAG_INPUT_SEALED|WUR_FLAG_PADDING_USED;
                    int rc=sony_chain_render_silence_receiver_pcm(e.chain,e.padding_ms,tail,&e,&e.padding_acked);
                    if(rc||config_fault()){e.flags|=WUR_FLAG_FAULTED;status=e.cancelled?WUR_CANCELLED:WUR_BACKEND;}
                    else{state(&e,&result);reply_bytes=sizeof(result);}
            }else if(h.command==WUR_SONY_STOP){
                    int rc=sony_chain_stop_receiver_pcm(e.chain);
                    if(rc<0||config_fault()){e.flags|=WUR_FLAG_FAULTED;status=WUR_BACKEND;}
                    else{e.flags|=WUR_FLAG_INPUT_SEALED|WUR_FLAG_STOPPED;state(&e,&result);reply_bytes=sizeof(result);}
            }else if(h.command==WUR_SERVICE_FINISH){
                    unsigned chosen=payload[0]==WUR_SERVICE_STOP?WUR_FLAG_SERVICE_STOP:WUR_FLAG_SERVICE_DRAIN;
                    if(e.flags&WUR_FLAG_STOPPED){state(&e,&result);reply_bytes=sizeof(result);}
                    else{
                        e.flags|=WUR_FLAG_INPUT_SEALED|chosen;int rc=0;
                        if(e.pending_frames){
                            unsigned frames=e.pending_frames;e.pending_frames=0;
                            rc=service_packet(&e,e.pending,frames,payload[0]);
                        }
                        if(!rc&&config_fault())rc=-1;
                        if(!rc&&e.input_processed&&!e.bluetooth){
                            e.padding_ms=100;e.flags|=WUR_FLAG_PADDING_USED;
                            rc=sony_chain_render_silence_receiver_pcm(e.chain,100,tail,&e,&e.padding_acked);
                        }
                        if(!rc&&config_fault())rc=-1;
                        if(!rc&&payload[0]==WUR_SERVICE_DRAIN)rc=sony_chain_send_eos(e.chain);
                        if(!rc&&config_fault())rc=-1;
                        if(!rc)rc=sony_chain_stop_receiver_pcm(e.chain);
                        if(rc||config_fault()||e.cancelled){e.flags|=WUR_FLAG_FAULTED;status=e.cancelled?WUR_CANCELLED:WUR_BACKEND;}
                        else{e.flags|=WUR_FLAG_STOPPED;state(&e,&result);reply_bytes=sizeof(result);}
                    }
            }else status=WUR_PROTOCOL;
        if(e.dead)break;
        struct wur_header reply={WUR_MAGIC,WUR_VERSION,h.command|WUR_REPLY,h.request,e.session,0,status?0:reply_bytes,status,0,0,0,0};
        if(transfer(e.reply_fd,&reply,sizeof(reply),1)||(!status&&reply_bytes&&transfer(e.reply_fd,&result,reply_bytes,1)))break;
        if(quit){exit_code=0;break;}
    }
    if(release(&e))exit_code=73;
    /* Keep the original library owned until process exit: native atexit
     * callbacks can still reference this dependency graph after manager exit. */
    close(e.reply_fd);return exit_code;
}
