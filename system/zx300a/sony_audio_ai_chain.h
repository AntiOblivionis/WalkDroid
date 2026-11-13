#ifndef WALKDROID_SONY_AI_CHAIN_H
#define WALKDROID_SONY_AI_CHAIN_H
#include <stdint.h>
#include "sony_audio_chain_bridge.h"
#include "sony_audio_ai_client.h"
/* Active AI adapter: 44.1/48k stereo S32 at the Sony input, private original
 * ZX manager + separately owned original AI client. No hardware or process
 * launcher. One adapter per process; callback reentry returns busy.
 * Calls through inherited copies require caller coordination. Other work must
 * remain serialized by the enclosing worker/config owner.
 *
 * Direct Source and unsupported/non-AI routes use the existing non-AI backend.
 * Changing direct to ON requires an outer owner transition (-EAGAIN here).
 * This adapter never pretends the original HX GetParam is actual AI status.
 */
struct sony_ai_chain;
#define SONY_AI_CHAIN_OPEN 1u
#define SONY_AI_CHAIN_STREAMING 2u
#define SONY_AI_CHAIN_FINISHED 3u
#define SONY_AI_CHAIN_FAILED 4u
struct sony_ai_chain_state {
    uint32_t phase,input_rate,output_rate,buffered_frames,finish_ms;
    uint32_t native_packets,zero_packets,trimmed_padding_frames;
    int32_t failure;
    uint64_t source_frames,ai_output_frames,returned_frames,tail_sink_frames;
    uint64_t padding_source_frames,ai_tail_ack_frames;
};
typedef int (*sony_ai_chain_sink)(void *,const int *,unsigned int,unsigned int);
/* Takes *opened_client ONLY on success. The caller supplies the AI stream.
 * On failure the caller still owns the client and must close/cancel it.
 * Initial normalizer is explicitly OFF; the caller may update it afterwards.
 */
int sony_ai_chain_open(struct sony_ai_chain **,void *audited_zx_library,
    const struct sony_chain_settings *,unsigned int rate,struct sony_ai_client **opened_client);
/* 512 source frames. Output may be zero during startup. capacity>=2048 is
 * checked before processing; errors never publish output or replay input.
 * Returned samples are software ownership transfer, not DAC consumption.
 */
int sony_ai_chain_process(struct sony_ai_chain *,const int *,unsigned int,int *,unsigned int,
    unsigned int *produced,unsigned int *rate);
/* Original AI 100/250ms silence/finish, original ZX postprocessing and EOS.
 * AI tail ACK means the adapter accepted that block (possibly buffered), not
 * DAC consumption. Last buffered output is delivered before success. Failure
 * is terminal, never retried. Repeated same successful finish returns 1.
 * Complete postfilter tail sufficiency must be accepted for each DSP profile.
 */
int sony_ai_chain_finish(struct sony_ai_chain *,unsigned int ms,sony_ai_chain_sink,void *);
int sony_ai_chain_state(struct sony_ai_chain *,struct sony_ai_chain_state *);
int sony_ai_chain_update(struct sony_ai_chain *,const struct sony_chain_settings *);
int sony_ai_chain_update_tone(struct sony_ai_chain *,const struct sony_tone_settings *);
int sony_ai_chain_update_output(struct sony_ai_chain *,const struct sony_output_controls *);
int sony_ai_chain_update_normalizer(struct sony_ai_chain *,int enabled);
int sony_ai_chain_get_parameter(struct sony_ai_chain *,const char *,char *,unsigned int);
/* Normal close refuses an unfinished nonempty stream. Cancel discards pending
 * samples. A terminal failure is still consumed on close, returning -EIO.
 * Reentrant closes leave the handle untouched. The process
 * launcher, not this adapter, owns waitpid/root/resource-lease cleanup.
 */
int sony_ai_chain_close(struct sony_ai_chain **,int discard);
#endif
