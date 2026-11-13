/* Private original-Sony processing queue; never clocks, retries or presents
 * audio. The worker owns one instance for one already-confirmed native peer. */
#include "sony_audio_bt_tx_pcm.h"
#include "sony_audio_srconv.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
struct sony_tx_pcm {
    struct sony_tx_pcm_stats stats;
    struct sony_srconv *src;
    sony_tx_pcm_send send;
    void *context;
    unsigned preferred,capacity,input_count,output_count;
    uint64_t received,limit;
    int failed,finishing;
    int16_t input[512],converted[4096],output[4096];
};
static int fail(struct sony_tx_pcm *s,int rc){s->failed=rc<0?rc:-EIO;s->stats.flags|=SONY_TX_PCM_FAILED;return s->failed;}
static int send_block(struct sony_tx_pcm *s,unsigned n){
    int rc=s->send(s->context,s->output,n);
    if(rc){
        /* Once handed to the native sender, the entire attempt is uncertain.
         * It is removed from retryable/pending storage and never replayed. */
        s->stats.codec_uncertain+=n;s->output_count-=n;
        memmove(s->output,s->output+2*n,s->output_count*4);return fail(s,rc);
    }
    s->stats.codec_submitted+=n;s->output_count-=n;
    memmove(s->output,s->output+2*n,s->output_count*4);return 0;
}
static int append(struct sony_tx_pcm *s,const int16_t *data,unsigned n){
    if(s->finishing){
        if(s->stats.codec_generated>s->limit)return fail(s,-EOVERFLOW);
        uint64_t left=s->limit-s->stats.codec_generated;if(n>left)n=(unsigned)left;
    }
    if(UINT64_MAX-s->stats.codec_generated<n)return fail(s,-EOVERFLOW);
    s->stats.codec_generated+=n;
    while(n){
        unsigned take=s->preferred-s->output_count;if(take>n)take=n;
        memcpy(s->output+2*s->output_count,data,take*4);s->output_count+=take;
        data+=2*take;n-=take;
        if(s->output_count==s->preferred){int rc=send_block(s,s->preferred);if(rc){s->stats.codec_discarded+=n;return rc;}}
    }
    return 0;
}
static int convert(struct sony_tx_pcm *s,unsigned real_frames){
    unsigned n=0;int rc=sony_srconv_process(s->src,s->input,256,s->converted,s->capacity,&n);
    if(rc)return fail(s,rc);
    s->stats.input_processed+=real_frames;s->input_count=0;
    return append(s,s->converted,n);
}
int sony_tx_pcm_open(struct sony_tx_pcm **out,unsigned input,unsigned codec,unsigned preferred,sony_tx_pcm_send send,void *context){
    if(!out||*out||!send)return -EINVAL;
    if(!input)input=1;if(!codec)codec=input;
    if(!preferred)preferred=128;if(preferred>2048)preferred=2048;
    struct sony_tx_pcm *s=calloc(1,sizeof(*s));if(!s)return -ENOMEM;
    s->stats.input_rate=input;s->stats.codec_rate=codec;s->preferred=preferred;s->send=send;s->context=context;
    if(input!=codec){
        int rc=sony_srconv_open(&s->src,input,codec);unsigned mandatory=0;
        if(!rc)rc=sony_srconv_limits(s->src,&mandatory,&s->capacity);
        if(!rc&&(mandatory>256||s->capacity>2048))rc=-EOVERFLOW;
        if(rc){sony_srconv_close(&s->src);free(s);return rc;}
        s->stats.flags=SONY_TX_PCM_CONVERT;
    }
    *out=s;return 0;
}
int sony_tx_pcm_write(struct sony_tx_pcm *s,const int16_t *data,unsigned frames){
    if(!s||(!data&&frames))return -EINVAL;
    if(s->failed)return s->failed;if(s->stats.flags&(SONY_TX_PCM_FINISHED|SONY_TX_PCM_CANCELED))return -ESHUTDOWN;
    if(UINT64_MAX-s->received<frames)return fail(s,-EOVERFLOW);
    if(!s->src){
        while(frames){
            unsigned take=s->preferred-s->output_count;if(take>frames)take=frames;
            s->received+=take;s->stats.input_processed+=take;
            int rc=append(s,data,take);if(rc)return rc;data+=2*take;frames-=take;
        }
        return 0;
    }
    while(frames){
        unsigned take=256-s->input_count;if(take>frames)take=frames;
        memcpy(s->input+2*s->input_count,data,take*4);s->input_count+=take;
        s->received+=take;data+=2*take;frames-=take;
        if(s->input_count==256){int rc=convert(s,256);if(rc)return rc;}
    }
    return 0;
}
int sony_tx_pcm_finish(struct sony_tx_pcm *s){
    if(!s)return -EINVAL;if(s->failed)return s->failed;
    if(s->stats.flags&(SONY_TX_PCM_FINISHED|SONY_TX_PCM_CANCELED))return -ESHUTDOWN;
    /* Exact rational duration, rounded up by at most one codec sample. Avoid
     * multiplying the unbounded frame count before division. */
    uint64_t seconds=s->received/s->stats.input_rate,rest=s->received%s->stats.input_rate;
    if(seconds>UINT64_MAX/s->stats.codec_rate)return fail(s,-EOVERFLOW);
    uint64_t tail=(rest*s->stats.codec_rate+s->stats.input_rate-1)/s->stats.input_rate;
    if(UINT64_MAX-seconds*s->stats.codec_rate<tail)return fail(s,-EOVERFLOW);
    s->limit=seconds*s->stats.codec_rate+tail;s->finishing=1;
    int rc=0;
    if(s->src&&s->received){
        if(s->input_count){
            unsigned real=s->input_count;s->stats.input_padding=256-real;
            memset(s->input+2*real,0,(256-real)*4);rc=convert(s,real);
        }
        unsigned n=0;if(!rc)rc=sony_srconv_flush(s->src,s->converted,s->capacity,&n);
        if(!rc)rc=append(s,s->converted,n);
    }
    if(rc)return fail(s,rc);
    if(s->output_count){
        unsigned final=(s->output_count+127)/128*128;
        s->stats.codec_padding=final-s->output_count;
        memset(s->output+2*s->output_count,0,s->stats.codec_padding*4);s->output_count=final;
        if((rc=send_block(s,final)))return rc;
    }
    s->stats.flags|=SONY_TX_PCM_FINISHED;return 0;
}
void sony_tx_pcm_cancel(struct sony_tx_pcm *s){
    if(!s||s->stats.flags&(SONY_TX_PCM_FINISHED|SONY_TX_PCM_CANCELED))return;
    s->stats.input_discarded+=s->input_count;s->stats.codec_discarded+=s->output_count;
    s->input_count=s->output_count=0;s->stats.flags|=SONY_TX_PCM_CANCELED;
}
void sony_tx_pcm_stats(const struct sony_tx_pcm *s,struct sony_tx_pcm_stats *out){
    if(!s||!out)return;*out=s->stats;out->input_pending=s->input_count;out->codec_pending=s->output_count;
}
void sony_tx_pcm_close(struct sony_tx_pcm **out){
    if(!out||!*out)return;struct sony_tx_pcm *s=*out;*out=NULL;
    sony_tx_pcm_cancel(s);sony_srconv_close(&s->src);free(s);
}
