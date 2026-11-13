#ifndef SONY_AUDIO_USB_SOURCE_DEVICE_H
#define SONY_AUDIO_USB_SOURCE_DEVICE_H
#include "sony_audio_usb_source_queue.h"
#ifdef __KERNEL__
#include <linux/ioctl.h>
#else
#include <sys/ioctl.h>
#endif
#define SONY_SOURCE_DEVICE_NODE "/dev/walkdroid-uac-source"
#define SONY_SOURCE_DEVICE_PARENT_LIVE 1u
#define SONY_SOURCE_DEVICE_CONSUMER 2u
#define SONY_SOURCE_DEVICE_FAULTED 4u
struct sony_source_caps {
    sony_sq_u32 magic,version,bytes,capacity;
    sony_sq_u64 instance;
    sony_sq_u32 descriptor_limit,queued_sources;
    sony_sq_u64 accepted_bytes,consumed_bytes,delivered_bytes;
    sony_sq_s32 error;
    sony_sq_u32 flags;
};
#define SONY_SOURCE_FEEDBACK_UNUSED 0u
#define SONY_SOURCE_FEEDBACK_NOMINAL 1u
#define SONY_SOURCE_FEEDBACK_TRACKING 2u
#define SONY_SOURCE_FEEDBACK_REVOKED 3u
/* Last actual feedback callback, not playback admission or DAC consumption.
 * sample increases only when the source-FD path handles that callback.
 * queued/target are meaningful only in TRACKING. Fixed96byte ARM32 ABI. */
struct sony_source_feedback_info {
    sony_sq_u32 magic,version,bytes,state;
    sony_sq_u64 instance,source_id,sample,initial_frames,queued_frames,target_frames;
    sony_sq_u32 rate,threshold,plus,minus,nominal,clock;
    sony_sq_s32 error;
    sony_sq_u32 reserved;
};
#define SONY_SOURCE_IOCTL_CAPS _IOR('S',0x70,struct sony_source_caps)
#define SONY_SOURCE_IOCTL_PEEK _IOR('S',0x71,struct sony_source_info)
/* CLAIM returns information for the oldest queued source. */
#define SONY_SOURCE_IOCTL_CLAIM _IOWR('S',0x72,struct sony_source_info)
#define SONY_SOURCE_IOCTL_RELEASE _IOW('S',0x73,sony_sq_u64)
#define SONY_SOURCE_IOCTL_FEEDBACK _IOR('S',0x74,struct sony_source_feedback_info)
/* One concurrent consumer per USB binding; a closed FD may be reopened.
 * The device does not compare CLAIM/RELEASE inputs to source identifiers.
 *
 * read takes >=sizeof(record)+8 bytes and returns one native-layout record
 * followed by at most 32768 payload bytes. A zero-payload record indicates
 * source EOF. RELEASE drops the oldest source's unread bytes. The record's
 * offset and reservation are informational, including when they wrap to zero.
 */
#endif
