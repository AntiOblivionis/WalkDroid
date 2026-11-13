#ifndef SONY_AUDIO_PCM_ENTRY_AUDIT_H
#define SONY_AUDIO_PCM_ENTRY_AUDIT_H
/* Diagnostic builds only. No PCM rewriting, syscalls or allocation in push. */
struct sony_pcm_entry_audit;
struct sony_pcm_entry_audit *sony_pcm_entry_audit_begin(unsigned int rate);
void sony_pcm_entry_audit_push(struct sony_pcm_entry_audit *,const int *samples);
void sony_pcm_entry_audit_push_frames(struct sony_pcm_entry_audit *,const int *samples,unsigned int frames);
void sony_pcm_entry_audit_result(struct sony_pcm_entry_audit *,int native_status);
void sony_pcm_entry_audit_close(struct sony_pcm_entry_audit *);
#endif
