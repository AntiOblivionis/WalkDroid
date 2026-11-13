#ifndef WALKDROID_SONY_CHAIN_BRIDGE_H
#define WALKDROID_SONY_CHAIN_BRIDGE_H
#include "sony_audio_output_controls.h"
#include "sony_audio_dsee_controls.h"
#include "sony_audio_dsd_controls.h"
/* Experimental original ARM32/glibc master-input adapter, not deployment ready.
 * Caller supplies the original library and Sony config transport/resources;
 * different library ABIs and DSEE compatibility are not established.
 * Serialize all calls; update acknowledges requests, not asynchronous settling.
 * Default open/legacy process: 48k stereo interleaved S32LE, 512 frames/call.
 * Explicit open_rate records the caller's PCM rate; the legacy API stays 48k/512.
 * abort_close discards tails. A normal drain API is deliberately not claimed.
 */
struct sony_chain_bridge;
struct sony_chain_settings {
    int direct, volume, eq_enabled, bands[10];
    int dc_enabled, dc_mode, vinyl_enabled, vinyl_mode;
};
struct sony_tone_settings {int enabled,gains[3],centerfreq[3];};
/* Experimental tone extension; settings are passed to the native backend.
 * It does not alter the 68-byte WDC2 settings layout. */
int sony_chain_update_tone(struct sony_chain_bridge *,const struct sony_tone_settings *);
int sony_chain_update_output(struct sony_chain_bridge *,const struct sony_output_controls *);
int sony_chain_update_dsee(struct sony_chain_bridge *,const struct sony_dsee_controls *);
/* Original DynamicNormalizer switch, independent of Android loudness effects.
 * Accepted command only; caller must confirm original readback after real PCM
 * and coordinate snapshot persistence. This extension is not in B105 binaries.
 */
int sony_chain_update_normalizer(struct sony_chain_bridge *,int enabled);
int sony_chain_open(struct sony_chain_bridge **,void *audited_library,const struct sony_chain_settings *);
/* Additional native input family. Existing open defaults to 48000. */
int sony_chain_open_rate(struct sony_chain_bridge **,void *audited_library,const struct sony_chain_settings *,unsigned int input_rate);
#ifdef SONY_CHAIN_AI_BOUNDARY
#include "sony_audio_pcm_boundary.h"
/* Internal additive AI entry. Call only on a private 44.1/48k chain configured
 * with native HX gate ON and source-direct OFF. Callback returns 0/2048 float
 * frames; the final output may be trimmed by its owning adapter. Legacy APIs
 * retain their fixed-duration contract and cannot call this implicitly. */
int sony_chain_process_transform(struct sony_chain_bridge *,const int *,unsigned int,
    sony_pcm_float_transform,void *,int *,unsigned int,unsigned int *,unsigned int *);
#endif
/* USB receiver entry. Caller supplies a 25-word TrackParam; this adapter does
 * not compare it with a fixed tuple table. The native implementation may still
 * reject or misinterpret unsupported fields. No device, route or persistence
 * is provided. */
int sony_chain_open_receiver_usb_pcm(struct sony_chain_bridge **,void *audited_library,
    const struct sony_chain_settings *,const unsigned int track[25]);
/* Serialized receiver volume notification. expected is retained for API
 * compatibility and is not compared. A native failure is returned; a later
 * call may try again. This does not write the DAC or prove persistence. */
int sony_chain_receiver_volume(struct sony_chain_bridge *,unsigned int expected,unsigned int volume);
struct sony_receiver_chain_result {unsigned int track[25],bytes,frames;};
/* Native input bytes use the supplied container width; no conversion is done.
 * Input must fit the native packet allocation and output must fit capacity.
 * Actual output type/rate/metadata comes from Sony and may change through effects.
 * Zero output is accepted as native buffering, not output/hardware completion.
 * Output/data unchanged on error; native failure faults the lifetime, no retry.
 * Input, output, result are disjoint. All calls have one serialized owner. */
int sony_chain_process_receiver_pcm(struct sony_chain_bridge *,const void *input,
    unsigned int frames,void *output,unsigned int capacity_bytes,struct sony_receiver_chain_result *);
