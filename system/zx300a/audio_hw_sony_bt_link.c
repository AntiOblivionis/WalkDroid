/* A2DP PCM attachment to the root-owned persistent Sony connection service.
 * This HAL never connects/disconnects RF or receives pairing material.
 * Standby finishes one PCM epoch; the root service retains the peer link. */
#define _GNU_SOURCE
#include <hardware/audio.h>
#include "sony_audio_bt_tx_data.h"
#include "sony_audio_bt_tx_hal_config.h"
#include "sony_audio_bt_tx_route_gate.h"
#include "sony_audio_bt_link_service_config.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>
struct tx_device;
struct tx_out {
    struct audio_stream_out stream;
    struct tx_device *device;
    struct tx_out *next;
    unsigned rate,session_tail;
    int error,closing,routed;
    uint64_t accepted,submitted,uncertain,session_frames,accepted_base,submitted_base;
    struct timespec started;
    struct wbt_hal_route route;
    struct wbt_route_guard *route_guard;
    struct sony_tx_pcm_stats processing;
    uint64_t processing_epoch;
};
enum tx_job {TX_WRITE,TX_STOP,TX_DRAIN,TX_REAP};
struct tx_device {
    struct audio_hw_device device;
    pthread_mutex_t lock;
    pthread_cond_t changed;
    pthread_t controller;
    int busy,done,quit,failed,suspended,log,active,routed;
    enum tx_job job;
    const void *data;
    size_t bytes;
    ssize_t result;
    struct tx_out *out;
    struct tx_out *outputs;
    int data_fd,attached;
    struct wbd_client client;
};
static pthread_mutex_t module_lock=PTHREAD_MUTEX_INITIALIZER;
static int module_open;
static void snapshot(struct tx_device*d,int rc){
    struct tx_out*s=d->out;struct wbd_client*c=&d->client;if(!s||!d->attached)return;
    s->accepted=s->accepted_base+c->accepted;s->submitted=s->submitted_base+c->submitted;
    uint64_t uncertain=rc?c->accepted-c->submitted:0;if(uncertain>s->uncertain)s->uncertain=uncertain;
    if(c->last_reply.flags&WBD_PROCESSING_STATS)s->processing=c->last_reply.processing;
}
static void detach(struct tx_device*d){
    if(d->data_fd>=0)close(d->data_fd);d->data_fd=-1;d->attached=d->active=0;
}
static int stop_locked(struct tx_device *d,int cancel){
    int rc=0;if(d->attached){
        if(d->client.phase==WBD_STREAMING)rc=cancel?wbd_client_drop(&d->client):wbd_client_pause(&d->client);
        snapshot(d,rc);
        if(cancel||rc){if(!rc&&d->client.phase==WBD_IDLE)rc=wbd_client_detach(&d->client);detach(d);}
    }
    d->active=0;if(rc&&d->out&&!d->out->error)d->out->error=rc;return rc;
}
static int attach(struct tx_device*d){
    int fd=socket(AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC,0);if(fd<0)return -errno;
    struct sockaddr_un a={.sun_family=AF_UNIX};memcpy(a.sun_path+1,WLC_DATA_SOCKET,sizeof(WLC_DATA_SOCKET)-1);
    int rc=connect(fd,(void*)&a,offsetof(struct sockaddr_un,sun_path)+sizeof(WLC_DATA_SOCKET))?-errno:0;
    if(!rc)rc=wbd_client_init(&d->client,fd,-1,d->out->route.generation,-1);
    if(!rc)rc=wbd_client_attach(&d->client);
    if(rc){close(fd);return rc;}
    d->data_fd=fd;d->attached=1;return 0;
}
static ssize_t write_locked(struct tx_device *d){
    struct tx_out *s=d->out;int rc=d->failed?d->failed:s->error;
    if(rc)return rc;if(s->closing)return -ESHUTDOWN;
    if(d->suspended||!d->routed)return -EAGAIN;
    rc=wbt_route_guard_check(s->route_guard);
    if(rc){s->error=rc;stop_locked(d,1);return rc;}
    if(!d->attached){rc=attach(d);if(rc){s->error=rc;return rc;}}
    if(!d->active){
        s->accepted_base=s->accepted;s->submitted_base=s->submitted;
        rc=wbd_client_resume(&d->client,s->rate);
        if(rc){s->error=rc;stop_locked(d,1);return rc;}
        if(clock_gettime(CLOCK_MONOTONIC,&s->started)){rc=-errno;s->error=rc;stop_locked(d,1);return rc;}
        s->session_frames=0;s->session_tail=0;d->active=1;++s->processing_epoch;
        /* COMMIT may have been revoked during RESUME. Do not
         * submit its first PCM block. Steady writes need only the entry
         * check above; their lifetime SH guard already prevents retirement. */
        rc=wbt_route_guard_check(s->route_guard);
        if(rc){s->error=rc;stop_locked(d,1);return rc;}
    }
    struct wbd_client *client=&d->client;
    /* The outer write splits requests at the transport capacity. A failed
     * transaction remains an error; prior completed chunks return short. */
    uint32_t request=client->next_request;
    rc=wbd_client_write(client,d->data,(unsigned)d->bytes);
    if(client->last_reply.flags&WBD_PROCESSING_STATS)s->processing=client->last_reply.processing;
    s->accepted=s->accepted_base+client->accepted;s->submitted=s->submitted_base+client->submitted;
    s->uncertain=rc?(client->last_reply.request==request?client->last_reply.uncertain_frames:
        client->accepted-client->submitted+d->bytes/4):0;
    if(rc){s->error=rc;stop_locked(d,1);return rc;}
    uint64_t frame_bytes=(uint64_t)s->session_tail+d->bytes;
    s->session_frames+=frame_bytes/4;s->session_tail=(unsigned)(frame_bytes%4);
    /* Source clock pacing, NOT a hardware clock. Allow one native block of
     * prefill; never derive a presentation position from this timer. */
    uint64_t paced=s->session_frames>2048?s->session_frames-2048:0;
    uint64_t ns=(uint64_t)s->started.tv_nsec+(paced*UINT64_C(1000000000))/s->rate;
    struct timespec deadline={s->started.tv_sec+(time_t)(ns/1000000000),(long)(ns%1000000000)};
    do{rc=clock_nanosleep(CLOCK_MONOTONIC,TIMER_ABSTIME,&deadline,NULL);}while(rc==EINTR);
    if(rc){s->error=-rc;stop_locked(d,1);return -rc;}
    return (ssize_t)d->bytes;
}
static void *controller(void *context){
    struct tx_device *d=context;pthread_mutex_lock(&d->lock);
    for(;;){
        while((!d->busy||d->done)&&!d->quit)pthread_cond_wait(&d->changed,&d->lock);
        if(d->quit)break;
        d->result=d->job==TX_WRITE?write_locked(d):stop_locked(d,d->job!=TX_DRAIN);
        d->done=1;pthread_cond_broadcast(&d->changed);
    }
    pthread_mutex_unlock(&d->lock);return NULL;
}
static ssize_t dispatch(struct tx_device *d,enum tx_job job,const void *data,size_t bytes){
    while(d->busy)pthread_cond_wait(&d->changed,&d->lock);
    d->busy=1;d->done=0;d->job=job;d->data=data;d->bytes=bytes;pthread_cond_broadcast(&d->changed);
    while(!d->done)pthread_cond_wait(&d->changed,&d->lock);
    ssize_t rc=d->result;d->busy=0;d->data=NULL;pthread_cond_broadcast(&d->changed);return rc;
}
/* AudioPolicy may reopen a dynamic profile before the probe stream's close
 * reaches the HAL. Keep each opened stream alive, and attach the one that
 * actually writes to the single Sony PCM transport. */
