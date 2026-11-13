#define _GNU_SOURCE 1
#include "sony_audio_pcm_boundary.h"
#include <dlfcn.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>
extern unsigned int sony_audio_native_call3(void *,unsigned int,unsigned int,unsigned int);
static void *original_i2f,*original_hx;
static unsigned int binding_state;
static __thread struct sony_pcm_boundary *current;
static const char i2f_symbol[]="_ZN3pst8services5sound6mobile3I2f18ProcessAudioPacketEPNS1_10DataPacketE";
static const char hx_symbol[]="_ZN3pst8services5sound6mobile12DseeHxCustom18ProcessAudioPacketEPNS1_10DataPacketE";
int sony_boundary_i2f(void *,void *) __asm__("_ZN3pst8services5sound6mobile3I2f18ProcessAudioPacketEPNS1_10DataPacketE");
int sony_boundary_hx(void *,void *) __asm__("_ZN3pst8services5sound6mobile12DseeHxCustom18ProcessAudioPacketEPNS1_10DataPacketE");
int sony_pcm_boundary_bind(void *library){
    if(!library)return -EINVAL;
    void *i2f=dlsym(library,i2f_symbol),*hx=dlsym(library,hx_symbol);
    if(!i2f||!hx||i2f==(void *)sony_boundary_i2f||hx==(void *)sony_boundary_hx)return -ENOSYS;
    unsigned expected=0;
    if(__atomic_compare_exchange_n(&binding_state,&expected,1,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE)){
        __atomic_store_n(&original_i2f,i2f,__ATOMIC_RELEASE);__atomic_store_n(&original_hx,hx,__ATOMIC_RELEASE);
        __atomic_store_n(&binding_state,2,__ATOMIC_RELEASE);return 0;
    }
    if(expected!=2)return -EBUSY;
    __atomic_store_n(&original_i2f,i2f,__ATOMIC_RELEASE);__atomic_store_n(&original_hx,hx,__ATOMIC_RELEASE);return 0;
}
int sony_pcm_boundary_enter(struct sony_pcm_boundary *g){
    if(!g||!g->packet||!g->transform||!g->payload||!g->audio_info||!g->written||!g->size)return -EINVAL;
    if(__atomic_load_n(&binding_state,__ATOMIC_ACQUIRE)!=2)return -ENOSYS;
    if(current)return -EBUSY;
    g->calls=g->hx_calls=g->produced=0;g->failed=0;current=g;return 0;
}
int sony_pcm_boundary_leave(struct sony_pcm_boundary *g){
    if(!g||current!=g)return -EINVAL;
    current=NULL;
    return g->failed;
}
static int delegate(int hx,void *self,void *packet){
    if(__atomic_load_n(&binding_state,__ATOMIC_ACQUIRE)!=2)return -ENOSYS;
    void *method=hx?__atomic_load_n(&original_hx,__ATOMIC_ACQUIRE):__atomic_load_n(&original_i2f,__ATOMIC_ACQUIRE);
    return (int)sony_audio_native_call3(method,(unsigned)self,(unsigned)packet,0);
}
int sony_boundary_i2f(void *self,void *packet){
    struct sony_pcm_boundary *g=current;
    if(!g||packet!=g->packet)return delegate(0,self,packet);
    ++g->calls;
    int result=delegate(0,self,packet);
    unsigned *track=(void *)sony_audio_native_call3((void *)g->audio_info,(unsigned)packet,0,0);
    float *samples=(void *)sony_audio_native_call3((void *)g->payload,(unsigned)packet,0,0);
    unsigned count=sony_audio_native_call3((void *)g->written,(unsigned)packet,0,0);
    if(result||!track||!samples||(uint64_t)count<(uint64_t)g->source_frames*8){
        g->failed=-EPROTO;return g->failed;
    }
    unsigned frames=0,rate=0;
    result=g->transform(g->context,samples,g->source_frames,g->scratch,2048,&frames,&rate);
    if(result||frames>2048){g->failed=result?result:-EPROTO;return g->failed;}
    memcpy(samples,g->scratch,frames*8);track[0]=rate;
    sony_audio_native_call3((void *)g->size,(unsigned)packet,frames*8,0);g->produced=frames;
    return 0;
}
int sony_boundary_hx(void *self,void *packet){
    struct sony_pcm_boundary *g=current;
    if(!g||packet!=g->packet)return delegate(1,self,packet);
    ++g->hx_calls;
    return 0;
}
