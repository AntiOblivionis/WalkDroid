#ifndef WALKDROID_SONY_BT_TX_LINK_CLIENT_H
#define WALKDROID_SONY_BT_TX_LINK_CLIENT_H
#include "sony_audio_bt_tx_link.h"
#include <sys/types.h>
/* Serialized controller-thread API. Failure closes the exact transport;
 * no implicit reconnect/replay. Caller owns socket/child/leases. Native
 * CLOSED is not process/FD cleanup. Calls using inherited sockets need
 * coordination because shutdown affects all descriptor copies. */
struct wbl_client {
    pid_t creator;
    int fd,cancel_fd;
    uint64_t nonce,epoch,accepted,submitted;
    uint32_t next_request,phase;
    unsigned input_rate,codec_rate,accepted_tail;
    struct wbl_reply last_reply;
};
int wbl_client_init(struct wbl_client *,int fd,int cancel_fd,uint64_t);
int wbl_client_connect(struct wbl_client *,const uint8_t peer[6]);
int wbl_client_resume(struct wbl_client *,unsigned rate);
int wbl_client_write(struct wbl_client *,const void *,unsigned bytes);
int wbl_client_pause(struct wbl_client *);
/* Discard this PCM epoch while retaining the independently owned RF link.
 * Used for a vanished HAL attachment; never replays its accepted samples. */
int wbl_client_drop(struct wbl_client *);
int wbl_client_close(struct wbl_client *);
int wbl_client_cancel(struct wbl_client *);
#endif
