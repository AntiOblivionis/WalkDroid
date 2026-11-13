#ifndef SONY_AUDIO_DSD_RENDER_BRIDGE_H
#define SONY_AUDIO_DSD_RENDER_BRIDGE_H
#include <stddef.h>
struct sony_dsd_render_bridge;
/* Normal renderer lifecycle, separate from demux/converter trick-play flags.
 * Calls the supplied Sony crossfade implementation through configurable native
 * offsets; an incompatible ABI or offset can fail or corrupt the process.
 * Serialize calls; callbacks may not reenter/close this context.
 */
int sony_dsd_render_open(struct sony_dsd_render_bridge **,void *audited_player_library,unsigned bitrate);
typedef int (*sony_dsd_render_sink)(void *,const unsigned char *,unsigned bytes);
/* Open preroll: 200ms of 0x69 at the requested DSD rate, rounded down to a
 * stereo byte pair, up to 65536 bytes/callback. A zero-length first preroll
 * succeeds with zero acknowledged bytes; later calls return 1.
 * Deliver once before input. ACK counts successful callbacks, not hardware.
 * Failure faults lifetime, never retries the failed callback. Invalid preflight
 * leaves ACK unchanged; begun calls zero it. 1 means preroll already submitted.
 */
int sony_dsd_render_preroll(struct sony_dsd_render_bridge *,sony_dsd_render_sink,void *,unsigned *ack);
/* Canonical stereo input, even 1..32768 bytes. Output length equals input.
 * First packet after open/resume gets original renderer-style fade-in; ordinary
 * subsequent packets (including natural EOS data) are unchanged. Explicit pause
 * request fades one packet out, then silences later packets until pause completes.
 * Source buffers and output/written must not overlap. Outputs unchanged on error.
 * Not a file END flag, DSP filter, hardware pause, ALSA drain or playback complete.
 */
int sony_dsd_render_process(struct sony_dsd_render_bridge *,const unsigned char *,unsigned bytes,
                           unsigned char *,unsigned capacity,unsigned *written);
int sony_dsd_render_request_pause(struct sony_dsd_render_bridge *);
/* Call only after the caller's output controller has completed pause; this resets
 * local renderer fade state and blocks input. This function performs no hardware
 * operation and cannot itself establish that output has paused or drained. */
int sony_dsd_render_pause_complete(struct sony_dsd_render_bridge *);
/* Resume local state after output controller resumes. No second Open preroll. */
int sony_dsd_render_resume(struct sony_dsd_render_bridge *);
int sony_dsd_render_close(struct sony_dsd_render_bridge *);
#endif
