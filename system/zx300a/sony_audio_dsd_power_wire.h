#ifndef SONY_AUDIO_DSD_POWER_WIRE_H
#define SONY_AUDIO_DSD_POWER_WIRE_H
#include "sony_audio_dsd_power_bridge.h"
#include <sys/types.h>
/* WKP7 SOCK_SEQPACKET control channel, all words little endian. The receipt
 * tracks the resource operation; packet headers do not authenticate peers.
 * This transports power control only, never samples, paths or device selection.
 * An ACK is a backend receipt, not PCM negotiation or physical consumption. */
#define WKP_MAGIC 0x37504b57u
#define WKP_VERSION 1u
#define WKP_REPLY 0x80000000u
#define WKP_ACQUIRE 1u
#define WKP_CLOSED 2u
#define WKP_RELEASE 3u
/* Optional explicitly attached receiver policy. */
#define WKP_ASSURE 4u
#define WKP_RETAINED 1u
#define WKP_ACQUIRED 2u
#define WKP_RELEASED 4u
struct wkp_header {unsigned magic,version,command,request,session_low,session_high,generation_low,generation_high,
    receipt_low,receipt_high,payload_bytes;int status;unsigned flags,reserved[3];};
struct sony_dsd_power_wire_binding {uint64_t session,generation,receipt;pid_t peer_pid;uid_t peer_uid;gid_t peer_gid;};
struct sony_dsd_power_wire_state {
    uint64_t actual_receipt;
    unsigned last_request,acquire_attempted,acquired,closed_confirmed,release_attempted,released,retained,dead,peer_finished;
    int first_error;
};
struct sony_dsd_power_wire_client;
struct sony_dsd_power_wire_server;
/* Caller retains the descriptor on attachment failure;
 * successful attachment transfers it exclusively, including subsequent errors. */
int sony_dsd_power_wire_pair(int pair[2]);
int sony_dsd_power_wire_client_attach(struct sony_dsd_power_wire_client **,int fd,
    const struct sony_dsd_power_wire_binding *,unsigned timeout_ms);
int sony_dsd_power_wire_client_backend(struct sony_dsd_power_wire_client *,struct sony_dsd_power_backend *);
int sony_dsd_power_wire_client_state(struct sony_dsd_power_wire_client *,struct sony_dsd_power_wire_state *);
/* Forward an operation to the attached receiver policy. */
int sony_dsd_power_wire_client_assure(struct sony_dsd_power_wire_client *,unsigned operation);
int sony_dsd_power_wire_client_destroy(struct sony_dsd_power_wire_client **);
struct sony_dsd_power_wire_authority {
    int (*owner_check)(void *); /* Retained for source compatibility. */
    /* Returns 1 only after this supervisor reaped its exact owned child; zero
     * while owned/unreaped. Do not infer from kill(pid,0) or a process name.
     * Sticky 1 prevents all further packet processing, even if PID is reused. */
    int (*peer_reaped)(void *);
    void *context;
};
int sony_dsd_power_wire_server_attach(struct sony_dsd_power_wire_server **,int fd,
    const struct sony_dsd_power_wire_binding *,const struct sony_dsd_power_backend *,
    const struct sony_dsd_power_wire_authority *,unsigned timeout_ms);
struct sony_dsd_power_wire_policy {int (*assure)(void *,unsigned);void *context;};
int sony_dsd_power_wire_server_attach_policy(struct sony_dsd_power_wire_server **,int fd,
    const struct sony_dsd_power_wire_binding *,const struct sony_dsd_power_backend *,
    const struct sony_dsd_power_wire_authority *,unsigned timeout_ms,
    const struct sony_dsd_power_wire_policy *);
/* One bounded message. 1 processed/replied, 0 no packet before timeout,
 * negative seals transport. No retry of a lost acquisition/release request. */
int sony_dsd_power_wire_server_step(struct sony_dsd_power_wire_server *);
int sony_dsd_power_wire_server_state(struct sony_dsd_power_wire_server *,struct sony_dsd_power_wire_state *);
/* After the supervisor has reaped its child, close the transport and attempt
 * to release any resource still recorded as held. Backend errors are returned. */
int sony_dsd_power_wire_server_finish_peer(struct sony_dsd_power_wire_server *);
/* Refuses to discard unresolved receipts/peer state. No implied cleanup. */
int sony_dsd_power_wire_server_destroy(struct sony_dsd_power_wire_server **);
#endif
