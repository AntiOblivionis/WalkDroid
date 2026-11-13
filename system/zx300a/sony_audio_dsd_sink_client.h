#ifndef WALKDROID_SONY_DSD_SINK_CLIENT_H
#define WALKDROID_SONY_DSD_SINK_CLIENT_H
#include "sony_audio_dsd_sink_protocol.h"
struct sony_dsd_sink_client;
struct sony_dsd_sink_progress {
 /* Serialized owner thread, invoked under client lock on EVERY wait slice.
  * Must check exact retained generation and service pending WKP7 messages.
  * No waiting for idle power input: use an advisory poll(0) on the server's
  * borrowed fd, then its normal bounded message/reply timeout. Actual resource
  * operations keep their own bounds and count against this command's deadline.
  * Zero=continue, negative=seal data transport. Never call this client
  * recursively, retry a power request, or block waiting for a data reply.
  * Parent authority must distinguish holding cleanup ownership from admitting
  * new playback after quiesce. This transport does not grant either authority. */
 int (*pump)(void *);
 void *context;
};
/* Nonblocking distinct FIFO ends transfer ONLY on successful attachment.
 * Caller pins root/target/worker/CFW, owns the WKP7 server and exact child reap.
 * All calls serialized, timeout is a total per-command bound, no retry. */
int sony_dsd_sink_attach(struct sony_dsd_sink_client **,int tx,int rx,int timeout_ms,const struct sony_dsd_sink_progress *);
int sony_dsd_sink_ready(struct sony_dsd_sink_client *);
int sony_dsd_sink_open(struct sony_dsd_sink_client *,unsigned bit_rate,struct wdn_state *);
int sony_dsd_sink_write(struct sony_dsd_sink_client *,const void *,unsigned bytes);
int sony_dsd_sink_drain(struct sony_dsd_sink_client *,struct wdn_state *);
int sony_dsd_sink_state(struct sony_dsd_sink_client *,struct wdn_state *);
int sony_dsd_sink_close(struct sony_dsd_sink_client *);
int sony_dsd_sink_shutdown(struct sony_dsd_sink_client *);
/* Closes pipes only. Mandatory supervisor cleanup/compensation still follows. */
int sony_dsd_sink_destroy(struct sony_dsd_sink_client *);
#endif
