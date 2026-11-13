#include "sony_audio_ai_paused_power.h"
#include <stdio.h>

/* Original calls are intercepted; the parent manages the actual power
 * resources. The counters are diagnostic and do not authorize operations. */
static unsigned sysfs,latency,failed_open;
int sony_ai_paused_power_bind(uint64_t nonce){
    (void)nonce;
    __atomic_store_n(&sysfs,0,__ATOMIC_RELAXED);
    __atomic_store_n(&latency,0,__ATOMIC_RELAXED);
    __atomic_store_n(&failed_open,0,__ATOMIC_RELAXED);
    return 0;
}
int sony_ai_paused_power_fault(void){return 0;}
void sony_ai_paused_power_failed_open(void){__atomic_store_n(&failed_open,1,__ATOMIC_RELAXED);}
int sony_ai_pcm_idle_intent(void *,const unsigned *,const unsigned *)
 __asm__("_ZN3pst8services8funcarch10SysFsUtils5WriteERKNSt3__112basic_stringIcNS3_11char_traitsIcEENS3_9allocatorIcEEEESB_");
int sony_ai_pcm_idle_intent(void *self,const unsigned *path,const unsigned *value){
    (void)self;(void)path;(void)value;__atomic_add_fetch(&sysfs,1,__ATOMIC_RELAXED);return 0;
}
int sony_ai_pcm_latency_intent(void *,const unsigned *)
 __asm__("_ZN3pst8audiohal20AudioHalOutAdlerAlsa18SetPlaybackLatencyERKNSt3__112basic_stringIcNS2_11char_traitsIcEENS2_9allocatorIcEEEE");
int sony_ai_pcm_latency_intent(void *self,const unsigned *value){
    (void)self;(void)value;__atomic_add_fetch(&latency,1,__ATOMIC_RELAXED);return 0;
}
int sony_ai_paused_power_finish(void){
    fprintf(stderr,"SonyAIPower delegated_idle=%u delegated_latency=%u failed_open=%u\n",
            __atomic_load_n(&sysfs,__ATOMIC_RELAXED),__atomic_load_n(&latency,__ATOMIC_RELAXED),
            __atomic_load_n(&failed_open,__ATOMIC_RELAXED));
    return 0;
}
