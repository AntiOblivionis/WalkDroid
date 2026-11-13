#define _GNU_SOURCE
#include "sony_audio_receiver_async.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define JOBS 512u
#define POOL_FRAMES (384000u*2u)
#define BT_POOL_FRAMES (96000u*2u)
enum operation {OPEN=1,DATA=2,FINISH=3,VOLUME_STEP=4,OPEN_BT=5};
struct job {uint64_t stream,ns;struct sony_usb_stream source;struct sony_bt_pcm_stream bt_source;unsigned operation,frames,expected,frame_bytes;int step;};
struct sony_receiver_async {
    struct sony_receiver_session *session;struct sony_audio_owner *owner;
    uint64_t generation,last_stream,worker_stream;unsigned producer_open,producer_rate,producer_frame_bytes;enum sony_audio_owner_kind kind;
    struct sony_receiver_async_state state;struct job jobs[JOBS];
    unsigned head,count,read_at,write_at,pool_used,pool_frames;int32_t *pool;
    int32_t work[SONY_RECEIVER_ASYNC_MAX_FRAMES*2];
    pthread_mutex_t mutex;pthread_cond_t ready;pthread_t worker;
    unsigned attempted,close_attempted;
    unsigned controls;int wake[2];
};
static int status(int rc){return rc>0?-EPROTO:rc;}
static int producer(struct sony_receiver_async *p){return p?0:-EINVAL;}
static int authority(struct sony_receiver_async *p){return p&&p->owner?0:-EINVAL;}
/* mutex held. First errors never clear, even after successful join. */
static void record(struct sony_receiver_async *p,int rc){rc=status(rc);if(rc&&!p->state.first_error)p->state.first_error=rc;}
static void signal_work(struct sony_receiver_async *p){
    if(!p->controls){pthread_cond_signal(&p->ready);return;}
    unsigned char byte=1;ssize_t n;do{n=write(p->wake[1],&byte,1);}while(n<0&&errno==EINTR);
    if(n!=1&&(n>=0||errno!=EAGAIN)){record(p,n<0?-errno:-EIO);p->state.stopping=1;}
}
/* Worker-only event pump. No settings thread can enter the session. Pending
 * hardware updates run before the next DATA; fsync runs in the existing
 * one-shot SCHED_OTHER writer while this worker continues the current PCM.
 * OPEN/FINISH/relative-volume wait for that save before touching the route. */
