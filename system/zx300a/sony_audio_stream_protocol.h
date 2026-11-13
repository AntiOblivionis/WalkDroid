#ifndef WALKDROID_SONY_STREAM_PROTOCOL_H
#define WALKDROID_SONY_STREAM_PROTOCOL_H
#include "sony_audio_worker_protocol.h"
#define STREAM_MAGIC 0x33534457u /* WDS3, native output acceptance, not PCM return. */
#define STREAM_VERSION 2u
#define STREAM_PROCESS 15u
#define STREAM_FINISH 16u
#define STREAM_STATE 17u
#define STREAM_READY 18u /* Empty/closed-session readiness ACK. No DSP/hardware open. */
#define STREAM_TIMING 19u /* 16-byte sony_output_timing; original STATE stays 32 bytes. */
#define STREAM_OUTPUT_UPDATE 20u /* 20 bytes sony_output_controls, increments revision. */
#define STREAM_OUTPUT_STATE 21u /* Accepted output context, same revision; not hardware ACK. */
#define STREAM_DSEE_UPDATE 22u /* 8 bytes sony_dsee_controls, increments revision. */
#define STREAM_DSEE_STATE 23u /* Accepted settings only; PCM settling is separate. */
#define STREAM_VOLUME_MEMORY_UPDATE 24u /* 8-byte original AVLS/last-headphone memory, staged only. */
#define STREAM_VOLUME_MEMORY_STATE 25u /* Same session/revision, no hardware acknowledgement. */
#define STREAM_NORMALIZER_UPDATE 26u /* int32 bool; accepted request increments revision. */
#define STREAM_NORMALIZER_STATE 27u /* int32 accepted setting; not processed PCM confirmation. */
#define STREAM_PRESET_COMMIT 28u /* op/slot u32 plus snapshot-shaped payload. */
#define STREAM_CLEANUP_UNCERTAIN -117
/* WDS3 OPEN carries the requested rate. PROCESS carries the session's
 * 512/1024/2048/4096-frame source quantum selected by the rate band.
 * Existing 44.1/48k messages remain byte compatible. Backend support varies. */
#endif
