#ifndef WALKDROID_SONY_WORKER_PROCESS_H
#define WALKDROID_SONY_WORKER_PROCESS_H
#include "sony_audio_chain_client.h"
#include <stdint.h>
#define SONY_PROCESS_EXPLICIT_START 0x57535031u
#define SONY_PROCESS_AI_EXPLICIT_START 0x57534131u
#define SONY_PROCESS_AI_GUARDED_EXPLICIT_START 0x57534731u
#define SONY_PROCESS_CLEANUP_PENDING -120
#define SONY_PROCESS_CHILD_LOST -121
struct sony_worker_process;
struct sony_worker_exit {
    int reaped;
    int exit_code; /* -1 unless WIFEXITED. */
    int signal_number;
    int kill_sent;
    int child_lost; /* Another waiter or SIGCHLD disposition broke ownership. */
    int launch_stage;
    int launch_errno;
};
/* Launch a worker in the directory supplied by root_fd; it does not mount,
 * select devices/routes or install anything. root_fd/log_fd are borrowed.
 * No device identity, root ownership, file hash or AI socket type is checked.
 * The activation argument remains for ABI compatibility but is ignored.
 * Parent Android /proc/self/fd must be available for child FD enumeration; no
 * /proc mount is needed inside the private root. It is closed before exec.
 *
 * Success requires WDS3 READY, which
 * loads libraries but does not open DSP/PCM. No shell, inherited environment,
 * inherited descriptors, or automatic restart. Nonzero feature selects 1.
 *
 * *out must initially be NULL; an occupied handle is rejected unchanged (-16).
 * All operations and borrowed client use have ONE owner. No external waitpid,
 * SIGCHLD auto-reaping or concurrent teardown. Caller must stop/join client users
 * before stop. A failed start can return a non-NULL handle ONLY if cleanup is
 * pending; stop must then be retried until reaped. Do not launch a replacement.
 * Callers coordinate their own roots and output ownership.
 */
int sony_worker_process_start(struct sony_worker_process **out,unsigned int activation,
    int root_fd,int log_fd,int feature,int timeout_ms,struct sony_worker_exit *failure);
/* Same ownership contract; profile values are forwarded to the worker. */
int sony_worker_process_start_profile(struct sony_worker_process **out,unsigned int activation,
    int root_fd,int log_fd,int feature,int profile,int timeout_ms,struct sony_worker_exit *failure);
/* Dedicated /bin/sony-output-ai-worker using the supplied fd and nonce.
 * The descriptor is inherited as fd4;
 * no AI WAI command is sent by this launcher. Root/resources and AI process
 * must outlive playback-worker cleanup. Borrowed fd is preserved: caller must
 * close its export copy after this call, including failure. Stop playback
 * before AI; release hardware owner only after both children are reaped.
 * One AI stream per playback process; no auto-retry or fallback to non-AI.
 */
int sony_worker_process_start_profile_ai(struct sony_worker_process **out,unsigned int activation,
    int root_fd,int log_fd,int feature,int profile,int ai_endpoint_fd,uint64_t nonce,
    int timeout_ms,struct sony_worker_exit *failure);
/* Parent owns the coordinated CPU/platform transaction throughout this
 * dedicated /bin/sony-output-ai-guarded-worker lifetime. The worker delegates
 * its native idle/latency intents to that transaction; no native sleep writes.
 * Use sony_ai_hal_prepare/launch/end; this is not an app-facing authorization.
 */
int sony_worker_process_start_profile_ai_guarded(struct sony_worker_process **out,unsigned int activation,
    int root_fd,int log_fd,int feature,int profile,int ai_endpoint_fd,uint64_t nonce,
    int timeout_ms,struct sony_worker_exit *failure);
struct wdc_client *sony_worker_process_client(struct sony_worker_process *);
/* Additive context entry points. Signature -1 preserves legacy argv; 0..3
 * appends explicit CFW context and Sony ship enum 0..7 to playback only.
 * Worker support for these arguments depends on the supplied root.
 * Store reading and active-owner coordination remain
 * caller duties. These calls do not alter the dedicated original AI child. */
int sony_worker_process_start_profile_context(struct sony_worker_process **out,unsigned int activation,
    int root_fd,int log_fd,int feature,int profile,int signature,int ship,int timeout_ms,struct sony_worker_exit *failure);
int sony_worker_process_start_profile_ai_guarded_context(struct sony_worker_process **out,unsigned int activation,
    int root_fd,int log_fd,int feature,int profile,int ai_endpoint_fd,uint64_t nonce,int signature,int ship,
    int timeout_ms,struct sony_worker_exit *failure);
/* Abort boundary, not drain: closes client FDs, lets EOF end worker, then sends
 * SIGKILL only to the still-owned/unreaped child if needed. Bounded wait;
 * -120 retains handle/PID for later reaping, -121 reports lost ownership and
 * never signals the now-unknown PID. 0 means reaped, NOT successful playback.
 * Explicit finish/close/shutdown via client are required for graceful audio EOS.
 */
int sony_worker_process_stop(struct sony_worker_process **,int timeout_ms,struct sony_worker_exit *);
#endif
