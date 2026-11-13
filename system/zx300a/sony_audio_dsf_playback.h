#ifndef SONY_AUDIO_DSF_PLAYBACK_H
#define SONY_AUDIO_DSF_PLAYBACK_H
#include "sony_audio_dsf_client.h"
#include "sony_audio_pcm_sink_client.h"
#include "sony_audio_dsf_source.h"
struct sony_dsf_playback;
struct sony_dsf_playback_owner {
    /* Required caller-owned exclusive route lease. Check must be observational;
     * started may enable the already-prepared muted route after PCM opens.
     * quiesce mutes/stops that route on terminal failure or close, exactly once.
     * These callbacks do not establish global exclusion by themselves: caller
     * must implement and retain actual ownership, identity and config binding. */
    int (*check)(void *);
    int (*started)(void *,unsigned int actual_rate);
    int (*quiesce)(void *);
    void *context;
};
struct sony_dsf_playback_state {
    uint64_t source_bytes,pcm_acked_bytes;
    unsigned int rate,source_finished,drain_accepted,failed,cancel_requested;
    int first_error,dsf_status,sink_status,quiesce_status;
};
struct sony_dsf_playback_cleanup {
    int dsf_close,sink_close,source_close,quiesce;
    int output_cleanup_unverified; /* Always 1; no physical completion claim. */
};
/* Serialized owner, with idle borrowed clients from verified owned supervisors.
 * No spawning, target selection, settings persistence or global route arbitration.
 * Opens and owns a leased regular file, then explicit original Sony normal EOF.
 * Output opens lazily at actual PCM rate. Native DSD BTL mode is rejected, never
 * silently converted. No application mixer/resampler or software volume added.
 * *out starts NULL. A failure after allocation retains *out for close/recovery.
 * Stop/join all other client users first; do not use clients while attached here. */
int sony_dsf_playback_start(struct sony_dsf_playback **out,
    struct sony_dsf_client *,struct sony_pcm_sink_client *,int directory_fd,
    const char *basename,uint64_t max_file_bytes,const struct sony_chain_settings *,
    const struct sony_dsd_controls *,const struct sony_output_controls *,unsigned latency,
    const struct sony_dsf_playback_owner *);
/* 0 progressed, 1 original DSP terminal and output Drain accepted. Repeated
 * completed step returns 1 without new callbacks/Drain. Error seals lifetime;
 * close is mandatory. Partial sink acceptance is counted separately from DSP
 * packet ACKs and is never retried. Callback errors preserve their first cause. */
int sony_dsf_playback_step(struct sony_dsf_playback *);
int sony_dsf_playback_state(struct sony_dsf_playback *,struct sony_dsf_playback_state *);
/* Serialized live DSP controls through this leased source and owned sink.
 * The external owner must coordinate muting, hardware effect state, persistence
 * and public state. These calls do not save or authorize arbitrary settings.
 * Checks source lease/owner/cancel both before and after the worker RPC; any
 * ownership/IO/transport uncertainty quiesces and seals playback immediately.
 * Not-started returns EAGAIN; stale/busy/invalid controls do not poison a valid
 * session. No stage after source EOF; confirmed receipts can be read at EOF.
 * Return codes use errno (WDF stale/busy/format become ESTALE/EBUSY/EINVAL).
 * commit returns 1 pending, 0 confirmed, with out unchanged on errors. */
int sony_dsf_playback_get_controls(struct sony_dsf_playback *,struct wdf_controls *);
int sony_dsf_playback_stage_controls(struct sony_dsf_playback *,unsigned revision,
    const struct sony_dsd_live_controls *,struct wdf_controls *);
int sony_dsf_playback_commit_controls(struct sony_dsf_playback *,unsigned revision,unsigned request,struct wdf_controls *);
struct sony_dsf_output_retirement {
    unsigned revision,stage_request,rate,output_ready;
    uint64_t acked_before_retirement; /* Admission total, NOT dropped/played count. */
};
/* CLOSE executes the sink's Drop unless already drained, then calls closed.
 * Reopen at the same rate if the source continues; at EOF leave output closed.
 * A repeated request can close the sink again and discard queued samples.
 * Errors seal playback; close remains mandatory. No samples are replayed. */
int sony_dsf_playback_retire_output(struct sony_dsf_playback *,const struct wdf_controls *,
    int (*closed)(void *),void *context,struct sony_dsf_output_retirement *);
/* External transaction failure (e.g. persistence) must not leave a live route.
 * Negative error only. Seals this lifetime and quiesces once, then close remains
 * mandatory. It does not claim to roll back native controls or durable data. */
int sony_dsf_playback_abort_controls(struct sony_dsf_playback *,int error);
/* May be called concurrently; caller must join cancel callers before close.
 * A request is observed on next owner check, not a kernel IO cancellation. */
void sony_dsf_playback_cancel(struct sony_dsf_playback *);
/* Quiesce -> sink Close -> DSF Close -> source lease release. Does not shut down
 * or destroy borrowed clients/processes. Source-close busy retains ownership.
 * Other errors release this coordinator but require supervising both workers
 * and verifying route cleanup before playback can restart. */
int sony_dsf_playback_close(struct sony_dsf_playback **,struct sony_dsf_playback_cleanup *);
#endif
