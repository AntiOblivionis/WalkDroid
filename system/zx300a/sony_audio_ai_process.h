#ifndef WALKDROID_SONY_AI_PROCESS_H
#define WALKDROID_SONY_AI_PROCESS_H
#include <stdint.h>
#include "sony_audio_owner.h"
#define SONY_AI_PROCESS_EXPLICIT_START UINT32_C(0x57414931)
#define SONY_AI_PROCESS_CLEANUP_PENDING (-120)
struct sony_ai_process;
struct sony_ai_process_exit {
    int libraries_ready,reaped,exit_code,signal_number,kill_sent,child_lost;
    int launch_stage,launch_errno;
    uint64_t audio_generation;
};
/* Android controller on its serialized owner thread. The owner is borrowed
 * and checked through sony_audio_owner_check; this process does not acquire or
 * release the audio lease. The caller keeps the owner alive until stop.
 *
 * The child chroots through root_fd and sends stdout/stderr to log_fd, then
 * executes /bin/sony-ai-worker with a fixed environment. Parent FDs are kept.
 * Mount/network namespace creation is best effort. Credentials are inherited;
 * this code does not set UID/GID, enable no-new-privileges, verify the root
 * tree, or authenticate its readiness packet by nonce, PID or UID.
 *
 * Start waits for exec and a readiness packet of the expected size; this does
 * not establish library contents, AI initialization or hardware readiness.
 * Nonpositive timeouts default to 3000 ms. A failed start may retain *out if
 * cleanup is pending. Only one instance is started per controller process.
 */
int sony_ai_process_start(struct sony_ai_process **out,unsigned activation,
    struct sony_audio_owner *borrowed_owner,int root_fd,int log_fd,uint64_t nonce,
    int timeout_ms,struct sony_ai_process_exit *failure);
/* Initially *fd=-1. Success returns an owned CLOEXEC socket and the nonce;
 * the caller chooses where to transfer it. The launcher keeps a revocation
 * copy but never sends protocol commands. Export does not prove stream readiness.
 */
int sony_ai_process_export(struct sony_ai_process *,int *fd,uint64_t *nonce);
int sony_ai_process_pid(const struct sony_ai_process *); /* observation only */
/* Caller closes/finishes its WAI client first for graceful stop. This is the
 * abort boundary: revoke all copies with shutdown, close own socket, bounded
 * wait then SIGKILL only the still-owned/unreaped child. 0 means reaped only;
 * inspect exit_code/kill_sent, never infer audio hardware cleanup or playback.
 * Other waiters/SIGCHLD auto-reaping can cause ECHILD. Pending stop retains
 * the handle; ECHILD is reported and releases the local instance slot.
 */
int sony_ai_process_stop(struct sony_ai_process **,int timeout_ms,struct sony_ai_process_exit *);
#endif
