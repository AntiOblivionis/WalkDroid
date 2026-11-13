#ifndef SONY_AUDIO_RECEIVER_SINK_PROCESS_H
#define SONY_AUDIO_RECEIVER_SINK_PROCESS_H
#include "sony_audio_pcm_sink_client.h"
#include "sony_audio_dsd_power_wire.h"
#define SONY_RECEIVER_SINK_PROCESS_EXPLICIT_START 0x57525031u
#define SONY_RECEIVER_SINK_PROCESS_CLEANUP_PENDING -120
#define SONY_RECEIVER_SINK_PROCESS_CHILD_LOST -121
struct sony_receiver_sink_process;
struct sony_receiver_sink_process_scope {
    uint64_t session,generation,receipt;
    unsigned cfw_profile,mode; /* Original1 USB,2 BT receiver. */
    struct sony_dsd_power_backend backend;
    int (*assure)(void *,unsigned); /* Actual parent legacy policy1..5. */
    /* Retained exact mode lease; independent of user new-playback admission.
     * Survives stream Close/reopen and every pending cleanup receipt. */
    int (*owner_check)(void *);
    void *context;
};
struct sony_receiver_sink_process_exit {
    int spawned_pid,reaped,exit_code,signal_number,kill_sent,child_lost,launch_stage,launch_errno;
    int power_cleanup_confirmed,power_cleanup_status,output_cleanup_unverified;
    struct sony_dsd_power_wire_state power;
};
/* Explicit Android-root launch, verified target and generated immutable root.
 * No mount/owner acquisition/route/unmute; only exact original PCM1/2 worker,
 * fixed args/env, two WDP pipes, log and authenticated WKP7 fd3. Resource plan
 * stays held across stream Close/reopen until Shutdown/EOF and PCM closure.
 * Caller retains mode lease/backend until stop succeeds; nonNULL failure
 * handles must be stopped, never overwritten or used to launch a replacement.
 * One serialized controller, no concurrent waitpid or SIGCHLD reaper. */
int sony_receiver_sink_process_start(struct sony_receiver_sink_process **,unsigned activation,
    int root_fd,int log_fd,const struct sony_receiver_sink_process_scope *,int timeout_ms,
    struct sony_receiver_sink_process_exit *);
struct sony_pcm_sink_client *sony_receiver_sink_process_client(struct sony_receiver_sink_process *);
int sony_receiver_sink_process_pid(struct sony_receiver_sink_process *);
int sony_receiver_sink_process_state(struct sony_receiver_sink_process *,struct sony_receiver_sink_process_exit *);
/* Bounded exact-child reap/kill while servicing resource cleanup; lost child
 * or uncertain release retains ownership. No global CLEAN/DAC proof implied. */
int sony_receiver_sink_process_stop(struct sony_receiver_sink_process **,int timeout_ms,struct sony_receiver_sink_process_exit *);
#endif
