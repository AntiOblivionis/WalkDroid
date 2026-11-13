#ifndef WALKDROID_SONY_RECEIVER_WORKER_PROTOCOL_H
#define WALKDROID_SONY_RECEIVER_WORKER_PROTOCOL_H
#include <stdint.h>
#include "sony_audio_chain_bridge.h"
/* WUR1, little-endian ARM32. Dedicated serialized DSP worker only: no capture,
 * device, owner, route, persistence or hardware completion is provided.
 * Request/session fields associate operations and replies. Protocol version,
 * magic and reserved fields do not authenticate or gate the connection. */
#define WUR_MAGIC 0x31525557u
#define WUR_VERSION 1u
#define WUR_REPLY 0x80000000u
#define WUR_OPEN 1u
#define WUR_PROCESS 2u
#define WUR_STATE 3u
#define WUR_CLOSE 4u
#define WUR_SHUTDOWN 5u
#define WUR_READY 6u
#define WUR_SILENCE 7u
#define WUR_SONY_STOP 8u
#define WUR_PROCESS_FINAL 9u
/* Additive explicit original USB service lifetime. Legacy OPEN remains raw. */
#define WUR_OPEN_SERVICE 10u
#define WUR_SERVICE_PROCESS 11u
#define WUR_SERVICE_FINAL 12u
#define WUR_SERVICE_FINISH 13u
#define WUR_VOLUME 14u
#define WUR_OPEN_BT_SERVICE 15u
#define WUR_SAMPLES 0x100u
#define WUR_TAIL 1u
#define WUR_PROTOCOL -1
#define WUR_FORMAT -2
#define WUR_STATE_ERROR -3
#define WUR_STALE -4
#define WUR_BACKEND -5
#define WUR_STORAGE -6
#define WUR_CANCELLED -125
#define WUR_FLAG_OPEN 1u
#define WUR_FLAG_FAULTED 2u
#define WUR_FLAG_INPUT_SEALED 4u
#define WUR_FLAG_PADDING_USED 8u
#define WUR_FLAG_STOPPED 16u
#define WUR_FLAG_SERVICE 32u
#define WUR_FLAG_SERVICE_STOP 64u
#define WUR_FLAG_SERVICE_DRAIN 128u
#define WUR_FLAG_BT_SERVICE 256u
#define WUR_SERVICE_STOP 2u
#define WUR_SERVICE_DRAIN 4u
#define WUR_MAX_INPUT 4096u
#define WUR_MAX_OUTPUT 131072u
struct wur_header {
    uint32_t magic,version,command,request,session,sequence,payload_bytes;
    int32_t status;
    uint32_t arg0,arg1,flags,reserved;
};
struct wur_open {
    uint32_t track[25];
    struct sony_chain_settings settings;
    struct sony_tone_settings tone;
    struct sony_dsee_controls dsee;
    struct sony_output_controls output;
    uint32_t selector; /* 2=EQ10,3=tone. Includes inactive tone bank. */
};
struct wur_state {
    uint32_t flags,input_processed_low,input_processed_high;
    uint32_t output_acked_low,output_acked_high,packets_acked_low,packets_acked_high;
    uint32_t output_rate,output_bits,output_container_bytes;
    uint32_t last_request,last_event_sequence,padding_ms,padding_acked_bytes;
    uint32_t stop_accepted,reserved; /* Service only: reserved=pending frames. */
};
/* Request, session and event fields are carried for observation rather than
 * used to authenticate or select a lifetime. OPEN attempts to release an
 * existing chain before opening another. READY and SHUTDOWN are available
 * during a lifetime; shutdown releases the chain as the worker exits.
 *
 * Request bodies are read into a bounded buffer: missing bytes are zero-filled
 * and extra bytes are consumed without processing. PROCESS derives a whole
 * frame count from the received bytes and opening container width, discarding
 * an incomplete trailing frame. In USB service mode, the newest packet is held
 * and the preceding packet is sent to the native service. Bluetooth service
 * sends packets immediately. FINISH flushes held data and calls native terminal
 * operations; repeated finish after stop returns the recorded state.
 *
 * SAMPLES carries native metadata followed by data. The worker accepts ACK
 * fields without comparing them to the event, consumes any ACK payload, and
 * treats a nonzero ACK status or transport error as a delivery failure. Reply
 * state is diagnostic and does not prove hardware playback or cleanup.
 *
 * The native chain and configuration implementation may still reject formats,
 * settings, prior volume, or states. This protocol adapter does not establish
 * that arbitrary input is supported; truncated or inconsistent metadata may
 * lead to lost or incorrectly interpreted audio. The caller is responsible for
 * coordinating the physical output and resource lifecycle.
 */
#endif
