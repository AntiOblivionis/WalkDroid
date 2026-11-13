#ifndef SONY_AUDIO_DSD_FADE_BRIDGE_H
#define SONY_AUDIO_DSD_FADE_BRIDGE_H

struct sony_dsd_fade_bridge;
#define SONY_DSD_FADE_START 1u
#define SONY_DSD_FADE_END 2u

/* ARM32/glibc bridge to internal functions of the caller-provided library.
 * Keep the handle alive through close and serialize calls. Function offsets
 * default to those used by the original implementation and may be supplied by
 * WALKDROID_DSD_FADE_{CONSTRUCT,INITIALIZE,CONFIGURE,PROCESS,DESTROY}.
 * Wrong offsets or an incompatible ABI can crash or corrupt the worker.
 */
int sony_dsd_fade_open(struct sony_dsd_fade_bridge **, void *audited_library,
                       unsigned int bitrate);
/* Forwards nonempty packets up to 32768 bytes. Returns the library-reported
 * output length, limited to the internal buffer; native errors fault the
 * context. END does not send track::eos or establish DSP/ALSA drain.
 */
int sony_dsd_fade_process(struct sony_dsd_fade_bridge *, const unsigned char *,
    unsigned int bytes, unsigned int flags, unsigned char *output,
    unsigned int capacity, unsigned int *written);
int sony_dsd_fade_close(struct sony_dsd_fade_bridge *);

#endif
