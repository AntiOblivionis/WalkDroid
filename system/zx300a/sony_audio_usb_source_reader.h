#ifndef SONY_AUDIO_USB_SOURCE_READER_H
#define SONY_AUDIO_USB_SOURCE_READER_H
#include "sony_audio_usb_source_device.h"
#include "sony_audio_usb_stream.h"
struct sony_usb_source_reader;
#define SONY_USB_SOURCE_READER_EXPLICIT 0x57555331u
#define SONY_USB_SOURCE_READER_END 1
enum sony_usb_source_stage {SONY_USB_SOURCE_CLAIM=1,SONY_USB_SOURCE_PREFILL=2,SONY_USB_SOURCE_READ=3};
/* Reader IO over a caller-provided binding. Main data operations report their
 * actual errors. caps is best-effort status; feedback is optional. check and
 * binding_closed remain for API compatibility and are not invoked here. */
struct sony_usb_source_io {
    int (*check)(void *);
    int (*caps)(void *,struct sony_source_caps *);
    int (*peek)(void *,struct sony_source_info *);
    int (*claim)(void *,struct sony_source_info *);
    int (*read_record)(void *,void *,unsigned);
    int (*release)(void *,uint64_t);
    int (*feedback)(void *,struct sony_source_feedback_info *);
    int (*close_binding)(void *);
    int (*binding_closed)(void *);
    void *context;
};
/* Legacy callback retained for API compatibility; reader does not invoke it. */
struct sony_usb_source_scope {
    int (*admit)(void *,const struct sony_usb_event *,enum sony_usb_source_stage);
    void *context;
};
struct sony_usb_source_reader_state {
    struct sony_source_caps caps;
    struct sony_source_info source;
    struct sony_usb_event event;
    uint64_t returned_total,returned_source,last_reservation,last_released_id;
    uint32_t prefill_frames,claim_attempts,read_attempts,transient_reads,claimed,prefilled,eof,release_attempted,release_confirmed,sealed,close_attempted;
    int32_t first_error;
};
struct sony_usb_source_reader_cleanup {
    struct sony_usb_source_reader_state state;
    int32_t close_status,closed_status;
    uint32_t binding_closed_verified,global_mode_cleanup_unverified;
};
/* Reported metadata is not an admission condition. An optional scope is kept
 * for compatibility; the IO backend/context must outlive the reader. */
int sony_usb_source_reader_create(struct sony_usb_source_reader **,unsigned,const struct sony_usb_source_io *,const struct sony_usb_source_scope *);
int sony_usb_source_reader_begin(struct sony_usb_source_reader *,const struct sony_usb_event *);
/* Original100ms minimum, derived from source rate, or any finalized short/
 * empty source. No consumption, parameter writes or blocking waits. */
int sony_usb_source_reader_prefill(struct sony_usb_source_reader *,unsigned *available_frames);
/* One record plus payload, at most 4096 stereo S32 frames. Lengths must fit the
 * allocated buffer; stream identity, sample bits and cursors are not pinned. */
int sony_usb_source_reader_read(struct sony_usb_source_reader *,void *,unsigned,unsigned *out_frames);
/* Normal source release follows EOF. RELEASE reports its actual error. */
int sony_usb_source_reader_finish(struct sony_usb_source_reader *);
int sony_usb_source_reader_feedback(struct sony_usb_source_reader *,struct sony_source_feedback_info *);
int sony_usb_source_reader_state(struct sony_usb_source_reader *,struct sony_usb_source_reader_state *);
/* Whole-binding cancellation/close, also after a data/admission error.
 * close_status reports the operation; binding_closed_verified remains zero
 * because other holders of the descriptor are not independently observed. */
int sony_usb_source_reader_close(struct sony_usb_source_reader **,struct sony_usb_source_reader_cleanup *);
#endif
