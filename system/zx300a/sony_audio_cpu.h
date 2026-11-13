#ifndef WALKDROID_SONY_AUDIO_CPU_H
#define WALKDROID_SONY_AUDIO_CPU_H
#include <stdint.h>
struct sony_audio_cpu;
/* File locks coordinate participating audio and brightness users. The status
 * record, process ID, file ownership, governor readback and wakelock state are
 * not admission conditions. Operating-system permissions still apply. */
int sony_audio_cpu_prepare(void);
int sony_audio_cpu_acquire(struct sony_audio_cpu **,uint64_t generation,
    int (*check)(void *),int (*output_closed)(void *),void *context);
int sony_audio_cpu_check(struct sony_audio_cpu *);
/* Brightness selects the requested governor on release; absent brightness
 * falls back to hotplug. I/O errors are returned, without hardware readback. */
int sony_audio_cpu_release(struct sony_audio_cpu **);
int sony_audio_cpu_backlight(unsigned brightness);
#endif
