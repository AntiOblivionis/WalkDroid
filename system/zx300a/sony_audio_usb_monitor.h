#ifndef WALKDROID_SONY_AUDIO_USB_MONITOR_H
#define WALKDROID_SONY_AUDIO_USB_MONITOR_H
#include "sony_audio_usb_stream.h"
struct sony_usb_monitor;
#define SONY_USB_MONITOR_EXPLICIT_START 0x57445531u
/* Open a protocol-24/group-1 event socket without changing the USB mode. */
int sony_usb_monitor_open(struct sony_usb_monitor **,unsigned activation);
int sony_usb_monitor_fd(struct sony_usb_monitor *);
/* Nonblocking, one datagram; -EAGAIN means no event. The parser checks the
 * buffer shape needed for decoding; it does not authenticate the sender or
 * enforce sequence continuity. Output is unchanged on decoding errors. */
int sony_usb_monitor_receive(struct sony_usb_monitor *,struct sony_usb_event *);
int sony_usb_monitor_close(struct sony_usb_monitor **);
#endif