static int select_output(struct tx_device *d,struct tx_out *s){
    while(d->busy)pthread_cond_wait(&d->changed,&d->lock);
    if(d->out==s)return 0;
    if(d->active)return -EBUSY;
    if(d->attached){int rc=dispatch(d,TX_STOP,NULL,0);if(rc)return rc;}
    d->out=s;d->routed=s->routed;return 0;
}
static uint32_t rate(const struct audio_stream *s){return ((const struct tx_out *)s)->rate;}
static int set_rate(struct audio_stream *stream,uint32_t n){
    struct tx_out *s=(struct tx_out *)stream;struct tx_device *d=s->device;
    if(!n)n=48000;pthread_mutex_lock(&d->lock);
    int rc=0;if(s->rate!=n&&d->out==s&&d->active)rc=dispatch(d,TX_STOP,NULL,0);
    if(!rc)s->rate=n;pthread_mutex_unlock(&d->lock);return rc;
}
static size_t buffer_size(const struct audio_stream *s){(void)s;return WBT_MAX_PCM;}
static audio_channel_mask_t channels(const struct audio_stream *s){(void)s;return AUDIO_CHANNEL_OUT_STEREO;}
static audio_format_t format(const struct audio_stream *s){(void)s;return AUDIO_FORMAT_PCM_16_BIT;}
/* The getter and open config report the actual S16 format; there is no converter here. */
static int set_format(struct audio_stream *s,audio_format_t f){(void)s;(void)f;return 0;}
static int standby(struct audio_stream *stream){
    struct tx_device *d=((struct tx_out *)stream)->device;pthread_mutex_lock(&d->lock);
    struct tx_out *s=(struct tx_out*)stream;
    /* Normal idle drains the PCM already accepted at Sony's entry. Explicit
     * route removal, suspension, closing or failure uses CANCEL and never
     * creates a converter tail merely to clean up. */
    int cancel=d->failed||s->error||d->suspended||!d->routed||s->closing;
    int rc=d->out==s?dispatch(d,cancel?TX_STOP:TX_DRAIN,NULL,0):0;pthread_mutex_unlock(&d->lock);return rc;
}
static int drain(struct audio_stream_out *s,audio_drain_type_t type){
    if(type!=AUDIO_DRAIN_ALL)return -ENOSYS;
    struct tx_device *d=((struct tx_out *)s)->device;pthread_mutex_lock(&d->lock);
    int rc=d->out==(struct tx_out *)s?dispatch(d,TX_DRAIN,NULL,0):0;pthread_mutex_unlock(&d->lock);return rc;
}
static ssize_t write_audio(struct audio_stream_out *stream,const void *data,size_t bytes){
    if(!bytes)return 0;if(!data)return -EINVAL;
    struct tx_out *s=(struct tx_out *)stream;struct tx_device *d=s->device;const uint8_t *p=data;size_t done=0;
    pthread_mutex_lock(&d->lock);
    int selected=select_output(d,s);if(selected){pthread_mutex_unlock(&d->lock);return selected;}
    while(done<bytes&&done<(size_t)SSIZE_MAX){
        size_t part=bytes-done;if(part>WBT_MAX_PCM)part=WBT_MAX_PCM;
        if(part>(size_t)SSIZE_MAX-done)part=(size_t)SSIZE_MAX-done;
        ssize_t rc=dispatch(d,TX_WRITE,p+done,part);
        if(rc<0){pthread_mutex_unlock(&d->lock);return done?(ssize_t)done:rc;}
        if(!rc)break;done+=(size_t)rc;
    }
    pthread_mutex_unlock(&d->lock);return (ssize_t)done;
}
static uint32_t latency(const struct audio_stream_out *s){(void)s;return 200;} /* Conservative estimate only. */
static int volume(struct audio_stream_out *s,float l,float r){(void)s;(void)l;(void)r;return -ENOSYS;}
static int position(const struct audio_stream_out *s,uint32_t *p){(void)s;(void)p;return -ENOSYS;}
static int presentation(const struct audio_stream_out *s,uint64_t *p,struct timespec *t){(void)s;(void)p;(void)t;return -ENOSYS;}
static int timestamp(const struct audio_stream_out *s,int64_t *t){(void)s;(void)t;return -ENOSYS;}
static int effect(const struct audio_stream *s,effect_handle_t e){(void)s;(void)e;return -ENOSYS;}
static int params_locked(struct tx_device *d,struct tx_out *stream,const char *p){
    if(!p||!*p)return 0;char *copy=strdup(p);if(!copy)return -ENOMEM;
    int suspend=-1,closing=0,routed=-1;
    for(char *part=copy;part;){
        char *next=strchr(part,';');if(next)*next++=0;
        char *value=strchr(part,'=');if(value)*value++=0;
        if(value&&!strcmp(part,"A2dpSuspended")){
            if(!strcmp(value,"true"))suspend=1;else if(!strcmp(value,"false"))suspend=0;
        }else if(value&&(!strcmp(part,"closing")||!strcmp(part,"exiting"))){
            if(!strcmp(value,"true")||!strcmp(value,"1"))closing=1;
        }else if(value&&!strcmp(part,AUDIO_PARAMETER_STREAM_ROUTING)){
            char *end;errno=0;unsigned long v=strtoul(value,&end,10);
            if(end!=value&&!*end&&!errno)routed=!!v;
        }
        part=next;
    }
    free(copy);
    if(d->failed)return d->failed;
    if(suspend>=0)d->suspended=suspend;
    if(routed>=0){
        if(stream)stream->routed=routed;
        else for(struct tx_out *s=d->outputs;s;s=s->next)s->routed=routed;
        if(!stream||d->out==stream)d->routed=routed;
    }
    /* Closing belongs to this stream. A later open gets a fresh stream while
     * a genuine device suspension survives until explicit resume. */
    struct tx_out *target=stream?stream:d->out;
    if(closing&&target)target->closing=1;
    if(d->out&&(suspend==1||((closing||routed==0)&&(!stream||stream==d->out))))return dispatch(d,TX_STOP,NULL,0);
    return 0;
}
static int device_parameters(struct audio_hw_device *dev,const char *p){
    struct tx_device *d=(struct tx_device *)dev;pthread_mutex_lock(&d->lock);
    int rc=params_locked(d,NULL,p);pthread_mutex_unlock(&d->lock);return rc;
}
static int stream_parameters(struct audio_stream *stream,const char *p){
    struct tx_out *s=(struct tx_out *)stream;struct tx_device *d=s->device;pthread_mutex_lock(&d->lock);
    int rc=params_locked(d,s,p);pthread_mutex_unlock(&d->lock);return rc;
}
static int requested_parameter(const char *keys,const char *name){
    size_t n=strlen(name);
    for(const char *p=keys;*p;){
        const char *end=strchr(p,';');size_t len=end?(size_t)(end-p):strlen(p);
        if(len>=n&&!strncmp(p,name,n)&&(len==n||p[n]=='='))return 1;
        if(!end)break;p=end+1;
    }
    return 0;
}
static char *parameters(struct tx_device *d,const struct tx_out *stream,const char *keys){
    if(!keys)return strdup("");
    /* AudioPolicy includes format=... in its capability queries. */
    char capabilities[192]="";
    if(requested_parameter(keys,"sup_sampling_rates"))strcat(capabilities,"sup_sampling_rates=44100|48000;");
    if(requested_parameter(keys,"sup_channels"))strcat(capabilities,"sup_channels=AUDIO_CHANNEL_OUT_STEREO;");
    if(requested_parameter(keys,"sup_formats"))strcat(capabilities,"sup_formats=AUDIO_FORMAT_PCM_16_BIT;");
    if(capabilities[0]){capabilities[strlen(capabilities)-1]=0;return strdup(capabilities);}
    if(strcmp(keys,"sony_bttx_state"))return strdup("");
    /* Binder dumps are TimeCheck guarded. Cold RF connection and native stop
     * may be slow: observing them must never wait on the controller lock. */
    int lock=pthread_mutex_trylock(&d->lock);
    if(lock==EBUSY)return strdup("sony_bttx_state=busy;sony_bttx_snapshot=unavailable");
    if(lock)return strdup("sony_bttx_state=query-error;sony_bttx_snapshot=unavailable");
    char *out=NULL;const struct tx_out *s=stream?stream:d->out;
    struct sony_tx_pcm_stats zero={0};const struct sony_tx_pcm_stats *v=s?&s->processing:&zero;
    if(asprintf(&out,"sony_bttx_backend=connection-service-v1;sony_bttx_attachment=%d;sony_bttx_active=%d;sony_bttx_failed=%d;sony_bttx_stream_error=%d;sony_bttx_suspended=%d;sony_bttx_accepted=%llu;sony_bttx_submitted=%llu;sony_bttx_uncertain=%llu;sony_bttx_generation=%llu;sony_bttx_presentation=unavailable;sony_bttx_processing_epoch=%llu;sony_bttx_input_rate=%u;sony_bttx_codec_rate=%u;sony_bttx_input_processed=%llu;sony_bttx_input_pending=%u;sony_bttx_input_discarded=%llu;sony_bttx_input_padding=%llu;sony_bttx_codec_generated=%llu;sony_bttx_codec_submitted=%llu;sony_bttx_codec_pending=%u;sony_bttx_codec_padding=%llu;sony_bttx_codec_discarded=%llu;sony_bttx_codec_uncertain=%llu;sony_bttx_processing_flags=%u",
        d->attached,d->active,d->failed,s?s->error:0,d->suspended,(unsigned long long)(s?s->accepted:0),(unsigned long long)(s?s->submitted:0),
        (unsigned long long)(s?s->uncertain:0),(unsigned long long)(s?s->route.generation:0),(unsigned long long)(s?s->processing_epoch:0),
        v->input_rate,v->codec_rate,(unsigned long long)v->input_processed,v->input_pending,(unsigned long long)v->input_discarded,(unsigned long long)v->input_padding,
        (unsigned long long)v->codec_generated,(unsigned long long)v->codec_submitted,v->codec_pending,(unsigned long long)v->codec_padding,
        (unsigned long long)v->codec_discarded,(unsigned long long)v->codec_uncertain,v->flags)<0)out=NULL;
    pthread_mutex_unlock(&d->lock);return out;
}
static char *get_device_parameters(const struct audio_hw_device *d,const char *k){return parameters((struct tx_device *)d,NULL,k);}
static char *get_stream_parameters(const struct audio_stream *s,const char *k){return parameters(((struct tx_out *)s)->device,(const struct tx_out *)s,k);}
static int dump_device(const struct audio_hw_device *d,int fd){char *p=get_device_parameters(d,"sony_bttx_state");if(!p)return -ENOMEM;dprintf(fd,"WalkDroid Sony original A2DP: %s\n",p);free(p);return 0;}
static int dump_stream(const struct audio_stream *s,int fd){return dump_device(&((struct tx_out *)s)->device->device,fd);}
static int open_output(struct audio_hw_device *device,audio_io_handle_t handle,audio_devices_t devices,
 audio_output_flags_t flags,struct audio_config *config,struct audio_stream_out **out,const char *address){
    (void)handle;(void)devices;(void)flags;(void)address;if(!config||!out)return -EINVAL;*out=NULL;
    unsigned n=config->sample_rate?config->sample_rate:48000;
    struct tx_device *d=(struct tx_device *)device;pthread_mutex_lock(&d->lock);
    int rc=d->failed?d->failed:0;struct wbt_hal_route route;struct wbt_route_guard *guard=NULL;
    if(!rc){int root=open(WLC_RAM_PARENT,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);if(root<0)rc=-errno;else{rc=wbt_route_guard_acquire_at(&guard,root,WLC_RAM_NAME,&route);close(root);}}
    struct tx_out *s=rc?NULL:calloc(1,sizeof(*s));if(!rc&&!s)rc=-ENOMEM;
    if(!rc){
        s->device=d;s->rate=n;s->route=route;s->route_guard=guard;s->routed=1;guard=NULL;
        s->next=d->outputs;d->outputs=s;
        struct audio_stream *c=&s->stream.common;
        c->get_sample_rate=rate;c->set_sample_rate=set_rate;c->get_buffer_size=buffer_size;c->get_channels=channels;c->get_format=format;c->set_format=set_format;
        c->standby=standby;c->dump=dump_stream;c->set_parameters=stream_parameters;c->get_parameters=get_stream_parameters;c->add_audio_effect=effect;c->remove_audio_effect=effect;
        s->stream.get_latency=latency;s->stream.set_volume=volume;s->stream.write=write_audio;s->stream.get_render_position=position;
        s->stream.get_presentation_position=presentation;s->stream.get_next_write_timestamp=timestamp;s->stream.drain=drain;
        config->sample_rate=n;config->format=AUDIO_FORMAT_PCM_16_BIT;config->channel_mask=AUDIO_CHANNEL_OUT_STEREO;*out=&s->stream;
    }
    wbt_route_guard_release(&guard);pthread_mutex_unlock(&d->lock);return rc;
}
static void close_output(struct audio_hw_device *device,struct audio_stream_out *stream){
    if(!stream)return;struct tx_device *d=(struct tx_device *)device;pthread_mutex_lock(&d->lock);
    struct tx_out *s=(struct tx_out *)stream;
    while(d->busy)pthread_cond_wait(&d->changed,&d->lock);
    if(d->out==s){dispatch(d,TX_STOP,NULL,0);d->out=NULL;}
    struct tx_out **p=&d->outputs;while(*p&&*p!=s)p=&(*p)->next;
    if(*p){*p=s->next;wbt_route_guard_release(&s->route_guard);free(s);}
    pthread_mutex_unlock(&d->lock);
}
static int init_check(const struct audio_hw_device *device){struct tx_device *d=(struct tx_device *)device;pthread_mutex_lock(&d->lock);int rc=d->failed;pthread_mutex_unlock(&d->lock);return rc;}
static int float_set(struct audio_hw_device *d,float v){(void)d;(void)v;return -ENOSYS;}
static int float_get(struct audio_hw_device *d,float *v){(void)d;(void)v;return -ENOSYS;}
static int bool_set(struct audio_hw_device *d,bool v){(void)d;(void)v;return -ENOSYS;}
static int bool_get(const struct audio_hw_device *d,bool *v){(void)d;(void)v;return -ENOSYS;}
static int master_mute(struct audio_hw_device *d,bool *v){return bool_get(d,v);}
static int mode(struct audio_hw_device *d,audio_mode_t m){(void)d;return m==AUDIO_MODE_NORMAL?0:-ENOSYS;}
static size_t input_size(const struct audio_hw_device *d,const struct audio_config *c){(void)d;(void)c;return 0;}
static int open_input(struct audio_hw_device *d,audio_io_handle_t h,audio_devices_t v,struct audio_config *c,struct audio_stream_in **s,audio_input_flags_t f,const char *a,audio_source_t o){
    (void)d;(void)h;(void)v;(void)c;(void)f;(void)a;(void)o;if(s)*s=NULL;return -ENOSYS;}
