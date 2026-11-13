#ifndef SONY_AUDIO_PCM_SINK_PROTOCOL_H
#define SONY_AUDIO_PCM_SINK_PROTOCOL_H
#include <stdint.h>
/* WDP5: already-processed stereo S32_LE only. No DSP, mixing, conversion,
 * software volume, implicit tail or route selection. Caller coordinates the
 * output and worker. A WRITE ACK is native
 * admission, not physical consumption. DRAIN is an observed ALSA operation.
 * All words little endian. Requests strictly increase within one worker life.
 * OPEN has rate only, WRITE has matching rate/frames/PCM. Other commands have
 * zero rate/frames/payload. All requests flags/status=0. Responses echo header
 * except command|REPLY, session, payload and status. OPEN assigns session.
 * Success OPEN/WRITE/DRAIN/STATE returns state; other replies have no payload.
 * Errors have no payload. CLOSE may clear session even with cleanup error.
 * No automatic retry after backend/transport error. Worker stops accepting OPEN
 * after a hardware/backend error; STATE/CLOSE/SHUTDOWN remain available.
 */
#define WDP_MAGIC 0x35504457u
#define WDP_VERSION 1u
#define WDP_REPLY 0x80000000u
#define WDP_OPEN 1u
#define WDP_WRITE 2u
#define WDP_DRAIN 3u
#define WDP_STATE 4u
#define WDP_CLOSE 5u
#define WDP_READY 6u
#define WDP_SHUTDOWN 7u
#define WDP_PROTOCOL -1
#define WDP_FORMAT -2
#define WDP_STATE_ERROR -3
#define WDP_STALE -4
#define WDP_BACKEND -5
#define WDP_STORAGE -6
#define WDP_CLEANUP_UNCERTAIN -117
#define WDP_MAX_FRAMES 8192u
#define WDP_SEALED 1u
#define WDP_FAULTED 2u
#define WDP_DRAIN_ACCEPTED 4u
struct wdp_header {uint32_t magic,version,command,request,session,payload_bytes;int32_t status;uint32_t rate,frames,flags;};
struct wdp_state {uint32_t rate,accepted_low,accepted_high,flags,period_frames,buffer_frames;};
/* Rate is carried to the output backend without a protocol-level whitelist. */
#endif
