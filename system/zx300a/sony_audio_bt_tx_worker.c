/* Original Sony transmitter, single-use glibc process. Launch inside
 * the radio root under both audio/controller leases.
 * Inherited socket and nonce belong to one broker attempt. Never daemonize,
 * reopen a peer, generate PCM, adjust gain, or unload callback libraries. */
#define _GNU_SOURCE
#include "sony_audio_bt_tx_session.h"
#include "sony_audio_bt_components.h"
#include "sony_audio_bt_common_abi.h"
#include "sony_audio_bt_tx_pcm.h"
#include "sony_audio_bt_tx_packet_clock.h"
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static struct {
    pthread_mutex_t lock;
    unsigned common,source,codec,rate,channels,preferred,sound_sequence;
    unsigned peer_seen,own_seen,fault;
    uint8_t peer[6];
} state={.lock=PTHREAD_MUTEX_INITIALIZER};
static int notify_fd[2]={-1,-1},control_fd=-1;
static volatile sig_atomic_t interrupted;
static struct {
    void *mw,*components,*driver;
    int (*init)(const struct sony_bt_common_callbacks *),(*deinit)(void);
    int (*is_on)(void),(*power)(unsigned);
    int (*connect)(const void *),(*disconnect)(void),(*format)(unsigned,unsigned,unsigned char);
    int (*start)(void),(*stop)(unsigned short),(*send)(unsigned short,unsigned char *),(*reopen)(void);
    int (*numeric)(void *,_Bool);
    const unsigned char *(*context)(void);
    int init_attempted,initialized,power_requested,connect_requested,started;
    unsigned rate;
    struct sony_tx_pcm *pcm;
    struct sony_tx_pcm_stats final_stats;
} native;
static int64_t now_ms(void) {
    struct timespec t; if(clock_gettime(CLOCK_MONOTONIC,&t))return -1;
    return (int64_t)t.tv_sec*1000+t.tv_nsec/1000000;
}
static void notify(void) {
    int saved=errno; unsigned char byte=1;
    if(notify_fd[1]>=0) { ssize_t written=write(notify_fd[1],&byte,1); (void)written; }
    errno=saved;
}
static void caught(int signal_number) { (void)signal_number; interrupted=1; notify(); }
static void drain_notifications(void) { unsigned char b[128]; while(read(notify_fd[0],b,sizeof(b))>0){} }
#define LOCK() pthread_mutex_lock(&state.lock)
#define UNLOCK_NOTIFY() do { pthread_mutex_unlock(&state.lock); notify(); } while(0)
static void common_status(unsigned v) { LOCK();state.common=v;UNLOCK_NOTIFY(); }
static void source_status(unsigned v) { LOCK();state.source=v;UNLOCK_NOTIFY(); }
static void peer_info(unsigned char *p,unsigned char n,unsigned char *name) {
    (void)n;(void)name;LOCK();if(p)state.peer_seen=1;UNLOCK_NOTIFY();
}
static void sound_info(unsigned codec,unsigned rate,unsigned channels,_Bool flag) {
    (void)flag;LOCK();state.codec=codec;state.rate=rate;state.channels=channels;++state.sound_sequence;UNLOCK_NOTIFY();
}
static void preferred_info(unsigned short bytes) { LOCK();state.preferred=bytes;UNLOCK_NOTIFY(); }
static void own_info(void *p,unsigned a,unsigned char b,unsigned char *c,unsigned char d,unsigned char *e,unsigned f,void *g) {
    (void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g;
    LOCK();if(p)state.own_seen=1;UNLOCK_NOTIFY();
}
/* This worker consumes a previously selected bond; pairing is a separate
 * user-controlled operation. Even the selected peer cannot trigger silent pairing. */
static void numeric(void *p,unsigned a,unsigned char b,unsigned char *c,unsigned key) {
    (void)a;(void)b;(void)c;(void)key;
    LOCK();state.fault=1;UNLOCK_NOTIFY();if(p)native.numeric(p,0);
}
static void paired(unsigned a,void *p,void *key,unsigned b,unsigned char c,unsigned char *d) {
    (void)a;(void)p;(void)key;(void)b;(void)c;(void)d;LOCK();state.fault=1;UNLOCK_NOTIFY();
}
static void common_error(unsigned char error) { (void)error;LOCK();state.fault=1;UNLOCK_NOTIFY(); }
static void acl_disconnect(void *p,_Bool flag) {
    (void)flag;LOCK();if(p)state.peer_seen=0;UNLOCK_NOTIFY();
}
/* Unused raw callbacks do not retain borrowed names, keys or pointers. */
static void noop_u(unsigned a){(void)a;}
static void noop_v(void){}
static void noop_p(void *a){(void)a;}
static void noop_pub(void*a,unsigned b,unsigned char c,unsigned char*d){(void)a;(void)b;(void)c;(void)d;}
static void noop_pb(void*a,_Bool b){(void)a;(void)b;}
static void noop_device(void*a,void*b,unsigned c,unsigned char d,unsigned char*e,unsigned f){(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;}
static void noop_ss(unsigned short a,unsigned short b){(void)a;(void)b;}
static void noop_remote(void*a,unsigned char b,void*c){(void)a;(void)b;(void)c;}
static void noop_di(void*a,unsigned short b,unsigned short c,unsigned short d){(void)a;(void)b;(void)c;(void)d;}
static void noop_i(int a){(void)a;}
static void noop_pu(void*a,unsigned b){(void)a;(void)b;}
static void noop_ssp(void*a,unsigned char b,unsigned char*c,unsigned d,unsigned e,unsigned f){(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;}
static void noop_class(void*a,unsigned char b,unsigned short*c){(void)a;(void)b;(void)c;}
static void noop_sep(unsigned a,unsigned short b,unsigned char c){(void)a;(void)b;(void)c;}
static void noop_command(unsigned a,_Bool b){(void)a;(void)b;}
static void noop_attrs(unsigned char a,unsigned *b){(void)a;(void)b;}
#ifndef SONY_TX_AVRCP_CALLBACKS
static void noop_volume(unsigned char a){(void)a;}
#endif
static const struct sony_bt_common_callbacks common_cb={sizeof(common_cb),common_status,noop_pub,noop_pub,numeric,paired,noop_pb,noop_device,noop_v,noop_p,noop_v,own_info,common_error,acl_disconnect,noop_u,noop_ss,noop_remote,noop_di,noop_i,noop_v,noop_pu,noop_ssp,noop_class};
static const struct sony_bt_tx_callbacks source_cb={sizeof(source_cb),source_status,peer_info,sound_info,noop_sep,preferred_info};
#ifndef SONY_TX_AVRCP_CALLBACKS
#define SONY_TX_AVRCP_CALLBACKS {sizeof(avrcp_cb),noop_u,noop_command,noop_v,noop_attrs,noop_volume,noop_ss}
#endif
static const struct sony_bt_tx_avrcp_callbacks avrcp_cb=SONY_TX_AVRCP_CALLBACKS;

static int native_live(void) {
    LOCK();int ok=!state.fault&&state.source==5;
    pthread_mutex_unlock(&state.lock);return ok&&!interrupted?0:-ENOTCONN;
}
enum wait_kind { WAIT_ON,WAIT_OFF,WAIT_OWN,WAIT_CONNECTED,WAIT_FORMAT,WAIT_STREAMING,WAIT_IDLE };
static int await_native(enum wait_kind kind,unsigned sequence,unsigned timeout_ms,int cleanup) {
    int64_t end=now_ms()+timeout_ms;
    for(;;) {
        LOCK(); unsigned common=state.common,source=state.source,fault=state.fault;
        unsigned sound_sequence=state.sound_sequence,rate=state.rate;
        pthread_mutex_unlock(&state.lock);
        if(!cleanup&&(fault||interrupted))return -ECANCELED;
        int done=0;
        switch(kind) {
        case WAIT_ON:done=common>=2&&common<=5&&native.is_on()==1;break;
        case WAIT_OFF:done=common==7&&native.is_on()==0;break;
        case WAIT_OWN:done=1;break;
        case WAIT_CONNECTED:done=source==4;break;
        case WAIT_FORMAT:{
            const uint8_t*p=native.context();
            /* Source status 6 requests a reopen; the middleware remains in
             * control of its internal transition and reconfiguration. */
            if(source==6)return -EAGAIN;
            if(sound_sequence>sequence&&source==4&&(!p||!__atomic_load_n(p+0x3d,__ATOMIC_ACQUIRE))){
                /* Fresh settled native callback, not a timeout guess. Keep
                 * Android input unchanged; original Sony SRC can adapt it. */
                return rate&&rate!=native.rate?-EXDEV:0;
            }
            break;
        }
        case WAIT_STREAMING:done=source==5;break;
        case WAIT_IDLE:done=source==1;break;
        }
        if(done)return 0;
        int64_t remaining=end-now_ms();if(remaining<=0)return -ETIMEDOUT;
        /* Only the audited reconfiguration busy byte lacks a callback after
         * being cleared. Bounded 20ms recheck applies during FORMAT only;
         * normal streaming blocks on socket/callback events with no polling. */
        int timeout=(int)remaining;if(kind==WAIT_FORMAT&&timeout>20)timeout=20;
        struct pollfd p[2]={{notify_fd[0],POLLIN,0},{control_fd,0,0}};
        int rc=poll(p,control_fd>=0&&!cleanup?2:1,timeout);
        if(rc<0&&errno!=EINTR)return -errno;
        if(p[1].revents&(POLLHUP|POLLERR|POLLNVAL))return -EPIPE;
        if(p[0].revents&POLLIN)drain_notifications();
    }
}
static int load_native(void) {
    native.mw=dlopen("libBtMw.so",RTLD_NOW|RTLD_LOCAL);
    native.components=dlopen("libBtCompIf.so",RTLD_NOW|RTLD_LOCAL);
    native.driver=dlopen("libbtdrv.so",RTLD_NOW|RTLD_LOCAL);
    if(!native.mw||!native.components||!native.driver)return -ELIBACC;
#define SYMBOL(field,name) do{*(void **)(&native.field)=dlsym(native.mw,name);if(!native.field)return -ELIBBAD;}while(0)
    SYMBOL(init,"BtMwCommonInit");SYMBOL(deinit,"BtMwCommonDeinit");
    SYMBOL(is_on,"btmwCommonIsRfOn");SYMBOL(power,"BtMwCommonRequestRfOnOff");
    SYMBOL(connect,"BtMwAvSrcRequestConnect");
    SYMBOL(disconnect,"BtMwAvSrcRequestDisconnect");SYMBOL(format,"BtMwAvSrcRequestSetSoundStatus");
    SYMBOL(start,"BtMwAvSrcRequestStartStreaming");SYMBOL(stop,"BtMwAvSrcRequestStopStreamingWithTime");
    SYMBOL(reopen,"BtMwAvSrcRequestReopen");
    SYMBOL(send,"BtMwAvSrcRequestSendData");SYMBOL(context,"btmwAvSrcGetInstance");
    SYMBOL(numeric,"BtMwCommonRequestNumericComparison");
#undef SYMBOL
    return 0;
}
static int initialize(void) {
    int rc=load_native();if(rc)return rc;
    native.init_attempted=1;rc=native.init(&common_cb);if(rc)return -EIO;
    native.initialized=1;unsigned mask=0;
    rc=sony_bt_components_prepare_transmitter(native.mw,native.components,&source_cb,&avrcp_cb,&mask);
    return rc>0?-EIO:rc;
}
static int prepare_format(unsigned rate) {
    LOCK();unsigned sequence=state.sound_sequence;pthread_mutex_unlock(&state.lock);
    if(native.format(rate,2,16))return -EIO;
    int rc=await_native(WAIT_FORMAT,sequence,15000,0);
    if(rc==-EAGAIN){
        /* Exactly one original reopen, only after its explicit status 6.
         * Reuse the MW's saved desired format; never resubmit samples or
         * silently fall back to a different sample rate. */
        if(native.reopen())return -EIO;
        if((rc=await_native(WAIT_CONNECTED,0,20000,0)))return rc;
        rc=await_native(WAIT_FORMAT,sequence,15000,0);
    }
    return rc;
}
static int send_codec(void *context,const int16_t *pcm,unsigned frames){
    (void)context;int rc=native_live();if(rc)return rc;
    if(!frames)return 0;
    rc=sony_tx_packet_clock_begin(native.rate,frames);if(rc)return rc;
    int sent=native.send((unsigned short)(frames*4),(unsigned char*)pcm);
    rc=sony_tx_packet_clock_end();
    if(sent||rc)return rc?rc:-EIO;
    return native_live();
}
static int begin_session(void *context,const struct wbt_request *q,unsigned *preferred) {
    (void)context;LOCK();memcpy(state.peer,q->peer,6);pthread_mutex_unlock(&state.lock);native.rate=q->rate;
    int rc=initialize();if(rc)return rc;
    native.power_requested=1;if(native.power(1))return -EIO;
    if((rc=await_native(WAIT_ON,0,15000,0)))return rc;
    native.connect_requested=1;if(native.connect(q->peer))return -EIO;
    if((rc=await_native(WAIT_CONNECTED,0,20000,0)))return rc;
    rc=prepare_format(q->rate);
    if(rc==-EXDEV){
        LOCK();unsigned rate=state.rate;pthread_mutex_unlock(&state.lock);
        if(rate)native.rate=rate; /* Only the downstream codec clock changes. */
    }else if(rc)return rc;
    LOCK();unsigned sequence=state.sound_sequence-1;pthread_mutex_unlock(&state.lock);
    if(native.start())return -EIO;
    native.started=1;
    if((rc=await_native(WAIT_STREAMING,sequence,10000,0)))return rc;
    LOCK();unsigned codec_bytes=state.preferred;pthread_mutex_unlock(&state.lock);
    rc=native_live();if(rc)return rc;
    rc=sony_tx_pcm_open(&native.pcm,q->rate,native.rate,codec_bytes/4,send_codec,NULL);
    if(!rc)*preferred=1024; /* 256 INPUT frames per native processing quantum. */
    return rc;
}
static int send_pcm(void *context,const void *p,unsigned bytes) {
    (void)context;int rc=native_live();if(rc)return rc;
    if(!native.pcm)return -ENODEV;
    if(bytes<4)return 0;
    return sony_tx_pcm_write(native.pcm,p,bytes/4);
}
static int finish_pcm(void *context){(void)context;int rc=native_live();return rc?rc:sony_tx_pcm_finish(native.pcm);}
static int processing_stats(void *context,struct sony_tx_pcm_stats *out){
    (void)context;if(native.pcm){sony_tx_pcm_stats(native.pcm,out);return 0;}
    if(!native.final_stats.input_rate)return -ENOENT;*out=native.final_stats;return 0;
}
static int close_session(void *context) {
    (void)context;int rc=native.init_attempted&&!native.initialized?-EIO:0;
    if(native.pcm){
        sony_tx_pcm_cancel(native.pcm); /* No-op after successful explicit finish. */
        sony_tx_pcm_stats(native.pcm,&native.final_stats);sony_tx_pcm_close(&native.pcm);
    }
    if(native.started) {
        if(native.stop(3000))rc=-EIO;
        else if(await_native(WAIT_CONNECTED,0,5000,1))rc=-ETIMEDOUT;
    }
    if(native.connect_requested) {
        if(native.disconnect())rc=-EIO;
        else if(await_native(WAIT_IDLE,0,5000,1))rc=-ETIMEDOUT;
    }
    if(native.power_requested) {
        if(native.power(0))rc=-EIO;
        else if(await_native(WAIT_OFF,0,10000,1))rc=-ETIMEDOUT;
    }
    if(native.initialized) {
        if(native.deinit())rc=-EIO;
        LOCK();int off=state.common==7;pthread_mutex_unlock(&state.lock);
        if(!off)rc=-EPROTO;
    }
    struct sony_tx_packet_clock_stats clock;sony_tx_packet_clock_stats(&clock);
    fprintf(stderr,"SONY_TX_PACKET_CLOCK groups=%llu packets=%llu codec_frames=%llu failures=%u\n",
        (unsigned long long)clock.groups,(unsigned long long)clock.packets,
        (unsigned long long)clock.frames,clock.failures);
    /* Do not dlclose: callback storage and original native threads have
     * process lifetime. Supervisor must additionally reap and verify no
     * controller FDs/physical PCM before marking either lease CLEAN. */
    return rc;
}
static int transfer(void *buffer,size_t bytes,int output,int timeout_ms) {
    uint8_t *p=buffer;int64_t end=now_ms()+timeout_ms;
    while(bytes) {
        if(interrupted)return -ECANCELED;
        if(native.started&&native_live())return -ENOTCONN;
        int64_t remaining=end-now_ms();if(remaining<=0)return -ETIMEDOUT;
        struct pollfd fds[2]={{control_fd,output?POLLOUT:POLLIN,0},{notify_fd[0],POLLIN,0}};
        int rc=poll(fds,2,(int)remaining);if(rc<0){if(errno==EINTR)continue;return -errno;}
        if(fds[1].revents&POLLIN)drain_notifications();
        if(fds[0].revents&(POLLERR|POLLNVAL))return -EPIPE;
        if(!(fds[0].revents&(output?POLLOUT:(POLLIN|POLLHUP))))continue;
        ssize_t n=output?send(control_fd,p,bytes,MSG_NOSIGNAL|MSG_DONTWAIT):recv(control_fd,p,bytes,MSG_DONTWAIT);
        if(n<0){if(errno==EINTR||errno==EAGAIN)continue;return -errno;}
        if(!n)return -EPIPE;
        p+=n;bytes-=n;
    }
    return 0;
}
static int read_worker_payload(void *context,void *buffer,size_t bytes){
    (void)context;return transfer(buffer,bytes,0,5000);
}
struct software_pcm_result {unsigned calls,frames;uint32_t checksum;};
static int software_pcm_send(void *v,const int16_t *pcm,unsigned frames){
    struct software_pcm_result *r=v;const uint8_t *p=(const uint8_t*)pcm;
    if(!frames||frames>2048||frames%128)return -EINVAL;
    r->calls++;r->frames+=frames;for(unsigned i=0;i<frames*4;i++)r->checksum=(r->checksum^p[i])*UINT32_C(16777619);
    return 0; /* CPU-only recorder; it never calls SourceIF or opens RF. */
}
int main(int argc,char **argv) {
    setvbuf(stdout,NULL,_IONBF,0);
    if(pipe2(notify_fd,O_NONBLOCK|O_CLOEXEC))return 65;
    signal(SIGPIPE,SIG_IGN);signal(SIGTERM,caught);signal(SIGINT,caught);
    if(argc==2&&!strcmp(argv[1],"--software-init")) {
        int init=initialize(),src=init;struct sony_tx_pcm *pcm=NULL;
        struct software_pcm_result r={.checksum=UINT32_C(2166136261)};struct sony_tx_pcm_stats stats={0};
        int16_t samples[512]={4096,-4096};
        if(!src)src=sony_tx_pcm_open(&pcm,48000,44100,1024,software_pcm_send,&r);
        if(!src)src=sony_tx_pcm_write(pcm,samples,256);
        if(!src)src=sony_tx_pcm_finish(pcm);
        if(pcm)sony_tx_pcm_stats(pcm,&stats);sony_tx_pcm_close(&pcm);
        int clean=close_session(NULL);int passed=!init&&!src&&!clean&&stats.input_processed==256&&stats.codec_generated==236&&stats.codec_submitted==256&&stats.codec_padding==20&&r.calls==1;
        printf("{\"native_stack_result\":true,\"session_worker\":true,\"passed\":%s,\"init_rc\":%d,\"src_rc\":%d,\"cleanup_rc\":%d,\"src_input_frames\":%llu,\"src_codec_frames\":%llu,\"src_padding_frames\":%llu,\"src_recorded_frames\":%u,\"src_checksum\":%u,\"src_sender_cpu_only\":true,\"rf_requested\":false}\n",passed?"true":"false",init,src,clean,(unsigned long long)stats.input_processed,(unsigned long long)stats.codec_generated,(unsigned long long)stats.codec_padding,r.frames,r.checksum);
        return passed?0:70;
    }
    if(argc!=4||strcmp(argv[1],"--session-fd"))return 64;
    char *end=NULL;errno=0;long fd=strtol(argv[2],&end,10);
    if(errno||!end||*end||fd<3||fd>INT_MAX)return 64;
    errno=0;uint64_t nonce=strtoull(argv[3],&end,16);
    if(errno||!end||*end||!nonce||argv[3][0]=='-')return 64;
    control_fd=(int)fd;int type=0;socklen_t size=sizeof(type);
    if(getsockopt(control_fd,SOL_SOCKET,SO_TYPE,&type,&size)||type!=SOCK_STREAM)return 65;
    struct sockaddr_storage address;size=sizeof(address);
    if(getpeername(control_fd,(struct sockaddr*)&address,&size)||address.ss_family!=AF_UNIX)return 65;
    static const struct wbt_ops ops={.begin=begin_session,.submit=send_pcm,.close=close_session,.finish=finish_pcm,.stats=processing_stats};
    struct wbt_session session; if(wbt_init(&session,nonce,&ops,NULL))return 65;
    int transport=0,operation=0;
    for(;;) {
        struct wbt_request q;struct wbt_reply reply;
        transport=transfer(&q,sizeof(q),0,30000);if(transport)break;
        transport=wbt_consume_payload(&session,&q,&reply,read_worker_payload,NULL,&operation);
        if(transport)break;
        /* Final/error replies must still be writable after native shutdown. */
        if(session.phase==WBT_CLOSED||session.phase==WBT_FAILED)native.started=0;
        transport=transfer(&reply,sizeof(reply),1,5000);
        if(transport||operation||session.phase==WBT_CLOSED)break;
    }
    int clean=wbt_abort(&session);
    printf("{\"native_stack_result\":true,\"session_worker\":true,\"passed\":%s,\"cleanup_verified\":%s,\"transport_status\":%d,\"operation_status\":%d,\"accepted_frames\":%llu,\"submitted_frames\":%llu,\"uncertain_frames\":%llu,\"hardware_consumption_verified\":false}\n",!transport&&!operation&&!clean?"true":"false",session.cleanup_verified?"true":"false",transport,operation,(unsigned long long)session.accepted,(unsigned long long)session.submitted,(unsigned long long)session.uncertain);
    /* Ordinary protocol/client failure is not an unclean controller. Parent
     * reads the result and independently verifies cleanup before lease release. */
    return clean?70:0;
}
