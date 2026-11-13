#ifndef SONY_AUDIO_USB_SOURCE_INGRESS_H
#define SONY_AUDIO_USB_SOURCE_INGRESS_H
#include "sony_audio_usb_ingress.h"
#include "sony_audio_usb_source_linux.h"
struct sony_usb_source_ingress;
#define SONY_USB_SOURCE_INGRESS_EXPLICIT 0x57555349u
struct sony_usb_source_ingress_info {
    struct sony_usb_source_reader_state reader;
    struct sony_usb_source_reader_cleanup reader_cleanup;
    struct sony_usb_source_linux_cleanup linux_cleanup;
    uint64_t journal_source_id;
    int cancel_status,linux_destroy_status;
    unsigned cancel_attempted,binding_closed_verified;
};
/* Explicit new-kernel input. The oldest descriptor is the retained record of
 * an ACTUAL wd_source_event PLAY, published under the same kernel lock before
 * its netlink broadcast. It is not the current-format/WDB1 snapshot. Exact
 * instance/source-id/sequence/tuple are claimed without skipping any source.
 * This journal survives a PLAY broadcast before the monitor subscribed, and
 * also retains stopped/short sources during Sony DSP preparation. The borrowed
 * monitor is validated for outer-mode lifetime, but datagrams are not a second
 * competing source of ordering authority for this input.
 *
 * Old tinyalsa ingress and its ABI remain separate. No ALSA capture is opened
 * here. Compatibility state leaves every ALSA open/prepare/start flag zero;
 * source/FD receipts are reported by info(), not fabricated ALSA cleanup.
 */
int sony_usb_source_ingress_create(struct sony_usb_source_ingress **,unsigned,
    struct sony_audio_owner *,struct sony_usb_monitor *,uint64_t receipt);
int sony_usb_source_ingress_next_event(struct sony_usb_source_ingress *,struct sony_usb_event *);
int sony_usb_source_ingress_begin_pcm(struct sony_usb_source_ingress *);
int sony_usb_source_ingress_poll_fd(struct sony_usb_source_ingress *);
int sony_usb_source_ingress_read(struct sony_usb_source_ingress *,void *,unsigned,unsigned *);
/* Normal EOF releases only the exhausted source; one binding FD spans sources. */
int sony_usb_source_ingress_close_stream(struct sony_usb_source_ingress *);
/* User cancellation closes the entire binding, once. Policy/owner stay held
 * until stop. An unfinished source is never reported as normally released. */
int sony_usb_source_ingress_cancel(struct sony_usb_source_ingress *);
int sony_usb_source_ingress_state(struct sony_usb_source_ingress *,struct sony_usb_ingress_state *);
int sony_usb_source_ingress_info(struct sony_usb_source_ingress *,struct sony_usb_source_ingress_info *);
int sony_usb_source_ingress_stop(struct sony_usb_source_ingress **,struct sony_usb_ingress_cleanup *,struct sony_usb_source_ingress_info *);
#endif
