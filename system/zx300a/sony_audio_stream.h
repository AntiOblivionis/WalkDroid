#ifndef WALKDROID_SONY_STREAM_H
#define WALKDROID_SONY_STREAM_H
#include "sony_audio_chain_bridge.h"
#include "sony_audio_output_timing.h"
/* Experimental owner-thread synchronous flow. MAY OPEN REAL OUTPUT HARDWARE.
 * Caller supplies libraries/config/assets/route and the IO guard preload.
 * No automatic retry, thread creation, format conversion, or device selection.
 * Output opens lazily on first PCM at its actual rate. If a later packet has
 * another rate, the old output drains/closes before the new one opens.
 */
struct sony_stream;
struct sony_stream_state {
    unsigned long long input_frames,output_frames;
    unsigned int sample_rate,sealed,faulted,drain_accepted;
};
int sony_stream_open(struct sony_stream **,void *sound_library,void *output_library,const struct sony_chain_settings *);
/* Immutable native PCM rate/quantum. Close/reopen to change either. */
int sony_stream_open_rate(struct sony_stream **,void *sound_library,void *output_library,
    const struct sony_chain_settings *,unsigned int input_rate);
#ifdef SONY_STREAM_AI
struct sony_ai_client;
struct sony_ai_chain_state;
/* Explicit AI-only build/entry. Borrowed native libraries already verified;
 * consumes the fresh OPENed AI client on success only. 44.1/48k, direct OFF.
 * A one-stream endpoint: direct ON, AI OFF or another mode requires an outer
 * finish/cancel + fresh process transition (-11), never live rate rewriting.
 * AI source/tail ACK is software acceptance, not output hardware consumption.
 * Empty finish seals without opening output and leaves drain_accepted=0.
 */
int sony_stream_open_ai(struct sony_stream **,void *,void *,const struct sony_chain_settings *,
    unsigned int input_rate,struct sony_ai_client **opened_client);
int sony_stream_get_ai_state(struct sony_stream *,struct sony_ai_chain_state *);
#endif
int sony_stream_update_tone(struct sony_stream *,const struct sony_tone_settings *);
int sony_stream_update_output(struct sony_stream *,const struct sony_output_controls *);
int sony_stream_update_dsee(struct sony_stream *,const struct sony_dsee_controls *);
int sony_stream_update_normalizer(struct sony_stream *,int enabled);
int sony_stream_update(struct sony_stream *,const struct sony_chain_settings *);
int sony_stream_query(struct sony_stream *,const char *,char *,unsigned int);
int sony_stream_process(struct sony_stream *,const int *input,unsigned int frames);
/* Delivers native padding, sends EOS, then invokes output Drain once. Success
 * means native acceptance, not physical consumption or full algorithm flush. */
int sony_stream_finish(struct sony_stream *,unsigned int padding_ms);
int sony_stream_get_state(struct sony_stream *,struct sony_stream_state *);
int sony_stream_get_timing(struct sony_stream *,struct sony_output_timing *);
/* No implicit finish. -16 retains ownership if output guard cannot enter.
 * Otherwise closes output before chain and nulls owner even on cleanup error. */
int sony_stream_abort_close(struct sony_stream **);
#endif
