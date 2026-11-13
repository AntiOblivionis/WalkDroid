#ifndef SONY_AUDIO_DSD_NATIVE_SINK_H
#define SONY_AUDIO_DSD_NATIVE_SINK_H
#include "sony_audio_dsd_native_writer.h"
#include "sony_audio_dsd_power_profiles.h"
#include "sony_audio_dsd_control_guard.h"
/* Private ARM/glibc worker only. Caller independently pins BOTH original ELF
 * handles/CFW profile, readonly root and exact-generation output ownership.
 * Power backend is the same owned WKP7 transaction. This component never
 * selects a device/player, changes DAC routes/volume or releases global owner.
 * All APIs run on the same serialized owner thread; do not borrow its object.
 */
struct sony_dsd_native_geometry {
    unsigned bit_rate,alsa_rate,channels,format,access,period_frames,buffer_frames;
    unsigned bytes_per_write,physical_width;
};
struct sony_dsd_native_sink_state {
    struct sony_dsd_native_geometry geometry;
    struct sony_dsd_native_writer_state writer;
    struct sony_dsd_power_report power;
    struct sony_dsd_control_report drain,drop,close;
    unsigned opened,configured,sealed,drain_accepted,failed,close_called;
    unsigned output_closed_confirmed,resources_clean,retained;
    int first_error,drain_status,drop_status,control_close_status,power_close_status;
};
struct sony_dsd_native_sink;
/* A non-NULL *out always remains addressable on partial failure. Caller MUST
 * close it even when Open/configuration fails. No implicit retry/cleanup.
 * The caller supplies the Adler and ALSA handles used by the worker.
 */
int sony_dsd_native_sink_open(struct sony_dsd_native_sink **,void *library,void *alsa,
    unsigned cfw_profile,unsigned bit_rate,unsigned low_latency,const struct sony_dsd_power_backend *);
int sony_dsd_native_sink_write(struct sony_dsd_native_sink *,const void *,unsigned bytes);
/* Seals before the call. Full observed nonblock/drain/restore + SETUP required.
 * Admission/drain ACK is not a physical DAC consumption or acoustic claim. */
int sony_dsd_native_sink_drain(struct sony_dsd_native_sink *);
int sony_dsd_native_sink_state(struct sony_dsd_native_sink *,struct sony_dsd_native_sink_state *);
/* Stop/drop if needed, then exactly one native Close. No replay of an unknown
 * close/release. Re-entry may only resume a precondition/read-only closure
 * check that has not attempted the next mutation. Unknown effects retain *out.
 * When physical closure and resource cleanliness are independently proven,
 * *out may become NULL even with an IO error; report distinguishes that error
 * from retained cleanup. Caller keeps global route/owner until child reap.
 */
int sony_dsd_native_sink_close(struct sony_dsd_native_sink **,struct sony_dsd_native_sink_state *);
#endif
