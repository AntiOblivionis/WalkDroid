#ifndef WALKDROID_SONY_AUDIO_ROUTE_H
#define WALKDROID_SONY_AUDIO_ROUTE_H
#include "sony_audio_output_controls.h"
/* Mixer operations return their actual errors. Begin applies a muted route;
 * enable unmutes, and end requests mute and output-off. Hardware presence,
 * audio readiness, control readback and other PCM users are not verified. */
struct sony_audio_route;
int sony_audio_route_check(struct sony_audio_route *);
int sony_audio_route_begin(struct sony_audio_route **,unsigned int volume,int effects,const struct sony_output_controls *);
int sony_audio_route_enable(struct sony_audio_route *);
int sony_audio_route_mute(struct sony_audio_route *,int mute);
int sony_audio_route_effects(struct sony_audio_route *,int enabled);
int sony_audio_route_volume(struct sony_audio_route *,unsigned int volume);
int sony_audio_route_live_volume(struct sony_audio_route *,unsigned expected,unsigned volume,const struct sony_output_controls *);
int sony_audio_route_apply(struct sony_audio_route *,unsigned int volume,const struct sony_output_controls *);
int sony_audio_route_end(struct sony_audio_route **);
/* Compatibility entry points: success does not establish hardware state. */
int sony_audio_route_idle(void);
int sony_audio_route_parked(struct sony_audio_route *,unsigned volume,int effects,const struct sony_output_controls *);
#endif
