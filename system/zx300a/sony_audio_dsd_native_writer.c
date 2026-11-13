#include "sony_audio_dsd_native_writer.h"
#include "sony_audio_dsd_io_guard.h"
#include <dlfcn.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
extern unsigned int sony_audio_native_call3(void *,unsigned int,unsigned int,unsigned int);
struct sony_dsd_native_writer {
    unsigned int *object;
    void *pcm,*write;
    int busy;
    struct sony_dsd_native_writer_state state;
    unsigned char scratch[SONY_DSD_IO_MAX_BYTES];
};
static int lock(struct sony_dsd_native_writer *w){
    return !w?-EINVAL:__atomic_exchange_n(&w->busy,1,__ATOMIC_ACQUIRE)?-EBUSY:0;
}
static int unlock(struct sony_dsd_native_writer *w,int rc){__atomic_store_n(&w->busy,0,__ATOMIC_RELEASE);return rc;}
static int seal(struct sony_dsd_native_writer *w,int error){
    w->state.failed=1;if(!w->state.first_error)w->state.first_error=error;return error;
}
int sony_dsd_native_writer_bind(struct sony_dsd_native_writer **out,void *library,void *native,unsigned rate){
    if(!out||*out||!library||!native||(rate!=2822400&&rate!=5644800&&rate!=11289600))return -EINVAL;
    /* This bridge uses the supplied Sony ARM32 object's PCM field directly. */
    unsigned int *object=native;void *pcm=(void *)object[7];if(!pcm)return -ENODEV;
    void *write=dlsym(library,"_ZN3pst8audiohal20AudioHalOutAdlerAlsa5WriteERKNS0_14AudioHalBufferEPj");
    if(!write)return -ENOSYS;
    struct sony_dsd_native_writer *w=calloc(1,sizeof(*w));if(!w)return -ENOMEM;
    w->object=object;w->pcm=pcm;w->write=write;w->state.bit_rate=rate;*out=w;return 0;
}
int sony_dsd_native_writer_write(struct sony_dsd_native_writer *w,const void *input,unsigned bytes){
    if(!input||!bytes||bytes>SONY_DSD_IO_MAX_BYTES||bytes%SONY_DSD_IO_BLOCK_BYTES||
       (uintptr_t)input>UINTPTR_MAX-bytes)return -EINVAL;
    int rc=lock(w);if(rc)return rc;
    if(w->state.failed)return unlock(w,-EPIPE);
    if(w->state.completed_packets==UINT32_MAX||w->state.completed_bytes>UINT64_MAX-bytes||
       w->state.observed_admitted_bytes>UINT64_MAX-bytes)return unlock(w,seal(w,-EOVERFLOW));
    memcpy(w->scratch,input,bytes);struct sony_dsd_io_report report;
    rc=sony_dsd_io_begin(&report,w->pcm,w->scratch,bytes);
    if(rc)return unlock(w,rc); /* Another scope owns the thread: no native call. */
    unsigned buffer[3]={(unsigned)w->scratch,bytes,sizeof(w->scratch)},ack=0;
    int native=(int)sony_audio_native_call3(w->write,(unsigned)w->object,(unsigned)buffer,(unsigned)&ack);
    rc=sony_dsd_io_end(native,ack);
    w->state.observed_admitted_bytes+=(uint64_t)report.accepted_frames*2;
    if(rc)seal(w,rc);
    else{++w->state.completed_packets;w->state.completed_bytes+=bytes;}
    return unlock(w,rc);
}
int sony_dsd_native_writer_state(struct sony_dsd_native_writer *w,struct sony_dsd_native_writer_state *out){
    if(!out)return -EINVAL;
    int rc=lock(w);if(rc)return rc;*out=w->state;return unlock(w,0);
}
int sony_dsd_native_writer_destroy(struct sony_dsd_native_writer **owner){
    if(!owner||!*owner)return -EINVAL;
    struct sony_dsd_native_writer *w=*owner;int rc=lock(w);if(rc)return rc;
    memset(w->scratch,0,sizeof(w->scratch));free(w);*owner=NULL;return 0;
}