/* Explicit original service boundary, additive to the raw DSP entry above.
 * Attenuate -> actual FilterIn -> native conditional FadeIn/FadeOut; original
 * HEAD/STOP-tail/END-tail markers are preserved through the DSP. Caller owns
 * the original pending queue/terminal decision; HEAD is required exactly once
 * on first source input, tails seal further source input. HEAD may combine with
 * one tail for a short stream. STOP-tail requests FadeOut; END-tail does not.
 * Generated silence follows original direct-module path, preserving complete
 * native packet boundaries into DSP (no512-frame subdivision). This is not
 * complete USB capture, queue, stop control, renderer close or ownership logic.
 * Existing raw process API rejects a service lifetime, and vice versa.
 */
#define SONY_RECEIVER_HEAD 1u
#define SONY_RECEIVER_STOP_TAIL 2u
#define SONY_RECEIVER_END_TAIL 4u
int sony_chain_open_receiver_usb_service(struct sony_chain_bridge **,void *audited_library,
    const struct sony_chain_settings *,const unsigned int track[25]);
/* Original Bluetooth receiver metadata, independently captured from
 * TransferCodec/TransferChannel/CreateAudioTrack. SBC/AAC use native S16;
 * LDAC has24 valid bits in its original four-byte container. This is the
 * original service DSP stage, not a radio, output route or shared mixer.
 * Explicit BT entry prevents confusing its codec/extra fields with UAC. */
int sony_chain_open_receiver_bt_service(struct sony_chain_bridge **,void *audited_library,
    const struct sony_chain_settings *,const unsigned int track[25]);
int sony_chain_process_receiver_service(struct sony_chain_bridge *,const void *input,
    unsigned int frames,unsigned int flags,void *output,unsigned int capacity_bytes,struct sony_receiver_chain_result *);
typedef int (*sony_receiver_chain_sink)(void *,const void *,const struct sony_receiver_chain_result *);
/* Ask the native backend to generate padding using the supplied duration.
 * Data is processed up to the requested packet capacity, with incomplete
 * trailing frames dropped; bytes and metadata are not compared to constants.
 * Callback may not reenter. ACK counts delivered native bytes only, not
 * physical playback. This primitive does not choose the receiver stop policy. */
int sony_chain_render_silence_receiver_pcm(struct sony_chain_bridge *,unsigned int ms,
    sony_receiver_chain_sink,void *,unsigned int *acknowledged_bytes);
/* Explicit original track::stop, once; permits prior explicit send_eos. No
 * implicit padding/EOS, renderer drain, hardware close or owner release. */
int sony_chain_stop_receiver_pcm(struct sony_chain_bridge *);
/* Separate experimental DSD entry. Canonical input: stereo MSB-first byte
 * interleaving, exactly 32768 bytes per packet; DSF/DFF parsing is not provided.
 * Controls/output/direct are immutable in this session. Reopen to change them.
 * Original ResolveProcCond and original full master chain remain authoritative.
 * Output bytes, rate and format come from the native chain's returned packet.
 * Result rate is bit rate for native DSD, frame rate for PCM. No ALSA output,
 * tail padding, drain or persistence is implied. Not yet a deployed feature. */
int sony_chain_open_dsd(struct sony_chain_bridge **,void *audited_library,
    const struct sony_chain_settings *,const struct sony_dsd_controls *,
    const struct sony_output_controls *,unsigned int input_rate);
/* Full initial effect selection, applied before the first DSD packet.
 * tone=NULL selects EQ10 from settings; non-NULL selects EqTone instead.
 * dsee is required here. Original DSD capability/direct rules determine which
 * effects actually process the stream; acceptance does not override those rules.
 * Legacy open_dsd retains its immutable EQ10/request and inherited-DSEE
 * semantics. Configured sessions may use the explicit live API below. */
int sony_chain_open_dsd_configured(struct sony_chain_bridge **,void *audited_library,
    const struct sony_chain_settings *,const struct sony_dsd_controls *,
    const struct sony_output_controls *,unsigned int input_rate,
    const struct sony_tone_settings *tone,const struct sony_dsee_controls *dsee);
