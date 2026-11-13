#ifndef WALKDROID_SONY_AUDIO_WORKER_PROTOCOL_H
#define WALKDROID_SONY_AUDIO_WORKER_PROTOCOL_H
/* Little-endian wire format; no C++/libc objects cross this boundary.
 * This v1 endpoint is EQ processing only, not a hardware route or full chain.
 */
#define WDA_MAGIC 0x55414457u /* bytes W D A U */
#define WDA_VERSION 1u
#define WDA_REPLY 0x80000000u
#define WDA_OPEN 1u
#define WDA_UPDATE 2u
#define WDA_PROCESS 3u
#define WDA_STATE 4u
#define WDA_CLOSE 5u
#define WDA_SHUTDOWN 6u
#define WDA_CONTROLS 7u /* Native filter readback; not physical hardware state. */
#define WDA_OK 0
#define WDA_PROTOCOL -1
#define WDA_FORMAT -2
#define WDA_STATE_ERROR -3
#define WDA_STALE -4
#define WDA_BACKEND -5
#define WDA_STORAGE -6

struct wda_header {
    unsigned int magic, version, command, request_id, session, revision;
    unsigned int payload_bytes;
    int status;
    unsigned int frames, sample_rate, channels, reserved;
};
#endif
