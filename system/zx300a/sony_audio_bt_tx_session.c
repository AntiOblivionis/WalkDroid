#include "sony_audio_bt_tx_session.h"
#include <errno.h>
#include <string.h>

static int close_once(struct wbt_session *s) {
    if (!s->begin_attempted) { s->cleanup_verified=1; return 0; }
    if (s->close_attempted) return s->close_status;
    s->close_attempted=1;
    int rc=s->ops->close(s->context);
    s->close_status=rc;s->cleanup_verified=!rc;
    return rc;
}
int wbt_init(struct wbt_session *s,uint64_t nonce,const struct wbt_ops *ops,void *context) {
    if (!s||!ops||!ops->begin||!ops->submit||!ops->close) return -EINVAL;
    memset(s,0,sizeof(*s)); s->nonce=nonce; s->next_request=1;
    s->ops=ops; s->context=context; return 0;
}
int wbt_abort(struct wbt_session *s) {
    if (!s||!s->ops) return -EINVAL;
    s->queued=0;
    int rc=close_once(s);
    if (s->phase!=WBT_CLOSED) s->phase=WBT_FAILED;
    return rc;
}
static int submit(struct wbt_session *s,unsigned bytes) {
    int rc=s->ops->submit(s->context,s->pcm,bytes);
    if (rc) { s->uncertain=bytes/4; return rc<0?rc:-EIO; }
    s->submitted+=bytes/4; s->queued=0; return 0;
}
int wbt_handle(struct wbt_session *s,const struct wbt_request *q,const void *pcm,struct wbt_reply *r) {
    if (!s||!q||!r||!s->ops) return -EINVAL;
    int rc=0;
    if (s->phase==WBT_CLOSED||s->phase==WBT_FAILED) rc=-ESHUTDOWN;
    else if (q->command==WBT_OPEN) {
        if (s->phase!=WBT_NEW) rc=-EINVAL;
        else {
            s->begin_attempted=1;
            rc=s->ops->begin(s->context,q,&s->preferred);
            if (rc>0) rc=-EIO;
            if (!rc){if(s->preferred>WBT_MAX_PCM)s->preferred=WBT_MAX_PCM;s->preferred&=~3u;if(!s->preferred)s->preferred=4;}
            if (!rc) s->phase=WBT_STREAMING;
        }
    } else if (q->command==WBT_WRITE) {
        if (s->phase!=WBT_STREAMING||(!pcm&&q->payload_bytes)) rc=-EINVAL;
        else if (UINT64_MAX-s->accepted<((uint64_t)s->accepted_tail+q->payload_bytes)/4) rc=-EOVERFLOW;
        else {
            const uint8_t *p=pcm; unsigned remaining=q->payload_bytes;
            while (remaining&&!rc) {
                unsigned take=s->preferred-s->queued; if(take>remaining)take=remaining;
                memcpy(s->pcm+s->queued,p,take); s->queued+=take;
                unsigned counted=s->accepted_tail+take;s->accepted+=counted/4;s->accepted_tail=counted%4;
                p+=take; remaining-=take;
                if (s->queued==s->preferred) rc=submit(s,s->queued);
            }
        }
    } else if (q->command==WBT_FINISH||q->command==WBT_CANCEL) {
        if (s->phase!=WBT_STREAMING) rc=-EINVAL;
        else {
            /* Forward the remaining bytes; the actual downstream sender may
             * report an error or ignore an incomplete sample frame. */
            if (q->command==WBT_FINISH&&s->queued) {
                rc=submit(s,s->queued);
            }
            if(q->command==WBT_FINISH&&!rc&&s->ops->finish)rc=s->ops->finish(s->context);
            int cleanup=close_once(s); s->queued=0;
            if (!rc) rc=cleanup;
            if (!rc) s->phase=WBT_CLOSED;
        }
    } else rc=-ENOSYS;
    if (rc) { s->phase=WBT_FAILED; wbt_abort(s); }
    else ++s->next_request;
    memset(r,0,sizeof(*r));
    r->magic=WBT_MAGIC; r->version=WBT_VERSION; r->command=q->command; r->request=q->request;
    r->session=s->nonce; r->accepted_frames=s->accepted; r->submitted_frames=s->submitted;
    r->status=rc; r->phase=s->phase; r->queued_bytes=s->queued;
    r->flags=WBT_NATIVE_SUBMISSION_ONLY|(s->cleanup_verified?WBT_CLEANUP_VERIFIED:0);
    r->uncertain_frames=s->uncertain;
    if(s->ops->stats&&!s->ops->stats(s->context,&r->processing))r->flags|=WBT_PROCESSING_STATS;
    return rc;
}
int wbt_consume_payload(struct wbt_session *s,struct wbt_request *q,struct wbt_reply *reply,
                        wbt_read_payload read_payload,void *context,int *operation) {
    uint8_t pcm[WBT_MAX_PCM];unsigned remaining=q->payload_bytes;int writing=q->command==WBT_WRITE;
    *operation=0;
    if(!remaining){q->payload_bytes=0;*operation=wbt_handle(s,q,NULL,reply);return 0;}
    while(remaining&&!*operation){
        unsigned take=remaining<sizeof(pcm)?remaining:sizeof(pcm);
        int rc=read_payload(context,pcm,take);if(rc)return rc;
        remaining-=take;
        if(writing){q->payload_bytes=take;*operation=wbt_handle(s,q,pcm,reply);}
    }
    if(!writing){q->payload_bytes=0;*operation=wbt_handle(s,q,NULL,reply);}
    return 0;
}
