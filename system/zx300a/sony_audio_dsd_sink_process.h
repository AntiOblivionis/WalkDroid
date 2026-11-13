#ifndef WALKDROID_SONY_DSD_SINK_PROCESS_H
#define WALKDROID_SONY_DSD_SINK_PROCESS_H
#include "sony_audio_dsd_sink_client.h"
#include "sony_audio_dsd_power_wire.h"
#define SONY_DSD_SINK_PROCESS_EXPLICIT_START 0x57444e31u
#define SONY_DSD_SINK_PROCESS_CLEANUP_PENDING -120
#define SONY_DSD_SINK_PROCESS_CHILD_LOST -121
struct sony_dsd_sink_process;
struct sony_dsd_sink_process_scope {
 uint64_t session,generation,receipt;
 unsigned cfw_profile,low_latency;
 struct sony_dsd_power_backend backend;
 /* Retained cleanup ownership, NOT user-facing new-playback admission.
  * Keep this exact generation and backend alive until stop succeeds. */
 int (*owner_check)(void *);
 void *context;
};
struct sony_dsd_sink_process_exit {
 int spawned_pid,reaped,exit_code,signal_number,kill_sent,child_lost,launch_stage,launch_errno;
 int power_cleanup_confirmed,power_cleanup_status;
 int output_cleanup_unverified; /* Always 1: no DAC/global route proof. */
 struct sony_dsd_power_wire_state power;
};
/* Root Android parent, verified ZX300A and build-pinned read-only native root.
 * Fixed worker/args/environment; no mount, device choice, route/unmute or new
 * global lease. WDN1 data and WKP7 resources have one serialized controller.
 * Worker gets only stdin/out/log + exact power socket; parent verifies READY
 * and target after exec. Borrowed root/log/backend untouched on success.
 * *out starts NULL. Any non-NULL result after failure MUST be stopped; never
 * overwrite/restart while exact child/resource cleanup remains pending.
 * No other waitpid/SIGCHLD reaper or simultaneous client users are permitted.
 */
int sony_dsd_sink_process_start(struct sony_dsd_sink_process **,unsigned activation,int root_fd,int log_fd,
 const struct sony_dsd_sink_process_scope *,int timeout_ms,struct sony_dsd_sink_process_exit *);
struct sony_dsd_sink_client *sony_dsd_sink_process_client(struct sony_dsd_sink_process *);
int sony_dsd_sink_process_state(struct sony_dsd_sink_process *,struct sony_dsd_sink_process_exit *);
/* Close data pipes, continue servicing EOF-triggered native Close/Release,
 * bounded exact-child reap/kill, then independent closed-resource compensation.
 * A lost child or uncertain release retains the object/recovery barrier; no
 * signal to an already-reaped PID and no second attempt at uncertain release.
 * Success proves this child/resource receipt cleanup, not playback success,
 * DAC drain/route silence or global owner CLEAN. Graceful client Close/Shutdown
 * precede normal stop. Pending handles allow only serialized stop/state.
 */
int sony_dsd_sink_process_stop(struct sony_dsd_sink_process **,int timeout_ms,struct sony_dsd_sink_process_exit *);
#endif
