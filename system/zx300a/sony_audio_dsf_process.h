#ifndef SONY_AUDIO_DSF_PROCESS_H
#define SONY_AUDIO_DSF_PROCESS_H
#include "sony_audio_dsf_client.h"
#define SONY_DSF_PROCESS_EXPLICIT_START 0x57444634u
#define SONY_DSF_PROCESS_CLEANUP_PENDING -120
#define SONY_DSF_PROCESS_CHILD_LOST -121
struct sony_dsf_process;
struct sony_dsf_process_exit {
    int reaped,exit_code,signal_number,kill_sent,child_lost,launch_stage,launch_errno;
};
/* Start a DSF worker in the supplied root. Device identity, root ownership and
 * file hashes are not checked. The activation argument is ignored.
 * Borrowed root/log fds are unmodified. Fixed executable/config path, clean env,
 * no inherited descriptors. No mounting, package verification, route selection
 * or persistence is performed here. /proc/self/fd must exist outside the root.
 * Success requires WDF4 READY: libraries loaded, no media/DSP/output open.
 *
 * *out must be NULL. All APIs and the borrowed client have ONE owner. Stop/join
 * client users before stop. No other waitpid/SIGCHLD auto-reaper is allowed.
 * Failed start can retain a handle only when cleanup is pending; retry stop,
 * never spawn a replacement until the owned child has been reaped. No restart.
 */
int sony_dsf_process_start(struct sony_dsf_process **out,unsigned activation,
    int root_fd,int log_fd,int timeout_ms,struct sony_dsf_process_exit *failure);
struct sony_dsf_client *sony_dsf_process_client(struct sony_dsf_process *);
/* Abort boundary: close pipes, allow EOF cleanup, then kill only the exact
 * still-owned unreaped child within a bounded wait. -120 retains ownership;
 * -121 records lost child ownership and never signals that PID. 0 means reaped,
 * not audio completion. Use explicit client CLOSE/SHUTDOWN for graceful exit. */
int sony_dsf_process_stop(struct sony_dsf_process **,int timeout_ms,struct sony_dsf_process_exit *);
#endif
