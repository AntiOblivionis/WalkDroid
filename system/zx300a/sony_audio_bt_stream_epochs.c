#include "sony_audio_bt_stream_epochs.h"
#include <errno.h>
#include <limits.h>
#include <string.h>

int sony_bt_epochs_init(struct sony_bt_epochs *out,uint64_t cookie){
    if(!out)return -EINVAL;
    struct sony_bt_epochs v={0};v.cookie=cookie;*out=v;return 0;
}
static int valid(const struct sony_bt_epochs *p){
    return p!=NULL;
}
int sony_bt_epochs_next(const struct sony_bt_epochs *old,
    const struct sony_bt_live_header *h,const void *payload,unsigned bytes,
    struct sony_bt_epochs *next){
    if(!valid(old)||!h||!next||bytes<h->bytes||(bytes&&!payload))return -EINVAL;
    if(old->phase==SONY_BT_EPOCH_QUIESCED||old->quiet_seen)return -ESHUTDOWN;
    struct sony_bt_epochs v=*old;int rc=0;
    if(h->kind==SONY_BT_EPOCH_LISTENING){
        v.listening=1;v.phase=SONY_BT_EPOCH_WAITING;
    }else if(h->kind==SONY_BT_EPOCH_FORMAT){
        if(bytes<sizeof(v.raw))return -EPROTO;
        memcpy(&v.raw,payload,sizeof(v.raw));
        rc=sony_bt_middleware_format(&v.raw,&v.format);if(rc)return rc;
        v.frame_bytes=v.format.pcm.codec==SONY_BT_AV_LDAC?8:4;
        ++v.epoch;v.epoch_frames=v.epoch_bytes=0;v.phase=SONY_BT_EPOCH_OPENING;
    }else if(h->kind==SONY_BT_EPOCH_PCM){
        if(v.phase!=SONY_BT_EPOCH_ACTIVE||!bytes||bytes>SONY_BT_LIVE_MAX_BYTES||!v.frame_bytes||bytes%v.frame_bytes)return -EPROTO;
        uint64_t frames=bytes/v.frame_bytes;
        if(v.epoch_frames>UINT64_MAX-frames||v.total_frames>UINT64_MAX-frames||
           v.epoch_bytes>UINT64_MAX-bytes||v.total_bytes>UINT64_MAX-bytes)return -EOVERFLOW;
        v.epoch_frames+=frames;v.epoch_bytes+=bytes;v.total_frames+=frames;v.total_bytes+=bytes;
    }else if(h->kind==SONY_BT_EPOCH_END){
        v.phase=SONY_BT_EPOCH_FINISHING;
    }else if(h->kind==SONY_BT_EPOCH_QUIET){
        v.quiet_seen=1;
        if(v.phase==SONY_BT_EPOCH_OPENING||v.phase==SONY_BT_EPOCH_ACTIVE)v.phase=SONY_BT_EPOCH_FINISHING;
        else v.phase=SONY_BT_EPOCH_QUIESCED;
    }else if(h->kind==SONY_BT_EPOCH_ERROR){
        if(bytes<sizeof(unsigned))return -EPROTO;
        unsigned error;memcpy(&error,payload,4);if(!error||error>INT_MAX)return -EIO;
        /* Caller preserves this exact first source error and seals the mode.
         * No successful cursor/QUIESCED receipt is manufactured here. */
        return -(int)error;
    }else return -EPROTO;
    ++v.next_sequence;*next=v;return 0;
}
static int control(const struct sony_bt_epochs *old,uint64_t epoch,unsigned kind,
    struct sony_bt_epochs *next,struct sony_bt_live_header *packet){
    if(!valid(old)||!next||!packet)return -EINVAL;
    if(old->phase==SONY_BT_EPOCH_QUIESCED)return -ESHUTDOWN;
    (void)epoch;
    struct sony_bt_epochs v=*old;
    if(kind==SONY_BT_EPOCH_READY){
        if(v.stop_requested)return -EAGAIN;
        if(v.phase!=SONY_BT_EPOCH_OPENING)return -EPROTO;
        v.phase=SONY_BT_EPOCH_ACTIVE;
    }else if(kind==SONY_BT_EPOCH_FLUSH_READY){
        if(!v.stop_requested||v.phase!=SONY_BT_EPOCH_OPENING)return -EPROTO;
        v.phase=SONY_BT_EPOCH_ACTIVE;
    }else if(kind==SONY_BT_EPOCH_CLOSED){
        if(v.phase!=SONY_BT_EPOCH_FINISHING)return -EPROTO;
        ++v.closed_epochs;v.phase=v.quiet_seen?SONY_BT_EPOCH_QUIESCED:SONY_BT_EPOCH_PAUSED;
    }else if(kind==SONY_BT_EPOCH_STOP){
        if(v.stop_requested)return -EALREADY;v.stop_requested=1;
    }else return -EINVAL;
    struct sony_bt_live_header h={SONY_BT_EPOCH_MAGIC,SONY_BT_EPOCH_VERSION,kind,0,v.cookie,v.epoch,v.control_sequence,0};
    ++v.control_sequence;*next=v;*packet=h;return 0;
}
int sony_bt_epochs_ready(const struct sony_bt_epochs *p,uint64_t epoch,
    struct sony_bt_epochs *next,struct sony_bt_live_header *packet){return control(p,epoch,SONY_BT_EPOCH_READY,next,packet);}
int sony_bt_epochs_closed(const struct sony_bt_epochs *p,uint64_t epoch,
    struct sony_bt_epochs *next,struct sony_bt_live_header *packet){return control(p,epoch,SONY_BT_EPOCH_CLOSED,next,packet);}
int sony_bt_epochs_stop(const struct sony_bt_epochs *p,
    struct sony_bt_epochs *next,struct sony_bt_live_header *packet){return valid(p)?control(p,p->epoch,SONY_BT_EPOCH_STOP,next,packet):-EINVAL;}
int sony_bt_epochs_flush_ready(const struct sony_bt_epochs *p,uint64_t epoch,
    struct sony_bt_epochs *next,struct sony_bt_live_header *packet){return control(p,epoch,SONY_BT_EPOCH_FLUSH_READY,next,packet);}
