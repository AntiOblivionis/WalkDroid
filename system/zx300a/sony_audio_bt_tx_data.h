#ifndef WALKDROID_SONY_BT_TX_DATA_H
#define WALKDROID_SONY_BT_TX_DATA_H
#include "sony_audio_bt_tx_link_client.h"
#include <sys/types.h>
/* WBD1 carries PCM between HAL and the connection service.
 * No command can CONNECT/DISCONNECT RF, choose a peer or supply a path.
 * DETACHED means this attachment ended, never that RF/leases are CLEAN. */
#define WBD_MAGIC UINT32_C(0x31444257)
#define WBD_VERSION 1u
enum wbd_command {WBD_ATTACH=1,WBD_RESUME=2,WBD_WRITE=3,WBD_PAUSE=4,WBD_DETACH=5,WBD_DROP=7};
enum wbd_phase {WBD_NEW=0,WBD_IDLE=1,WBD_STREAMING=2,WBD_DETACHED=3,WBD_FAILED=4};
enum wbd_flags {WBD_SUBMISSION_ONLY=1,WBD_RF_CONNECTED=2,WBD_PROCESSING_STATS=4,WBD_ATTACHMENT_CLOSED=8};
struct wbd_request {
    uint32_t magic,version,command,request;
    uint64_t route,session,epoch,frame_position;
    uint32_t payload_bytes,rate,channels,bits;
    uint8_t reserved[16];
};
struct wbd_reply {
    uint32_t magic,version,command,request;
    uint64_t route,session,epoch,accepted_frames,submitted_frames;
    int32_t status;
    uint32_t phase,queued_bytes,flags;
    uint64_t uncertain_frames;
    struct sony_tx_pcm_stats processing;
};
struct wbd_server;
/* The server retains its own FD copy; caller closes its accepted copy. Route,
 * nonce and PID are carried for bookkeeping rather than request admission. */
int wbd_server_open(struct wbd_server **,int fd,pid_t expected_pid,
    uint64_t route,uint64_t nonce,struct wbl_client *link);
int wbd_server_step(struct wbd_server *);
/* Drops an owned PCM epoch and closes this attachment; RF is not disconnected.
 * A shared or replaced native epoch may be affected by this drop. */
int wbd_server_close(struct wbd_server **);
int wbd_server_phase(const struct wbd_server *);
int wbd_server_events(const struct wbd_server *);
struct wbd_client {
    pid_t creator;
    int fd,cancel_fd;
    uint64_t route,nonce,epoch,accepted,submitted;
    uint32_t next_request,phase;
    unsigned input_rate,codec_rate;
    struct wbd_reply last_reply;
};
/* The server PID argument is retained for source compatibility. */
int wbd_client_init(struct wbd_client *,int fd,int cancel_fd,uint64_t route,pid_t server);
int wbd_client_attach(struct wbd_client *);
int wbd_client_resume(struct wbd_client *,unsigned rate);
int wbd_client_write(struct wbd_client *,const void *,unsigned bytes);
int wbd_client_pause(struct wbd_client *);
int wbd_client_drop(struct wbd_client *);
int wbd_client_detach(struct wbd_client *);
#endif
