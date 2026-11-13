#define _GNU_SOURCE 1
#include "sony_audio_ai_hal.h"
#include "sony_audio_ai_root.h"
#include "sony_audio_cpu.h"
#include "sony_audio_route.h"
#include "sony_audio_dsd_power_linux.h"
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
struct sony_ai_hal {
    struct sony_audio_owner *owner;struct sony_worker_process **playback;
    struct sony_ai_process *ai;struct sony_ai_process_exit ai_exit;
    struct sony_audio_cpu *cpu;struct sony_dsd_power_linux *linux_io;
    struct sony_dsd_power_platform *power;struct sony_dsd_power_backend backend;
    uint64_t generation,token;
    int root,log,prepared,playback_started;
};
static int thread(struct sony_ai_hal *h){
    return h?0:-EINVAL;
}
static int owned(void *v){
    struct sony_ai_hal *h=v;int rc=thread(h);if(rc)return rc;
    return sony_audio_owner_check(h->owner);
}
static int closed(void *v){
    struct sony_ai_hal *h=v;int rc=owned(h);if(rc)return rc;
    if(h->ai||*h->playback)return -EBUSY;
    return sony_audio_route_idle();
}
int sony_ai_hal_root_open(void){
    int fd=open("/vendor/sony-audio-ai/rootfs",O_RDONLY|O_DIRECTORY|O_CLOEXEC);
    return fd<0?-errno:fd;
}
int sony_ai_hal_log_open(void){
    int fd=open("/dev/walkdroid-sony-ai.log",O_WRONLY|O_CREAT|O_APPEND|O_CLOEXEC,0600);
    return fd<0?-errno:fd;
}
int sony_ai_hal_prepare(struct sony_ai_hal **out,struct sony_audio_owner *owner,struct sony_worker_process **slot,int root,int log){
    if(!out||*out||!owner||!slot||*slot||root<0||log<0)return -EINVAL;
    int rc=sony_audio_owner_check(owner);if(rc)return rc;
    struct sony_ai_hal *h=calloc(1,sizeof(*h));if(!h)return -ENOMEM;
    h->root=h->log=-1;h->owner=owner;h->playback=slot;h->generation=sony_audio_owner_generation(owner);
    h->ai_exit.exit_code=-1;
    h->root=fcntl(root,F_DUPFD_CLOEXEC,10);if(h->root>=0)h->log=fcntl(log,F_DUPFD_CLOEXEC,10);
    if(h->root<0||h->log<0){rc=-errno;if(h->root>=0)close(h->root);free(h);return rc;}
    *out=h;rc=closed(h);
    if(!rc)rc=sony_audio_cpu_acquire(&h->cpu,h->generation,owned,closed,h);
    struct sony_dsd_power_platform_io io={0};
    if(!rc)rc=sony_dsd_power_linux_create(&h->linux_io,owned,h);
    if(!rc)rc=sony_dsd_power_linux_io(h->linux_io,&io);
    uint64_t receipt=h->generation^UINT64_C(0x5741495000000000);if(!receipt)receipt=1;
    if(!rc)rc=sony_dsd_power_platform_create(&h->power,&io,receipt);
    if(!rc)rc=sony_dsd_power_platform_backend(h->power,&h->backend);
    const struct sony_dsd_power_plan budget={2,1300000,1,0,{80,80,80}};
    if(!rc)rc=h->backend.acquire(h->backend.context,&budget,&h->token);
    if(!rc)h->prepared=1;return rc;
}
int sony_ai_hal_launch_context(struct sony_ai_hal *h,int root,int log,int profile,int signature,int ship,uint64_t nonce,int timeout,struct sony_worker_exit *out){
    int rc=owned(h);if(rc)return rc;
    if(!out||!h->prepared||*h->playback)return -EINVAL;
    rc=sony_audio_cpu_check(h->cpu);if(rc)return rc;
    rc=sony_ai_process_start(&h->ai,SONY_AI_PROCESS_EXPLICIT_START,h->owner,h->root,h->log,nonce,timeout,&h->ai_exit);
    int fd=-1;uint64_t exported=0;
    if(!rc)rc=sony_ai_process_export(h->ai,&fd,&exported);
    if(!rc)rc=owned(h);
    if(!rc)rc=sony_worker_process_start_profile_ai_guarded_context(h->playback,SONY_PROCESS_AI_GUARDED_EXPLICIT_START,root,log,1,profile,fd,exported,signature,ship,timeout,out);
    if(!rc)h->playback_started=1;
    if(fd>=0)close(fd);return rc;
}
int sony_ai_hal_end_report(struct sony_ai_hal **owner,int timeout,struct sony_ai_process_exit *report){
    if(!owner||!*owner)return -EINVAL;
    struct sony_ai_hal *h=*owner;int rc=thread(h),process_error=0;if(rc)return rc;if(report)*report=h->ai_exit;
    if(*h->playback)return -EBUSY;
    if(h->ai){
        rc=sony_ai_process_stop(&h->ai,timeout,&h->ai_exit);
        if(report)*report=h->ai_exit;
        if(h->ai)return rc;
        process_error=rc;
    }
    rc=closed(h);if(rc)return rc;
    if(h->token){
        rc=h->backend.release(h->backend.context,h->token);
        if(rc)return rc;h->token=0;
    }
    if(h->power){rc=sony_dsd_power_platform_destroy(&h->power);if(rc)return rc;}
    if(h->linux_io){rc=sony_dsd_power_linux_destroy(&h->linux_io);if(rc)return rc;}
    if(h->cpu){rc=sony_audio_cpu_release(&h->cpu);if(rc)return rc;}
    int abnormal=h->ai_exit.reaped&&(h->ai_exit.kill_sent||h->ai_exit.signal_number||
        (h->ai_exit.exit_code!=0&&(h->playback_started||h->ai_exit.exit_code!=74)));
    close(h->root);close(h->log);free(h);*owner=NULL;return process_error?process_error:abnormal?-EIO:0;
}
int sony_ai_hal_end(struct sony_ai_hal **owner,int timeout){return sony_ai_hal_end_report(owner,timeout,NULL);}

int sony_ai_hal_launch(struct sony_ai_hal *h,int root,int log,int profile,uint64_t nonce,int timeout,struct sony_worker_exit *out){return sony_ai_hal_launch_context(h,root,log,profile,-1,0,nonce,timeout,out);}
