#ifndef WALKDROID_SONY_AI_PROCESS_PROTOCOL_H
#define WALKDROID_SONY_AI_PROCESS_PROTOCOL_H
#include <stdint.h>
/* Separate one-shot bootstrap channel. Never consumes a WAI1 request number. */
#define WAI_PROCESS_READY_MAGIC UINT32_C(0x31524157)
#define WAI_PROCESS_LIBRARIES_READY 1u
#define WAI_PROCESS_NNP 1u
#define WAI_PROCESS_NOT_DUMPABLE 2u
struct wai_process_ready {
    uint32_t magic,version,nonce_lo,nonce_hi,pid,uid,euid,gid,egid,stage,flags,reserved;
};
#endif
