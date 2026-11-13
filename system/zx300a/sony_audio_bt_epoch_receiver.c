#include "sony_audio_bt_epoch_receiver.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
struct sony_bt_epoch_receiver {
    int fd;pid_t pid;uid_t uid;unsigned expected,stop,pending,offset;
    struct sony_receiver_async *async;uint64_t finish_baseline;
    struct sony_bt_epochs cursor;
    struct sony_bt_live_volume_cursor volume;
    struct sony_bt_live_header header;
    unsigned char data[SONY_BT_LIVE_MAX_BYTES];
    struct sony_bt_epoch_receiver_state state;
};
static int thread(struct sony_bt_epoch_receiver *p){return p?0:-EINVAL;}
static int fail(struct sony_bt_epoch_receiver *p,int rc){if(rc>=0||rc< -4095)rc=-EPROTO;if(!p->state.first_error)p->state.first_error=rc;return p->state.first_error;}
static int backend(struct sony_bt_epoch_receiver *p){
    struct sony_receiver_async_state s;int rc=sony_receiver_async_state(p->async,&s);if(rc)return fail(p,rc);p->state.backend=s;
    if(s.first_error||s.finish_status||s.close_status||s.state_status)return fail(p,s.first_error?s.first_error:s.finish_status?s.finish_status:s.close_status?s.close_status:s.state_status);
    if(s.output.first_error||s.output.config_error)return fail(p,s.output.first_error?s.output.first_error:s.output.config_error);
    return 0;
}
int sony_bt_epoch_receiver_create(struct sony_bt_epoch_receiver **out,int fd,pid_t pid,uid_t uid,uint64_t cookie,unsigned expected,struct sony_receiver_async *async){
    if(!out||*out||fd<0||!async)return -EINVAL;
    struct sony_receiver_async_state s;int rc=sony_receiver_async_state(async,&s);if(rc)return rc;
    unsigned buffer;rc=sony_bt_epoch_transport_prepare(fd,&buffer);if(rc)return rc;
    struct sony_bt_epoch_receiver *p=calloc(1,sizeof(*p));if(!p)return -ENOMEM;
    p->fd=fd;p->pid=pid;p->uid=uid;p->expected=expected;p->async=async;p->state.backend=s;
    rc=sony_bt_epochs_init(&p->cursor,cookie);if(!rc)rc=sony_bt_live_volume_init(&p->volume,cookie);if(rc){free(p);return rc;}*out=p;return 0;
}
static int control(struct sony_bt_epoch_receiver *p,unsigned kind){
    struct sony_bt_epochs next;struct sony_bt_live_header h;int rc;
    if(kind==SONY_BT_EPOCH_STOP)rc=sony_bt_epochs_stop(&p->cursor,&next,&h);
    else if(kind==SONY_BT_EPOCH_READY)rc=sony_bt_epochs_ready(&p->cursor,p->cursor.epoch,&next,&h);
    else if(kind==SONY_BT_EPOCH_FLUSH_READY)rc=sony_bt_epochs_flush_ready(&p->cursor,p->cursor.epoch,&next,&h);
    else rc=sony_bt_epochs_closed(&p->cursor,p->cursor.epoch,&next,&h);
    if(rc)return fail(p,rc);rc=sony_bt_epoch_transport_send(p->fd,&h,NULL);if(rc==-EAGAIN)return 0;if(rc)return fail(p,rc);p->cursor=next;if(kind==SONY_BT_EPOCH_READY)++p->state.ready_count;return 0;
}
static int closed(struct sony_bt_epoch_receiver *p){
    const struct sony_bt_epochs *c=&p->cursor;const struct sony_receiver_async_state *a=&p->state.backend;
    if(a->finished_streams<=p->finish_baseline)return 0;
    const struct sony_receiver_playback_state *v=&a->output.playback;
    if(p->state.completed_epochs!=c->epoch){
        p->state.source_acked_frames+=v->source_acked_frames;p->state.native_acked_bytes+=v->native_acked_bytes;p->state.sink_acked_bytes+=v->sink_acked_bytes;p->state.completed_epochs=c->epoch;
        p->state.output_acked_frames+=v->sink_acked_bytes/8u;
    }
    return control(p,SONY_BT_EPOCH_CLOSED);
}
static int admit(struct sony_bt_epoch_receiver *p){
    const struct sony_bt_live_header *h=&p->header;int rc;
    if(h->kind==SONY_BT_LIVE_VOLUME_STEP&&h->bytes>=sizeof(int32_t)){
        struct sony_bt_live_volume_cursor next;int step;rc=sony_bt_live_volume_next(&p->volume,h,p->data,&next,&step);if(rc)return fail(p,rc);
        rc=sony_receiver_async_volume_step(p->async,step);if(rc==-EAGAIN)return 0;if(rc)return fail(p,rc);p->volume=next;++p->state.volume_events;p->pending=0;return 0;
    }
    struct sony_bt_epochs next;rc=sony_bt_epochs_next(&p->cursor,h,h->bytes?p->data:NULL,h->bytes,&next);if(rc)return fail(p,rc);
    if(h->kind==SONY_BT_EPOCH_FORMAT){p->finish_baseline=p->state.backend.finished_streams;rc=sony_receiver_async_open_bt(p->async,next.epoch,&next.format.pcm,p->expected);}
    else if(h->kind==SONY_BT_EPOCH_PCM){
        unsigned frames=h->bytes/p->cursor.frame_bytes;
        while(p->offset<frames){unsigned chunk=frames-p->offset;if(chunk>SONY_RECEIVER_ASYNC_MAX_FRAMES)chunk=SONY_RECEIVER_ASYNC_MAX_FRAMES;
            rc=sony_receiver_async_push(p->async,p->cursor.epoch,p->data+p->offset*p->cursor.frame_bytes,chunk);if(rc==-EAGAIN)return 0;if(rc)return fail(p,rc);p->offset+=chunk;}
        rc=0;
    }else if(h->kind==SONY_BT_EPOCH_END||(h->kind==SONY_BT_EPOCH_QUIET&&next.phase==SONY_BT_EPOCH_FINISHING))rc=sony_receiver_async_finish(p->async,next.epoch);
    else rc=0;
    if(rc==-EAGAIN)return 0;if(rc)return fail(p,rc);p->cursor=next;if(h->kind==SONY_BT_EPOCH_PCM)++p->state.packets;p->pending=p->offset=0;return 0;
}
int sony_bt_epoch_receiver_poll(struct sony_bt_epoch_receiver *p){
    int rc=thread(p);if(rc)return rc;if(p->state.first_error)return p->state.first_error;
    if((rc=backend(p)))return rc;
    /* Control sends are whole records. No input cursor may advance while a
     * proposed control is unsent. Reconstruct from the unchanged cursor on
     * EAGAIN, so an explicit STOP can supersede an unsent READY safely. */
    if(p->stop&&!p->cursor.stop_requested){rc=control(p,SONY_BT_EPOCH_STOP);if(rc)return rc;if(!p->cursor.stop_requested)return 0;}
    if(p->cursor.phase==SONY_BT_EPOCH_OPENING){
        const struct sony_receiver_async_state *a=&p->state.backend;
        if(a->output.stream_open){
            rc=control(p,p->cursor.stop_requested?SONY_BT_EPOCH_FLUSH_READY:SONY_BT_EPOCH_READY);if(rc)return rc;if(p->cursor.phase==SONY_BT_EPOCH_OPENING)return 0;
        }
    }
    if(p->cursor.phase==SONY_BT_EPOCH_FINISHING){rc=closed(p);if(rc)return rc;if(p->cursor.phase==SONY_BT_EPOCH_FINISHING&&p->state.backend.finished_streams>=p->cursor.epoch)return 0;}
    if(p->cursor.phase==SONY_BT_EPOCH_QUIESCED){
        const struct sony_receiver_async_state *a=&p->state.backend;
        if(a->queued_jobs||a->worker_busy||a->volume_queued||a->volume_inflight)return 0;
        return 1;
    }
    if(!p->pending){rc=sony_bt_epoch_transport_receive(p->fd,p->pid,p->uid,p->cursor.cookie,&p->header,p->data,sizeof(p->data));if(rc==-EAGAIN)return 0;if(rc)return fail(p,rc);p->pending=1;p->offset=0;}
    return admit(p);
}
int sony_bt_epoch_receiver_request_stop(struct sony_bt_epoch_receiver *p){int rc=thread(p);if(rc)return rc;p->stop=1;return p->state.first_error;}
int sony_bt_epoch_receiver_stop_source(struct sony_bt_epoch_receiver *p){
    int rc=thread(p);if(rc)return rc;p->stop=1;if(p->cursor.stop_requested||p->cursor.phase==SONY_BT_EPOCH_QUIESCED)return 0;
    struct sony_bt_epochs next;struct sony_bt_live_header h;rc=sony_bt_epochs_stop(&p->cursor,&next,&h);if(rc)return rc;rc=sony_bt_epoch_transport_send(p->fd,&h,NULL);if(!rc)p->cursor=next;return rc;
}
int sony_bt_epoch_receiver_state(struct sony_bt_epoch_receiver *p,struct sony_bt_epoch_receiver_state *out){int rc=thread(p);if(rc||!out)return rc?rc:-EINVAL;p->state.cursor=p->cursor;p->state.pending_kind=p->pending?p->header.kind:0;p->state.pending_bytes=p->pending?p->header.bytes:0;p->state.partial_frames=p->offset;p->state.stop_requested=p->stop;*out=p->state;return 0;}
int sony_bt_epoch_receiver_destroy(struct sony_bt_epoch_receiver **out,unsigned quiet){(void)quiet;if(!out||!*out)return -EINVAL;struct sony_bt_epoch_receiver *p=*out;int rc=thread(p);if(rc)return rc;free(p);*out=NULL;return 0;}
