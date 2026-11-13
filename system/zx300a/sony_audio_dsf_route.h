#ifndef WALKDROID_SONY_DSF_ROUTE_H
#define WALKDROID_SONY_DSF_ROUTE_H
#include "sony_audio_controls.h"
#include "sony_audio_dsf_playback.h"
#include "sony_audio_dsf_process.h"
#include "sony_audio_pcm_sink_process.h"
#include "sony_audio_owner_commands.h"
#include "sony_audio_dsf_native_playback.h"
#include "sony_audio_dsd_sink_process.h"
struct sony_dsf_route;
#define SONY_DSF_ROUTE_EXPLICIT_START 0x44535231u
struct sony_dsf_route_binding {
    struct sony_controls controls;
    struct sony_dsd_controls dsd;
    uint64_t generation;
};
/* Hardware-capable, single serialized controller. All output/config writers,
 * including the running HAL, MUST already participate in sony_audio_owner.
 * Never use alongside B68's old HAL. No deployment, supervisor launch, source
 * selection, recovery or Android app policy is performed by this component.
 * Acquires global ownership, reads actual jacks/config, prepares muted route,
 * and saves only the original output-transition volume/lastHeadphone fields.
 * Native BTL DSD is rejected, never silently converted. *out starts NULL;
 * failures after allocation retain it for finish, before launching children. */
int sony_dsf_route_prepare(struct sony_dsf_route **,unsigned activation,const struct sony_dsd_controls *);
/* Broker's format decision must still match the real jack under this global
 * lease, before any route/config writes. Conversion mode2 only; expected is
 * SONY_OUTPUT_SE or BTL. A changed jack is refused, never silently downgraded. */
int sony_dsf_route_pcm_prepare_output(struct sony_dsf_route **,unsigned activation,
    const struct sony_dsd_controls *,unsigned expected_output);
/* Bootstrap a NEW root-owned private config directory (0700), current.bin
 * absent. Snapshot is copied exactly after volume planning. Caller must bind
 * this exact directory at /data/config of both verified worker roots; do not
 * replace it or its current.bin during the session. No profile defaults copied:
 * use binding.controls.cfw_profile as the explicit output supervisor argument. */
int sony_dsf_route_export_config(struct sony_dsf_route *,const char *directory);
/* Call before starting either worker. Checks retained directory/file identity,
 * produces real route callbacks, and arms mandatory cleanup receipts. Both
 * workers must use this binding/config; don't attach them to another owner.
 * Failure before arm allows finish with all receipts NULL (no child launched).
 * After arm, partial launch/uncertain cleanup conservatively leaves ACTIVE. */
int sony_dsf_route_arm(struct sony_dsf_route *,struct sony_dsf_route_binding *,struct sony_dsf_playback_owner *);
/* Optional root-only commands for this exact owner. Controller owns the server,
 * serializes poll with playback and closes it BEFORE route_finish. */
int sony_dsf_route_commands_open(struct sony_dsf_route *,struct sony_command_server **,const struct sony_command_ops *);
/* Live processing/hardware-volume transaction, single controller. Receipts come
 * from this route's borrowed playback coordinator, never another worker.
 * check binds initial actual controls to this route; later receipts must match
 * its current revision/request or exact pending stage. Errors in native/route
 * identity seal/quiesce. Caller generation/revision/invalid-target errors in
 * begin are rejected before side effects and do not poison healthy playback.
 * begin: 1 same-state no-op, 0 admitted, negative error. A new target
 * is fully encoded/validated against the retained global snapshot first.
 * Output is normalized to preserve inactive tone. Direct/DSD mode remain fixed.
 * Master volume is prechecked against original AVLS policy; an unclamped value
 * is rejected before mutation. Pure volume keeps PCM/unmute running, applies
 * and reads back DAC volume and saves BEFORE native notification, following
 * the original main-path order. Native real-sample ACK plus final hardware/save
 * recheck is mandatory before publication; failure quiesces, not rollback.
 * Other/mixed DSP changes are MUTED and require output retirement as before.
 * The native volume getter does not supply an authoritative confirmation.
 * staged binds the exact STAGE request id. commit requires native confirmation
 * after actual sink ACK, performs full snapshot CAS/readback, updates hardware
 * effects, unmutes, then publishes actual state. All failures after admission
 * quiesce. No private bootstrap file replacement or native Set retry occurs.
 * Changed DSD fields alone are persisted; unrelated explicit session overrides
 * must not silently overwrite stored values. Caller closes on any sealed error.
 */
