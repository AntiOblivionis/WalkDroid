#ifndef WALKDROID_SONY_RECEIVER_PROTOCOL_H
#define WALKDROID_SONY_RECEIVER_PROTOCOL_H
#include <stdint.h>
#include <sys/types.h>
#define WDR_MAGIC 0x52534457u
#define WDR_VERSION 1u
#ifndef WDR_SOCKET
#define WDR_SOCKET "walkdroid.sony.receiver.v1"
#endif
#define WDR_BT_SOCKET "walkdroid.sony.bt.receiver.v1"
enum wdr_command {WDR_STATUS=1,WDR_USB_ON=2,WDR_OFF=3,WDR_BT_ON=4};
enum wdr_phase {WDR_OFFLINE=0,WDR_STARTING=1,WDR_READY=2,WDR_STREAMING=3,WDR_FAULT=4,WDR_STOPPING=5,WDR_RETAINED=6};
enum wdr_flags {WDR_ENABLED=1u,WDR_CLEAN=2u,WDR_CHILD_TERMINAL=4u,WDR_REFUSED=8u};
struct wdr_request {uint32_t magic,version,command,reserved;uint64_t request,session;};
struct wdr_reply {
    uint32_t magic,version,phase;int32_t error;
    uint64_t request,session,generation,source_frames,output_acked_bytes;
    uint32_t rate,flags;int32_t cleanup_error;uint32_t retained;
};
/* Seqpacket intake. Short messages are zero-filled, received descriptors are
 * closed, and the legacy last-request value is not used to reject requests. */
int wdr_receive(int fd,uint64_t last_request,struct wdr_request *);
#endif
