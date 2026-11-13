#ifndef WALKDROID_SONY_PLAYER_ROUTE_POLICY_H
#define WALKDROID_SONY_PLAYER_ROUTE_POLICY_H
#include "sony_audio_dsd_controls.h"
#include "sony_audio_output_controls.h"
/* Original policy: actual BTL wins if both jacks are present; mode1 + BTL is
 * native, SE is conversion, mode2 is conversion on either jack. No-jack is a
 * refusal, never an implicit speaker route. Result leaves saved settings alone.
 * The chosen owned route must validate the same output again under its lease. */
int sony_player_route_select(const struct sony_dsd_controls *saved,unsigned se,unsigned btl,
    struct sony_dsd_controls *session,unsigned *expected_output,int *native);
#endif
