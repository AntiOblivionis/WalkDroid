#ifndef WALKDROID_SONY_AUDIO_USB_MODE_H
#define WALKDROID_SONY_AUDIO_USB_MODE_H
#include "sony_audio_usb_duplex.h"
#include "sony_audio_usb_gadget_policy.h"
#include "sony_audio_dsd_power_linux.h"
struct sony_usb_mode;
#define SONY_USB_MODE_EXPLICIT_START 0x57554d31u
enum sony_usb_mode_phase {SONY_USB_MODE_ENTERING=1,SONY_USB_MODE_READY,
    SONY_USB_MODE_STREAMING,SONY_USB_MODE_FAULT,SONY_USB_MODE_STOPPING,
    SONY_USB_MODE_RETAINED,SONY_USB_MODE_OFF};
struct sony_usb_mode_state {
    enum sony_usb_mode_phase phase;
    uint64_t generation;
    unsigned enabled,gadget_active,stop_requested,cleanup_verified;
    int first_error,quiesce_error,cleanup_error;
    struct sony_usb_duplex_state duplex;
};
struct sony_usb_mode_cleanup {
    struct sony_usb_mode_state state;
    struct sony_usb_duplex_cleanup duplex;
    struct sony_usb_gadget_state gadget;
    unsigned resources_before[SONY_DSD_RESOURCE_COUNT],resources_after[SONY_DSD_RESOURCE_COUNT];
    unsigned resources_captured,resources_restored,usb_snapshot_verified;
    int duplex_stop,affinity_restore,boost_release,boost_destroy,monitor_close;
    int gadget_restore,gadget_destroy,gadget_linux_destroy,resource_readback;
    int idle_readback,platform_destroy,cpu_release,independent_idle,owner_finish;
};
/* Formal outer USB mode, for a dedicated root controller process with prepared
 * read-only roots and logs. Uses the coordinated lights HAL/boot initialization.
 * Owns global USB lease, CPU governor+resources+affinity, duplex, monitor and
 * gadget for the ENTIRE enabled mode, including READY and FAULT with no audio.
 * No duration/source-file test condition. No mount, daemon or Android UI here.
 * On any nonNULL start failure keep this object, display FAULT and call stop
 * only for an explicit OFF/client death. Never replace/restart it on failure.
 * Roots, config path, observer and their lifetimes stay borrowed through stop.
 * Calls serialize on the mode object. */
int sony_usb_mode_start(struct sony_usb_mode **,unsigned activation,const struct sony_usb_duplex_launch *);
/* Process one bounded block/event. EAGAIN/EINTR only while the underlying input
 * is healthy. Any other failure quiesces input/output once and retains mode
 * exclusivity; no automatic return to Android audio or source replay. */
int sony_usb_mode_step(struct sony_usb_mode *,unsigned maximum_frames);
int sony_usb_mode_state(struct sony_usb_mode *,struct sony_usb_mode_state *);
/* Borrowed netlink FD for poll only, never read/close. After EAGAIN wait on
 * this plus the external control FD. Active capture timeout=20ms, idle=-1.
 * FAULT/RETAINED returns fd=-1,timeout=-1: wait only for explicit control. */
int sony_usb_mode_wait(struct sony_usb_mode *,int *event_fd,int *timeout_ms);
/* OFF closes upstream, output and children, then attempts to restore affinity,
 * CPU resources and USB functions before releasing the global owner. Actual
 * operation errors are returned; the cleanup does not verify hardware state. */
int sony_usb_mode_stop(struct sony_usb_mode **,int timeout_ms,struct sony_usb_mode_cleanup *);
#endif
