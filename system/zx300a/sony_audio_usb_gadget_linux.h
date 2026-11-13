#ifndef SONY_AUDIO_USB_GADGET_LINUX_H
#define SONY_AUDIO_USB_GADGET_LINUX_H
#include "sony_audio_owner.h"
#include "sony_audio_usb_gadget_policy.h"
struct sony_usb_gadget_linux;
/* Sysfs backend for the requested gadget attributes. It does not verify an
 * owner, process, device identity or physical PCM state. No writes at create. */
int sony_usb_gadget_linux_create(struct sony_usb_gadget_linux **,struct sony_audio_owner *);
int sony_usb_gadget_linux_io(struct sony_usb_gadget_linux *,struct sony_usb_gadget_io *);
/* Caller has resolved/destroyed every policy user before dropping these refs.
 * No restoration, ownership finish, signal or process action at destroy. */
int sony_usb_gadget_linux_destroy(struct sony_usb_gadget_linux **);
#endif
