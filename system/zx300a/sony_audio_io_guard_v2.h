#ifndef WALKDROID_SONY_IO_GUARD_V2_H
#define WALKDROID_SONY_IO_GUARD_V2_H
/* glibc interposer, control operations only. Not a hardware-completion oracle. */
struct sony_io_report_v2 {
    unsigned int calls[6]; /* nonblock, drain, hw_free, close, writei, wait */
    unsigned int failures[6],foreign_handle_calls;
    int first_error,native_result;
    unsigned int confirmed_frames;
};
/* Serialized owner thread; report and PCM identity live until end. No nesting. */
int sony_io_guard_begin_v2(struct sony_io_report_v2 *,void *expected_pcm);
/* Returns first negative ALSA error, -5 for native failure, else 0.
 * -38 if no operation was observed (missing hook or wrong scope).
 * Zero only means observed synchronous calls succeeded, not DAC consumption. */
int sony_io_guard_end_v2(int native_result,int allow_empty);
#endif
