#ifndef SONY_AUDIO_USB_INPUT_LINUX_H
#define SONY_AUDIO_USB_INPUT_LINUX_H
#include "sony_audio_owner.h"
#include "sony_audio_usb_input_policy.h"
#include "sony_audio_usb_boundary.h"
struct sony_usb_input_linux;
/* Open the USB function's control directory and borrow the caller's owner.
 * No gadget activation, PCM open or parameter writes occur during creation.
 * Failure leaves *out unchanged. */
int sony_usb_input_linux_create(struct sony_usb_input_linux **,struct sony_audio_owner *borrowed_owner);
/* IO context must outlive every policy/capture user. Four parameter names are
 * supported; write performs one syscall and reports EINTR or a short write.
 * Policy, not this backend, owns before-values, restoration and uncertainty. */
int sony_usb_input_linux_io(struct sony_usb_input_linux *,struct sony_usb_input_policy_io *);
int sony_usb_input_linux_check(struct sony_usb_input_linux *);
/* Read the four parameter values; output is unchanged on a read error. */
int sony_usb_input_linux_read_all(struct sony_usb_input_linux *,unsigned values[SONY_USB_INPUT_RESOURCE_COUNT]);
/* Decode the first complete boundary field sequence from the control file.
 * Output is unchanged on an I/O or decoding error. */
int sony_usb_input_linux_boundary_snapshot(struct sony_usb_input_linux *,struct sony_usb_boundary *);
/* Close only these reference FDs, no sysfs writes. Caller first resolves and
 * destroys every policy/capture borrowing this context; no implicit cleanup. */
int sony_usb_input_linux_destroy(struct sony_usb_input_linux **);
#endif
