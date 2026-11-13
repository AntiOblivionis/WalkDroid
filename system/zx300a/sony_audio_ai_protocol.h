#ifndef WALKDROID_SONY_AI_PROTOCOL_H
#define WALKDROID_SONY_AI_PROTOCOL_H
#include <stdint.h>
#define WAI_MAGIC 0x31494157u
#define WAI_VERSION 1u
#define WAI_REPLY 0x80000000u
#define WAI_OPEN 1u
#define WAI_PROCESS 2u
#define WAI_STATE 3u
#define WAI_SET 4u
#define WAI_FINISH 5u
#define WAI_TAIL_ACK 6u
#define WAI_CLOSE 7u
#define WAI_ABORT 8u
#define WAI_MORE 1u
#define WAI_ALREADY 2u
#define WAI_MAX_BYTES 32768u
/* Measured original f089... profile, stereo float32 PCM at 44.1/48 kHz.
 * Changing the engine/metadata/latency contract requires a new protocol. */
#define WAI_AI_DELAY_FRAMES 6420u
#define WAI_OPENED 1u
#define WAI_STREAMING 2u
#define WAI_ENDED 3u
/* Only fixed-width words and PCM cross the ABI boundary. Never serialize a
 * libc/C++ object, pointer, native sizeof(status), or errno storage address. */
struct wai_header {
    uint32_t magic,version,command,request,nonce_lo,nonce_hi,bytes;
    int32_t status;
    uint32_t frames,rate,flags,index;
};
struct wai_state {
    uint32_t input_rate,output_rate,quantum,enabled,expected,observed,phase,reserved;
    uint32_t source_lo,source_hi,padding_lo,padding_hi,produced_lo,produced_hi,tail_lo,tail_hi;
};
struct wai_packet {struct wai_header h;unsigned char payload[WAI_MAX_BYTES];};
static inline uint64_t wai_join(uint32_t lo,uint32_t hi){return (uint64_t)lo|((uint64_t)hi<<32);}
static inline int wai_rate_valid(uint32_t rate){
    switch(rate){case 8000:case 11025:case 12000:case 16000:case 22050:
    case 24000:case 32000:case 44100:case 48000:case 88200:case 96000:
    case 176400:case 192000:case 352800:case 384000:return 1;default:return 0;}
}
/* Internal shared transport. deadline=-1 is an idle receive that wakes on
 * peer EOF, not a periodic timer. Active operations use absolute deadlines. */
int64_t wai_now_ms(void);
int wai_socket_valid(int fd);
int wai_send(int fd,const struct wai_header *,const void *payload,int64_t deadline);
int wai_receive(int fd,struct wai_packet *,int64_t deadline);
#endif
