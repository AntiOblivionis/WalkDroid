#ifndef WALKDROID_SONY_BT_TX_VOLUME_CHANNEL_H
#define WALKDROID_SONY_BT_TX_VOLUME_CHANNEL_H
#include "sony_audio_bt_tx_volume.h"
#include <sys/types.h>
struct wva_client {
 int fd,failed;pid_t creator;
 uint64_t nonce;
 uint32_t next,pending,pending_operation;
 struct wva_snapshot state;
};
int wva_client_init(struct wva_client *,int fd,uint64_t nonce);
/* Zero means sent exactly once. Pending survives until a non-event reply;
 * EAGAIN before send is unsent. There is no internal retry or timeout replay. */
int wva_client_submit(struct wva_client *,unsigned operation,uint64_t expected,int value);
/* 0 event,1 non-event reply,-EAGAIN no packet,other negative terminal. */
int wva_client_observe(struct wva_client *,struct wva_reply *);
struct wva_server {
 int fd,pending,blocked;
 uint64_t nonce,sent_revision;
 uint32_t next;
 struct sony_tx_volume *volume;
 struct wva_reply reply;
};
int wva_server_init(struct wva_server *,int fd,uint64_t nonce,struct sony_tx_volume *);
short wva_server_events(const struct wva_server *);
/* One bounded nonblocking service step. Responses are retained on EAGAIN;
 * state events coalesce to the latest revision without replaying commands. */
int wva_server_step(struct wva_server *,short events);
#endif