int sony_dsf_route_live_check(struct sony_dsf_route *,const struct wdf_controls *);
int sony_dsf_route_live_begin(struct sony_dsf_route *,uint64_t generation,unsigned revision,
    const struct wdf_controls *base,const struct sony_dsd_live_controls *target,struct sony_dsd_live_controls *normalized);
int sony_dsf_route_live_staged(struct sony_dsf_route *,const struct wdf_controls *);
int sony_dsf_route_live_is_volume(const struct sony_dsf_route *);
/* Only this route's confirmed pure-volume receipt. output_ready is 0 after
 * normal EOF/drain; it must not republish or reopen an ended output. */
int sony_dsf_route_live_commit_volume(struct sony_dsf_route *,const struct wdf_controls *,int output_ready);
/* Called by playback only after successful CLOSE; verifies independent PCM idle
 * while retaining mute and the exact pending route. Required before commit. */
int sony_dsf_route_live_output_closed(void *);
int sony_dsf_route_live_commit(struct sony_dsf_route *,const struct wdf_controls *,const struct sony_dsf_output_retirement *);
int sony_dsf_route_live_fail(struct sony_dsf_route *,int error);
/* First close playback, then SHUTDOWN and reap BOTH owned workers. Pass their
 * actual receipts, never fabricated zeros. Only normal close/natural exit plus
 * independent mute/off/all-PCM-closed can clear ACTIVE. Cancellation/unplug can
 * close cleanly; IO/config/owner uncertainty remains latched. Missing/failed
 * receipts release the file descriptors but retain ACTIVE, returning EUCLEAN.
 * Join all callback users first. NULL receipts are valid only before arm. */
int sony_dsf_route_finish(struct sony_dsf_route **,const struct sony_dsf_playback_cleanup *,
    const struct sony_dsf_process_exit *,const struct sony_pcm_sink_process_exit *);
/* Additive native-DSD route, never entered by legacy prepare/arm/finish above.
 * Requires actual BTL presence and mode1; SE is refused before mixer/save.
 * Same original output-transition volume/AVLS/lastHeadphone policy and CFW.
 * Native path bypasses PCM effects; stored preferences remain unchanged. */
#define SONY_DSF_ROUTE_NATIVE_EXPLICIT_START 0x44534e31u
int sony_dsf_route_native_prepare(struct sony_dsf_route **,unsigned activation,const struct sony_dsd_controls *);
int sony_dsf_route_native_arm(struct sony_dsf_route *,struct sony_dsf_route_binding *,struct sony_dsf_native_playback_owner *);
/* Exact spawned PID from this session's verified native process supervisor,
 * before first sample/open callback. No caller-provided arbitrary process ID. */
int sony_dsf_route_native_child(struct sony_dsf_route *,int spawned_pid);
/* Retained lease only, for owned WKP7 cleanup. Remains valid after publication
 * retirement/quiesce/config failure; never admits new playback or publishes.
 * Actual power backend still verifies target/guard/PCM-closed/receipt itself. */
int sony_dsf_route_native_cleanup_check(void *);
/* Owner-generated DAC/save receipts, NOT original WDF live-DSP confirmations.
 * Native samples stay immutable. Only master volume may change in this API.
 * stage validates all fields/revision/AVLS before mutation, retires stale
 * publication, writes/readbacks DAC then saves/readbacks complete snapshot.
 * Returns OLD+pending. commit after the next real playback step verifies the
 * same native PCM/PID, DAC and save, then publishes new revision. No sample
 * injection, mixer volume, restart, retries or rollback. Invalid requests have
 * no side effects; admitted uncertainty quiesces and retains recovery barrier.
 */
int sony_dsf_route_native_get(struct sony_dsf_route *,struct wdf_controls *);
int sony_dsf_route_native_stage_volume(struct sony_dsf_route *,uint64_t generation,unsigned revision,
    const struct sony_dsd_live_controls *,struct wdf_controls *);
int sony_dsf_route_native_commit_volume(struct sony_dsf_route *,int output_ready);
int sony_dsf_route_native_finish(struct sony_dsf_route **,const struct sony_dsf_native_playback_cleanup *,
    const struct sony_dsf_process_exit *,const struct sony_dsd_sink_process_exit *);
#endif
