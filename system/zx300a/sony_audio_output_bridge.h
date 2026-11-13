#ifndef WALKDROID_SONY_OUTPUT_BRIDGE_H
#define WALKDROID_SONY_OUTPUT_BRIDGE_H
#include "sony_audio_output_timing.h"
/* Experimental audited ARM32/glibc Adler adapter. May operate real hardware!
 * Host tests redirect Open explicitly. Not deployment-ready, S32 stereo.
 * Caller supplies library/assets/config, target/route and IO guard preload.
 * All calls serialized on owner thread. No internal retry or route selection. */
#define SONY_OUTPUT_CLEANUP_UNCERTAIN -117
struct sony_output_bridge;
/* Native Open failure may leave power side effects despite object cleanup.
 * Returns CLEANUP_UNCERTAIN; caller must not automatically retry or claim rollback. */
int sony_output_open(struct sony_output_bridge **,void *audited_adler);
/* Explicit rate; passes the request to the backend and reports the timing
 * returned by ALSA. Close/reopen to change rate. */
int sony_output_open_rate(struct sony_output_bridge **,void *audited_adler,unsigned int sample_rate);
/* 0 acknowledges native acceptance, not DAC consumption; no retry on failure. */
int sony_output_write(struct sony_output_bridge *,const int *,unsigned int frames);
int sony_output_get_timing(struct sony_output_bridge *,struct sony_output_timing *);
/* Seals input on first attempt, including failure. Not safe to retry. */
int sony_output_drain(struct sony_output_bridge *);
/* Closes/destroys regardless of prior fault, frees bridge, nulls caller pointer.
 * If another guard scope is active, returns -16 and retains ownership.
 * No drain is implied; tails can be discarded. */
int sony_output_abort_close(struct sony_output_bridge **);
#endif
