#ifndef WALKDROID_SONY_BT_TX_LINK_PROCESS_H
#define WALKDROID_SONY_BT_TX_LINK_PROCESS_H
#include "sony_audio_bt_tx_link_client.h"
#include "sony_audio_bt_tx_volume_channel.h"
#include "sony_audio_bt_tx_process.h" /* WBP1 supervisor report/exit ABI only */
#define WBL_PROCESS_EXPLICIT_CONNECT UINT32_C(0x4c4e4b33)
struct sony_bt_link_process;
/* Controller API. Connect owns both leases and original
 * peer across PCM epochs; no automatic reconnect. *out retained if unreaped. */
int sony_bt_link_process_connect(struct sony_bt_link_process **,unsigned activation,
    const char *runtime,const char *seed,int log_fd,const uint8_t peer[6],struct sony_bt_tx_exit *);
/* Optional readable cancellation FD, owned by the caller. Allows a broker's
 * parent-death/control event to abort slow preparation without replay. */
int sony_bt_link_process_connect_cancel(struct sony_bt_link_process **,unsigned activation,
    const char *runtime,const char *seed,int log_fd,const uint8_t peer[6],int cancel_fd,struct sony_bt_tx_exit *);
struct wbl_client *sony_bt_link_process_client(struct sony_bt_link_process *);
/* Explicit opt-in requires a runtime containing WVA1 worker/supervisor.
 * Legacy connect never sends extra arguments to a frozen older runtime. */
int sony_bt_link_process_connect_volume_cancel(struct sony_bt_link_process **,unsigned activation,
    const char *runtime,const char *seed,int log_fd,const uint8_t peer[6],int cancel_fd,struct sony_bt_tx_exit *);
struct wva_client *sony_bt_link_process_volume(struct sony_bt_link_process *);
/* Poll this FD; only observe/stop may read it. No periodic watcher required.
 * observe:0 still active,1 terminal report,-errno transport/protocol failure.
 * Terminal does NOT reap the child or retire the seed; stop remains required. */
int sony_bt_link_process_events(struct sony_bt_link_process *);
int sony_bt_link_process_observe(struct sony_bt_link_process *,struct sony_bt_tx_exit *);
/* cancel0 drains active PCM and closes the link. cancel1 discards queued PCM
 * and closes it. Then join exact terminal report, child reap and cleanup.
 * Lost ACK/unclean exit never authorizes replay or clearing an owner lease. */
int sony_bt_link_process_stop(struct sony_bt_link_process **,int cancel,unsigned timeout_ms,struct sony_bt_tx_exit *);
#endif