/* Additive live-control admission for configured DSD->PCM sessions. Legacy DSD
 * open remains immutable. One serialized controller, no calls from callbacks.
 * direct and DSD mode must match current values pending output negotiation.
 * Master volume sends the original notification; its native Get is unsupported.
 * It is NOT a hardware-volume receipt. Caller must coordinate DAC volume, native
 * limits, persistence and hardware readback while muted before publishing it.
 * EQ/tone/DC/vinyl/DSEE and DSD conversion filter/gain may change. In EQ mode,
 * inactive tone storage is retained and is not an authoritative snapshot of
 * unused saved tone values. No persistence, route change or playback ACK here.
 * Stage is not confirmation: commit needs actual successful DSD packets and
 * original getters. Filter/gain need >=2 packets (native getter leads samples
 * by one packet). No synthetic audio is injected. Original filter transition
 * transients remain original behavior; confirmation is not end of crossfade.
 * Revision starts at 1. Same-state request is a no-op. Get returns acknowledged
 * controls only; pending is separate. Stale revision or overlapping stage is
 * rejected before native mutation. At EOF with pending update, commit may stay
 * pending; caller must not save/display it as applied. Abort/close is permitted.
 * Returns: 0 success, commit 1 pending; -1 invalid/unsupported state, -2 native
 * failure, -3 unconfirmed after 8 real packets, -4 stale revision, -5 busy stage.
 * Native/confirmation failures fault the session. Invalid/rejected inputs do
 * not. Output arguments are unchanged on errors/pending. */
struct sony_dsd_live_controls {
    struct sony_chain_settings settings;
    struct sony_tone_settings tone;
    struct sony_dsee_controls dsee;
    struct sony_dsd_controls dsd;
    unsigned int selector;
};
int sony_chain_dsd_get_controls(struct sony_chain_bridge *,struct sony_dsd_live_controls *,unsigned int *revision,unsigned int *pending);
int sony_chain_dsd_stage_controls(struct sony_chain_bridge *,unsigned int expected_revision,const struct sony_dsd_live_controls *);
int sony_chain_dsd_commit_controls(struct sony_chain_bridge *,unsigned int *confirmed_revision);
/* Input chunks are limited to SONY_DSD_PACKET_BYTES so that the existing native
 * packet allocation can accommodate its conversion output. Caller outputs remain
 * unchanged on error. An output that exceeds the provided capacity is reported
 * after native processing and faults the session; no retry. */
int sony_chain_process_dsd(struct sony_chain_bridge *,const unsigned char *input,
    unsigned int input_bytes,unsigned char *output,unsigned int output_capacity,
    struct sony_dsd_result *);
typedef int (*sony_chain_dsd_tail_sink)(void *,const unsigned char *,const struct sony_dsd_result *);
/* Explicit original DSD silence generation + original full master processing.
 * The requested duration is passed to the generator. Its returned payloads are
 * processed at their actual sizes in chunks of at most SONY_DSD_PACKET_BYTES.
 * Original SoundService uses 100 or 200 for DSD according to per-track state;
 * the caller must determine that policy, not assume the longest duration.
 * Callback synchronously consumes/copies the borrowed buffer/metadata and must
 * not reenter this session. ACK counts bytes from successful callbacks only,
 * in the actual output format, not DSD source bytes or hardware-consumed bytes.
 * Invalid arguments leave ACK unchanged; once begun it starts at zero. A native
 * or callback failure faults the stream; previous callbacks may have succeeded
 * before a later failure, and the failed callback must not be retried.
 * This supplies explicit padding, not a file-tail assembler, converter Flush,
 * EOS, or proof of hardware drain. It does not choose a padding policy. */
int sony_chain_render_silence_dsd(struct sony_chain_bridge *,unsigned int requested_ms,
    sony_chain_dsd_tail_sink,void *context,unsigned int *acknowledged_bytes);
/* Separate DSD control admission: send track::eos once and seal this adapter.
 * 0 accepted, 1 already sealed; subsequent DSD input/padding is rejected.
 * Does not call Dsd2Lpcm::Drain or imply DSP/hardware consumption. Caller owns
 * file-tail preparation and the required terminal output lifecycle. */
