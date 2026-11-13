#include "sony_audio_dsd_render_bridge.h"
#include "sony_audio_dsd_fade_bridge.h"
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
struct sony_dsd_render_bridge {
    struct sony_dsd_fade_bridge *native;
    unsigned preroll_remaining;
    int first,pause_requested,faded_out,paused,faulted,busy,preroll_completed;
    unsigned char first_output[32768],final_output[32768],silence[65536];
};
static int overlap(const void *a,unsigned na,const void *b,unsigned nb) {
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    if(na>UINTPTR_MAX-x||nb>UINTPTR_MAX-y)return 1;
    return na&&nb&&x<y+nb&&y<x+na;
}
static int ready(struct sony_dsd_render_bridge *b) {
    if(!b)return -EINVAL;
    if(b->busy)return -EBUSY;
    if(b->faulted)return -EPIPE;
    return 0;
}
int sony_dsd_render_open(struct sony_dsd_render_bridge **out,void *library,unsigned rate) {
    if(!out||!library)return -EINVAL;
    struct sony_dsd_render_bridge *b=calloc(1,sizeof(*b));if(!b)return -ENOMEM;
    int rc=sony_dsd_fade_open(&b->native,library,rate);
    if(rc){free(b);return rc;}
    /* 200 ms of interleaved stereo DSD bytes, rounded down to a whole pair. */
    b->first=1;b->preroll_remaining=(rate/20u)&~1u;
    memset(b->silence,0x69,sizeof(b->silence));*out=b;return 0;
}
int sony_dsd_render_preroll(struct sony_dsd_render_bridge *b,sony_dsd_render_sink sink,void *context,unsigned *ack) {
    int rc=ready(b);if(rc)return rc;
    if(!sink||!ack||overlap(ack,sizeof(*ack),b,sizeof(*b)))return -EINVAL;
    if(b->paused)return -EPIPE;
    if(b->preroll_completed)return 1;
    *ack=0;b->busy=1;
    while(b->preroll_remaining) {
        unsigned n=b->preroll_remaining>sizeof(b->silence)?sizeof(b->silence):b->preroll_remaining;
        if(sink(context,b->silence,n)){b->faulted=1;rc=-EIO;break;}
        b->preroll_remaining-=n;*ack+=n;
    }
    if(!rc)b->preroll_completed=1;
    b->busy=0;return rc;
}
int sony_dsd_render_process(struct sony_dsd_render_bridge *b,const unsigned char *input,unsigned bytes,
                           unsigned char *output,unsigned capacity,unsigned *written) {
    int rc=ready(b);unsigned n=0;if(rc)return rc;
    if(!input||!output||!written||!bytes||bytes>32768||(bytes&1))return -EINVAL;
    if(b->paused||b->preroll_remaining)return -EPIPE;
    if(capacity<bytes)return -ENOSPC;
    if(overlap(input,bytes,output,bytes)||overlap(input,bytes,written,sizeof(*written))||
       overlap(output,bytes,written,sizeof(*written))||overlap(input,bytes,b,sizeof(*b))||
       overlap(output,bytes,b,sizeof(*b))||overlap(written,sizeof(*written),b,sizeof(*b)))return -EINVAL;
    b->busy=1;
    if(b->first) {
        memset(b->first_output,0x69,sizeof(b->first_output));
        rc=sony_dsd_fade_process(b->native,input,bytes,SONY_DSD_FADE_START,b->first_output,sizeof(b->first_output),&n);
        if(rc)goto fail;
        /* Native renderer copies the first min(input,8192) bytes of its left-
         * padded fade window back; it never expands the original OMX buffer. */
    } else memcpy(b->first_output,input,bytes);
    if(b->pause_requested) {
        if(b->faded_out)memset(b->final_output,0x69,bytes);
        else {
            memset(b->final_output,0x69,sizeof(b->final_output));
            rc=sony_dsd_fade_process(b->native,b->first_output,bytes,SONY_DSD_FADE_END,
                                    b->final_output,sizeof(b->final_output),&n);
            if(rc)goto fail;
        }
    } else memcpy(b->final_output,b->first_output,bytes);
    memcpy(output,b->final_output,bytes);*written=bytes;b->first=0;
    if(b->pause_requested)b->faded_out=1;
    b->busy=0;return 0;
fail:
    b->faulted=1;b->busy=0;return -EIO;
}
int sony_dsd_render_request_pause(struct sony_dsd_render_bridge *b) {
    int rc=ready(b);if(rc)return rc;
    if(b->paused)return -EPIPE;
    if(b->pause_requested)return 1;
    b->pause_requested=1;return 0;
}
int sony_dsd_render_pause_complete(struct sony_dsd_render_bridge *b) {
    int rc=ready(b);if(rc)return rc;
    if(b->paused)return 1;
    if(!b->pause_requested)return -EINVAL;
    b->paused=1;b->first=1;b->pause_requested=0;b->faded_out=0;return 0;
}
int sony_dsd_render_resume(struct sony_dsd_render_bridge *b) {
    int rc=ready(b);if(rc)return rc;
    if(!b->paused)return -EINVAL;
    b->paused=0;return 0;
}
int sony_dsd_render_close(struct sony_dsd_render_bridge *b) {
    if(!b)return 0;
    if(b->busy)return -EBUSY;
    int rc=sony_dsd_fade_close(b->native);free(b);return rc;
}
