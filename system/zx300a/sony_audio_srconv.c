/* ARM/glibc hard-float. Signatures come from the original
 * mobile::Resampler::ConfigureLib/ProcessLib and SRConv_* instructions. */
#include "sony_audio_srconv.h"
#include <dlfcn.h>
#include <errno.h>
#include <stdlib.h>
struct sony_srconv {
    void *library,*handle;
    int (*init)(unsigned,unsigned,unsigned,unsigned,unsigned,float,unsigned,unsigned,void*);
    int (*mandatory)(unsigned*,void*),(*count)(unsigned*,void*);
    int (*process)(const void*,void*,unsigned,unsigned*,void*);
    int (*flush)(void*,unsigned*,void*);
    void (*destroy)(void*);
    unsigned capacity;
    int failed,flushed;
};
void sony_srconv_close(struct sony_srconv **out){
    if(!out||!*out)return;struct sony_srconv *s=*out;*out=NULL;
    if(s->handle&&s->destroy)s->destroy(s->handle);
    if(s->library)dlclose(s->library);free(s);
}
int sony_srconv_open(struct sony_srconv **out,unsigned in,unsigned rate){
    if(!out||*out)return -EINVAL;
    struct sony_srconv *s=calloc(1,sizeof(*s));if(!s)return -ENOMEM;
    s->library=dlopen("libSoundServiceFw.so",RTLD_NOW|RTLD_LOCAL);
    if(!s->library){free(s);return -ELIBACC;}
    void *(*create)(void)=NULL;
#define LOAD(dst,name) do{*(void **)(&(dst))=dlsym(s->library,name);if(!(dst)){sony_srconv_close(&s);return -ELIBBAD;}}while(0)
    LOAD(create,"SRConv_GetHandle");LOAD(s->init,"SRConv_Init");
    LOAD(s->mandatory,"SRConv_GetMandatoryInputSampleCount");LOAD(s->count,"SRConv_GetOutputSampleCount");
    LOAD(s->process,"SRConv_RateConvert");LOAD(s->flush,"SRConv_RateConvertFlush");LOAD(s->destroy,"SRConv_FreeHandle");
#undef LOAD
    s->handle=create();int rc=s->handle?s->init(in,rate,2,256,120,0.91f,2,2,s->handle):-ENOMEM;
    if(!rc)rc=s->count(&s->capacity,s->handle);
    if(!rc&&(!s->capacity||s->capacity>2048))rc=-EOVERFLOW;
    if(rc){sony_srconv_close(&s);return rc;}*out=s;return 0;
}
int sony_srconv_limits(struct sony_srconv *s,unsigned *mandatory,unsigned *capacity){
    if(!s||!mandatory||!capacity)return -EINVAL;
    int rc=s->mandatory(mandatory,s->handle);if(!rc)*capacity=s->capacity;return rc;
}
int sony_srconv_process(struct sony_srconv *s,const int16_t *in,unsigned frames,int16_t *out,unsigned cap,unsigned *produced){
    if(!s||!in||!out||!produced||!frames||frames>256||cap<s->capacity)return -EINVAL;
    *produced=0;if(s->failed||s->flushed)return -ESHUTDOWN;
    int rc=s->process(in,out,frames,produced,s->handle);
    if(rc||*produced>cap){s->failed=1;return rc?rc:-EOVERFLOW;}return 0;
}
int sony_srconv_flush(struct sony_srconv *s,int16_t *out,unsigned cap,unsigned *produced){
    if(!s||!out||!produced||cap<s->capacity)return -EINVAL;
    *produced=0;if(s->failed||s->flushed)return -ESHUTDOWN;
    /* Original flush drains filter history. Output bound is validated on PC
     * before any use in a device runtime. */
    int rc=s->flush(out,produced,s->handle);s->flushed=1;
    if(rc||*produced>cap){s->failed=1;return rc?rc:-EOVERFLOW;}return 0;
}
