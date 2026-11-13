#ifndef WALKDROID_SONY_AI_HAL_H
#define WALKDROID_SONY_AI_HAL_H
#include "sony_audio_ai_process.h"
#include "sony_audio_worker_process.h"
struct sony_ai_hal;
/* Open the default AI runtime directory and log. These functions do not
 * verify file contents, prevent symlink traversal or make the tree immutable. */
int sony_ai_hal_root_open(void);
int sony_ai_hal_log_open(void);
/* Borrow owner until end succeeds and consumes the handle.
 * playback_slot is the caller's stable ownership slot, initially NULL. It must
 * outlive this context and is checked before resource restoration. Root/log
 * are borrowed and preserved. Prepare occurs BEFORE route_begin, with all PCM
 * closed: governor lease -> original platform snapshot/2 CPUs/1.3GHz budget.
 * It does not open audio or remove the paused-suspend guard. Failed prepare may
 * retain a handle for cleanup; it must not be replaced or freed by the caller.
 */
int sony_ai_hal_prepare(struct sony_ai_hal **,struct sony_audio_owner *,
    struct sony_worker_process **playback_slot,int ai_root,int ai_log);
/* Once, after route is parked muted. No stream OPEN yet. Caller continues with
 * normal WDS3 adapter OPEN; the raw WAI endpoint is passed only to that worker.
 * Both children remain in their audio-controller process group; this module
 * never moves audioserver or another task between cgroups.
 */
int sony_ai_hal_launch(struct sony_ai_hal *,int output_root,int output_log,int profile,
    uint64_t fresh_nonce,int timeout_ms,struct sony_worker_exit *);
int sony_ai_hal_launch_context(struct sony_ai_hal *,int output_root,int output_log,int profile,
    int signature,int ship_enum,uint64_t fresh_nonce,int timeout_ms,struct sony_worker_exit *);
/* Finish playback via the registered slot, then stop AI and request restoration
 * of the saved platform settings. The borrowed audio owner is not released.
 * Cleanup errors are returned; a failed restoration can be retried by the caller.
 */
int sony_ai_hal_end(struct sony_ai_hal **,int timeout_ms);
/* Same operation, with the actual AI waitpid/launch receipt copied before
 * context consumption or retained failure. No inferred hardware readiness. */
int sony_ai_hal_end_report(struct sony_ai_hal **,int timeout_ms,struct sony_ai_process_exit *);
#endif
