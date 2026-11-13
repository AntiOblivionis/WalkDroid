#ifndef SONY_AUDIO_RECEIVER_ASYNC_H
#define SONY_AUDIO_RECEIVER_ASYNC_H
#include "sony_audio_receiver_session.h"
struct sony_receiver_async;
#define SONY_RECEIVER_ASYNC_EXPLICIT_START 0x57524131u
#define SONY_RECEIVER_ASYNC_MAX_FRAMES 3584u
#define SONY_RECEIVER_ASYNC_MAX_QUEUED_NS UINT64_C(2000000000)
struct sony_receiver_async_state {
    uint64_t offered_frames,pushed_frames,discarded_frames,failed_frames,finished_streams;
    uint64_t queued_frames,inflight_frames,queued_ns,inflight_ns,producer_stream;
    struct sony_receiver_session_state output;
    struct sony_receiver_playback_cleanup last_stream;
    unsigned queued_jobs,worker_busy,stopping,exited;
    int first_error,finish_status,close_status,state_status;
    uint64_t volume_offered,volume_applied,volume_discarded,volume_failed;
    unsigned volume_queued,volume_inflight,last_volume,last_volume_revision;
};
/* Borrow an already-started session and its owner. Session calls are made by
 * the worker until it is joined. Copied native stereo data and source barriers use
 * one bounded FIFO (<=2s including the current write, <=512 pending jobs).
 * DATA reserves two job slots for its FINISH and the next source's OPEN.
 * Worker may combine adjacent already-queued DATA of the same source within
 * the existing maximum batch. It never waits to fill a batch, crosses source
 * barriers or releases any duration reservation before the combined write.
 * The physical pool covers2s at the highest admitted USB384kHz or BT96kHz;
 * BT reserves1,536,000bytes instead of the USB6,144,000byte PCM pool.
 * Failure returns *out=NULL without taking session ownership. */
int sony_receiver_async_start(struct sony_receiver_async **,unsigned activation,
    struct sony_receiver_session *,struct sony_audio_owner *);
/* BT entry uses the smaller pool. Native S16/LDAC containers stay unchanged. */
int sony_receiver_async_start_bt(struct sony_receiver_async **,unsigned activation,
    struct sony_receiver_session *,struct sony_audio_owner *);
/* Explicit live-volume variant. Session must already own a LIVE_VOLUME
 * command channel. Worker waits on commands, save completion and FIFO wake
 * events; the caller never polls that channel or touches the session until
 * stop has joined. This is not enabled for existing USB/BT entry points. */
int sony_receiver_async_start_bt_controls(struct sony_receiver_async **,unsigned activation,
    struct sony_receiver_session *,struct sony_audio_owner *);
/* Stream values are caller metadata; enqueue order defines OPEN/DATA/FINISH. */
int sony_receiver_async_open(struct sony_receiver_async *,uint64_t stream,
    const struct sony_usb_stream *,unsigned expected_output);
int sony_receiver_async_open_bt(struct sony_receiver_async *,uint64_t stream,
    const struct sony_bt_pcm_stream *,unsigned expected_output);
/* No blocking waits or discarded input on full: EAGAIN means enqueue NOTHING.
 * Caller may use accepts before consuming input. */
int sony_receiver_async_accepts(struct sony_receiver_async *,uint64_t,unsigned frames);
int sony_receiver_async_push(struct sony_receiver_async *,uint64_t,const void *,unsigned frames);
int sony_receiver_async_finish(struct sony_receiver_async *,uint64_t);
/* Relative-volume event. Mode-wide:
 * valid while waiting for audio and after a closed stream. Shares the same
 * bounded worker FIFO as OPEN/DATA/FINISH, so DATA coalescing cannot cross it.
 * Worker reads fresh controls, clamps to the original0..120/current AVLS rule,
 * and calls the existing Sony DSP/DAC setter.
 * No PCM scaling, direct callback hardware IO, absolute-volume mapping or
 * caller-thread session use. EAGAIN enqueues nothing. Stop accounts queued
 * unapplied events as discarded. Do not concurrently enable another session
 * controller; all mutations must be serialized through this worker. */
int sony_receiver_async_volume_step(struct sony_receiver_async *,int step);
/* Snapshot of actual COMPLETED worker calls. worker_busy/inflight explicitly
 * identify work not yet acknowledged; this is not a fresh hardware admission
 * check or proof of physical consumption. Session guards still run on every
 * actual native write in the owning worker thread. State remains readable on
 * data/lease errors so outer cleanup can preserve the first error. */
int sony_receiver_async_state(struct sony_receiver_async *,struct sony_receiver_async_state *);
/* Explicit cancellation: seal producer, count and discard queued-but-unoffered
 * data, finish/close the current output after its in-flight call returns. No
 * pthread cancellation, child kill, source replay, waitpid or global CLEAN. */
int sony_receiver_async_request_stop(struct sony_receiver_async *);
/* Nonblocking completion observation. EAGAIN retains the exact live worker,
 * session, owner and all dependent contexts. Only0 joins/frees this wrapper;
 * caller must THEN stop/reap/verify its still-owned session and other modules.
 * Data/finish/close errors remain in out and are not mistaken for join failure. */
int sony_receiver_async_stop(struct sony_receiver_async **,struct sony_receiver_async_state *);
#endif
