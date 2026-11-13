/* Original Sony DSP file-session endpoint. No file paths or hardware output.
 * Runtime dependencies and configuration are supplied by the caller.
 * Host tests supply config IPC fixtures; this file contains no host substitutes.
 */
#include "sony_audio_dsf_worker_protocol.h"
#include "sony_audio_dsf_session.h"
#include "sony_audio_chain_bridge.h"
#include <dlfcn.h>
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#ifdef SONY_DSF_PRIVATE_CONFIG
#include "sony_audio_snapshot_store.h"
#include "sony_audio_snapshot_config.h"
#include "sony_audio_snapshot_eq.h"
#include "sony_audio_snapshot_tone.h"
static unsigned char private_snapshot[SONY_SNAPSHOT_BYTES];
#endif
static int config_fault(void) {
#ifdef SONY_DSF_PRIVATE_CONFIG
    return sony_snapshot_config_fault();
#else
    return 0; /* External transport profile; never supplied implicitly here. */
#endif
}

struct endpoint {
    struct sony_dsf_session *file;
    struct sony_chain_bridge *chain;
    uint32_t session,next_session,request,sequence;
    int reply_fd,dead,cancelled,faulted;
    const char *config_directory;
    struct sony_dsf_info info;
    uint64_t output_acked,packets_acked;
    unsigned output_rate,output_format;
    unsigned terminal,latency;
    struct wdf_controls controls;
    unsigned control_base;
    unsigned char samples[WDF_MAX_SAMPLES];
};
static int transfer(int fd,void *data,size_t bytes,int sending) {
    size_t done=0;
    while(done<bytes) {
        ssize_t n=sending?write(fd,(char *)data+done,bytes-done):read(fd,(char *)data+done,bytes-done);
        if(n<0&&errno==EINTR)continue;
        if(n<=0)return -1;
        done+=(size_t)n;
    }
    return 0;
}
static int exchange(struct endpoint *e,unsigned kind,uint32_t arg0,uint32_t arg1,uint32_t flags,
                    void *payload,unsigned bytes,void *destination,unsigned receive,unsigned *actual) {
    if(e->dead||e->cancelled)return -EPIPE;
    struct wdf_header event={WDF_MAGIC,WDF_VERSION,kind,e->request,e->session,++e->sequence,bytes,0,arg0,arg1,flags,0},reply;
    if(transfer(e->reply_fd,&event,sizeof(event),1)||(bytes&&transfer(e->reply_fd,payload,bytes,1))||
       transfer(0,&reply,sizeof(reply),0))goto broken;
    unsigned copied=destination&&reply.payload_bytes<receive?reply.payload_bytes:destination?receive:0;
    if(copied&&transfer(0,destination,copied,0))goto broken;
    unsigned remaining=reply.payload_bytes-copied;
    while(remaining){unsigned char discard[1024];unsigned n=remaining<sizeof(discard)?remaining:sizeof(discard);if(transfer(0,discard,n,0))goto broken;remaining-=n;}
    if(actual)*actual=copied;
    if(reply.status){if(reply.status==WDF_CANCELLED)e->cancelled=1;return reply.status;}
    return 0;
broken:
    e->dead=1;return -EPROTO;
}
static int read_source(void *context,uint64_t offset,uint8_t *buffer,size_t size,size_t *actual) {
    struct endpoint *e=context;
    unsigned requested=size>UINT_MAX?UINT_MAX:(unsigned)size,received=0;
    int rc=exchange(e,WDF_READ,(uint32_t)offset,(uint32_t)(offset>>32),requested,NULL,0,buffer,requested,&received);
    if(!rc)*actual=received;
    return rc;
}
static int deliver(struct endpoint *e,const uint8_t *data,const struct sony_dsd_result *r,unsigned flags) {
    if(e->dead||e->cancelled||config_fault()||!data||!r)return -EIO;
    unsigned bytes=r->bytes<sizeof(e->samples)?r->bytes:sizeof(e->samples);
    int rc=exchange(e,WDF_SAMPLES,r->rate,r->format,flags,(void *)data,bytes,NULL,0,NULL);
    if(rc)return rc;
    e->output_rate=r->rate;e->output_format=r->format;e->output_acked+=bytes;++e->packets_acked;return 0;
}
static int packet(void *context,const uint8_t *data,size_t source,int terminal) {
    struct endpoint *e=context;struct sony_dsd_result r={0};
    if(!e->chain||e->dead||e->cancelled||!data)return -EPIPE;(void)terminal;
    unsigned char padded[32768];unsigned copied=source<sizeof(padded)?(unsigned)source:sizeof(padded);
    memcpy(padded,data,copied);if(copied<sizeof(padded))memset(padded+copied,0x69,sizeof(padded)-copied);
    if(config_fault()||sony_chain_process_dsd(e->chain,padded,sizeof(padded),e->samples,sizeof(e->samples),&r)||config_fault())return -EIO;
    int rc=deliver(e,e->samples,&r,0);if(rc)return rc;
    /* Confirmation cannot precede successful sink ACK. Generated service tails
     * use tail(), not this function. Native DSD/legacy-open stay immutable. */
    if(e->controls.pending) {
        unsigned revision=0;rc=sony_chain_dsd_commit_controls(e->chain,&revision);
        if(rc<0||config_fault())return -EIO;
    }
    rc=sony_chain_dsd_get_controls(e->chain,&e->controls.controls,&e->controls.revision,&e->controls.pending);
    if((rc&&e->controls.revision)||config_fault())return -EIO;
    return 0;
}
static int tail(void *context,const unsigned char *data,const struct sony_dsd_result *r) {
    return deliver(context,data,r,WDF_SAMPLES_TAIL);
}
static int eos(void *context) {
    struct endpoint *e=context;
    if(e->dead||e->cancelled||config_fault())return -EIO;
    struct sony_dsd_finish_result receipt;
    int rc=e->terminal?sony_chain_finish_dsd(e->chain,e->latency,tail,e,&receipt):sony_chain_send_dsd_eos(e->chain);
    if(rc||config_fault())return -EIO;
    return exchange(e,WDF_EOS,0,0,e->terminal?WDF_EOS_SONY_FINISHED:0,NULL,0,NULL,0,NULL);
}
static int release(struct endpoint *e) {
    int a=sony_dsf_session_abort_free(e->file);e->file=NULL;
    int b=e->chain?sony_chain_abort_close(e->chain):0;e->chain=NULL;
#ifdef SONY_DSF_PRIVATE_CONFIG
    if(config_fault()&&!b)b=-EIO;
    sony_snapshot_config_unbind();
#endif
    e->session=0;e->cancelled=e->faulted=0;e->output_rate=e->output_format=0;
    e->output_acked=e->packets_acked=0;e->terminal=e->latency=0;
    memset(&e->controls,0,sizeof(e->controls));e->control_base=0;
    memset(&e->info,0,sizeof(e->info));return a?a:b;
}
static int state(struct endpoint *e,struct wdf_state *out) {
    struct sony_dsf_session_status st={0};
    if(!e->file||sony_dsf_session_status(e->file,&st))return -1;
    struct wdf_state s={e->info.rate,e->info.channels,e->output_rate,e->output_format,
        (uint32_t)st.canonical_file_bytes_accepted,(uint32_t)(st.canonical_file_bytes_accepted>>32),
        (uint32_t)e->output_acked,(uint32_t)(e->output_acked>>32),(uint32_t)e->packets_acked,(uint32_t)(e->packets_acked>>32),
        st.preroll_accepted,WDF_STATE_OPEN|(st.faulted||e->faulted?WDF_STATE_FAULTED:0)|(st.finished?WDF_STATE_INPUT_FINISHED:0)};
    *out=s;return 0;
}
int main(int argc,char **argv) {
    struct endpoint e={0};uint32_t payload[sizeof(struct wdf_controls_stage)/4];
#ifdef SONY_DSF_PRIVATE_CONFIG
    if(argc!=3||strcmp(argv[1],"--snapshot-dir"))return 64;
    e.config_directory=argv[2];
#else
    (void)argv;if(argc!=1)return 64;
#endif
    signal(SIGPIPE,SIG_IGN);
    e.reply_fd=dup(1);if(e.reply_fd<0||dup2(2,1)<0)return 65;
    void *player=dlopen("libPlayerService.so",RTLD_NOW|RTLD_LOCAL),*sound=dlopen("libSoundServiceFw.so",RTLD_NOW|RTLD_LOCAL);
    if(!player||!sound)return 66;
    int exit_code=72;
    for(;;) {
        struct wdf_header h;struct wdf_state result={0};const void *response=&result;
        unsigned response_bytes=0;int status=0,quit=0;
        if(transfer(0,&h,sizeof(h),0))break;
        if(h.payload_bytes>sizeof(payload))break;
        memset(payload,0,sizeof(payload));
        if(transfer(0,payload,h.payload_bytes,0))break;
        {
            e.request=h.request;
            if(h.command==WDF_OPEN||h.command==WDF_OPEN_TERMINAL) {
                unsigned terminal=h.command==WDF_OPEN_TERMINAL;
                if(e.file&&release(&e))status=WDF_BACKEND;
                if(!status) {
                    e.session=++e.next_session;e.cancelled=e.faulted=0;
                    e.terminal=terminal;e.latency=terminal?payload[27]:0;
                    uint64_t bytes=payload[25]|((uint64_t)payload[26]<<32);
                    int rc=0;
#ifdef SONY_DSF_PRIVATE_CONFIG
                    if(sony_snapshot_read(e.config_directory,private_snapshot)||sony_snapshot_config_bind(private_snapshot))rc=-EIO;
                    struct sony_tone_settings tone;struct sony_dsee_controls dsee;
                    struct sony_chain_settings checked={0};int selected_tone=0;
                    if(!rc){
                        int decoded=sony_snapshot_decode_tone(private_snapshot,sizeof(private_snapshot),&tone);
                        if(!decoded)selected_tone=1;
                        else if(decoded!=-ENOTSUP)rc=decoded;
                        if(!rc)rc=selected_tone?sony_snapshot_apply_tone_base(private_snapshot,sizeof(private_snapshot),1,&checked):
                            sony_snapshot_apply_supported_effects(private_snapshot,sizeof(private_snapshot),1,&checked);
                        if(!rc)rc=sony_snapshot_apply_dsee(private_snapshot,sizeof(private_snapshot),&dsee);
                    }
#endif
                    if(!rc)rc=sony_dsf_session_open(&e.file,player,bytes,read_source,&e,packet,eos,&e);
                    if(!rc)rc=sony_dsf_session_info(e.file,&e.info);
#ifdef SONY_DSF_PRIVATE_CONFIG
                    if(!rc)rc=sony_chain_open_dsd_configured(&e.chain,sound,(const struct sony_chain_settings *)payload,
                        (const struct sony_dsd_controls *)(payload+17),(const struct sony_output_controls *)(payload+20),e.info.rate,
                        selected_tone?&tone:NULL,&dsee);
#else
                    if(!rc)rc=sony_chain_open_dsd(&e.chain,sound,(const struct sony_chain_settings *)payload,
                        (const struct sony_dsd_controls *)(payload+17),(const struct sony_output_controls *)(payload+20),e.info.rate);
#endif
                    if(!rc&&config_fault())rc=-EIO;
                    if(rc){status=e.cancelled?WDF_CANCELLED:WDF_BACKEND;release(&e);}
                    else if(state(&e,&result)){e.faulted=1;status=WDF_BACKEND;}
                    else response_bytes=sizeof(result);
                }
            }else if(h.command==WDF_SHUTDOWN||h.command==WDF_READY) {
                if(h.command==WDF_SHUTDOWN){if(e.file&&release(&e))status=WDF_BACKEND;quit=1;}
            }else if(!e.file)status=WDF_STATE_ERROR;
            else if(h.command>=WDF_CONTROLS_GET&&h.command<=WDF_CONTROLS_COMMIT) {
                if(e.faulted||e.cancelled)status=WDF_STATE_ERROR;
                else if(config_fault()){e.faulted=1;status=WDF_BACKEND;}
                else if(h.command==WDF_CONTROLS_STAGE) {
                    struct wdf_controls_stage stage;memcpy(&stage,payload,sizeof(stage));
                    int rc=sony_chain_dsd_stage_controls(e.chain,stage.revision,&stage.controls);
                    if(rc==-4)status=WDF_STALE;
                    else if(rc==-5)status=WDF_BUSY;
                    else if(rc==-1)status=WDF_FORMAT;
                    else if(rc||config_fault()){e.faulted=1;status=WDF_BACKEND;}
                    else {
                        if(sony_chain_dsd_get_controls(e.chain,&e.controls.controls,&e.controls.revision,&e.controls.pending)){
                            e.faulted=1;status=WDF_BACKEND;
                        }else if(e.controls.pending){e.control_base=stage.revision;e.controls.stage_request=h.request;}
                    }
                }
                if(!status){response=&e.controls;response_bytes=sizeof(e.controls);}
            }
            else if(h.command==WDF_CLOSE) {if(release(&e))status=WDF_BACKEND;}
            else if(h.command==WDF_STATE) {if(state(&e,&result))status=WDF_BACKEND;else response_bytes=sizeof(result);}
            else if(h.command==WDF_STEP) {
                if(e.faulted)status=WDF_STATE_ERROR;
                else {
                    int rc=sony_dsf_session_step(e.file);
                    if(rc<0){e.faulted=1;status=e.cancelled?WDF_CANCELLED:WDF_BACKEND;}
                    else if(state(&e,&result)){e.faulted=1;status=WDF_BACKEND;}
                    else response_bytes=sizeof(result);
                }
            }else status=WDF_PROTOCOL;
        }
        if(e.dead)break;
        struct wdf_header reply={WDF_MAGIC,WDF_VERSION,h.command|WDF_REPLY,h.request,e.session,0,
            status?0:response_bytes,status,0,0,0,0};
        if(transfer(e.reply_fd,&reply,sizeof(reply),1)||(!status&&response_bytes&&transfer(e.reply_fd,(void *)response,response_bytes,1)))break;
        if(quit){exit_code=0;break;}
    }
    if(release(&e))exit_code=73;
    /* Native process-exit callbacks still reference libpstcore after an OPEN.
     * Early dlclose of either dependency graph can unmap those callbacks;
     * reversing the unload order does not fix it. Keep both handles owned by
     * this dedicated process. Per-file objects are released above/on CLOSE;
     * the OS reclaims the libraries on exit, as in the PCM stream worker. */
    close(e.reply_fd);return exit_code;
}
