#ifndef WALKDROID_SONY_PCM_BOUNDARY_H
#define WALKDROID_SONY_PCM_BOUNDARY_H
/* Internal ARM/glibc boundary. Only included in the explicit AI build.
 * The verified native methods remain the fallback for every other packet.
 * A guard is stack-owned, thread-local and lives for one synchronous Write.
 */
typedef int (*sony_pcm_float_transform)(void *,const float *,unsigned int,
    float *,unsigned int,unsigned int *,unsigned int *);
struct sony_pcm_boundary {
    void *packet,*context;
    sony_pcm_float_transform transform;
    void *(*payload)(void *);
    void *(*audio_info)(void *);
    unsigned int (*written)(void *);
    void (*size)(void *,unsigned int);
    unsigned int source_rate,source_frames,output_rate;
    unsigned int calls,hx_calls,produced;
    int failed;
    float scratch[4096];
};
int sony_pcm_boundary_bind(void *audited_library);
int sony_pcm_boundary_enter(struct sony_pcm_boundary *);
int sony_pcm_boundary_leave(struct sony_pcm_boundary *);
#endif
