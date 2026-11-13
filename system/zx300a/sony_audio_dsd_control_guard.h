#ifndef SONY_AUDIO_DSD_CONTROL_GUARD_H
#define SONY_AUDIO_DSD_CONTROL_GUARD_H
/* Dedicated native-DSD process only. Synchronous call verification, not a DAC
 * consumption oracle. Bind an exact PCM on the same serialized owner thread.
 * Drain/Drop: nonblock(0), action, nonblock(original_mode=0).
 * Close: hw_free, close. Foreign/out-of-order calls are blocked before IO.
 * A failed setup blocks the destructive middle action; the final mode restore
 * remains a distinct cleanup step. Close still follows a failed hw_free.
 */
enum sony_dsd_control_operation {SONY_DSD_CTL_DRAIN=1,SONY_DSD_CTL_DROP=2,SONY_DSD_CTL_CLOSE=3};
struct sony_dsd_control_report {
    unsigned operation,attempted,forwarded,blocked,foreign,step,complete;
    unsigned nonblock_calls,drain_calls,drop_calls,hw_free_calls,close_calls;
    int first_error,native_result;
};
int sony_dsd_control_begin(struct sony_dsd_control_report *,void *pcm,unsigned operation);
int sony_dsd_control_end(int native_result);
#endif
