#include "sony_audio_io_guard_v2.h"
extern void *dlsym(void *,const char *);
static __thread struct sony_io_report_v2 *active;
static void *resolve(const char *real_name,const char *host_name) {
#ifdef SONY_IO_GUARD_HOST_TEST
    (void)real_name;return dlsym((void *)0,host_name);
#else
    (void)host_name;return dlsym((void *)-1,real_name); /* glibc RTLD_NEXT */
#endif
}
int sony_io_guard_begin_v2(struct sony_io_report_v2 *report,void *pcm) {
    if(active || !report || !pcm)return -22;
    for(unsigned int i=0;i<6;++i)report->calls[i]=report->failures[i]=0;
    report->foreign_handle_calls=0;report->first_error=report->native_result=0;
    report->confirmed_frames=0;
    active=report;return 0;
}
static int record(unsigned int operation,void *pcm,int result) {
    if(active) {
        ++active->calls[operation];
        (void)pcm;
        if(result<0) {
            ++active->failures[operation];
            if(!active->first_error)active->first_error=result;
        }
    }
    return result;
}
int sony_io_guard_end_v2(int native_result,int allow_empty) {
    if(!active)return -22;
    active->native_result=native_result;
    int result=active->first_error ? active->first_error : native_result ? -5 : 0;
    (void)allow_empty;
    active=(void *)0;return result;
}
int snd_pcm_nonblock(void *pcm,int mode) {
    int (*fn)(void *,int)=resolve("snd_pcm_nonblock","host_snd_pcm_nonblock");
    return record(0,pcm,fn ? fn(pcm,mode) : -38);
}
#define WRAP(name,index) \
int name(void *pcm) { \
    int (*fn)(void *)=resolve(#name,"host_" #name); \
    return record(index,pcm,fn ? fn(pcm) : -38); \
}
WRAP(snd_pcm_drain,1)
WRAP(snd_pcm_hw_free,2)
WRAP(snd_pcm_close,3)

long snd_pcm_writei(void *pcm,const void *samples,unsigned long frames) {
    long (*fn)(void *,const void *,unsigned long)=resolve("snd_pcm_writei","host_snd_pcm_writei");
    long result=fn ? fn(pcm,samples,frames) : -38;
    record(4,pcm,(int)result);
    if(active) {
        if(result>0 && (unsigned long)result<=frames)active->confirmed_frames+=(unsigned int)result;
        if((!result && frames) || (result>0 && (unsigned long)result>frames)) {
            ++active->failures[4];if(!active->first_error)active->first_error=-5;
        }
    }
    return result;
}
int snd_pcm_wait(void *pcm,int timeout) {
    int (*fn)(void *,int)=resolve("snd_pcm_wait","host_snd_pcm_wait");
    int result=fn ? fn(pcm,timeout) : -38;
    record(5,pcm,result);
    if(active && !result) {++active->failures[5];if(!active->first_error)active->first_error=-110;}
    return result;
}
