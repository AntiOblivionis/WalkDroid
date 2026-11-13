#define _POSIX_C_SOURCE 200809L
#include "sony_audio_chain_client.h"
#include "sony_audio_chain_worker_protocol.h"
#include "sony_audio_stream_protocol.h"
#include "sony_audio_pcm_format.h"
#include "sony_audio_sound_presets.h"
#include "sony_audio_snapshot_store.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

struct wdc_client {
    int tx, rx, timeout_ms, failed;
    unsigned int request, session, revision,magic,input_rate;
    pthread_mutex_t mutex;
};

static long long milliseconds(void) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now)) return -1;
    return (long long)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

static ssize_t pipe_write(int fd, const void *data, unsigned int size) {
    /* Prevent a peer-close race from terminating the whole audio process.
     * Do not change the application's global SIGPIPE disposition. */
    sigset_t blocked, old, pending;
    sigemptyset(&blocked); sigaddset(&blocked,SIGPIPE);
    if (pthread_sigmask(SIG_BLOCK,&blocked,&old)) { errno = EIO; return -1; }
    if (sigpending(&pending)) { pthread_sigmask(SIG_SETMASK,&old,NULL); errno = EIO; return -1; }
    int was_pending = sigismember(&pending,SIGPIPE);
    ssize_t result = write(fd,data,size);
    int error = errno;
    if (result < 0 && error == EPIPE && !was_pending && !sigismember(&old,SIGPIPE)) {
        struct timespec zero = {0,0};
        while (sigtimedwait(&blocked,NULL,&zero) < 0 && errno == EINTR) {}
    }
    pthread_sigmask(SIG_SETMASK,&old,NULL); errno = error;
    return result;
}

static int transfer(int fd, void *buffer, unsigned int size, int writing, long long deadline) {
    unsigned int done = 0;
    while (done < size) {
        long long now = milliseconds();
        if (now < 0) return WDA_CLIENT_IO;
        if (now >= deadline) return WDA_CLIENT_TIMEOUT;
        int remaining = (int)(deadline-now);
        struct pollfd descriptor = {fd, writing ? POLLOUT : POLLIN, 0};
        int ready = poll(&descriptor, 1, remaining);
        if (ready < 0 && errno == EINTR) continue;
        if (!ready) return WDA_CLIENT_TIMEOUT;
        if (ready < 0 || descriptor.revents & (POLLERR | POLLNVAL)) return WDA_CLIENT_IO;
        /* POLLIN|POLLHUP can have buffered final bytes; drain before EOF. */
        unsigned int amount = size-done;
        if (amount > 4096) amount = 4096;
        ssize_t n = writing ? pipe_write(fd, (unsigned char *)buffer+done, amount)
                            : read(fd, (unsigned char *)buffer+done, amount);
        if (n < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) continue;
        if (n <= 0) return WDA_CLIENT_IO;
        done += (unsigned int)n;
    }
    return 0;
}


