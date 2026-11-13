#ifndef WALKDROID_SONY_RECEIVER_PROCESS_H
#define WALKDROID_SONY_RECEIVER_PROCESS_H
#include "sony_audio_receiver_client.h"
#define SONY_RECEIVER_PROCESS_EXPLICIT_START 0x57555231u
#define SONY_RECEIVER_PROCESS_CLEANUP_PENDING -120
#define SONY_RECEIVER_PROCESS_CHILD_LOST -121
struct sony_receiver_process;
struct sony_receiver_process_exit {
    int reaped,exit_code,signal_number,kill_sent,child_lost,launch_stage,launch_errno;
    int route_cleanup_unverified; /* Always1: never releases upstream audio owner. */
};
/* Start a receiver in the supplied root. Device identity, root hashes and
 * ownership/modes are not verified. The activation argument is ignored.
 * The caller supplies its runtime/config mounts. No mount/install/device selection.
 * Fixed private worker/arguments, clean environment, no preload or inherited
 * descriptors. READY loads Sony libraries only; no OPEN/DSP/PCM or auto restart.
 * Nonzero feature selects SourceDirect. Exec/READY/stop each have a deadline.
 *
 * *out startsNULL. One owner for all APIs/client; join users before stop. No
 * outside waitpid or SIGCHLD reaper/auto-reaping. Failed start may retain a
 * cleanup-pending handle: retry stop, do not launch another child first.
 * Caller verifies capture/output/route/ownership cleanup independently. This
 * DSP worker dying does not release a USB/BT receiver's global ownership.
 */
int sony_receiver_process_start(struct sony_receiver_process **out,unsigned activation,
    int root_fd,int log_fd,int feature,int timeout_ms,struct sony_receiver_process_exit *failure);
struct sony_receiver_client *sony_receiver_process_client(struct sony_receiver_process *);
/* Borrowed unreaped child identity; only its existing serialized supervisor
 * may use it. Does not transfer waitpid ownership or grant signal authority. */
int sony_receiver_process_pid(struct sony_receiver_process *);
/* Closes IPC, lets EOF terminate worker, then kills only owned/unreaped child
 * if needed. EBUSY from active client leaves handle/child untouched. -120 keeps
 * handle for later reaping; -121 records lost child ownership, never signals
 * that PID. 0 proves child reaped only, not successful delivery or route cleanup.
 * Graceful use explicitly closes the session and shuts down the client first.
 */
int sony_receiver_process_stop(struct sony_receiver_process **,int timeout_ms,struct sony_receiver_process_exit *);
#endif
