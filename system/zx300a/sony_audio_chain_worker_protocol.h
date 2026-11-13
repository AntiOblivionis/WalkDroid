#ifndef WALKDROID_SONY_CHAIN_WORKER_PROTOCOL_H
#define WALKDROID_SONY_CHAIN_WORKER_PROTOCOL_H
#include "sony_audio_worker_protocol.h"
/* Reuse only the 48-byte header/status/command numbers, never v1 payloads. */
#define CHAIN_MAGIC 0x32434457u /* WDC2 */
#define CHAIN_VERSION 2u
#define CHAIN_TAIL 8u
#define CHAIN_EOS 9u
/* WDA_STATE (4) returns exactly 68-byte sony_chain_settings for accepted state,
 * with unchanged session/revision. Not native/hardware settling evidence. */
#define CHAIN_SAVE 10u /* Snapshot-enabled builds: save accepted effect controls. */
#define CHAIN_TONE_UPDATE 11u /* 28 bytes sony_tone_settings, selects EqTone. */
#define CHAIN_TONE_STATE 12u /* 32 bytes: selector (2/3), then tone settings. */
#define CHAIN_PROCESS_EXPANDED 13u /* capacity_frames u32 + 4096-byte PCM. */
#define CHAIN_MAX_EXPANDED_FRAMES 10240u /* bounded by worker response buffer. */
#define CHAIN_TAIL_EXPANDED 14u /* ms u32 + capacity_frames u32. */
#define CHAIN_MAX_EXPANDED_TAIL_FRAMES 40960u
#define CHAIN_STORAGE_UNCERTAIN -7 /* Rename visible, directory fsync unconfirmed. */
#endif
