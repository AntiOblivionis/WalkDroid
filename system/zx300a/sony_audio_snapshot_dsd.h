#ifndef WALKDROID_SONY_SNAPSHOT_DSD_H
#define WALKDROID_SONY_SNAPSHOT_DSD_H
#include "sony_audio_dsd_controls.h"
/* Pure codec for the audited full v8 NVP layout. It does not save, apply DSP,
 * authorize a route, or change DSD mode capability. All three native enums are
 * 1/2 in the currently admitted profile; unknown values fail rather than being
 * silently normalized. Decode errors preserve output. Encode touches only
 * SS_BUILTIN_DSDPROCESS, SS_DSDCONV_GAIN, SS_DSDCONV_FILTER and the global CRC.
 * Full snapshot validation is required. In-place encode is supported; errors
 * leave the output unchanged. Caller owns CAS/locking/durability/readback. */
int sony_snapshot_apply_dsd(const void *,unsigned int,struct sony_dsd_controls *);
int sony_snapshot_encode_dsd(const void *,unsigned int,const struct sony_dsd_controls *,void *);
#endif
