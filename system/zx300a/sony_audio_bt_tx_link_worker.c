/* Persistent-link variant. Reuse the audited original ABI, Sony SRC and RTP
 * packet adapter; WBT2 entry point remains compiled and unchanged in its own
 * runtime. This entry accepts WBL3 only, on a private inherited Unix socket. */
#define _GNU_SOURCE
#include "sony_audio_bt_tx_volume_channel.h"
#include <pthread.h>
static struct sony_tx_volume *link_volume;
static struct wva_server volume_server;
static int volume_enabled;
static void notify(void);
struct volume_event {unsigned kind,a,b;};
static struct {pthread_mutex_t lock;struct volume_event events[64];unsigned head,count;int open,overflow;} volume_queue={.lock=PTHREAD_MUTEX_INITIALIZER};
static void volume_event(unsigned kind,unsigned a,unsigned b){
    pthread_mutex_lock(&volume_queue.lock);
    if(volume_queue.open){
        if(volume_queue.count==64)volume_queue.overflow=1;
        else{volume_queue.events[(volume_queue.head+volume_queue.count)%64]=(struct volume_event){kind,a,b};++volume_queue.count;}
    }
    pthread_mutex_unlock(&volume_queue.lock);notify();
}
static void volume_connection(unsigned raw){volume_event(1,raw,0);}
static void volume_current(unsigned char raw){volume_event(2,raw,0);}
static void volume_remote(unsigned short version,unsigned short features){volume_event(3,version,features);}
#define SONY_TX_AVRCP_CALLBACKS {sizeof(avrcp_cb),volume_connection,noop_command,noop_v,noop_attrs,volume_current,volume_remote}
#define main wbl_legacy_worker_main
#include "sony_audio_bt_tx_worker.c"
#undef main
#include "sony_audio_bt_tx_link.h"

