#ifndef SONY_AUDIO_DSF_SESSION_H
#define SONY_AUDIO_DSF_SESSION_H
#include "sony_audio_dsf_reader.h"
#include "sony_audio_dsd_assembler.h"
struct sony_dsf_session;
/* One normal DSF file/render lifetime. Caller supplies a verified original
 * PlayerService handle, stable read-only file and synchronous packet/EOS sinks.
 * This composes reader -> native renderer fades/preroll -> WMX-style assembly.
 * Sinks receive canonical DSD, before Sony DSP. They do not prove DAC consumption.
 * Serialize all calls; user callbacks must not reenter or release the session.
 * No seek, trick play, gapless chaining, output pause, DSP Flush or drain API.
 */
int sony_dsf_session_open(struct sony_dsf_session **,void *audited_player_library,
                         uint64_t file_bytes,sony_dsf_read_at,void *file_context,
                         sony_dsd_packet_sink,sony_dsd_eos_sink,void *sink_context);
/* 0: one file block accepted; 1: input sealed after final padded packet and EOS.
 * Repeated completion returns 1 without any callback. Failure faults this
 * lifetime; never retry accepted/failed output and never synthesize success EOS.
 */
int sony_dsf_session_step(struct sony_dsf_session *);
int sony_dsf_session_info(const struct sony_dsf_session *,struct sony_dsf_info *);
struct sony_dsf_session_status {
    uint64_t canonical_file_bytes_accepted; /* Includes mono-to-stereo duplication. */
    unsigned preroll_accepted;
    int finished,faulted;
    struct sony_dsd_assembly_status assembly;
};
int sony_dsf_session_status(const struct sony_dsf_session *,struct sony_dsf_session_status *);
/* Drop unsubmitted data and release resources; never emits audio/EOS. */
int sony_dsf_session_abort_free(struct sony_dsf_session *);
#endif
