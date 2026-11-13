#ifndef SONY_AUDIO_DSF_CLIENT_H
#define SONY_AUDIO_DSF_CLIENT_H
#include "sony_audio_dsf_worker_protocol.h"
#include "sony_audio_chain_bridge.h"
#include <stddef.h>
#include <stdint.h>
struct sony_dsf_client;
/* Attach to a worker's two pipes without checking its identity. Does not
 * spawn/select a worker, device or route. Sets O_NONBLOCK on both descriptors.
 * Success transfers ownership; failure does not close them. A shared
 * full-duplex descriptor is closed once on teardown.
 * Calls use a bounded monotonic deadline; callback runtime belongs to caller.
 * All operations on a client require a single owner; callback reentry is rejected.
 * On protocol/transport uncertainty pipes are closed and this client is terminal.
 */
int sony_dsf_client_attach(struct sony_dsf_client **,int tx,int rx,int timeout_ms);
struct sony_dsf_client_callbacks {
    /* Short reads are returned with their actual length. A callback error sends
     * CANCEL; caller coordinates any subsequent source or output cleanup.
     * Use a caller-owned cancellation flag here, or CLOSE between STEP calls.
     * A failing sample sink may have consumed data: it must drop/clean its own
     * output route; neither CANCEL nor closing the worker proves that cleanup.
     */
    int (*read_at)(void *,uint64_t,uint8_t *,size_t,size_t *);
    int (*samples)(void *,const uint8_t *,const struct sony_dsd_result *);
    int (*input_eos)(void *); /* Input EOS only, not physical completion. */
    void *context;
};
int sony_dsf_client_open(struct sony_dsf_client *,uint64_t file_bytes,
    const struct sony_chain_settings *,const struct sony_dsd_controls *,
    const struct sony_output_controls *,const struct sony_dsf_client_callbacks *,struct wdf_state *);
/* Explicit normal master-DSD EOF policy. At EOF samples may include service
 * padding before input_eos; neither callback proves hardware consumption.
 */
int sony_dsf_client_open_terminal(struct sony_dsf_client *,uint64_t file_bytes,
    const struct sony_chain_settings *,const struct sony_dsd_controls *,
    const struct sony_output_controls *,unsigned int original_latency_mode,
    const struct sony_dsf_client_callbacks *,struct wdf_state *);
/* Returns 0 with state, including INPUT_FINISHED when sealed. No repeated EOS. */
int sony_dsf_client_step(struct sony_dsf_client *,struct wdf_state *);
int sony_dsf_client_state(struct sony_dsf_client *,struct wdf_state *);
/* GET returns controls and pending state. STAGE forwards the requested controls;
 * COMMIT may return 1 while pending or 0 when the worker reports completion.
 * Revision, stage, session and request fields do not authenticate the reply.
 * Completion does not establish snapshot persistence or hardware consumption.
 * One serialized caller; callback reentry rejected. Never retry lost requests.
 */
int sony_dsf_client_get_controls(struct sony_dsf_client *,struct wdf_controls *);
int sony_dsf_client_stage_controls(struct sony_dsf_client *,unsigned expected_revision,
    const struct sony_dsd_live_controls *,struct wdf_controls *);
int sony_dsf_client_commit_controls(struct sony_dsf_client *,unsigned base_revision,
    unsigned stage_request,struct wdf_controls *);
int sony_dsf_client_close_session(struct sony_dsf_client *);
int sony_dsf_client_shutdown(struct sony_dsf_client *);
/* Idle-only handshake: libraries loaded, no snapshot/media/DSP/output opened.
 * Successful SHUTDOWN closes owned pipes and makes all future commands fail. */
int sony_dsf_client_ready(struct sony_dsf_client *);
/* Closes owned pipes, no drain/callback. Busy callback destruction returns -EBUSY. */
int sony_dsf_client_destroy(struct sony_dsf_client *);
#endif
