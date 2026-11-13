#ifndef SONY_AUDIO_DSF_WORKER_PROTOCOL_H
#define SONY_AUDIO_DSF_WORKER_PROTOCOL_H
/* Separate little-endian ARM32 protocol. No pointers, native structs with
 * 64-bit alignment, filenames, device nodes or libc objects cross the pipe.
 * The supervisor supplies a source and output route. Output ACK is admission
 * by that supervisor, never a hardware drain claim.
 */
#include <stdint.h>
#include "sony_audio_chain_bridge.h"
#define WDF_MAGIC 0x34464457u /* W D F 4 */
#define WDF_VERSION 1u
#define WDF_REPLY 0x80000000u
#define WDF_OPEN 1u
#define WDF_STEP 2u
#define WDF_STATE 3u
#define WDF_CLOSE 4u
#define WDF_SHUTDOWN 5u
#define WDF_READY 6u
#define WDF_OPEN_TERMINAL 7u
#define WDF_CONTROLS_GET 8u
#define WDF_CONTROLS_STAGE 9u
#define WDF_CONTROLS_COMMIT 10u
#define WDF_READ 0x100u
#define WDF_SAMPLES 0x101u
#define WDF_EOS 0x102u
#define WDF_SAMPLES_TAIL 1u
#define WDF_EOS_SONY_FINISHED 1u
#define WDF_PROTOCOL -1
#define WDF_FORMAT -2
#define WDF_STATE_ERROR -3
#define WDF_STALE -4
#define WDF_BACKEND -5
#define WDF_BUSY -6
#define WDF_CANCELLED -125
#define WDF_STATE_OPEN 1u
#define WDF_STATE_FAULTED 2u
#define WDF_STATE_INPUT_FINISHED 4u
#define WDF_OPEN_WORDS 27u
#define WDF_OPEN_TERMINAL_WORDS 28u
#define WDF_MAX_READ 32768u
#define WDF_MAX_SAMPLES 131072u
/* GET has no payload. STAGE carries a revision and controls; COMMIT carries
 * the revision and stage request fields. These identifiers are informational.
 * A successful STAGE may return pending controls until the next sample packet
 * has been delivered. The subsequent state reflects accepted commands, not a
 * hardware readback or proof of audible output. Bounded framing remains:
 * STAGE 124 bytes; COMMIT 8; reply 132.
 */
struct wdf_controls {
    uint32_t revision,pending,stage_request;
    struct sony_dsd_live_controls controls;
};
struct wdf_controls_stage {uint32_t revision;struct sony_dsd_live_controls controls;};
struct wdf_controls_commit {uint32_t revision,stage_request;};
struct wdf_header {
    uint32_t magic,version,command,request,session,sequence,payload_bytes;
    int32_t status;
    uint32_t arg0,arg1,flags,reserved;
};
/* OPEN carries settings[17], DSD controls[3], output controls[5] and a file
 * size; OPEN_TERMINAL also carries latency. Missing payload words are zeroed.
 * READ carries a byte offset and requested length; its response may be short.
 * SAMPLES carries rate, format and sample bytes; oversized data is truncated
 * to the receiver buffer. EOS signals source completion, not hardware drain.
 * Request/session/sequence fields are informational. Reopening closes the old
 * source first; callers must coordinate concurrent access and interruptions.
 */
struct wdf_state {
    uint32_t input_rate,source_channels,output_rate,output_format;
    uint32_t canonical_accepted_low,canonical_accepted_high;
    uint32_t output_acked_low,output_acked_high;
    uint32_t packets_acked_low,packets_acked_high;
    uint32_t preroll_accepted,flags;
};
#endif
