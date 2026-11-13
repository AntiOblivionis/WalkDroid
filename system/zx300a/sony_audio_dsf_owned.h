#ifndef WALKDROID_SONY_DSF_OWNED_H
#define WALKDROID_SONY_DSF_OWNED_H
#include "sony_audio_dsf_route.h"
struct sony_dsf_owned;
#define SONY_DSF_OWNED_EXPLICIT_START 0x44534f31u
struct sony_dsf_owned_cleanup {
    struct sony_dsf_playback_cleanup playback;
    struct sony_dsf_process_exit dsf;
    struct sony_pcm_sink_process_exit sink;
    int dsf_shutdown,sink_shutdown,route;
};
/* Hardware-capable explicit session, no autostart or runtime mount preparation.
 * Exact target + build-pinned read-only WDF/WDP roots, both /data/config paths
 * must be bind mounts of the same new private directory (0700/current absent).
 * Current HAL and all writers must participate in output ownership first.
 * Single controller thread owns all calls, forks and child reaping. Borrowed
 * roots/log/media fds are unmodified. Any non-NULL result must be closed, even
 * if start failed. Never replace an instance with pending child/source cleanup. */
int sony_dsf_owned_start(struct sony_dsf_owned **,unsigned activation,int dsf_root,int sink_root,
    int log_fd,const char *config_directory,int media_directory,const char *basename,uint64_t max_bytes,
    const struct sony_dsd_controls *,unsigned latency);
/* Same conversion controller with the broker's selected output pinned under
 * route ownership before mixer/config mutations. Rejects hotplug races. */
int sony_dsf_owned_start_output(struct sony_dsf_owned **,unsigned activation,int dsf_root,int sink_root,
    int log_fd,const char *config_directory,int media_directory,const char *basename,uint64_t max_bytes,
    const struct sony_dsd_controls *,unsigned latency,unsigned expected_output);
int sony_dsf_owned_step(struct sony_dsf_owned *);
int sony_dsf_owned_state(struct sony_dsf_owned *,struct sony_dsf_playback_state *);
struct sony_dsf_owned_controls {
    uint64_t generation;
    struct wdf_controls acknowledged;
};
/* Same controller as step/close. Before first actual output returns EAGAIN.
 * get returns only this owner's validated native acknowledgement. During a
 * transaction it contains OLD controls plus pending; no public ready state is
 * available until save + hardware success. Stage requires the exact generation
 * and revision returned by get; invalid/no-op requests have no DSP effects.
 * step advances real audio then collects confirmation, persists through the
 * route, unmutes and publishes. No explicit commit/retry API is exposed.
 * Direct/master volume/DSD mode changes are currently unsupported. Native DSP
 * confirmation and sink ACK do not certify the DAC's queue consumption. */
int sony_dsf_owned_get_controls(struct sony_dsf_owned *,struct sony_dsf_owned_controls *);
int sony_dsf_owned_stage_controls(struct sony_dsf_owned *,uint64_t generation,unsigned revision,
    const struct sony_dsd_live_controls *,struct sony_dsf_owned_controls *);
/* Opt-in local commands, no new hardware activation. Same controller only.
 * step polls at most one command before advancing real audio; the explicit
 * nonblocking poll also serves receipts at EOF without generating extra audio.
 * No thread, Android authorization, settings relay or automatic deployment.
 * close stops admission first; peer loss never replays/cancels an admitted Set. */
int sony_dsf_owned_enable_commands(struct sony_dsf_owned *);
int sony_dsf_owned_poll_commands(struct sony_dsf_owned *);
/* Same-thread request. The lower playback API supports concurrent cancel, but
 * this outer object must not race start/close or mutate its playback pointer. */
void sony_dsf_owned_cancel(struct sony_dsf_owned *);
/* Quiesce -> close playback -> shutdown/reap owned children -> route/PCM idle
 * check -> owner finish. Retains *out while source/child cleanup is pending.
 * Return covers cleanup, not the preceding playback error or DAC consumption.
 * Error/failure receipts keep the ACTIVE recovery barrier. No automatic reset. */
int sony_dsf_owned_close(struct sony_dsf_owned **,struct sony_dsf_owned_cleanup *);
#endif
