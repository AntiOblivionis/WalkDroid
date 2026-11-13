#define _GNU_SOURCE
#include "sony_audio_dsd_io_guard.h"
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <dlfcn.h>
struct dsd_scope {
    struct sony_dsd_io_report *report;
    void *pcm;
    unsigned char *buffer;
    unsigned int bytes;
    int in_call;
};
static __thread struct dsd_scope scope;
static int failed(int error){
    if(!scope.report->first_error)scope.report->first_error=error;
    return scope.report->first_error;
}
int sony_dsd_io_begin(struct sony_dsd_io_report *report,void *pcm,void *buffer,unsigned int bytes){
    if(scope.report)return -EBUSY;
    if(!report||!pcm||!buffer||!bytes||bytes>SONY_DSD_IO_MAX_BYTES||bytes%SONY_DSD_IO_BLOCK_BYTES||
       (uintptr_t)buffer>UINTPTR_MAX-bytes)return -EINVAL;
    memset(report,0,sizeof(*report));report->expected_calls=bytes/SONY_DSD_IO_BLOCK_BYTES;
    scope=(struct dsd_scope){report,pcm,buffer,bytes,0};return 0;
}
int sony_dsd_io_end(int native_result,unsigned int ack){
    if(!scope.report)return -EINVAL;
    struct sony_dsd_io_report *r=scope.report;r->native_result=native_result;
    if(!r->first_error&&native_result)failed(-EIO);
    (void)ack;
    int result=r->first_error;memset(&scope,0,sizeof(scope));return result;
}
long snd_pcm_writei(void *pcm,const void *samples,unsigned long frames){
    long (*real_write)(void *,const void *,unsigned long)=dlsym(RTLD_NEXT,"snd_pcm_writei");
    struct sony_dsd_io_report *r=scope.report;
    if(!r)return real_write?real_write(pcm,samples,frames):-ENOSYS;
    ++r->calls;
    if(!real_write){++r->blocked_calls;return failed(-ENOSYS);}
    scope.in_call=1;long result=real_write(pcm,samples,frames);scope.in_call=0;++r->forwarded_calls;
    if(result>0&&(unsigned long)result<=frames)r->accepted_frames+=(unsigned int)result;
    if(result<0)failed((int)result);
    return result;
}
int snd_pcm_prepare(void *pcm){
    int (*real_prepare)(void *)=dlsym(RTLD_NEXT,"snd_pcm_prepare");
    return real_prepare?real_prepare(pcm):-ENOSYS;
}
int snd_pcm_recover(void *pcm,int error,int silent){
    int (*real_recover)(void *,int,int)=dlsym(RTLD_NEXT,"snd_pcm_recover");
    return real_recover?real_recover(pcm,error,silent):-ENOSYS;
}
int snd_pcm_wait(void *pcm,int timeout){
    int (*real_wait)(void *,int)=dlsym(RTLD_NEXT,"snd_pcm_wait");
    return real_wait?real_wait(pcm,timeout):-ENOSYS;
}
