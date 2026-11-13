#ifndef SONY_AUDIO_RECEIVER_IDLE_GUARD_LINUX_H
#define SONY_AUDIO_RECEIVER_IDLE_GUARD_LINUX_H
#include "sony_audio_receiver_idle_guard.h"
struct sony_receiver_idle_guard_linux {struct sony_dsd_power_platform_io verified_base;};
/* Base must be the real fixed-path target/owner/bringup-guard Linux IO for the
 * retained exact generation. Writes ONLY `time -1` or a validated old timer;
 * never writes switch/CPU/DSD/mode or tries any form of suspend. */
int sony_receiver_idle_guard_linux_io(struct sony_receiver_idle_guard_linux *,const struct sony_dsd_power_platform_io *,struct sony_receiver_idle_guard_io *);
#endif
