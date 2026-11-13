#ifndef SONY_AUDIO_PCM_SINK_CLIENT_H
#define SONY_AUDIO_PCM_SINK_CLIENT_H
#include "sony_audio_pcm_sink_protocol.h"
struct sony_pcm_sink_client;
struct sony_pcm_sink_progress {
    /* Serialized, bounded parent control-channel work during data RPC waits.
     * No reentry into this client. A nonzero result seals both data pipes. */
    int (*pump)(void *);
    void *context;
};
struct sony_pcm_sink_events {
    /* Invoked only when the pinned watch reports readable. It must perform
     * fresh request authority checks; poll_fd is never an output grant. */
    int (*pump)(void *);
    /* Borrowed authenticated nonblocking/CLOEXEC SEQPACKET resource socket.
     * Set the same descriptor until permanently disabled (*fd=-1) after
     * release. Return0 or a negative error; EPERM is never a disabled watch.
     * No mutations here. */
    int (*poll_fd)(void *,int *fd);
    void *context;
};
/* Caller coordinates worker and route. Sets O_NONBLOCK and takes ownership
 * of supplied descriptors on success; a shared full-duplex fd is closed once.
 * Single serialized owner, no callback/reentry. Timeout/bad frame closes pipes;
 * supervisor must still verify child and hardware cleanup. No retry/fallback. */
int sony_pcm_sink_attach(struct sony_pcm_sink_client **,int tx,int rx,int timeout_ms);
/* Optional authenticated resource supervision. Existing attach retains its
 * original behavior. Both variants require *out=NULL and transfer FDs only on
 * success; caller keeps independent child/hardware cleanup responsibility. */
int sony_pcm_sink_attach_progress(struct sony_pcm_sink_client **,int tx,int rx,int timeout_ms,
    const struct sony_pcm_sink_progress *);
/* Event-driven resource supervision: wait on data AND resource readiness with
 * one absolute RPC deadline. Pins the borrowed socket identity, never closes
 * it. Existing attach/progress ABI and legacy behavior remain available. */
int sony_pcm_sink_attach_events(struct sony_pcm_sink_client **,int tx,int rx,int timeout_ms,
    const struct sony_pcm_sink_events *);
int sony_pcm_sink_ready(struct sony_pcm_sink_client *);
int sony_pcm_sink_open(struct sony_pcm_sink_client *,unsigned rate,struct wdp_state *);
/* Only stereo S32_LE at session rate; <=8192 frames, no software transformation. */
int sony_pcm_sink_write(struct sony_pcm_sink_client *,const int *,unsigned frames);
/* Explicit, once, after source/DSP terminal completion. Not an acoustic oracle. */
int sony_pcm_sink_drain(struct sony_pcm_sink_client *,struct wdp_state *);
int sony_pcm_sink_state(struct sony_pcm_sink_client *,struct wdp_state *);
/* Abort if not drained. Backend error may clear local session but is returned. */
int sony_pcm_sink_close(struct sony_pcm_sink_client *);
/* A terminal auxiliary EPIPE/ECHILD does not discard a pending, fully checked
 * WDP SHUTDOWN reply. Missing/malformed replies still fail within the same
 * deadline. Success is a protocol ACK only: exact child and hardware/resource
 * cleanup remain the supervisor's independent responsibility. */
int sony_pcm_sink_shutdown(struct sony_pcm_sink_client *);
int sony_pcm_sink_destroy(struct sony_pcm_sink_client *);
#endif
