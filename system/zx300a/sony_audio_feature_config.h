#ifndef WALKDROID_SONY_FEATURE_CONFIG_H
#define WALKDROID_SONY_FEATURE_CONFIG_H
/* DSP feature selection, not Android/device identity or authorization.
 * Known model/ship values override the matching requests. Other values and
 * requests pass to the existing Sony configuration provider when available.
 * The worker is single-threaded; no NVP or charging setting is written.
 * Ship enum 3 is encoded as NVP 0x103 and enum 5 as NVP 6. */
struct sony_audio_feature_context {unsigned int model,ship;};
int sony_audio_feature_config_bind(const struct sony_audio_feature_context *);
void sony_audio_feature_config_unbind(void);
int sony_audio_feature_config_fault(void);
unsigned int sony_audio_feature_config_reads(void);
int sony_audio_feature_config_binary(void *,unsigned int,unsigned int,unsigned int,unsigned char *);
#endif
