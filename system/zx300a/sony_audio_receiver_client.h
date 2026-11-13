#ifndef WALKDROID_SONY_RECEIVER_CLIENT_H
#define WALKDROID_SONY_RECEIVER_CLIENT_H
#include "sony_audio_receiver_worker_protocol.h"
#include <stdint.h>
struct sony_receiver_client;
/* Attach to caller-supplied descriptors without selecting a process or device.
 * *out must be NULL. Sets O_NONBLOCK; only success transfers ownership.
 * tx and rx may be the same full-duplex descriptor, closed once on teardown.
 * One owner; join users before destroy. Callback reentry returns -EBUSY.
 * Bounded monotonic transport deadline includes subsequent ACK delivery;
 * callback runtime must itself be bounded by the caller. No automatic retry.
 * A transport failure closes both pipes and is terminal. Reply identities and
 * state fields are delivered without comparison to prior requests. */
int sony_receiver_client_attach(struct sony_receiver_client **,int tx,int rx,int timeout_ms);
typedef int (*sony_receiver_client_sink)(void *,const uint8_t *,const struct sony_receiver_chain_result *,unsigned tail);
int sony_receiver_client_ready(struct sony_receiver_client *);
int sony_receiver_client_open(struct sony_receiver_client *,const struct wur_open *,sony_receiver_client_sink,void *,struct wur_state *);
/* Explicit original USB service mode; holds one native packet. See WUR1
 * service contract: admitted source includes pending, no hardware is owned. */
int sony_receiver_client_open_service(struct sony_receiver_client *,const struct wur_open *,sony_receiver_client_sink,void *,struct wur_state *);
/* Original BT latency2, native codec2/5/11. Immediate service packets, no USB
 * pending tail/padding; see WUR_OPEN_BT_SERVICE. Does not own the radio. */
int sony_receiver_client_open_bt_service(struct sony_receiver_client *,const struct wur_open *,sony_receiver_client_sink,void *,struct wur_state *);
/* Input bytes must be immutable for the duration of the call. The worker may
 * still reject a format or request. Returned state is the worker's report,
 * not proof of physical playback or consumption. */
int sony_receiver_client_process(struct sony_receiver_client *,const void *,unsigned frames,int final,struct wur_state *);
int sony_receiver_client_process_service(struct sony_receiver_client *,const void *,unsigned frames,int final,struct wur_state *);
/* Sends the requested volume and caller-supplied previous value to the worker.
 * This does not itself write the DAC or prove that a setting was saved. */
int sony_receiver_client_volume(struct sony_receiver_client *,unsigned expected,unsigned volume,struct wur_state *);
/* STOP2 vs natural DRAIN4, once. Emits held tail then original100ms pad, then
 * STOP or EOS+STOP. Same-mode repeat returns cached counters without samples.
 * Does not mute, Drop/Drain/Close hardware, close DSP or release ownership. */
int sony_receiver_client_finish_service(struct sony_receiver_client *,unsigned mode,struct wur_state *);
/* Explicit original primitives; no automatic USB stop/queue policy or EOS. */
int sony_receiver_client_silence(struct sony_receiver_client *,unsigned ms,struct wur_state *);
int sony_receiver_client_stop(struct sony_receiver_client *,struct wur_state *);
int sony_receiver_client_state(struct sony_receiver_client *,struct wur_state *);
int sony_receiver_client_close(struct sony_receiver_client *);
int sony_receiver_client_shutdown(struct sony_receiver_client *);
/* Local status, no IPC. A failed sink may have consumed audio. Failure leaves
 * ownership/output cleanup to the caller; unchanged receipts are not rollback.
 * Wire WUR_* errors are returned as-is; other failures use negative errno. */
int sony_receiver_client_status(struct sony_receiver_client *,int *dead,int *faulted);
int sony_receiver_client_destroy(struct sony_receiver_client *);
#endif
