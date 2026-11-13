#include "sony_audio_bt_tx_link.h"
#include <errno.h>
#include <string.h>

static int normalized(int rc) { return rc>0?-EIO:rc; }
static int close_link(struct wbl_session *s) {
    if(s->close_attempted)return s->close_status;
    s->close_attempted=1;
    int rc=s->connect_attempted?normalized(s->ops->close_link(s->context)):0;
    s->close_status=rc;s->native_closed=!rc;return rc;
}
int wbl_init(struct wbl_session *s,uint64_t nonce,const struct wbl_ops *ops,void *ctx) {
    if(!s||!ops||!ops->connect||!ops->close_link||
       !ops->epoch.begin||!ops->epoch.submit||!ops->epoch.close)return -EINVAL;
    memset(s,0,sizeof(*s));s->nonce=nonce;s->next_request=1;s->ops=ops;s->context=ctx;return 0;
}
int wbl_abort(struct wbl_session *s) {
    if(!s||!s->ops)return -EINVAL;
    if(s->epoch_active) {
        s->epoch_stop_status=wbt_abort(&s->pcm);s->epoch_active=0;
    }
    /* Cancel never promises a drained/connected epoch. Full native shutdown
     * is authoritative even when peer loss or SIGTERM made epoch-stop fail.
     * Keep that operation diagnostic separate; never poison a proven OFF
     * controller solely because a cancelled epoch cannot remain connected. */
    int closed=close_link(s);
    if(s->phase!=WBL_CLOSED)s->phase=WBL_FAILED;
    return closed;
}
int wbl_check_live(struct wbl_session *s) {
    if(!s||!s->ops)return -EINVAL;
    return 0;
}
static int epoch_request(struct wbl_session *s,const struct wbl_request *q,const void *p,unsigned cmd) {
    struct wbt_request inner={.magic=WBT_MAGIC,.version=WBT_VERSION,.command=cmd,
        .request=s->pcm.next_request,.session=s->nonce,.frame_position=s->pcm.accepted,
        .payload_bytes=q->payload_bytes};
    if(cmd==WBT_OPEN) {
        inner.rate=q->rate;inner.channels=q->channels;inner.bits=q->bits;
        memcpy(inner.peer,s->peer,6);
    }
    struct wbt_reply r;return wbt_handle(&s->pcm,&inner,p,&r);
}
int wbl_handle(struct wbl_session *s,const struct wbl_request *q,const void *p,struct wbl_reply *r) {
    if(!s||!q||!r||!s->ops)return -EINVAL;
    int rc=0;
    if(s->phase==WBL_CLOSED||s->phase==WBL_FAILED)rc=-ESHUTDOWN;
    else if(q->command==WBL_RESUME) {
        if(s->phase!=WBL_CONNECTED)rc=-EINVAL;
        else if(!(rc=wbl_check_live(s))) {
            ++s->epoch;
            rc=wbt_init(&s->pcm,s->nonce,&s->ops->epoch,s->context);
            if(!rc){s->epoch_active=1;rc=epoch_request(s,q,NULL,WBT_OPEN);}
            if(!rc)s->phase=WBL_STREAMING;
        }
    } else if(q->command==WBL_CONNECT) {
        if(s->phase!=WBL_NEW)rc=-EINVAL;
        else {
            memcpy(s->peer,q->peer,6);s->connect_attempted=1;
            rc=normalized(s->ops->connect(s->context,s->peer));
            if(!rc){s->phase=WBL_CONNECTED;rc=wbl_check_live(s);}
        }
    } else if(q->command==WBL_CANCEL) {
        rc=wbl_abort(s);if(!rc)s->phase=WBL_CLOSED;
    } else if(q->command==WBL_CLOSE) {
        if(s->phase!=WBL_CONNECTED)rc=-EINVAL;
        else { rc=close_link(s);if(!rc)s->phase=WBL_CLOSED; }
    } else if(q->command==WBL_WRITE||q->command==WBL_PAUSE||q->command==WBL_DROP) {
        if(s->phase!=WBL_STREAMING)rc=-EINVAL;
        else if(!(rc=wbl_check_live(s))) {
            rc=epoch_request(s,q,p,q->command==WBL_WRITE?WBT_WRITE:q->command==WBL_DROP?WBT_CANCEL:WBT_FINISH);
            if(!rc&&(q->command==WBL_PAUSE||q->command==WBL_DROP)) {
                s->epoch_active=0;s->phase=WBL_CONNECTED;rc=wbl_check_live(s);
            }
        }
    } else rc=-ENOSYS;
    if(rc){s->phase=WBL_FAILED;wbl_abort(s);}else ++s->next_request;
    memset(r,0,sizeof(*r));r->magic=WBL_MAGIC;r->version=WBL_VERSION;
    r->command=q->command;r->request=q->request;r->session=s->nonce;r->epoch=s->epoch;
    r->accepted_frames=s->pcm.accepted;r->submitted_frames=s->pcm.submitted;
    r->queued_bytes=s->pcm.queued;r->uncertain_frames=s->pcm.uncertain;
    r->status=rc;r->phase=s->phase;r->flags=WBL_NATIVE_SUBMISSION_ONLY;
    if(s->phase==WBL_CONNECTED||s->phase==WBL_STREAMING)r->flags|=WBL_LINK_CONNECTED;
    if(s->native_closed)r->flags|=WBL_NATIVE_CLOSED;
    if(s->epoch&&s->ops->epoch.stats&&!s->ops->epoch.stats(s->context,&r->processing))r->flags|=WBL_PROCESSING_STATS;
    return rc;
}
int wbl_consume_payload(struct wbl_session *s,struct wbl_request *q,struct wbl_reply *reply,
                        wbt_read_payload read_payload,void *context,int *operation) {
    uint8_t pcm[WBT_MAX_PCM];unsigned remaining=q->payload_bytes;int writing=q->command==WBL_WRITE;
    *operation=0;
    if(!remaining){q->payload_bytes=0;*operation=wbl_handle(s,q,NULL,reply);return 0;}
    while(remaining&&!*operation){
        unsigned take=remaining<sizeof(pcm)?remaining:sizeof(pcm);
        int rc=read_payload(context,pcm,take);if(rc)return rc;
        remaining-=take;
        if(writing){q->payload_bytes=take;*operation=wbl_handle(s,q,pcm,reply);}
    }
    if(!writing){q->payload_bytes=0;*operation=wbl_handle(s,q,NULL,reply);}
    return 0;
}