static int exchange_full_rate(struct wdc_client *c,unsigned int command,const void *data,unsigned int size,
                    void *out,unsigned int capacity,unsigned int *received,unsigned int *reply_rate,
                    unsigned int input_rate) {
    if(c->failed || c->request==UINT_MAX) return WDA_CLIENT_IO;
    int process_expanded=command==CHAIN_PROCESS_EXPANDED;
    int expanded=process_expanded||command==CHAIN_TAIL_EXPANDED;
    unsigned int frames=(command==WDA_PROCESS||process_expanded||command==STREAM_PROCESS) ? 512 : 0;
    if(command==STREAM_PROCESS&&c->magic==STREAM_MAGIC)frames=sony_pcm_source_frames(input_rate);
    unsigned int rate=command==WDA_OPEN || command==WDA_PROCESS || process_expanded || command==STREAM_PROCESS ?
        (c->magic==STREAM_MAGIC?input_rate:48000) : 0;
    unsigned int channels=(command==WDA_OPEN||command==WDA_PROCESS||process_expanded||command==STREAM_PROCESS)?2:0;
    struct wda_header h={c->magic,CHAIN_VERSION,command,++c->request,c->session,
        command==WDA_OPEN ? 1 : c->revision,size,0,frames,rate,channels,0},r;
    long long now=milliseconds();int result;
    if(now<0) {c->failed=1;return WDA_CLIENT_IO;}
    long long deadline=now+c->timeout_ms;
    result=transfer(c->tx,&h,sizeof(h),1,deadline);
    if(!result && size) result=transfer(c->tx,(void *)data,size,1,deadline);
    if(!result) result=transfer(c->rx,&r,sizeof(r),0,deadline);
    if(result) {c->failed=1;return result;}
    int shape=0;
    if(command==CHAIN_TAIL_EXPANDED) {
        const unsigned int *request=data;
        unsigned int base=request[0]==100?4096u:10240u;
        shape=(r.sample_rate==48000||r.sample_rate==192000)&&r.channels==2&&
            r.frames==base*(r.sample_rate==192000?4u:1u)&&r.frames<=request[1]&&r.payload_bytes==r.frames*8;
    } else if(expanded)shape=r.frames && r.frames<=CHAIN_MAX_EXPANDED_FRAMES&&
        r.frames<=*(const unsigned int *)data&&r.payload_bytes==r.frames*8&&
        (r.sample_rate==48000||r.sample_rate==192000)&&r.channels==2;
    else if(command==WDA_PROCESS) shape=r.payload_bytes==4096;
    else if(command==STREAM_STATE)shape=r.payload_bytes==sizeof(struct sony_stream_state);
    else if(command==STREAM_TIMING)shape=r.payload_bytes==sizeof(struct sony_output_timing);
    else if(command==WDA_STATE)shape=r.payload_bytes==sizeof(struct sony_chain_settings);
    else if(command==CHAIN_TONE_STATE)shape=r.payload_bytes==sizeof(struct wdc_tone_state);
    else if(command==STREAM_OUTPUT_STATE)shape=r.payload_bytes==sizeof(struct sony_output_controls);
    else if(command==STREAM_DSEE_STATE)shape=r.payload_bytes==sizeof(struct sony_dsee_controls);
    else if(command==STREAM_NORMALIZER_STATE)shape=r.payload_bytes==sizeof(int);
    else if(command==STREAM_VOLUME_MEMORY_STATE)shape=r.payload_bytes==sizeof(struct sony_volume_memory);
    else if(command==WDA_CONTROLS) shape=r.payload_bytes>=1 && r.payload_bytes<=capacity;
    else if(command==CHAIN_TAIL) shape=r.payload_bytes==(*(const unsigned int *)data==100 ? 32768u : 81920u);
    else shape=r.payload_bytes==0;
    /* Preserve native close errors while tracking whether the worker has
     * released its session. Bounds and payload shape protect the caller's
     * buffers; response metadata is not used to authenticate the peer. */
    int closed_with_error=c->magic==STREAM_MAGIC&&command==WDA_CLOSE&&
        r.status==WDA_BACKEND&&!r.session&&!r.revision;
    if(r.payload_bytes>capacity || (r.status && r.payload_bytes) || (!r.status && !shape)) {
        c->failed=1;return WDA_CLIENT_BAD_REPLY;
    }
    if(r.status) {if(closed_with_error){c->session=0;c->revision=0;}return r.status;}
    if(r.payload_bytes) {
        result=transfer(c->rx,out,r.payload_bytes,0,deadline);
        if(result) {c->failed=1;return result;}
    }
    c->session=r.session;c->revision=r.revision;*received=r.payload_bytes;if(reply_rate)*reply_rate=r.sample_rate;return 0;
}
static int exchange_full(struct wdc_client *c,unsigned int command,const void *data,unsigned int size,
    void *out,unsigned int capacity,unsigned int *received,unsigned int *reply_rate) {
    return exchange_full_rate(c,command,data,size,out,capacity,received,reply_rate,c->input_rate);
}
static int exchange(struct wdc_client *c,unsigned int command,const void *data,unsigned int size,
    void *out,unsigned int capacity,unsigned int *received) {
    return exchange_full(c,command,data,size,out,capacity,received,NULL);
}
int wdc_client_attach(struct wdc_client **out, int tx, int rx, int timeout_ms) {
    if (!out) return WDA_PROTOCOL;
    *out = NULL;
    struct wdc_client *client = calloc(1,sizeof(*client));
    if (!client) return WDA_CLIENT_IO;
    if (pthread_mutex_init(&client->mutex,NULL)) { free(client); return WDA_CLIENT_IO; }
    int tx_flags = fcntl(tx,F_GETFL), rx_flags = fcntl(rx,F_GETFL);
    if (tx_flags < 0 || rx_flags < 0 || fcntl(tx,F_SETFL,tx_flags | O_NONBLOCK)) goto failure;
    if (fcntl(rx,F_SETFL,rx_flags | O_NONBLOCK)) { fcntl(tx,F_SETFL,tx_flags); goto failure; }
    client->magic=CHAIN_MAGIC;client->input_rate=48000;
    client->tx = tx; client->rx = rx; client->timeout_ms = timeout_ms;
    *out = client;
    return 0;
failure:
    pthread_mutex_destroy(&client->mutex); free(client); return WDA_CLIENT_IO;
}


