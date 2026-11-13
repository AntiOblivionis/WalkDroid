#ifndef WALKDROID_SONY_DSD_SINK_PROTOCOL_H
#define WALKDROID_SONY_DSD_SINK_PROTOCOL_H
#include <stdint.h>
/* WDN1: private already-processed native DSD_U8 stereo, never PCM or DoP.
 * All words little endian. Header shape resembles WDP, but MAGIC, units and
 * geometry differ: accepted is BYTES, bit_rate is per-channel DSD bits/sec.
 * The caller supplies the rate and data; request/session metadata is not an
 * admission condition. A new OPEN closes any existing sink first. If a WRITE
 * declares more bytes than supplied, only the received portion is forwarded.
 * Successful OPEN/WRITE/DRAIN/STATE return the available state snapshot.
 * Native admission/Drain ACK does not prove DAC/I2S/acoustic consumption.
 * Power acquisition/restoration is a SEPARATE WKP7 channel; parent services it
 * while awaiting data replies, then verifies exact child reap/physical idle.
 */
#define WDN_MAGIC 0x314e4457u
#define WDN_VERSION 1u
#define WDN_REPLY 0x80000000u
#define WDN_OPEN 1u
#define WDN_WRITE 2u
#define WDN_DRAIN 3u
#define WDN_STATE 4u
#define WDN_CLOSE 5u
#define WDN_READY 6u
#define WDN_SHUTDOWN 7u
#define WDN_PROTOCOL -1
#define WDN_FORMAT -2
#define WDN_STATE_ERROR -3
#define WDN_STALE -4
#define WDN_BACKEND -5
#define WDN_CLEANUP_UNCERTAIN -117
#define WDN_MAX_BYTES 32768u
#define WDN_BLOCK_BYTES 8192u
#define WDN_SEALED 1u
#define WDN_FAULTED 2u
#define WDN_DRAIN_ACCEPTED 4u
struct wdn_header {uint32_t magic,version,command,request,session,payload_bytes;int32_t status;uint32_t bit_rate,bytes,flags;};
struct wdn_state {uint32_t bit_rate,accepted_low,accepted_high,flags,alsa_rate,period_frames,buffer_frames,channels,format,access,bytes_per_write,physical_width;};
static inline int wdn_rate_valid(unsigned r){return r==2822400||r==5644800||r==11289600;}
static inline int wdn_geometry_valid(const struct wdn_state *s){
 return wdn_rate_valid(s->bit_rate)&&s->alsa_rate==s->bit_rate/32&&s->channels==2&&s->format==48&&s->access==3&&
  s->bytes_per_write==8192&&s->physical_width==8&&s->period_frames&&s->period_frames<=s->buffer_frames&&s->buffer_frames<=s->alsa_rate*2;
}
#endif
