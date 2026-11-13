#ifndef SONY_AUDIO_USB_SOURCE_LINUX_H
#define SONY_AUDIO_USB_SOURCE_LINUX_H
#include "sony_audio_usb_source_reader.h"
#include "sony_audio_usb_input_linux.h"
struct sony_usb_source_linux;
struct sony_usb_source_linux_cleanup {
    uint64_t instance;
    uint32_t opened_mask,known_mask,close_attempted_mask,closed_verified_mask;
    int32_t close_status[3],first_error;
    uint32_t close_attempted,binding_closed_verified,global_mode_cleanup_unverified;
};
/* Open the source queue exposed by the running USB driver. Borrow the existing
 * USB input scope; no sysfs ownership or device-number pin is required.
 * No gadget/parameter writes, ALSA open or legacy fallback.
 * After allocation, even a failure returns a handle requiring close/destroy.
 * Borrowed input context must outlive this backend and its reader. */
int sony_usb_source_linux_create(struct sony_usb_source_linux **,struct sony_usb_input_linux *);
/* Only the serialized source reader may consume this IO. */
int sony_usb_source_linux_io(struct sony_usb_source_linux *,struct sony_usb_source_io *);
int sony_usb_source_linux_poll_fd(struct sony_usb_source_linux *);
/* Index 0 is the source FD. Close it once without checking its identity.
 * An error from close retains the context; the descriptor is not retried. */
int sony_usb_source_linux_close(struct sony_usb_source_linux *,struct sony_usb_source_linux_cleanup *);
/* No descriptor operations; only after reader is gone and close verified. */
int sony_usb_source_linux_destroy(struct sony_usb_source_linux **);
#endif