int wdc_client_attach_output(struct wdc_client **out,int tx,int rx,int timeout_ms) {
    if(!out)return WDA_PROTOCOL;
    *out=NULL;
    int status=wdc_client_attach(out,tx,rx,timeout_ms);
    if(!status)(*out)->magic=STREAM_MAGIC;
    return status;
}
static int output_idle_command(struct wdc_client *c,unsigned int command) {
    if(!c)return WDA_PROTOCOL;
    pthread_mutex_lock(&c->mutex);unsigned int got=0;
    int status=c->session?WDA_STATE_ERROR:exchange(c,command,NULL,0,NULL,0,&got);
    pthread_mutex_unlock(&c->mutex);return status;
}
int wdc_client_output_ready(struct wdc_client *c) {return output_idle_command(c,STREAM_READY);}
int wdc_client_shutdown_output(struct wdc_client *c) {return output_idle_command(c,WDA_SHUTDOWN);}
static int control(struct wdc_client *c,unsigned int command,const void *data,unsigned int bytes) {
    if(!c) return WDA_PROTOCOL;
    pthread_mutex_lock(&c->mutex);unsigned int got=0;
    int result=(command==WDA_OPEN ? c->session!=0 : c->session==0) ? WDA_STATE_ERROR
        : (command==WDA_UPDATE||command==CHAIN_TONE_UPDATE||command==STREAM_OUTPUT_UPDATE||command==STREAM_DSEE_UPDATE||command==STREAM_VOLUME_MEMORY_UPDATE||command==STREAM_NORMALIZER_UPDATE) && c->revision==UINT_MAX ? WDA_STATE_ERROR
        : exchange(c,command,data,bytes,NULL,0,&got);
    pthread_mutex_unlock(&c->mutex);return result;
}
int wdc_client_open_stream(struct wdc_client *c,const struct sony_chain_settings *s) {
    if(c&&c->magic==STREAM_MAGIC)return wdc_client_open_output_rate(c,s,48000);
    return s ? control(c,WDA_OPEN,s,sizeof(*s)) : WDA_PROTOCOL;
}
int wdc_client_update(struct wdc_client *c,const struct sony_chain_settings *s) {
    return s ? control(c,WDA_UPDATE,s,sizeof(*s)) : WDA_PROTOCOL;
}
int wdc_client_get_settings(struct wdc_client *c,struct sony_chain_settings *out) {
    if(!c||!out)return WDA_PROTOCOL;
    struct sony_chain_settings state;unsigned int got=0;
    pthread_mutex_lock(&c->mutex);
    int status=!c->session?WDA_STATE_ERROR:exchange(c,WDA_STATE,NULL,0,&state,sizeof(state),&got);
    if(!status)*out=state;
    pthread_mutex_unlock(&c->mutex);return status;
}
static int valid_tone(const struct sony_tone_settings *s) {
    if(!s||(s->enabled!=0&&s->enabled!=1))return 0;
    for(unsigned int i=0;i<3;++i)if(s->gains[i]<-20||s->gains[i]>20||s->centerfreq[i]!=2)return 0;
    return 1;
}
int wdc_client_update_tone(struct wdc_client *c,const struct sony_tone_settings *s) {
    return valid_tone(s)?control(c,CHAIN_TONE_UPDATE,s,sizeof(*s)):WDA_FORMAT;
}
int wdc_client_update_output(struct wdc_client *c,const struct sony_output_controls *s) {
    if(!c)return WDA_PROTOCOL;
    return sony_output_controls_valid(s)?control(c,STREAM_OUTPUT_UPDATE,s,sizeof(*s)):WDA_FORMAT;
}
int wdc_client_get_output_controls(struct wdc_client *c,struct sony_output_controls *out) {
    if(!c||!out)return WDA_PROTOCOL;
    struct sony_output_controls state;unsigned int got=0;pthread_mutex_lock(&c->mutex);
    int status=!c->session?WDA_STATE_ERROR:exchange(c,STREAM_OUTPUT_STATE,NULL,0,&state,sizeof(state),&got);
    if(!status)*out=state;
    pthread_mutex_unlock(&c->mutex);return status;
}
int wdc_client_update_dsee(struct wdc_client *c,const struct sony_dsee_controls *s) {
    if(!c)return WDA_PROTOCOL;
    return sony_dsee_controls_valid(s)?control(c,STREAM_DSEE_UPDATE,s,sizeof(*s)):WDA_FORMAT;
}
int wdc_client_get_dsee(struct wdc_client *c,struct sony_dsee_controls *out) {
    if(!c||!out)return WDA_PROTOCOL;
    struct sony_dsee_controls state;unsigned int got=0;pthread_mutex_lock(&c->mutex);
    int status=!c->session?WDA_STATE_ERROR:exchange(c,STREAM_DSEE_STATE,NULL,0,&state,sizeof(state),&got);
    if(!status)*out=state;
    pthread_mutex_unlock(&c->mutex);return status;
}
int wdc_client_update_normalizer(struct wdc_client *c,int enabled) {
    if(!c)return WDA_PROTOCOL;
    return (enabled==0||enabled==1)?control(c,STREAM_NORMALIZER_UPDATE,&enabled,sizeof(enabled)):WDA_FORMAT;
}
int wdc_client_get_normalizer(struct wdc_client *c,int *out) {
    if(!c||!out)return WDA_PROTOCOL;
    int state;unsigned int got=0;pthread_mutex_lock(&c->mutex);
    int status=!c->session?WDA_STATE_ERROR:exchange(c,STREAM_NORMALIZER_STATE,NULL,0,&state,sizeof(state),&got);
    if(!status)*out=state;
    pthread_mutex_unlock(&c->mutex);return status;
}
int wdc_client_get_tone(struct wdc_client *c,struct wdc_tone_state *out) {
    if(!c||!out)return WDA_PROTOCOL;
    struct wdc_tone_state state;unsigned int got=0;
    pthread_mutex_lock(&c->mutex);
    int status=!c->session?WDA_STATE_ERROR:exchange(c,CHAIN_TONE_STATE,NULL,0,&state,sizeof(state),&got);
    if(!status)*out=state;
    pthread_mutex_unlock(&c->mutex);return status;
}
int wdc_client_update_volume_memory(struct wdc_client *c,const struct sony_volume_memory *s) {
    if(!c)return WDA_PROTOCOL;
    return sony_volume_memory_valid(s)?control(c,STREAM_VOLUME_MEMORY_UPDATE,s,sizeof(*s)):WDA_FORMAT;
}
int wdc_client_get_volume_memory(struct wdc_client *c,struct sony_volume_memory *out) {
    if(!c||!out)return WDA_PROTOCOL;
    struct sony_volume_memory state;unsigned int got=0;pthread_mutex_lock(&c->mutex);
    int status=!c->session?WDA_STATE_ERROR:exchange(c,STREAM_VOLUME_MEMORY_STATE,NULL,0,&state,sizeof(state),&got);
    if(!status)*out=state;
    pthread_mutex_unlock(&c->mutex);return status;
}
int wdc_client_eos(struct wdc_client *c) {return control(c,CHAIN_EOS,NULL,0);}
int wdc_client_save(struct wdc_client *c) {return control(c,CHAIN_SAVE,NULL,0);}
int wdc_client_save_preset(struct wdc_client *c,unsigned int operation,unsigned int slot,const void *expected){
    if(!c||!expected||slot<1||slot>3||
       (operation!=SONY_SOUND_PRESET_SAVE&&operation!=SONY_SOUND_PRESET_LOAD))return WDA_FORMAT;
    struct {unsigned int operation,slot;unsigned char expected[SONY_SNAPSHOT_BYTES];} request;
    request.operation=operation;request.slot=slot;memcpy(request.expected,expected,sizeof(request.expected));
    return control(c,STREAM_PRESET_COMMIT,&request,sizeof(request));
}
int wdc_client_close_stream(struct wdc_client *c) {return control(c,WDA_CLOSE,NULL,0);}
int wdc_client_process(struct wdc_client *c,const int *input,int *output,unsigned int frames) {
    if(!c || !input || !output || frames!=512) return WDA_FORMAT;
    int samples[1024];unsigned int got=0;
    pthread_mutex_lock(&c->mutex);
    int result=!c->session ? WDA_STATE_ERROR : exchange(c,WDA_PROCESS,input,4096,samples,sizeof(samples),&got);
    if(!result) memcpy(output,samples,sizeof(samples));
    pthread_mutex_unlock(&c->mutex);return result;
}
int wdc_client_process_expanded(struct wdc_client *c,const int *input,int *output,
    unsigned int input_frames,unsigned int capacity_frames,unsigned int *output_frames,unsigned int *output_rate) {
    if(!c||!input||!output||!output_frames||!output_rate||input_frames!=512||
       !capacity_frames||capacity_frames>CHAIN_MAX_EXPANDED_FRAMES)return WDA_FORMAT;
    unsigned int request[1025];int samples[CHAIN_MAX_EXPANDED_FRAMES*2];
    request[0]=capacity_frames;memcpy(request+1,input,4096);
    unsigned int got=0,rate=0;
    pthread_mutex_lock(&c->mutex);
    int status=!c->session?WDA_STATE_ERROR:exchange_full(c,CHAIN_PROCESS_EXPANDED,request,sizeof(request),
        samples,capacity_frames*8,&got,&rate);
    if(!status){memcpy(output,samples,got);*output_frames=got/8;*output_rate=rate;}
    pthread_mutex_unlock(&c->mutex);return status;
}
int wdc_client_tail_expanded(struct wdc_client *c,unsigned int ms,int *output,
    unsigned int capacity_frames,unsigned int *output_frames,unsigned int *output_rate) {
    if(!c||!output||!output_frames||!output_rate||(ms!=100&&ms!=250)||!capacity_frames||
       capacity_frames>CHAIN_MAX_EXPANDED_TAIL_FRAMES)return WDA_FORMAT;
    int *samples=malloc(capacity_frames*8);if(!samples)return WDA_CLIENT_IO;
    unsigned int request[2]={ms,capacity_frames},got=0,rate=0;
    pthread_mutex_lock(&c->mutex);
    int status=!c->session?WDA_STATE_ERROR:exchange_full(c,CHAIN_TAIL_EXPANDED,request,sizeof(request),
        samples,capacity_frames*8,&got,&rate);
    if(!status){memcpy(output,samples,got);*output_frames=got/8;*output_rate=rate;}
    pthread_mutex_unlock(&c->mutex);free(samples);return status;
}
int wdc_client_tail(struct wdc_client *c,unsigned int ms,int *output,unsigned int capacity_frames,unsigned int *frames) {
    if(!c || !output || !frames || (ms!=100 && ms!=250) || capacity_frames<(ms==100 ? 4096u : 10240u)) return WDA_FORMAT;
    /* No partial output on timeout/truncated response. One bounded staging buffer. */
    int *samples=malloc(81920);if(!samples) return WDA_CLIENT_IO;
    unsigned int got=0;pthread_mutex_lock(&c->mutex);
    int result=!c->session ? WDA_STATE_ERROR : exchange(c,CHAIN_TAIL,&ms,4,samples,81920,&got);
    if(!result) {memcpy(output,samples,got);*frames=got/8;}
    pthread_mutex_unlock(&c->mutex);free(samples);return result;
}
int wdc_client_query(struct wdc_client *c,const char *key,char *output,unsigned int capacity) {
    if(!c || !key || !output || !capacity) return WDA_PROTOCOL;
    unsigned int n=0;while(n<256 && key[n]) ++n;
    if(!n || n==256) return WDA_FORMAT;
    char text[1024];unsigned int got=0;pthread_mutex_lock(&c->mutex);
    int result=!c->session ? WDA_STATE_ERROR : exchange(c,WDA_CONTROLS,key,n+1,text,sizeof(text),&got);
    if(!result && (text[got-1] || strlen(text)!=got-1)) {c->failed=1;result=WDA_CLIENT_BAD_REPLY;}
    if(!result && got>capacity) result=WDA_FORMAT;
    if(!result) memcpy(output,text,got);
    pthread_mutex_unlock(&c->mutex);return result;
}
int wdc_client_failed(struct wdc_client *client) {
    if (!client) return 1;
    pthread_mutex_lock(&client->mutex); int failed = client->failed; pthread_mutex_unlock(&client->mutex);
    return failed;
}

