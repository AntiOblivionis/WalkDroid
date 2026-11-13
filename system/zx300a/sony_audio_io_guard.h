#ifndef WALKDROID_SONY_IO_GUARD_H
#define WALKDROID_SONY_IO_GUARD_H
/* glibc interposer, control operations only. Not a hardware-completion oracle. */
struct sony_io_report {
    unsigned int calls[4]; /* nonblock, drain, hw_free, close */
    unsigned int failures[4],foreign_handle_calls;
    int first_error,native_result;
};
/* Serialized owner thread; report and PCM identity live until end. No nesting. */
int sony_io_guard_begin(struct sony_io_report *,void *expected_pcm);
/* Returns first negative ALSA error, -5 for native failure, else 0.
 * -38 if no operation was observed (missing hook or wrong scope).
 * Zero only means observed synchronous calls succeeded, not DAC consumption. */
int sony_io_guard_end(int native_result);
#endif
