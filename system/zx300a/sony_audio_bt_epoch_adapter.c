#define _GNU_SOURCE
/* Explicit WBE2 original RF adapter. The qualified WBL1 source/template is
 * unchanged; only this new translation unit replaces its transport methods
 * and connection callbacks. Never clear the native decoder app-ready bit on
 * an ordinary source PAUSE: original LDAC START waits for decoder state2
 * before reporting connection5. Mode OFF retains original app-state0/deinit.
 * Raw4 closes this source; raw5 permits the next. Late boundary callbacks
 * remain counted. This is an experiment until actual RF byte/lifecycle tests. */
#include "sony_audio_bt_epoch_source.h"
#include "sony_audio_bt_live_source.h"
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>
struct sony_bt_epoch_adapter {
    struct sony_bt_epoch_source *impl;int error;unsigned initialized,started,sealed,listening,stop_requested;
    uint64_t frames,bytes,offered_frames,offered_bytes;
    uint64_t volume_offered,volume_sent,volume_closing_drops,closing_bytes;
    unsigned queued_peak_frames,packets,offered_packets,closing_packets;
};
static int epoch_error(struct sony_bt_epoch_adapter *a,int rc){if(rc<0){int zero=0;__atomic_compare_exchange_n(&a->error,&zero,rc,0,__ATOMIC_RELEASE,__ATOMIC_RELAXED);}return rc;}
static int epoch_init(struct sony_bt_epoch_adapter *a,int fd,pid_t parent,uint64_t cookie,uint64_t source){(void)source;if(!a||a->impl)return -EINVAL;int rc=sony_bt_epoch_source_create(&a->impl,fd,parent,cookie);if(!rc)a->initialized=1;return epoch_error(a,rc);}
static int epoch_format(struct sony_bt_epoch_adapter *a,const struct sony_bt_middleware_sound *raw){return epoch_error(a,sony_bt_epoch_source_format(a->impl,raw));}
static int epoch_listening(struct sony_bt_epoch_adapter *a){int rc=sony_bt_epoch_source_listening(a->impl);if(!rc)a->listening=1;return rc==-EAGAIN?rc:epoch_error(a,rc);}
static int epoch_start(struct sony_bt_epoch_adapter *a){a->started=1;return 0;}
static int epoch_poll(struct sony_bt_epoch_adapter *a){int rc=sony_bt_epoch_source_poll(a->impl);if(rc==2)a->stop_requested=1;return epoch_error(a,rc);}
static unsigned epoch_ready(const struct sony_bt_epoch_adapter *a){return sony_bt_epoch_source_ready(a->impl);}
static int epoch_pcm(struct sony_bt_epoch_adapter *a,unsigned bytes,const void *data){return epoch_error(a,sony_bt_epoch_source_pcm(a->impl,bytes,data));}
static int epoch_fail(struct sony_bt_epoch_adapter *a,int rc){return epoch_error(a,sony_bt_epoch_source_error(a->impl,rc));}
static int epoch_volume(struct sony_bt_epoch_adapter *a,int step){int rc=sony_bt_epoch_source_volume(a->impl,step);if(rc==-ESHUTDOWN){__atomic_add_fetch(&a->volume_closing_drops,1,__ATOMIC_RELAXED);return 0;}return epoch_error(a,rc);}
static int epoch_stop(struct sony_bt_epoch_adapter *a){a->stop_requested=1;return epoch_error(a,sony_bt_epoch_source_stop_begin(a->impl));}
static void epoch_snapshot(struct sony_bt_epoch_adapter *a,const struct sony_bt_epoch_source_state *s){if(s->packets>UINT_MAX||s->offered_packets>UINT_MAX||s->boundary_packets>UINT_MAX)epoch_error(a,-EOVERFLOW);a->frames=s->frames;a->bytes=s->bytes;a->offered_frames=s->offered_frames;a->offered_bytes=s->offered_bytes;a->packets=(unsigned)s->packets;a->offered_packets=(unsigned)s->offered_packets;a->volume_offered=s->volume_offered;a->volume_sent=s->volume_sent;a->closing_packets=(unsigned)s->boundary_packets;a->closing_bytes=s->boundary_bytes;a->queued_peak_frames=s->queued_peak_frames;a->sealed=s->sealed;epoch_error(a,s->first_error);}
static int epoch_finish(struct sony_bt_epoch_adapter *a){int rc=sony_bt_epoch_source_finish(a->impl);if(!rc){struct sony_bt_epoch_source_state s;rc=sony_bt_epoch_source_state(a->impl,&s);if(!rc)epoch_snapshot(a,&s);}return rc==-EAGAIN?rc:epoch_error(a,rc);}
#define SONY_BT_WITH_LIVE_WIRE 1
#define sony_bt_live_source sony_bt_epoch_adapter
#define sony_bt_live_source_init epoch_init
#define sony_bt_live_source_format epoch_format
#define sony_bt_live_source_listening epoch_listening
#define sony_bt_live_source_start epoch_start
#define sony_bt_live_source_poll epoch_poll
#define sony_bt_live_source_ready epoch_ready
#define sony_bt_live_source_pcm epoch_pcm
#define sony_bt_live_source_error epoch_fail
#define sony_bt_live_source_volume_step epoch_volume
#define sony_bt_live_source_stop_begin epoch_stop
#define sony_bt_live_source_finish epoch_finish
#define snk_connection epoch_old_connection
#define snk_peer epoch_old_peer
#define SONY_BT_SOURCE_ENTRY sony_bt_epoch_rf_main
#include "sony_audio_bt_receiver_source.inc"
#undef snk_connection
#undef snk_peer
static void epoch_event(const char *kind,unsigned raw){static unsigned count;unsigned n=__atomic_add_fetch(&count,1,__ATOMIC_RELAXED);if(n>128)return;struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);fprintf(stderr,"EPOCH_NATIVE_EVENT kind=%s raw=%u epoch=%llu phase=%u monotonic_ns=%llu tid=%ld\n",kind,raw,(unsigned long long)sony_bt_epoch_source_generation(live_source.impl),sony_bt_epoch_source_phase(live_source.impl),(unsigned long long)t.tv_sec*1000000000u+t.tv_nsec,(long)syscall(SYS_gettid));}
static void epoch_connection(unsigned raw){
    epoch_old_connection(raw);if(!live_mode)return;epoch_event("connection",raw);
    if(raw==5)epoch_error(&live_source,sony_bt_epoch_source_activity(live_source.impl,1));
    else if(raw==4)epoch_error(&live_source,sony_bt_epoch_source_activity(live_source.impl,0));
    /* Original btmwAvSnkConnectWaitStart publishes2 (0x15352..0x1535c).
     * A real phone disconnect returns to that listening state, not OFF0/1.
     * Waiting/connecting0..3 cannot retain the previous verified peer/format. */
    else if(raw<=3){epoch_error(&live_source,sony_bt_epoch_source_peer(live_source.impl,0));__atomic_store_n(&known_connection,0,__ATOMIC_RELEASE);__atomic_store_n(&format_known,0,__ATOMIC_RELEASE);}
    else epoch_error(&live_source,-EPROTO);
}
static void epoch_peer(unsigned char *a,unsigned char bytes,unsigned char *name){epoch_old_peer(a,bytes,name);if(live_mode){epoch_event("peer",1);epoch_error(&live_source,sony_bt_epoch_source_peer(live_source.impl,1));}}
int main(int argc,char **argv){
    snk.connection_status=epoch_connection;snk.connection_information=epoch_peer;int rc=sony_bt_epoch_rf_main(argc,argv);
    if(live_mode){struct sony_bt_epoch_source_state s;int got=sony_bt_epoch_source_state(live_source.impl,&s);if(!got){epoch_snapshot(&live_source,&s);int exact=s.frames==s.offered_frames&&s.bytes==s.offered_bytes&&s.packets==s.offered_packets&&s.volume_offered==s.volume_sent;
        printf("{\"epoch_native_source\":true,\"passed\":%s,\"epochs\":%llu,\"frames\":%llu,\"bytes\":%llu,\"packets\":%llu,\"offered_frames\":%llu,\"offered_bytes\":%llu,\"offered_packets\":%llu,\"boundary_packets\":%llu,\"boundary_bytes\":%llu,\"first_error\":%d,\"sender_joined\":%u,\"sealed\":%u,\"callback_boundary_proof_from_fixture\":false}\n",!rc&&!s.first_error&&exact?"true":"false",(unsigned long long)s.epochs,(unsigned long long)s.frames,(unsigned long long)s.bytes,(unsigned long long)s.packets,(unsigned long long)s.offered_frames,(unsigned long long)s.offered_bytes,(unsigned long long)s.offered_packets,(unsigned long long)s.boundary_packets,(unsigned long long)s.boundary_bytes,s.first_error,s.sender_joined,s.sealed);
        printf("{\"epoch_prefetch\":true,\"packets\":%llu,\"bytes\":%llu,\"unready_running_packets\":%llu,\"unready_running_bytes\":%llu,\"max_queue_ns\":2000000000,\"native_decoder_enabled_by_flush\":false,\"producer_epochs\":%llu,\"future_packets\":%llu,\"future_bytes\":%llu,\"queued_peak_ns\":%u,\"queued_ns\":%u,\"queued_bytes\":%u}\n",(unsigned long long)s.prefetched_packets,(unsigned long long)s.prefetched_bytes,(unsigned long long)s.unready_running_packets,(unsigned long long)s.unready_running_bytes,(unsigned long long)s.producer_epochs,(unsigned long long)s.future_packets,(unsigned long long)s.future_bytes,s.queued_peak_ns,s.queued_ns,s.queued_bytes);if(s.first_error||!exact||s.producer_epochs!=s.epochs||s.queued_peak_ns>2000000000u||s.queued_ns||s.queued_bytes)rc=70;
    }else{fprintf(stderr,"EPOCH_FINAL_STATE status=%d\n",got);rc=70;}}
    /* Keep the callback table and context alive through process exit, exactly
     * as the native template does. Deinit return alone is not thread proof. */
    return rc;
}
