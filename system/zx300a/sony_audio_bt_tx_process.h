#ifndef WALKDROID_SONY_BT_TX_PROCESS_H
#define WALKDROID_SONY_BT_TX_PROCESS_H
#include "sony_audio_bt_tx_client.h"
#define WBT_PROCESS_MAGIC UINT32_C(0x31504257) /* WBP1, private LE ABI */
#define WBT_PROCESS_EXPLICIT_START UINT32_C(0x54585331)
enum wbt_process_event { WBT_PROCESS_READY=1,WBT_PROCESS_TERMINAL=2 };
enum wbt_process_flags { WBT_PROCESS_CLEAN=1,WBT_PROCESS_CHILDREN_REAPED=2,WBT_PROCESS_FORCED=4 };
struct wbt_process_report {
    uint32_t magic,version,event,sequence;
    uint64_t nonce,audio_generation,radio_generation;
    int32_t status,worker_exit,daemon_exit;
    uint32_t flags,pid,reserved;
};
struct sony_bt_tx_process;
struct sony_bt_tx_exit {
    struct wbt_process_report terminal;
    int terminal_received,reaped,child_lost,kill_sent,exit_code;
};
/* The caller supplies runtime and pair_template paths. The supervisor owns
 * its children and leases; the caller owns *out until stop reaps it. */
int sony_bt_tx_process_start(struct sony_bt_tx_process **,unsigned activation,
    const char *runtime,const char *pair_template,int log_fd,const uint8_t peer[6],unsigned rate,
    struct sony_bt_tx_exit *);
struct wbt_client *sony_bt_tx_process_client(struct sony_bt_tx_process *);
/* cancel=0 requests FINISH; other values close the channel. Stop attempts to
 * reap the child, but does not claim that external hardware cleanup was verified. */
int sony_bt_tx_process_stop(struct sony_bt_tx_process **,int cancel,unsigned timeout_ms,
    struct sony_bt_tx_exit *);
#endif
