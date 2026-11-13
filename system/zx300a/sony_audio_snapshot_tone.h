#ifndef WALKDROID_SONY_SNAPSHOT_TONE_H
#define WALKDROID_SONY_SNAPSHOT_TONE_H
#include "sony_audio_chain_bridge.h"
/* Tone-mode-only codec. Requires EQ_USE=3, validates entire snapshot.
 * Current bridge acceptance limits centerfreq to raw default 2.
 * Errors preserve output. Encode touches tone fields, selector and CRC only.
 */
int sony_snapshot_decode_tone(const void *,unsigned int,struct sony_tone_settings *);
/* Preserve the inactive tone bank while EQ10 is selected. */
int sony_snapshot_decode_tone_values(const void *,unsigned int,struct sony_tone_settings *);
int sony_snapshot_encode_tone(const void *,unsigned int,const struct sony_tone_settings *,void *);
#endif
