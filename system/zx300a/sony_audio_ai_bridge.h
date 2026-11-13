#ifndef WALKDROID_SONY_AI_BRIDGE_H
#define WALKDROID_SONY_AI_BRIDGE_H
/* Original-AI bridge for the Sony ARM32 glibc interface; no hardware access.
 * The caller supplies the library handle and runtime resources. This bridge
 * resolves the symbols it calls but does not verify library or asset bytes.
 * Its native object layout and symbols must match the supplied implementation.
 * Serialized calls only. This interface does not establish live NVP storage,
 * Android routing, downstream hardware consumption or real-time suitability.
 */
struct sony_ai_bridge;
struct sony_ai_status {
    unsigned int input_rate,output_rate,source_quantum;
    int requested_enabled,processing_expected,processing_observed;
    int finishing,finished,faulted;
    unsigned long long source_frames,padding_source_frames,produced_frames;
    unsigned long long tail_acknowledged_frames;
};
typedef int (*sony_ai_tail_sink)(void *context,const float *samples,
                                unsigned int frames,unsigned int rate);
int sony_ai_bridge_open(struct sony_ai_bridge **out,void *library,
                        unsigned int source_rate,int enabled);
/* Original GetParam readback. Updating is allowed only before any source
 * input. Midstream UI changes require outer drain/cancel and a new stream.
 */
int sony_ai_bridge_set_enabled(struct sony_ai_bridge *,int enabled);
int sony_ai_bridge_status(struct sony_ai_bridge *,struct sony_ai_status *out);
/* One original source quantum of float32/stereo. A successful input may
 * produce zero frames; never replay that input. Output count is cleared on
 * failure. Capacity must cover the native upper bound before consuming input.
 */
int sony_ai_bridge_process(struct sony_ai_bridge *,const float *input,
                           unsigned int frames,float *output,unsigned int capacity,
                           unsigned int *produced,unsigned int *rate);
/* Original silence -> AI -> sink, then original Drain / track::eos. Sink
 * success acknowledges delivery
 * to that callback, not DAC consumption. Any callback/native failure is
 * terminal; no implicit retry or silent bypass. Successful repeated finish
 * returns 1 and never calls the sink again.
 */
int sony_ai_bridge_finish(struct sony_ai_bridge *,unsigned int milliseconds,
                          sony_ai_tail_sink sink,void *context);
/* discard=0 refuses to truncate an unfinished nonempty stream. discard=1 is
 * explicit cancellation and does not claim a flushed tail. Consumes *bridge
 * after native teardown; a second close on NULL is harmless.
 */
int sony_ai_bridge_close(struct sony_ai_bridge **bridge,int discard);
#endif
