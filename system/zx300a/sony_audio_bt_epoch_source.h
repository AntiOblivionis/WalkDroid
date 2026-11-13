#ifndef SONY_AUDIO_BT_EPOCH_SOURCE_H
#define SONY_AUDIO_BT_EPOCH_SOURCE_H
#include "sony_audio_bt_epoch_transport.h"
struct sony_bt_epoch_source;
struct sony_bt_epoch_source_state {
    uint64_t epochs,frames,bytes,offered_frames,offered_bytes,packets;
    uint64_t boundary_packets,boundary_bytes,volume_offered,volume_sent;
    unsigned phase,queued_peak_frames,sealed,sender_joined;
    int first_error;
    uint64_t offered_packets;
    uint64_t prefetched_packets,prefetched_bytes,unready_running_packets,unready_running_bytes;
    uint64_t producer_epochs,future_packets,future_bytes;
    unsigned queued_peak_ns,queued_bytes,queued_frames,queued_ns;
};
/* Inactive, caller-supplied socket; no RF or device access during creation.
 * One prefaulted two-second96k/stereo/S32 queue allocated before callbacks. */
int sony_bt_epoch_source_create(struct sony_bt_epoch_source **,int fd,pid_t parent,uint64_t cookie);
int sony_bt_epoch_source_listening(struct sony_bt_epoch_source *);
/* Native callback entry points: bounded atomics/copy only. No packet send,
 * allocation, sleep, DSP/DAC operation or control replay. Callers supply
 * native event observations; this component does not verify peer identity.
 * A peer loss clears format freshness, closes admission, and requires new
 * original format+peer observations before a new source can be opened. */
int sony_bt_epoch_source_peer(struct sony_bt_epoch_source *,unsigned valid_peer);
int sony_bt_epoch_source_activity(struct sony_bt_epoch_source *,unsigned playing);
int sony_bt_epoch_source_format(struct sony_bt_epoch_source *,const struct sony_bt_middleware_sound *);
unsigned sony_bt_epoch_source_ready(const struct sony_bt_epoch_source *);
/* Read-only local producer observations; neither is hardware authority. */
unsigned sony_bt_epoch_source_phase(const struct sony_bt_epoch_source *);
uint64_t sony_bt_epoch_source_generation(const struct sony_bt_epoch_source *);
int sony_bt_epoch_source_pcm(struct sony_bt_epoch_source *,unsigned bytes,const void *);
/* A verified native resume can precede the old output's CLOSED. Immutable
 * FORMAT/DATA/END jobs retain that future source in the SAME prefaulted pool;
 * aggregate queued/in-flight source duration stays <=2s across rates.
 * No send occurs in a callback. FORMAT waits for the prior actual CLOSED;
 * DATA waits for READY or STOP-only FLUSH_READY without decoder reset. */
int sony_bt_epoch_source_volume(struct sony_bt_epoch_source *,int step);
int sony_bt_epoch_source_error(struct sony_bt_epoch_source *,int negative_errno);
/* Single main thread. poll returns1 on a new READY,2 on the first
 * STOP,3 when admission closes for a source END,
 * 0 for no notification,negative on failure. It also
 * completes END/CLOSED/next-FORMAT transitions, without native RF calls. */
int sony_bt_epoch_source_poll(struct sony_bt_epoch_source *);
int sony_bt_epoch_source_stop_begin(struct sony_bt_epoch_source *);
/* Main after native SetPlayerState(0). Drives exact close acknowledgements,
 * joins the sender, flushes accepted controls and sends one QUIET or ERROR.
 * EAGAIN retains the entire object and is never a terminal observation. */
int sony_bt_epoch_source_finish(struct sony_bt_epoch_source *);
/* Accepted-data statistics after finish joins the sender. Boundary callback
 * telemetry is final only after native callbacks have also quiesced. */
int sony_bt_epoch_source_state(const struct sony_bt_epoch_source *,struct sony_bt_epoch_source_state *);
/* Caller must independently prove native callback registration/threads have
 * ended. Never call while the RF library can still invoke this context. */
int sony_bt_epoch_source_destroy(struct sony_bt_epoch_source **,unsigned callbacks_quiesced);
#endif
