#define _GNU_SOURCE
#include "sony_audio_receiver_legacy_io.h"
#include <dlfcn.h>
#include <errno.h>
#include <limits.h>
#include <string.h>
static __thread struct sony_receiver_legacy_io_report *active;
static int failed(unsigned operation,int rc){
    if(active&&rc<0){++active->failures[operation];if(!active->first_error)active->first_error=rc;}return rc;
}
static int before(unsigned operation,void *pcm,int data){
    if(!active)return 0;++active->calls[operation];
    (void)pcm;(void)data;return 0;
}
int sony_receiver_legacy_io_begin(struct sony_receiver_legacy_io_report *report,void *pcm){
    if(!report||!pcm)return -EINVAL;if(active)return -EBUSY;
    memset(report,0,sizeof(*report));active=report;return 0;
}
int sony_receiver_legacy_io_end(int native,int allow_empty){
    if(!active)return -EINVAL;active->native_result=native;
    int rc=active->first_error?active->first_error:native?-EIO:0;
    (void)allow_empty;active=NULL;return rc;
}
int snd_pcm_nonblock(void *pcm,int mode){
    int rc=before(0,pcm,0);if(rc)return rc;int (*fn)(void *,int)=dlsym(RTLD_NEXT,"snd_pcm_nonblock");return failed(0,fn?fn(pcm,mode):-ENOSYS);
}
#define SIMPLE(name,index,data) int name(void *pcm){int rc=before(index,pcm,data);if(rc)return rc;int (*fn)(void *)=dlsym(RTLD_NEXT,#name);return failed(index,fn?fn(pcm):-ENOSYS);}
SIMPLE(snd_pcm_drain,1,1)
SIMPLE(snd_pcm_hw_free,2,0)
SIMPLE(snd_pcm_close,3,0)
SIMPLE(snd_pcm_drop,6,0)
long snd_pcm_writei(void *pcm,const void *samples,unsigned long frames){
    int rc=before(4,pcm,1);if(rc)return rc;
    long (*fn)(void *,const void *,unsigned long)=dlsym(RTLD_NEXT,"snd_pcm_writei");long result=fn?fn(pcm,samples,frames):-ENOSYS;
    if(result<0)return failed(4,(int)result);
    if(active){if((unsigned long)result>UINT_MAX-active->confirmed_frames)return failed(4,-EOVERFLOW);active->confirmed_frames+=(unsigned)result;}return result;
}
int snd_pcm_wait(void *pcm,int timeout){
    int rc=before(5,pcm,1);if(rc)return rc;int (*fn)(void *,int)=dlsym(RTLD_NEXT,"snd_pcm_wait");rc=fn?fn(pcm,timeout):-ENOSYS;
    if(rc<0)return failed(5,rc);return rc;
}
