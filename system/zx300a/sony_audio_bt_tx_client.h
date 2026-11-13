#ifndef WALKDROID_SONY_BT_TX_CLIENT_H
#define WALKDROID_SONY_BT_TX_CLIENT_H
#include "sony_audio_bt_tx_session.h"
/* Serialized broker/controller-thread API; not thread-safe. Caller owns the
 * socket, native child and leases. Actual I/O or worker errors shut down this
 * socket; cancel_fd, if >=0, is a readable cancellation pipe/eventfd. Reply
 * identifiers and counters are exposed as received and do not gate calls. */
struct wbt_client {
    int fd,cancel_fd;
    uint64_t nonce,accepted,submitted;
    uint32_t next_request,phase;
    unsigned input_rate,codec_rate,processing_stats;
    struct wbt_reply last_reply;
};
int wbt_client_init(struct wbt_client *,int fd,int cancel_fd,uint64_t nonce);
int wbt_client_open(struct wbt_client *,const uint8_t peer[6],unsigned rate);
int wbt_client_write(struct wbt_client *,const void *,unsigned bytes);
int wbt_client_finish(struct wbt_client *);
int wbt_client_cancel(struct wbt_client *);
#endif
