#ifndef SONY_AUDIO_RECEIVER_ROUTE_H
#define SONY_AUDIO_RECEIVER_ROUTE_H
#include "sony_audio_receiver_playback.h"
#include "sony_audio_receiver_process.h"
#include "sony_audio_pcm_sink_process.h"
#include "sony_audio_controls.h"
#include "sony_audio_usb_stream.h"
struct sony_receiver_route;
#define SONY_RECEIVER_ROUTE_EXPLICIT_START 0x57525231u
#define SONY_RECEIVER_ROUTE_VERIFY_MUTED 1u
struct sony_receiver_route_binding {struct wur_open opening;struct sony_controls controls;uint64_t generation;};
/* Actual cooperative global USB_DAC owner. Requires the installed HAL and all
 * writers to participate in owner arbitration. Reads real jacks/config, applies
 * original output/volume/AVLS transition while muted and CAS/readbacks only its
 * volume/lastHeadphone save. Does not spawn/capture or select a device.
 * expected is exact SE/BTL, flags0 for playback or VERIFY_MUTED for a diagnostic
 * that must never enable sound. Later jack changes seal, no automatic reroute.
 * Failure after allocation retains handle for finish. *out starts NULL. */
int sony_receiver_route_prepare_usb(struct sony_receiver_route **,unsigned activation,unsigned expected,unsigned flags);
int sony_receiver_route_export_config(struct sony_receiver_route *,const char *new_private_directory);
/* Before launching children: binds exact native USB source metadata and the
 * exported original config. Both roots must mount this same private directory.
 * Output CFW profile comes from binding.controls; no defaults/fallback. */
int sony_receiver_route_arm(struct sony_receiver_route *,const struct sony_usb_stream *,struct sony_receiver_route_binding *,struct sony_receiver_playback_owner *);
/* After the real owned output supervisor READY, before opening playback.
 * Pass its actual getter PID, never a process-list guess. Immutable once bound.
 * started verifies actual PCM owner_pid/format/rate and all other PCMs closed. */
int sony_receiver_route_bind_sink(struct sony_receiver_route *,int actual_owned_pid);
/* After playback Close, SHUTDOWN and reap of both actual owned workers. Errors,
 * forced/lost/missing child, failed config identity or independent mute/off/
 * all-PCM idle leave ACTIVE. No recovery/reset API. Null receipts only before
 * arm (no child launched); caller must retain ownership through cleanup. */
int sony_receiver_route_finish(struct sony_receiver_route **,const struct sony_receiver_playback_cleanup *,
    const struct sony_receiver_process_exit *,const struct sony_pcm_sink_process_exit *);
#endif
