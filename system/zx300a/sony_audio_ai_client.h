#ifndef WALKDROID_SONY_AI_CLIENT_H
#define WALKDROID_SONY_AI_CLIENT_H
#include "sony_audio_ai_protocol.h"
/* One inherited private socket, one worker, one PCM stream.
 * Calls must be serialized, including calls through inherited descriptors.
 * Transport waits are bounded and operations are never automatically replayed
 * on failure. The caller must keep its sink callback bounded; this synchronous
 * API cannot preempt caller code. The process owner handles a stuck engine.
 * Launcher owns process management and reaping, separately from PCM.
 */
struct sony_ai_client;
typedef int (*sony_ai_client_tail_sink)(void *,const float *,unsigned int,unsigned int);
/* On success consumes fd. On error caller still owns fd. */
int sony_ai_client_create(struct sony_ai_client **out,int fd,uint64_t nonce,int timeout_ms);
int sony_ai_client_open(struct sony_ai_client *,unsigned int rate,int enabled);
int sony_ai_client_set(struct sony_ai_client *,int enabled);
int sony_ai_client_state(struct sony_ai_client *,struct wai_state *out);
int sony_ai_client_process(struct sony_ai_client *,const float *input,unsigned int frames,
                           float *output,unsigned int capacity,unsigned int *produced,unsigned int *rate);
/* Each tail block is ACKed only after sink returns 0. A failed or ambiguous
 * sink delivery is never replayed. Successful finish is not DAC consumption.
 */
int sony_ai_client_finish(struct sony_ai_client *,unsigned int ms,sony_ai_client_tail_sink,void *context);
/* discard=0 refuses an unfinished nonempty stream. Callback reentry returns
 * busy. Other terminal errors still consume the client and close its socket.
 * A forked copy uses the same underlying socket: closing it can also affect
 * other copies. No claim about worker reaping is made.
 */
int sony_ai_client_close(struct sony_ai_client **owner,int discard);
#endif
