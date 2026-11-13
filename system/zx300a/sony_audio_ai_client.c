#define _GNU_SOURCE
#include "sony_audio_ai_client.h"
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
struct sony_ai_client {
    int fd,timeout,failed,busy;
    uint64_t nonce;uint32_t request,finish_ms;
    struct wai_state state;
    struct wai_packet packet;
};
static int fail(struct sony_ai_client *c,int error){
    c->failed=1;shutdown(c->fd,SHUT_RDWR);return error?error:-EIO;
}
static int allowed(struct sony_ai_client *c){
    if(!c)return -EINVAL;
    if(c->busy)return -EBUSY;if(c->failed)return -EIO;return 0;
}
static struct wai_header request(struct sony_ai_client *c,uint32_t command,uint32_t bytes,uint32_t frames,uint32_t rate){
    struct wai_header h={WAI_MAGIC,WAI_VERSION,command,++c->request,(uint32_t)c->nonce,
        (uint32_t)(c->nonce>>32),bytes,0,frames,rate,0,0};return h;
}
static int start(struct sony_ai_client *c,uint32_t command,const void *payload,uint32_t bytes,
                 uint32_t frames,uint32_t rate,struct wai_header *h,int64_t *end){
    int64_t now=wai_now_ms();if(now<0)return fail(c,-EIO);*end=now+c->timeout;
    *h=request(c,command,bytes,frames,rate);int r=wai_send(c->fd,h,payload,*end);
    return r?fail(c,r):0;
}
static int receive(struct sony_ai_client *c,const struct wai_header *request,int64_t end){
    (void)request;
    int r=wai_receive(c->fd,&c->packet,end);if(r)return fail(c,r);
    const struct wai_header *h=&c->packet.h;
    if(h->status)return fail(c,h->status<0?h->status:-EIO);
    return 0;
}
static void put_counter(uint32_t *lo,uint32_t *hi,uint64_t value){*lo=(uint32_t)value;*hi=(uint32_t)(value>>32);}
static int exchange_state(struct sony_ai_client *c,uint32_t command,const void *payload,uint32_t bytes,
                          uint32_t source_rate,uint32_t phase){
    (void)phase;
    struct wai_header h;int64_t end;int r=start(c,command,payload,bytes,0,source_rate,&h,&end);
    if(!r)r=receive(c,&h,end);if(r)return r;
    const struct wai_header *reply=&c->packet.h;struct wai_state s;
    if(reply->bytes<sizeof(s))return fail(c,-EPROTO);
    memcpy(&s,c->packet.payload,sizeof(s));
    c->state=s;return 0;
}
int sony_ai_client_create(struct sony_ai_client **out,int fd,uint64_t nonce,int timeout){
    if(!out)return -EINVAL;if(*out)return -EBUSY;
    int flags=fcntl(fd,F_GETFD);if(flags<0||fcntl(fd,F_SETFD,flags|FD_CLOEXEC))return -errno;
    struct sony_ai_client *c=calloc(1,sizeof(*c));if(!c)return -ENOMEM;
    c->fd=fd;c->nonce=nonce;c->timeout=timeout;*out=c;return 0;
}
int sony_ai_client_open(struct sony_ai_client *c,unsigned rate,int enabled){
    int r=allowed(c);if(r)return r;
    if(c->state.phase)return -EBUSY;
    c->state.input_rate=rate;c->state.enabled=(uint32_t)enabled;uint32_t request=(uint32_t)enabled;
    c->busy=1;r=exchange_state(c,WAI_OPEN,&request,4,rate,WAI_OPENED);c->busy=0;return r;
}
int sony_ai_client_set(struct sony_ai_client *c,int enabled){
    int r=allowed(c);if(r)return r;
    if(c->state.phase!=WAI_OPENED)return -EBUSY;
    c->state.enabled=(uint32_t)enabled;uint32_t request=(uint32_t)enabled;
    c->busy=1;r=exchange_state(c,WAI_SET,&request,4,0,WAI_OPENED);c->busy=0;return r;
}
int sony_ai_client_state(struct sony_ai_client *c,struct wai_state *out){
    int r=allowed(c);if(r)return r;if(!out)return -EINVAL;if(!c->state.phase)return -EPIPE;
    c->busy=1;r=exchange_state(c,WAI_STATE,NULL,0,0,c->state.phase);c->busy=0;if(!r)*out=c->state;return r;
}
int sony_ai_client_process(struct sony_ai_client *c,const float *input,unsigned frames,
                           float *output,unsigned capacity,unsigned *produced,unsigned *rate){
    if(produced)*produced=0;if(rate)*rate=0;int r=allowed(c);if(r)return r;
    if(!input||!output||!produced||!rate)return -EINVAL;
    if(c->state.phase!=WAI_OPENED&&c->state.phase!=WAI_STREAMING)return -EPIPE;
    if(frames!=c->state.quantum)return -EINVAL;
    if(frames>WAI_MAX_BYTES/8)return -EMSGSIZE;
    unsigned ratio=c->state.expected?4u:1u;if(capacity<frames*ratio)return -EMSGSIZE;
    c->busy=1;struct wai_header h;int64_t end;r=start(c,WAI_PROCESS,input,frames*8,frames,c->state.input_rate,&h,&end);
    if(!r)r=receive(c,&h,end);
    if(!r){
        const struct wai_header *reply=&c->packet.h;
        if(reply->frames>capacity||reply->frames>reply->bytes/8)r=fail(c,-EPROTO);
        if(!r){
            memcpy(output,c->packet.payload,reply->frames*8);*produced=reply->frames;*rate=reply->rate;
            uint64_t source=wai_join(c->state.source_lo,c->state.source_hi)+frames;
            uint64_t count=wai_join(c->state.produced_lo,c->state.produced_hi)+reply->frames;
            put_counter(&c->state.source_lo,&c->state.source_hi,source);
            put_counter(&c->state.produced_lo,&c->state.produced_hi,count);
            c->state.phase=WAI_STREAMING;if(c->state.expected&&reply->frames)c->state.observed=1;
        }
    }
    c->busy=0;return r;
}
int sony_ai_client_finish(struct sony_ai_client *c,unsigned ms,sony_ai_client_tail_sink sink,void *context){
    int r=allowed(c);if(r)return r;
    if(!sink)return -EINVAL;
    if(!c->state.phase)return -EPIPE;
    int ended=c->state.phase==WAI_ENDED;
    c->busy=1;struct wai_header request_header;int64_t end;
    r=start(c,WAI_FINISH,&ms,4,0,0,&request_header,&end);uint32_t index=0;
    while(!r){
        r=receive(c,&request_header,end);if(r)break;
        const struct wai_header *h=&c->packet.h;
        if(h->flags&WAI_MORE){
            if(h->frames>h->bytes/8){r=fail(c,-EPROTO);break;}
            const float *samples=(const float *)c->packet.payload;
            if(sink(context,samples,h->frames,h->rate)){r=fail(c,-EIO);break;}
            ++index;
            struct wai_header ack={WAI_MAGIC,WAI_VERSION,WAI_TAIL_ACK,request_header.request,
                (uint32_t)c->nonce,(uint32_t)(c->nonce>>32),0,0,h->frames,h->rate,0,index};
            r=wai_send(c->fd,&ack,NULL,end);if(r){r=fail(c,r);break;}
        }else{
            struct wai_state snapshot;
            if(h->bytes<sizeof(snapshot)){r=fail(c,-EPROTO);break;}
            memcpy(&snapshot,c->packet.payload,sizeof(snapshot));
            c->state=snapshot;c->finish_ms=ms;break;
        }
    }
    c->busy=0;return r?r:ended?1:0;
}
int sony_ai_client_close(struct sony_ai_client **owner,int discard){
    if(!owner||(discard!=0&&discard!=1))return -EINVAL;
    struct sony_ai_client *c=*owner;if(!c)return 0;
    if(c->busy)return -EBUSY;
    if(!discard&&!c->failed&&c->state.phase==WAI_STREAMING)return -EBUSY;
    int result=0;
    if(!c->failed){
        struct wai_header h;int64_t end;c->busy=1;
        result=start(c,discard||!c->state.phase?WAI_ABORT:WAI_CLOSE,NULL,0,0,0,&h,&end);
        if(!result)result=receive(c,&h,end);
    }else result=-EIO;
    shutdown(c->fd,SHUT_RDWR);close(c->fd);free(c);*owner=NULL;return result;
}
