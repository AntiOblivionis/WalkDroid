#ifndef SONY_AUDIO_DSD_POWER_LINUX_H
#define SONY_AUDIO_DSD_POWER_LINUX_H
#include "sony_audio_dsd_power_platform.h"
struct sony_dsd_power_linux;
/* Android parent-side sysfs and mixer adapter. It does not verify device,
 * process, wake lock or lease identity; the retained callback is unused. */
int sony_dsd_power_linux_create(struct sony_dsd_power_linux **,int (*lease_check)(void *),void *);
int sony_dsd_power_linux_io(struct sony_dsd_power_linux *,struct sony_dsd_power_platform_io *);
int sony_dsd_power_linux_inspect(struct sony_dsd_power_linux *,unsigned values[SONY_DSD_RESOURCE_COUNT]);
/* Compatibility entry points; success is not a hardware observation. */
int sony_dsd_power_linux_observation_begin(void *);
int sony_dsd_power_linux_observation_end(void *);
int sony_dsd_power_linux_destroy(struct sony_dsd_power_linux **);
#endif
