#ifndef WALKDROID_SONY_OWNER_COMMANDS_H
#define WALKDROID_SONY_OWNER_COMMANDS_H
#include "sony_audio_owner.h"
#include "sony_audio_dsf_worker_protocol.h"
/* Local protocol, explicitly enabled by a participating owner. Connections are
 * not authenticated by UID, PID, protocol version or a caller-supplied token. */
#define SONY_COMMAND_MAGIC 0x43534457u
#define SONY_COMMAND_VERSION 1u
enum sony_command_op {SONY_COMMAND_GET=1,SONY_COMMAND_STAGE=2,SONY_COMMAND_STATUS=3};
enum sony_command_phase {SONY_COMMAND_READY=1,SONY_COMMAND_APPLYING=2,SONY_COMMAND_APPLIED=3,
    SONY_COMMAND_REJECTED=4,SONY_COMMAND_FAILED=5};
struct sony_command_request {
    uint32_t magic,version,op,request;uint64_t generation;uint32_t revision,bytes;
};
struct sony_command_response {
    uint32_t magic,version,op,request;uint64_t generation;int32_t error;uint32_t phase;
    uint32_t bytes,operation,reserved[2];
};
struct sony_command_reply {struct sony_command_response header;struct wdf_controls state;};
struct sony_command_server;struct sony_command_client;
struct sony_command_ops {
    /* Same serialized controller as playback. read must validate native state,
     * owner/route and persistence; a native getter alone is insufficient.
     * stage succeeds with OLD+pending or a confirmed unchanged no-op. read may
     * clear pending only AFTER queue retirement, save/readback and route commit.
     * No server mutex/thread, retry, cancellation or automatic playback exists. */
    int (*read)(void *,struct wdf_controls *);
    int (*stage)(void *,uint64_t,unsigned,const struct sony_dsd_live_controls *,struct wdf_controls *);
    void *context;
};
int sony_command_server_open(struct sony_command_server **,struct sony_audio_owner *,const struct sony_command_ops *);
/* Nonblocking: bounded to one peer and at most one request. 0 idle, 1 handled
 * (also malformed/disconnected peer), negative owner/server failure. Idle
 * connection expires after 2 s BOOTTIME. Client
 * failure never cancels/replays a previously admitted native transaction. */
int sony_command_server_poll(struct sony_command_server *);
/* Current read-ready descriptor (listener or admitted peer), owned by server.
 * Observe only on its serialized controller thread. Never close or read it. */
int sony_command_server_descriptor(const struct sony_command_server *);
/* Close before finishing the owner. */
int sony_command_server_close(struct sony_command_server **);
int sony_command_client_open(struct sony_command_client **,uint64_t generation);
int sony_command_client_open_at(struct sony_command_client **,int directory,uint64_t generation);
/* Existing open/open_at provide a default kind; kind is not authentication. */
int sony_command_client_open_kind(struct sony_command_client **,uint64_t generation,enum sony_audio_owner_kind);
int sony_command_client_open_at_kind(struct sony_command_client **,int directory,uint64_t generation,enum sony_audio_owner_kind);
/* Exactly one outstanding message per connection; one STAGE may be pending.
 * request ids increase across GET/STAGE/STATUS, no retry/replay on this channel.
 * Return 0 means sent ONLY. On transport loss after send the outcome is UNKNOWN;
 * reconnect/read current state, do NOT resend a write automatically. STATUS is
 * scoped to the last STAGE on this connection; it cannot claim another writer's
 * result. Controls in replies are authoritative only at their observation time. */
int sony_command_client_send(struct sony_command_client *,enum sony_command_op,unsigned revision,const struct sony_dsd_live_controls *);
/* 0 validated reply (inspect header.error/phase), EAGAIN pending; other negative
 * errors seal/close transport. Output unchanged on failure. No blocking waits. */
int sony_command_client_receive(struct sony_command_client *,struct sony_command_reply *);
int sony_command_client_close(struct sony_command_client **);
#endif