static int epoch_stop_attempted;
static struct {
    int (*up)(void),(*down)(void),(*absolute)(unsigned char),(*mode)(unsigned char);
    int (*connected)(const void *peer);
    void *(*context)(void);
} volume_native;
static void volume_admission(int open){
    pthread_mutex_lock(&volume_queue.lock);volume_queue.open=open;
    volume_queue.head=volume_queue.count=volume_queue.overflow=0;
    pthread_mutex_unlock(&volume_queue.lock);
}
static int volume_dispatch(void){
    for(unsigned i=0;i<64;++i){
        pthread_mutex_lock(&volume_queue.lock);
        if(!volume_queue.open){pthread_mutex_unlock(&volume_queue.lock);return 0;}
        if(volume_queue.overflow){pthread_mutex_unlock(&volume_queue.lock);return -EOVERFLOW;}
        if(!volume_queue.count){pthread_mutex_unlock(&volume_queue.lock);return 0;}
        struct volume_event event=volume_queue.events[volume_queue.head];volume_queue.head=(volume_queue.head+1)%64;--volume_queue.count;
        pthread_mutex_unlock(&volume_queue.lock);
        if(event.kind==1){
            uint8_t peer[6];LOCK();memcpy(peer,state.peer,6);pthread_mutex_unlock(&state.lock);
            if(!volume_native.connected)return -ENOSYS;
            int connected=volume_native.connected(peer);if(connected!=0&&connected!=1)return -EPROTO;
            sony_tx_volume_connection(link_volume,connected?2:0);
        }else if(event.kind==2)sony_tx_volume_current(link_volume,event.a);
        else if(event.kind==3)sony_tx_volume_remote(link_volume,event.a,event.b);
        else return -EPROTO;
    }
    // Process at most one bounded queue per PCM poll iteration. A callback
    // added during dispatch already left a wake byte for the next iteration.
    return 0;
}
static int volume_native_ready(void){
    LOCK();int good=!state.fault&&(state.source==4||state.source==5);pthread_mutex_unlock(&state.lock);
    return good&&native.initialized&&native.is_on()==1?0:-ENOTCONN;
}
static int volume_relative(void *ctx,int direction){
    (void)ctx;int rc=volume_native_ready();return rc?rc:direction==1?volume_native.up():volume_native.down();
}
static int volume_absolute(void *ctx,unsigned value){
    (void)ctx;int rc=volume_native_ready();return rc?rc:volume_native.absolute((unsigned char)value);
}
static int volume_mode(void *ctx,unsigned enabled){
    (void)ctx;int rc=volume_native_ready();if(rc)return rc;
    rc=volume_native.mode((unsigned char)enabled);if(rc)return rc;
    unsigned char *p=volume_native.context();return !p?-EPROTO:p[0x48]==enabled?0:-EPROTO;
}
static int prepare_volume(void){
    if(!volume_enabled)return 0;
    volume_native.up=dlsym(native.mw,"BtMwAvrcpSrcRequestVolumeUp");
    volume_native.down=dlsym(native.mw,"BtMwAvrcpSrcRequestVolumeDown");
    volume_native.absolute=dlsym(native.mw,"BtMwAvrcpSrcRequestSetVolume");
    volume_native.mode=dlsym(native.mw,"BtMwAvrcpSrcRequestSettingAbsoluteVolume");
    volume_native.context=dlsym(native.mw,"btmwAvrcpSrcGetInstance");
    volume_native.connected=dlsym(native.mw,"btmwAvrcpSrcIsConnectedBdAddr");
    if(!volume_native.up||!volume_native.down||!volume_native.absolute||!volume_native.mode||!volume_native.context||!volume_native.connected)return -ENOSYS;
    unsigned char *p=volume_native.context();if(!p||p[0x48]>1)return -EPROTO;
    sony_tx_volume_mode_observed(link_volume,p[0x48]);notify();return 0;
}
static unsigned source_now(void) {
    LOCK();unsigned source=state.source;pthread_mutex_unlock(&state.lock);return source;
}
static int link_live(void *ctx,unsigned phase) {
    (void)ctx;
    LOCK();int live=!state.fault&&state.source==(phase==WBL_STREAMING?5u:4u);
    pthread_mutex_unlock(&state.lock);
    if(!live||interrupted||!native.initialized||native.is_on()!=1)return -ENOTCONN;
    return phase==WBL_STREAMING?native_live():0;
}
static int connect_link(void *ctx,const uint8_t peer[6]) {
    (void)ctx;LOCK();memcpy(state.peer,peer,6);pthread_mutex_unlock(&state.lock);
    int rc=initialize();if(!rc)rc=prepare_volume();if(rc)return rc;
    native.power_requested=1;if(native.power(1))return -EIO;
    if((rc=await_native(WAIT_ON,0,15000,0)))return rc;
    /* RF-on can precede the source profile's ready callback. Issuing the
     * connection while the profile is still starting can lose the request. */
    if((rc=await_native(WAIT_IDLE,0,15000,0)))return rc;
    /* On this stack the idle callback can arrive before a connection request
     * is actually accepted. Allow the profile to settle before connecting. */
    usleep(750000);
    native.connect_requested=1;if(native.connect(peer))return -EIO;
    if((rc=await_native(WAIT_CONNECTED,0,20000,0)))return rc;
    return link_live(NULL,WBL_CONNECTED);
}
static int begin_epoch(void *ctx,const struct wbt_request *q,unsigned *preferred) {
    (void)ctx;
    if(native.pcm||native.started||link_live(NULL,WBL_CONNECTED))return -ESTALE;
    memset(&native.final_stats,0,sizeof(native.final_stats));
    native.rate=q->rate;epoch_stop_attempted=0;
    int rc=prepare_format(q->rate);
    if(rc==-EXDEV) {
        LOCK();unsigned rate=state.rate;pthread_mutex_unlock(&state.lock);
        if(rate)native.rate=rate;
    }else if(rc)return rc;
    LOCK();unsigned sequence=state.sound_sequence-1;pthread_mutex_unlock(&state.lock);
    if(native.start())return -EIO;
    native.started=1;
    if((rc=await_native(WAIT_STREAMING,sequence,10000,0)))return rc;
    LOCK();unsigned codec_bytes=state.preferred;pthread_mutex_unlock(&state.lock);
    if((rc=native_live()))return rc;
    rc=sony_tx_pcm_open(&native.pcm,q->rate,native.rate,codec_bytes/4,send_codec,NULL);
    if(!rc)*preferred=1024;
    return rc;
}
static void close_epoch_pcm(void) {
    if(!native.pcm)return;
    sony_tx_pcm_cancel(native.pcm);
    sony_tx_pcm_stats(native.pcm,&native.final_stats);sony_tx_pcm_close(&native.pcm);
}
static int pause_epoch(void *ctx) {
    (void)ctx;close_epoch_pcm();
    unsigned source=source_now();int rc=0;
    if(native.started&&source==5) {
        if(epoch_stop_attempted)return -EIO;
        epoch_stop_attempted=1;
        if(native.stop(3000))rc=-EIO;
        else rc=await_native(WAIT_CONNECTED,0,5000,1);
    }
    if(!rc)native.started=0;
    if(!rc)rc=link_live(NULL,WBL_CONNECTED);
    return rc;
}
static int close_native_link(void *ctx) {
    (void)ctx;if(volume_enabled)volume_admission(0);sony_tx_volume_seal(link_volume);close_epoch_pcm();
    int rc=native.init_attempted&&!native.initialized?-EIO:0;
    unsigned source=source_now();
    if(native.started&&source==5&&!epoch_stop_attempted) {
        epoch_stop_attempted=1;
        if(native.stop(3000))rc=-EIO;
        else if(await_native(WAIT_CONNECTED,0,5000,1))rc=-ETIMEDOUT;
    }
    /* A real remote disconnect already produced source IDLE: do not issue
     * invalid Stop/Disconnect requests and turn verified shutdown into an
     * artificial failure. Unknown/transitional states still require ACKs. */
    if(native.connect_requested&&source_now()!=1) {
        if(native.disconnect())rc=-EIO;
        else if(await_native(WAIT_IDLE,0,5000,1))rc=-ETIMEDOUT;
    }
    if(native.power_requested) {
        if(native.is_on()!=0) {
            if(native.power(0))rc=-EIO;
            else if(await_native(WAIT_OFF,0,10000,1))rc=-ETIMEDOUT;
        }else if(await_native(WAIT_OFF,0,1000,1))rc=-ETIMEDOUT;
    }
    if(native.initialized) {
        if(native.deinit())rc=-EIO;
        else native.initialized=0;
        LOCK();int off=state.common==7;pthread_mutex_unlock(&state.lock);
        if(!off)rc=-EPROTO;
    }
    native.started=0;return rc;
}
static int link_transfer(struct wbl_session *s,void *buffer,size_t bytes,int output,int timeout) {
    uint8_t *p=buffer;int64_t end=timeout<0?-1:now_ms()+timeout;
    while(bytes) {
        if(interrupted)return -ECANCELED;
        if(volume_enabled){int rc=volume_dispatch();if(!rc)rc=wva_server_step(&volume_server,0);if(rc)return rc;}
        if(!output) { int rc=wbl_check_live(s);if(rc)return rc; }
        int64_t remaining=end<0?-1:end-now_ms();
        if(end>=0&&remaining<=0)return -ETIMEDOUT;
        struct pollfd fds[3]={{control_fd,output?POLLOUT:POLLIN,0},{notify_fd[0],POLLIN,0},
            {volume_enabled?volume_server.fd:-1,volume_enabled?wva_server_events(&volume_server):0,0}};
        int n=poll(fds,3,(int)remaining);
        if(n<0){if(errno==EINTR)continue;return -errno;}
        if(fds[1].revents&POLLIN) {
            drain_notifications();
            if(!output){int rc=wbl_check_live(s);if(rc)return rc;}
        }
        if(volume_enabled){int rc=volume_dispatch();if(!rc)rc=wva_server_step(&volume_server,fds[2].revents);if(rc)return rc;}
        if(fds[0].revents&(POLLERR|POLLNVAL))return -EPIPE;
        if(!(fds[0].revents&(output?POLLOUT:(POLLIN|POLLHUP))))continue;
        ssize_t count=output?send(control_fd,p,bytes,MSG_NOSIGNAL|MSG_DONTWAIT):recv(control_fd,p,bytes,MSG_DONTWAIT);
        if(count<0){if(errno==EINTR||errno==EAGAIN)continue;return -errno;}
        if(!count)return -EPIPE;
        if(end<0)end=now_ms()+30000; /* First header byte starts bounded framing. */
        p+=count;bytes-=count;
    }
    return 0;
}
static int read_link_payload(void *context,void *buffer,size_t bytes){
    return link_transfer(context,buffer,bytes,0,5000);
}
#ifndef WBL_WORKER_MAIN
#define WBL_WORKER_MAIN main
#endif
int WBL_WORKER_MAIN(int argc,char **argv) {
    if(argc==2&&!strcmp(argv[1],"--software-init"))return wbl_legacy_worker_main(argc,argv);
    if((argc!=4&&argc!=6)||strcmp(argv[1],"--link-fd"))return 64;
    setvbuf(stdout,NULL,_IONBF,0);
    char *end=NULL;errno=0;long fd=strtol(argv[2],&end,10);
    if(errno||!end||*end||fd<0||fd>INT_MAX)return 64;
    errno=0;uint64_t nonce=strtoull(argv[3],&end,16);
    if(errno||!end||*end||argv[3][0]=='-')return 64;
    control_fd=(int)fd;
    if(pipe2(notify_fd,O_NONBLOCK|O_CLOEXEC))return 65;
    if(argc==6){
        if(strcmp(argv[4],"--volume-fd"))return 64;
        errno=0;long volume_fd=strtol(argv[5],&end,10);
        if(errno||!argv[5][0]||*end||volume_fd<0||volume_fd>INT_MAX)return 64;
        static const struct sony_tx_volume_ops volume_ops={volume_relative,volume_absolute,volume_mode};
        if(sony_tx_volume_open(&link_volume,&volume_ops,NULL)||wva_server_init(&volume_server,(int)volume_fd,nonce,link_volume))return 65;
        volume_enabled=1;volume_admission(1);
    }
    signal(SIGPIPE,SIG_IGN);signal(SIGTERM,caught);signal(SIGINT,caught);
    static const struct wbl_ops ops={.connect=connect_link,.live=link_live,
        .epoch={.begin=begin_epoch,.submit=send_pcm,.close=pause_epoch,.finish=finish_pcm,.stats=processing_stats},
        .close_link=close_native_link};
    struct wbl_session session;if(wbl_init(&session,nonce,&ops,NULL))return 65;
    int transport=0,operation=0;
    for(;;) {
        struct wbl_request q;struct wbl_reply reply;
        transport=link_transfer(&session,&q,sizeof(q),0,-1);if(transport)break;
        transport=wbl_consume_payload(&session,&q,&reply,read_link_payload,&session,&operation);
        if(transport)break;
        transport=link_transfer(&session,&reply,sizeof(reply),1,5000);
        if(transport||operation||session.phase==WBL_CLOSED)break;
    }
    int clean=wbl_abort(&session);
    if(volume_enabled)close(volume_server.fd);
    printf("{\"native_stack_result\":true,\"link_worker\":true,\"wbl_version\":3,\"epoch\":%llu,\"native_closed\":%s,\"transport_status\":%d,\"operation_status\":%d,\"cleanup_status\":%d,\"hardware_consumption_verified\":false}\n",
        (unsigned long long)session.epoch,session.native_closed?"true":"false",transport,operation,clean);
    /* Only shutdown result governs supervisor's cleanup; protocol failure
     * remains in the explicit reply/log and can never authorize a replay. */
    return clean?70:0;
}