static void close_input(struct audio_hw_device *d,struct audio_stream_in *s){(void)d;(void)s;}
static int close_device(struct hw_device_t *device){
    struct tx_device *d=(struct tx_device *)device;pthread_mutex_lock(&d->lock);
    if(d->outputs){pthread_mutex_unlock(&d->lock);return -EBUSY;}
    int rc=dispatch(d,TX_REAP,NULL,0);
    d->quit=1;pthread_cond_broadcast(&d->changed);pthread_mutex_unlock(&d->lock);pthread_join(d->controller,NULL);
    close(d->log);pthread_cond_destroy(&d->changed);pthread_mutex_destroy(&d->lock);free(d);
    pthread_mutex_lock(&module_lock);module_open=0;pthread_mutex_unlock(&module_lock);return rc;
}
static int open_device(const hw_module_t *module,const char *name,hw_device_t **out){
    if(!out)return -EINVAL;*out=NULL;if(!name||strcmp(name,AUDIO_HARDWARE_INTERFACE))return -EINVAL;
    int rc=0;
    pthread_mutex_lock(&module_lock);if(module_open){pthread_mutex_unlock(&module_lock);return -EBUSY;}
    struct tx_device *d=calloc(1,sizeof(*d));if(!d){pthread_mutex_unlock(&module_lock);return -ENOMEM;}
    d->data_fd=-1;d->log=dup(STDERR_FILENO);int mutex_ready=0,cond_ready=0;
    if(d->log<0)rc=-errno;
    if(!rc){int e=pthread_mutex_init(&d->lock,NULL);if(e)rc=-e;else mutex_ready=1;}
    if(!rc){int e=pthread_cond_init(&d->changed,NULL);if(e)rc=-e;else cond_ready=1;}
    if(!rc){int e=pthread_create(&d->controller,NULL,controller,d);if(e)rc=-e;}
    if(rc){if(cond_ready)pthread_cond_destroy(&d->changed);if(mutex_ready)pthread_mutex_destroy(&d->lock);if(d->log>=0)close(d->log);free(d);pthread_mutex_unlock(&module_lock);return rc;}
    d->device.common.tag=HARDWARE_DEVICE_TAG;d->device.common.version=AUDIO_DEVICE_API_VERSION_2_0;d->device.common.module=(hw_module_t *)module;d->device.common.close=close_device;
    d->device.init_check=init_check;d->device.set_voice_volume=float_set;d->device.set_master_volume=float_set;d->device.get_master_volume=float_get;
    d->device.set_master_mute=bool_set;d->device.get_master_mute=master_mute;d->device.set_mode=mode;d->device.set_mic_mute=bool_set;d->device.get_mic_mute=bool_get;
    d->device.set_parameters=device_parameters;d->device.get_parameters=get_device_parameters;d->device.get_input_buffer_size=input_size;
    d->device.open_output_stream=open_output;d->device.close_output_stream=close_output;d->device.open_input_stream=open_input;d->device.close_input_stream=close_input;d->device.dump=dump_device;
    module_open=1;*out=&d->device.common;pthread_mutex_unlock(&module_lock);return 0;
}
static struct hw_module_methods_t methods={.open=open_device};
struct audio_module HAL_MODULE_INFO_SYM={.common={.tag=HARDWARE_MODULE_TAG,.module_api_version=AUDIO_MODULE_API_VERSION_0_1,
    .hal_api_version=HARDWARE_HAL_API_VERSION,.id=AUDIO_HARDWARE_MODULE_ID,.name="Sony Walkman ZX300A original A2DP",.author="WalkDroid",.methods=&methods}};
