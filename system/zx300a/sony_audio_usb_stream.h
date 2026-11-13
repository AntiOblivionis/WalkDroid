#ifndef WALKDROID_SONY_AUDIO_USB_STREAM_H
#define WALKDROID_SONY_AUDIO_USB_STREAM_H
#include <stddef.h>
#include <stdint.h>
/* Values match the pinned original receiver library, not the kernel's private
 * event enum. No audio routing, ownership, clock feedback or gadget writes. */
enum sony_usb_action {SONY_USB_STOP=0,SONY_USB_PLAY=1,SONY_USB_NONE=2};
enum sony_usb_format {SONY_USB_FORMAT_NONE=0,SONY_USB_PCM=1,SONY_USB_DSD=2,SONY_USB_DOP=3};
struct sony_usb_stream {uint32_t action,format,rate,bit_width;};
struct sony_usb_event {uint32_t sequence;struct sony_usb_stream stream;};
struct sony_usb_capture_plan {
    uint32_t alsa_format,alsa_rate,channels,alsa_frame_bytes;
    uint32_t source_bit_width,source_rate;
    uint64_t source_bytes_per_second;
};
/* Decode bounded KEY=VALUE lines. Known action and format names are parsed;
 * sample widths are forwarded, unknown fields ignored and later fields win. */
int sony_usb_stream_parse(const void *,size_t,struct sony_usb_stream *);
/* Decode the first bounded netlink message. Sender, group, credentials and
 * sequence are metadata, not authentication or admission conditions. */
int sony_usb_event_parse(const void *,size_t,uint32_t sender_pid,uint32_t groups,
    unsigned msg_flags,struct sony_usb_event *);
/* Negotiation only. PCM is left-aligned S32_LE stereo; native/DoP capture is
 * already unpacked DSD_U8. Nominal ALSA rate is bit-rate/32, while the kernel's
 * original native packing transfers four times nominal ALSA byte geometry.
 * source_bytes_per_second describes source payload, not negotiated frame bytes
 * times nominal rate. Other format values use PCM geometry. This calculation
 * does not establish whether the stream is compatible with a particular path. */
int sony_usb_stream_capture_plan(const struct sony_usb_stream *,struct sony_usb_capture_plan *);
#endif
