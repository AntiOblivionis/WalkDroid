#ifndef SONY_AUDIO_PCM_SINK_PROCESS_H
#define SONY_AUDIO_PCM_SINK_PROCESS_H
#include "sony_audio_pcm_sink_client.h"
#define SONY_PCM_SINK_PROCESS_EXPLICIT_START 0x57445035u
#define SONY_PCM_SINK_PROCESS_CLEANUP_PENDING -120
#define SONY_PCM_SINK_PROCESS_CHILD_LOST -121
struct sony_pcm_sink_process;
struct sony_pcm_sink_process_exit {
    int reaped,exit_code,signal_number,kill_sent,child_lost,launch_stage,launch_errno;
    int output_cleanup_unverified; /* Always 1: child exit never certifies hardware. */
};
/* Start a sink in the supplied root. Device identity, root hashes and
 * ownership/modes are not verified. The activation argument is ignored.
 * Caller supplies the runtime/config/device mounts and coordinates the route.
 * Fixed binary/args, IO guard preload and clean environment; no inherited fds.
 * READY success means libraries loaded, no PCM opened. No automatic restart.
 * Timeout bounds exec/handshake/cleanup.
 *
 * *out must start NULL. All APIs/client accesses have ONE owner. Join all client
 * users before stop. No other waitpid/SIGCHLD reaper. Failed start may retain a
 * handle if cleanup is pending; retry stop, never spawn a replacement first.
 * Original Open/Close/forced child exit may leave power/route side effects.
 * This module never claims or performs hardware cleanup, unmute or rerouting.
 */
int sony_pcm_sink_process_start(struct sony_pcm_sink_process **out,unsigned activation,
    int root_fd,int log_fd,int timeout_ms,struct sony_pcm_sink_process_exit *failure);
/* Forward the selected CFW profile for the lifetime of the child. The legacy
 * start above retains its historical base-library selection. */
int sony_pcm_sink_process_start_profile(struct sony_pcm_sink_process **out,unsigned activation,
    int root_fd,int log_fd,int profile,int timeout_ms,struct sony_pcm_sink_process_exit *failure);
struct sony_pcm_sink_client *sony_pcm_sink_process_client(struct sony_pcm_sink_process *);
/* Observational PID of this owned, unreaped child;0 for no live handle. Caller
 * must serialize and retain ownership. Used to bind the kernel PCM owner_pid,
 * never process enumeration or authorization to signal an arbitrary process. */
int sony_pcm_sink_process_pid(const struct sony_pcm_sink_process *);
/* Close IPC, allow EOF cleanup, then SIGKILL only the exact owned unreaped child
 * if necessary. -120 retains handle, -121 records lost ownership without signal.
 * 0 means reaped only. Explicit client Drain/Close/Shutdown precede normal stop.
 * Caller must use a route recovery barrier before any later playback after an
 * error/forced exit; even normal stop is not physical consumption evidence. */
int sony_pcm_sink_process_stop(struct sony_pcm_sink_process **,int timeout_ms,struct sony_pcm_sink_process_exit *);
#endif
