#ifndef WALKDROID_SONY_PCM_FORMAT_H
#define WALKDROID_SONY_PCM_FORMAT_H
/* PCM packet sizes follow the existing rate bands. A band selects buffer
 * geometry; it does not establish support in the downstream DSP or hardware.
 */
#define SONY_PCM_MAX_SOURCE_FRAMES 4096u
static inline unsigned int sony_pcm_source_frames(unsigned int rate){
    return rate<=48000?512u:rate<=96000?1024u:rate<=192000?2048u:4096u;
}
static inline unsigned int sony_pcm_effective_rate(unsigned int rate,int direct,int dsee){
    if(direct)return rate;
    if(dsee&&(rate==44100||rate==48000))return rate*4u;
    return rate>192000?rate/2u:rate;
}
static inline unsigned int sony_pcm_tail_packet_bytes(unsigned int rate){
    return sony_pcm_source_frames(rate)*32u;
}
static inline unsigned int sony_pcm_tail_packets(unsigned int rate,unsigned int ms){
    unsigned int quantum=sony_pcm_source_frames(rate);
    return quantum?(unsigned int)((unsigned long long)ms*rate/(quantum*4000u)):0;
}
#endif