int sony_chain_send_dsd_eos(struct sony_chain_bridge *);
struct sony_dsd_finish_result {
    unsigned int requested_ms,acknowledged_bytes,eos_accepted,stop_accepted;
};
/* Normal EOF of a started master DSD stream, after all preceding output has
 * been accepted by the caller. Original service LatencyMode 0 requests 200ms,
 * 1/2 request 100ms. These are binary enum values, not UI labels. The original
 * generator rounds to whole packets; each goes through the full Sony chain.
 * Order: padding acknowledged by sink -> track::eos -> track::stop. This does
 * not open/close/drain a hardware renderer and is not physical completion.
 * Do not mix with explicit padding or send_dsd_eos. No input => reject; use
 * abort_close for an unused/cancelled lifetime. Serialize as for other APIs.
 * 0: controls accepted; 1: already finished with the same latency, returns the
 * cached receipt without callbacks. Invalid/reentrant calls leave out intact.
 * Once admitted, failure returns a partial receipt and faults the lifetime;
 * failed or uncertain sink delivery is never retried. No EOS after failed pad,
 * no stop after failed EOS. Caller must still abort its output on any error.
 */
int sony_chain_finish_dsd(struct sony_chain_bridge *,unsigned int original_latency_mode,
    sony_chain_dsd_tail_sink,void *context,struct sony_dsd_finish_result *out);
int sony_chain_update(struct sony_chain_bridge *,const struct sony_chain_settings *);
int sony_chain_process(struct sony_chain_bridge *,const int *input,int *output,unsigned int frames);
/* Experimental variable-output path: rate-band input packets, S32 stereo.
 * The native output rate is returned to the caller. Capacity is at most 16384 frames.
 * All output buffers/metadata unchanged on error; backend/format/capacity error
 * after input consumption faults the stream, requiring close/reopen (no retry).
 * Legacy WDC2 PROCESS remains fixed-rate; its expanded command is 48k-only.
 * The caller must account for the actual format and backend behavior. */
int sony_chain_process_expanded(struct sony_chain_bridge *,const int *input,int *output,
    unsigned int input_frames,unsigned int output_capacity_frames,
    unsigned int *output_frames,unsigned int *output_sample_rate);
/* Original input GetParam readback. Unsupported/empty replies are errors;
 * output is unchanged on error. A readback is not proof of DSP settling. */
int sony_chain_get_parameter(struct sony_chain_bridge *,const char *key,char *output,unsigned int capacity);
typedef int (*sony_chain_tail_sink)(void *,const int *samples,unsigned int frames);
/* Explicit original silence generation/processing only, not EOS or drain-complete.
 * For current S32 format: 100/250 requested ms, 16384-byte native packets.
 * Sink synchronously consumes/copies each borrowed buffer; 0 acknowledges all.
 * Sink must not reenter, update, process, or close this bridge.
 * On sink failure delivery of that chunk is uncertain; stream faults, no retry.
 */
int sony_chain_render_silence(struct sony_chain_bridge *,unsigned int requested_ms,
    sony_chain_tail_sink,void *context,unsigned int *acknowledged_frames);
typedef int (*sony_chain_expanded_tail_sink)(void *,const int *,unsigned int frames,unsigned int sample_rate);
/* Native silence-packet content/count, processed in safe 512-frame input
 * slices through the expanded API. Callback includes actual output rate.
 * Output-frame ACK counts only successful callbacks; sink failure faults stream.
 * Same 100/250ms request semantics as original (native rounds packet count).
 * This delivers padding, not EOS, native algorithm flush, or DAC consumption. */
int sony_chain_render_silence_expanded(struct sony_chain_bridge *,unsigned int requested_ms,
    sony_chain_expanded_tail_sink,void *context,unsigned int *acknowledged_frames);
/* Send original track::eos after caller has delivered required padding.
 * 0: accepted, input sealed; 1: already sent, no duplicate control.
 * This does NOT acknowledge output-device consumption or hardware drain.
 */
int sony_chain_send_eos(struct sony_chain_bridge *);
int sony_chain_abort_close(struct sony_chain_bridge *);
#endif
