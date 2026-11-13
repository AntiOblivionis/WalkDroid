#ifndef SONY_AUDIO_DSD_POWER_PLATFORM_H
#define SONY_AUDIO_DSD_POWER_PLATFORM_H
#include "sony_audio_dsd_power_bridge.h"
enum sony_dsd_power_resource {SONY_DSD_CPU_BASE,SONY_DSD_CPU_KHZ,SONY_DSD_DEEP_IDLE,
    SONY_DSD_MUTE64,SONY_DSD_MUTE128,SONY_DSD_MUTE256,SONY_DSD_CPU_MAX_KHZ,SONY_DSD_PLAYBACK_LATENCY,SONY_DSD_RESOURCE_COUNT};
struct sony_dsd_power_platform_io {
    /* Retained for source compatibility; the platform does not invoke it. */
    int (*check)(void *);
    int (*read)(void *,enum sony_dsd_power_resource,unsigned *);
    int (*write)(void *,enum sony_dsd_power_resource,unsigned);
    int (*online_two)(void *); /* Bounded wait/readback, no direct CPU writes. */
    int (*output_closed)(void *);
    void *context;
};
struct sony_dsd_power_platform;
struct sony_dsd_power_platform_state {
    uint64_t token;
    unsigned previous[SONY_DSD_RESOURCE_COUNT],requested[SONY_DSD_RESOURCE_COUNT],write_attempted_mask,restore_attempted_mask;
    unsigned acquired,release_attempted,restored;
    int first_error;
};
/* Single transaction: read previous values, apply the caller's plan, then
 * request restoration. Resource values and restore writes are not read back;
 * concurrent external changes can be overwritten during restoration. */
int sony_dsd_power_platform_create(struct sony_dsd_power_platform **,
    const struct sony_dsd_power_platform_io *,uint64_t owner_receipt);
int sony_dsd_power_platform_backend(struct sony_dsd_power_platform *,struct sony_dsd_power_backend *);
int sony_dsd_power_platform_state(struct sony_dsd_power_platform *,struct sony_dsd_power_platform_state *);
int sony_dsd_power_platform_destroy(struct sony_dsd_power_platform **);
#endif