static int control_events(struct sony_receiver_async *p,int pending,int wait){
    int fd[2],rc=sony_receiver_session_control_descriptors(p->session,fd);if(rc)return rc;
    struct pollfd fds[3]={{fd[0],POLLIN,0},{fd[1],POLLIN,0},{p->wake[0],POLLIN,0}};
    int n;do{n=poll(fds,3,pending==1?0:wait?1000:0);}while(n<0&&errno==EINTR);
    if(n<0)return -errno;
    if(fds[2].revents){unsigned char bytes[256];while(read(p->wake[0],bytes,sizeof(bytes))>0){}}
    if(wait&&!n&&!pending&&!p->attempted){rc=sony_receiver_session_observe(p->session);if(rc)return rc;}
    if(pending==1||fds[0].revents||fds[1].revents||(wait&&!n))return sony_receiver_session_poll_controls(p->session);
    return pending;
}
static void copy_in(struct sony_receiver_async *p,const void *data,unsigned frames){
    if(p->producer_frame_bytes==8){unsigned first=p->pool_frames-p->write_at;if(first>frames)first=frames;memcpy(p->pool+p->write_at*2,data,first*8u);memcpy(p->pool,(const char *)data+first*8u,(frames-first)*8u);}
    else for(unsigned n=0;n<frames;++n)memcpy(p->pool+((p->write_at+n)%p->pool_frames)*2,(const char *)data+n*4u,4);
    p->write_at=(p->write_at+frames)%p->pool_frames;p->pool_used+=frames;
}
static void copy_out(struct sony_receiver_async *p,unsigned frames,unsigned width){
    if(width==8){unsigned first=p->pool_frames-p->read_at;if(first>frames)first=frames;memcpy(p->work,p->pool+p->read_at*2,first*8u);memcpy((char *)p->work+first*8u,p->pool,(frames-first)*8u);}
    else for(unsigned n=0;n<frames;++n)memcpy((char *)p->work+n*4u,p->pool+((p->read_at+n)%p->pool_frames)*2,4);
    p->read_at=(p->read_at+frames)%p->pool_frames;p->pool_used-=frames;
}
static void drop(struct sony_receiver_async *p){
    p->state.discarded_frames+=p->state.queued_frames;p->state.queued_frames=p->state.queued_ns=0;
    p->state.volume_discarded+=p->state.volume_queued;p->state.volume_queued=0;
    p->head=p->count=p->read_at=p->write_at=p->pool_used=0;p->state.queued_jobs=0;p->producer_open=0;
}
/* Worker alone owns session calls; caller never holds mutex across these. */
static int finish_output(struct sony_receiver_async *p){
    if(!p->attempted)return 0;
    if(p->close_attempted)return p->state.finish_status?p->state.finish_status:p->state.close_status?p->state.close_status:p->state.state_status;
    p->close_attempted=1;struct sony_receiver_session_state s={0};
    struct sony_receiver_playback_cleanup cleanup={0};int inspect=sony_receiver_session_state(p->session,&s),a=0;
    if(!inspect&&s.stream_open&&!s.finished)a=sony_receiver_session_finish_stream(p->session,WUR_SERVICE_STOP);
    int b=sony_receiver_session_close_stream(p->session,&cleanup);
    pthread_mutex_lock(&p->mutex);p->state.last_stream=cleanup;p->state.finish_status=status(a);p->state.close_status=status(b);p->state.state_status=status(inspect);pthread_mutex_unlock(&p->mutex);
    if(!b){p->attempted=0;p->worker_stream=0;}return inspect?status(inspect):a?status(a):status(b);
}
static void observe_output(struct sony_receiver_async *p){
    struct sony_receiver_session_state state={0};int rc=sony_receiver_session_state(p->session,&state);
    pthread_mutex_lock(&p->mutex);p->state.state_status=status(rc);if(!rc)p->state.output=state;record(p,rc);pthread_mutex_unlock(&p->mutex);
}
static int volume_step(struct sony_receiver_async *p,int step,unsigned *applied_volume,unsigned *applied_revision){
    struct sony_controls c;unsigned revision=0,applied=0;
    int rc=sony_receiver_session_get_controls(p->session,&c,&revision);if(rc)return rc;
    /* Original VolumeAdlerOut Increment/Decrement clamp the index first;
     * SetVolume then applies CURRENT AVLS, except output3. The final setter
     * independently rechecks physical GPIO, owner, revision and that plan. */
    int64_t requested=(int64_t)c.settings.volume+step;int next=requested<0?0:requested>120?120:(int)requested;
    int threshold=c.avls_threshold<0?0:c.avls_threshold>120?120:c.avls_threshold;
    if(c.volume_memory.avls&&c.output.device!=3&&next>threshold)next=threshold;
    rc=sony_receiver_session_volume(p->session,p->generation,revision,(unsigned)next,&applied);
    if(!rc){*applied_volume=(unsigned)next;*applied_revision=applied;}return rc;
}
static void *worker(void *v){
    struct sony_receiver_async *p=v;int controls_pending=0;
    for(;;){
        if(p->controls){int rc=control_events(p,controls_pending,0);if(rc<0){pthread_mutex_lock(&p->mutex);record(p,rc);p->state.stopping=1;pthread_mutex_unlock(&p->mutex);}else controls_pending=rc;}
        pthread_mutex_lock(&p->mutex);
        while(!p->state.stopping&&(!p->count||(p->controls&&controls_pending&&(controls_pending==1||p->jobs[p->head].operation!=DATA)))){
            if(!p->controls)pthread_cond_wait(&p->ready,&p->mutex);
            else{pthread_mutex_unlock(&p->mutex);int rc=control_events(p,controls_pending,1);pthread_mutex_lock(&p->mutex);if(rc<0){record(p,rc);p->state.stopping=1;}else controls_pending=rc;}
        }
        if(p->state.stopping){drop(p);p->state.worker_busy=1;pthread_mutex_unlock(&p->mutex);int rc=finish_output(p);observe_output(p);pthread_mutex_lock(&p->mutex);record(p,rc);p->state.worker_busy=0;p->state.exited=1;pthread_mutex_unlock(&p->mutex);return NULL;}
        struct job job=p->jobs[p->head];p->head=(p->head+1)%JOBS;--p->count;
        /* Combine only DATA already queued for this exact source. Never
         * wait for more samples or cross OPEN/FINISH. The session's existing
         * maximum batch and per-native-write resource checks are unchanged.
         * Preserve the sum of original duration reservations, including
         * rounding, until this entire bounded write succeeds or fails. */
        if(job.operation==DATA)while(p->count){
            const struct job *next=&p->jobs[p->head];
            if(next->operation!=DATA||next->stream!=job.stream||next->frame_bytes!=job.frame_bytes||next->frames>SONY_RECEIVER_ASYNC_MAX_FRAMES-job.frames)break;
            job.frames+=next->frames;job.ns+=next->ns;p->head=(p->head+1)%JOBS;--p->count;
        }
        p->state.queued_jobs=p->count;p->state.worker_busy=1;
        if(job.operation==DATA){copy_out(p,job.frames,job.frame_bytes);p->state.queued_frames-=job.frames;p->state.queued_ns-=job.ns;p->state.inflight_frames=job.frames;p->state.inflight_ns=job.ns;}
        if(job.operation==VOLUME_STEP){--p->state.volume_queued;p->state.volume_inflight=1;}
        pthread_mutex_unlock(&p->mutex);
        unsigned applied_volume=0,applied_revision=0;int rc=authority(p);
        if(!rc&&(job.operation==OPEN||job.operation==OPEN_BT)){if(p->attempted)rc=-EPROTO;else{p->attempted=1;p->close_attempted=0;p->worker_stream=job.stream;rc=job.operation==OPEN_BT?sony_receiver_session_begin_bt(p->session,&job.bt_source,job.expected):sony_receiver_session_begin_usb(p->session,&job.source,job.expected);}}
        else if(!rc&&job.operation==VOLUME_STEP)rc=volume_step(p,job.step,&applied_volume,&applied_revision);
        else if(!rc&&(job.operation==DATA||job.operation==FINISH)){
            if(!p->attempted)rc=-EPIPE;
            else if(job.operation==DATA)rc=sony_receiver_session_push(p->session,p->work,job.frames);
            else rc=finish_output(p);
        }else if(!rc)rc=-EPROTO;
        observe_output(p);
        pthread_mutex_lock(&p->mutex);
        if(job.operation==DATA){if(!rc)p->state.pushed_frames+=job.frames;else p->state.failed_frames+=job.frames;p->state.inflight_frames=p->state.inflight_ns=0;}
        if(job.operation==FINISH&&!rc)++p->state.finished_streams;
        if(job.operation==VOLUME_STEP){p->state.volume_inflight=0;if(rc)++p->state.volume_failed;else{++p->state.volume_applied;p->state.last_volume=applied_volume;p->state.last_volume_revision=applied_revision;}}
        record(p,rc);if(p->state.first_error)p->state.stopping=1;p->state.worker_busy=0;pthread_mutex_unlock(&p->mutex);
    }
}
static int start(struct sony_receiver_async **out,unsigned activation,struct sony_receiver_session *session,struct sony_audio_owner *owner,enum sony_audio_owner_kind kind,unsigned controls){
    (void)activation;if(!out||*out||!session||!owner)return -EINVAL;
    int rc=0;uint64_t generation=sony_audio_owner_generation(owner);
    if(controls){int fd[2];rc=sony_receiver_session_control_descriptors(session,fd);if(rc)return rc;}
    struct sony_receiver_async *p=calloc(1,sizeof(*p));if(!p)return -ENOMEM;p->wake[0]=p->wake[1]=-1;p->pool_frames=kind==SONY_OWNER_BT_RX?BT_POOL_FRAMES:POOL_FRAMES;p->pool=malloc(p->pool_frames*8u);if(!p->pool){free(p);return -ENOMEM;}
    p->session=session;p->owner=owner;p->generation=generation;p->kind=kind;
    rc=sony_receiver_session_state(session,&p->state.output);if(rc){free(p->pool);free(p);return status(rc);}
    rc=pthread_mutex_init(&p->mutex,NULL);if(rc){free(p->pool);free(p);return -rc;}
    rc=pthread_cond_init(&p->ready,NULL);if(rc){pthread_mutex_destroy(&p->mutex);free(p->pool);free(p);return -rc;}
    p->controls=controls;if(controls&&pipe2(p->wake,O_NONBLOCK|O_CLOEXEC)){rc=-errno;pthread_cond_destroy(&p->ready);pthread_mutex_destroy(&p->mutex);free(p->pool);free(p);return rc;}
    rc=pthread_create(&p->worker,NULL,worker,p);if(rc){if(controls){close(p->wake[0]);close(p->wake[1]);}pthread_cond_destroy(&p->ready);pthread_mutex_destroy(&p->mutex);free(p->pool);free(p);return -rc;}
    *out=p;return 0;
}
int sony_receiver_async_start(struct sony_receiver_async **out,unsigned activation,struct sony_receiver_session *s,struct sony_audio_owner *o){return start(out,activation,s,o,SONY_OWNER_USB_DAC,0);}
int sony_receiver_async_start_bt(struct sony_receiver_async **out,unsigned activation,struct sony_receiver_session *s,struct sony_audio_owner *o){return start(out,activation,s,o,SONY_OWNER_BT_RX,0);}
int sony_receiver_async_start_bt_controls(struct sony_receiver_async **out,unsigned activation,struct sony_receiver_session *s,struct sony_audio_owner *o){return start(out,activation,s,o,SONY_OWNER_BT_RX,1);}
static int admission(struct sony_receiver_async *p){return p->state.first_error?p->state.first_error:p->state.stopping?-EPIPE:0;}
static void enqueue(struct sony_receiver_async *p,const struct job *job){p->jobs[(p->head+p->count)%JOBS]=*job;++p->count;p->state.queued_jobs=p->count;signal_work(p);}
int sony_receiver_async_open(struct sony_receiver_async *p,uint64_t stream,const struct sony_usb_stream *source,unsigned expected){
    int rc=producer(p);if(rc)return rc;if(!source||source->format!=SONY_USB_PCM)return -EINVAL;
    struct sony_usb_capture_plan plan;rc=sony_usb_stream_capture_plan(source,&plan);if(rc)return rc;rc=authority(p);if(rc)return rc;
    pthread_mutex_lock(&p->mutex);rc=admission(p);if(!rc&&p->producer_open)rc=-EBUSY;if(!rc&&p->count==JOBS)rc=-EAGAIN;
    if(!rc){struct job job={.stream=stream,.source=*source,.operation=OPEN,.expected=expected};enqueue(p,&job);p->last_stream=stream;p->state.producer_stream=stream;p->producer_rate=source->rate;p->producer_frame_bytes=8;p->producer_open=1;}
    pthread_mutex_unlock(&p->mutex);return rc;
}
int sony_receiver_async_open_bt(struct sony_receiver_async *p,uint64_t stream,const struct sony_bt_pcm_stream *source,unsigned expected){
    int rc=producer(p);if(rc)return rc;if(!source)return -EINVAL;
    uint32_t track[25];rc=sony_receiver_pcm_bt_track(source,track);if(rc)return rc;rc=authority(p);if(rc)return rc;
    pthread_mutex_lock(&p->mutex);rc=admission(p);if(!rc&&p->producer_open)rc=-EBUSY;if(!rc&&p->count==JOBS)rc=-EAGAIN;
    if(!rc){struct job job={.stream=stream,.bt_source=*source,.operation=OPEN_BT,.expected=expected};enqueue(p,&job);p->last_stream=stream;p->state.producer_stream=stream;p->producer_rate=source->rate;p->producer_frame_bytes=track[11]*2;p->producer_open=1;}
    pthread_mutex_unlock(&p->mutex);return rc;
}
static int room(struct sony_receiver_async *p,uint64_t stream,unsigned frames,uint64_t *ns){
    (void)stream;int rc=admission(p);if(!rc&&!p->producer_open)rc=-EPIPE;if(rc)return rc;
    *ns=p->producer_rate?((uint64_t)frames*1000000000u+p->producer_rate-1)/p->producer_rate:0;
    return p->count>=JOBS-2||frames>p->pool_frames-p->pool_used||*ns>SONY_RECEIVER_ASYNC_MAX_QUEUED_NS-p->state.queued_ns-p->state.inflight_ns?-EAGAIN:0;
}
int sony_receiver_async_accepts(struct sony_receiver_async *p,uint64_t stream,unsigned frames){
    int rc=producer(p);if(rc)return rc;if(!frames||frames>SONY_RECEIVER_ASYNC_MAX_FRAMES)return -EINVAL;rc=authority(p);if(rc)return rc;
    uint64_t ns;pthread_mutex_lock(&p->mutex);rc=room(p,stream,frames,&ns);pthread_mutex_unlock(&p->mutex);return rc;
}
int sony_receiver_async_push(struct sony_receiver_async *p,uint64_t stream,const void *data,unsigned frames){
    int rc=producer(p);if(rc)return rc;if(!data||!frames||frames>SONY_RECEIVER_ASYNC_MAX_FRAMES)return -EINVAL;rc=authority(p);if(rc)return rc;
    uint64_t ns;pthread_mutex_lock(&p->mutex);rc=room(p,stream,frames,&ns);if(!rc&&p->state.offered_frames>UINT64_MAX-frames)rc=-EOVERFLOW;
    if(!rc){copy_in(p,data,frames);struct job job={.stream=stream,.operation=DATA,.frames=frames,.ns=ns,.frame_bytes=p->producer_frame_bytes};enqueue(p,&job);p->state.offered_frames+=frames;p->state.queued_frames+=frames;p->state.queued_ns+=ns;}
    pthread_mutex_unlock(&p->mutex);return rc;
}
int sony_receiver_async_finish(struct sony_receiver_async *p,uint64_t stream){
    int rc=producer(p);if(rc)return rc;rc=authority(p);if(rc)return rc;pthread_mutex_lock(&p->mutex);rc=admission(p);
    if(!rc&&!p->producer_open)rc=-EPIPE;if(!rc&&p->count==JOBS)rc=-EAGAIN;
    if(!rc){struct job job={.stream=stream,.operation=FINISH};enqueue(p,&job);p->producer_open=0;}
    pthread_mutex_unlock(&p->mutex);return rc;
}
int sony_receiver_async_volume_step(struct sony_receiver_async *p,int step){
    int rc=producer(p);if(rc)return rc;rc=authority(p);if(rc)return rc;
    pthread_mutex_lock(&p->mutex);rc=admission(p);if(!rc&&p->count>=JOBS-2)rc=-EAGAIN;
    if(!rc&&p->state.volume_offered==UINT64_MAX)rc=-EOVERFLOW;
    if(!rc){struct job job={.operation=VOLUME_STEP,.step=step};enqueue(p,&job);++p->state.volume_offered;++p->state.volume_queued;}
    pthread_mutex_unlock(&p->mutex);return rc;
}
int sony_receiver_async_state(struct sony_receiver_async *p,struct sony_receiver_async_state *out){int rc=producer(p);if(rc)return rc;if(!out)return -EINVAL;pthread_mutex_lock(&p->mutex);*out=p->state;pthread_mutex_unlock(&p->mutex);return 0;}
int sony_receiver_async_request_stop(struct sony_receiver_async *p){int rc=producer(p);if(rc)return rc;pthread_mutex_lock(&p->mutex);p->state.stopping=1;p->producer_open=0;signal_work(p);pthread_mutex_unlock(&p->mutex);return 0;}
int sony_receiver_async_stop(struct sony_receiver_async **owner,struct sony_receiver_async_state *out){
    if(!owner||!*owner||!out)return -EINVAL;struct sony_receiver_async *p=*owner;int rc=producer(p);if(rc)return rc;
    sony_receiver_async_request_stop(p);pthread_mutex_lock(&p->mutex);*out=p->state;unsigned exited=p->state.exited;pthread_mutex_unlock(&p->mutex);if(!exited)return -EAGAIN;
    rc=pthread_join(p->worker,NULL);if(rc)return -rc;if(p->controls){close(p->wake[0]);close(p->wake[1]);}pthread_cond_destroy(&p->ready);pthread_mutex_destroy(&p->mutex);free(p->pool);free(p);*owner=NULL;return 0;
}
