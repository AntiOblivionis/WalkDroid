#ifndef SONY_AUDIO_RECEIVER_LEGACY_IO_H
#define SONY_AUDIO_RECEIVER_LEGACY_IO_H
/* Private native PCM IO scope. All operations bind one exact PCM pointer.
 * After the first error, further WRITE/WAIT/DRAIN attempts in that scope are
 * refused before ALSA. A subsequent explicit cleanup scope can Drop/Close.
 * Outside a scope, original library setup calls retain their normal behavior.
 * This is not a global audio owner or authority to select/open any PCM. */
struct sony_receiver_legacy_io_report {
    unsigned calls[7],failures[7]; /* nonblock/drain/hwfree/close/write/wait/drop */
    unsigned foreign_handle_calls,confirmed_frames,blocked_after_error;
    int first_error,native_result;
};
int sony_receiver_legacy_io_begin(struct sony_receiver_legacy_io_report *,void *pcm);
int sony_receiver_legacy_io_end(int native_result,int allow_empty);
#endif