void wdc_client_destroy(struct wdc_client *client) {
    if (!client) return;
    /* Caller must join all users before destruction. EOF stops worker audio. */
    close(client->tx); if(client->rx!=client->tx)close(client->rx);
    pthread_mutex_destroy(&client->mutex); free(client);
}

int wdc_client_process_output(struct wdc_client *c,const int *input,unsigned int frames) {
    if(!c||!input||frames!=sony_pcm_source_frames(c->input_rate))return WDA_FORMAT;
    return control(c,STREAM_PROCESS,input,frames*8);
}
int wdc_client_finish_output(struct wdc_client *c,unsigned int ms) {
    if(!c||(ms!=100&&ms!=250))return WDA_FORMAT;
    return control(c,STREAM_FINISH,&ms,4);
}
int wdc_client_get_output_state(struct wdc_client *c,struct sony_stream_state *out) {
    if(!c||!out)return WDA_FORMAT;
    struct sony_stream_state state;unsigned int got=0;
    pthread_mutex_lock(&c->mutex);
    int status=!c->session?WDA_STATE_ERROR:exchange(c,STREAM_STATE,NULL,0,&state,sizeof(state),&got);
    if(!status)*out=state;
    pthread_mutex_unlock(&c->mutex);return status;
}

int wdc_client_open_output(struct wdc_client *c,const struct sony_chain_settings *settings) {
    return wdc_client_open_output_rate(c,settings,48000);
}
int wdc_client_get_output_timing(struct wdc_client *c,struct sony_output_timing *out) {
    if(!c||!out)return WDA_FORMAT;
    struct sony_output_timing t;unsigned int got=0;
    pthread_mutex_lock(&c->mutex);
    int status=!c->session?WDA_STATE_ERROR:exchange(c,STREAM_TIMING,NULL,0,&t,sizeof(t),&got);
    if(!status)*out=t;
    pthread_mutex_unlock(&c->mutex);return status;
}
int wdc_client_open_output_rate(struct wdc_client *c,const struct sony_chain_settings *settings,unsigned int rate) {
    if(!c||!settings)return WDA_FORMAT;
    unsigned int got=0;
    pthread_mutex_lock(&c->mutex);
    int status=c->session?WDA_STATE_ERROR:exchange_full_rate(c,WDA_OPEN,settings,sizeof(*settings),NULL,0,&got,NULL,rate);
    if(!status)c->input_rate=rate;
    pthread_mutex_unlock(&c->mutex);return status;
}
