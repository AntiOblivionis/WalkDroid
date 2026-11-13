#ifndef WALKDROID_SONY_OUTPUT_ADAPTER_H
#define WALKDROID_SONY_OUTPUT_ADAPTER_H
#include "sony_audio_chain_client.h"
#define WDC_PCM_S16_LE 16u
#define WDC_PCM_S32_LE 32u
#define WDC_PCM_FLOAT_LE 0x10020u
struct wdc_output_adapter;
struct wdc_adapter_state {
    unsigned long long source_frames_copied,native_frames_accepted;
    unsigned int pending_frames,zero_padding_frames,sealed,faulted;
};
/* Borrowed WDS3 client; owner-thread serialization. Source packets use the
 * existing rate-band quantum, not a fixed 512 frames. Actual backend support
 * for a requested rate is determined by the backend.
 * Open creates a DSP session, output still opens lazily. No resampling/gain.
 * FLOAT_LE is an explicit format tag, not a bit count. Convert to Sony signed
 * Q31 at this boundary; all representable Q31 samples remain exact. Clip finite
 * out-of-range values and infinities; NaN becomes zero. */
int wdc_adapter_open(struct wdc_output_adapter **,struct wdc_client *,const struct sony_chain_settings *,
    unsigned int sample_rate,unsigned int channels,unsigned int pcm_format);
/* copied_frames includes input copied before an error, NOT hardware acceptance.
 * A failed write poisons adapter; never retry any part of that write. */
int wdc_adapter_write(struct wdc_output_adapter *,const void *,unsigned int bytes,unsigned int *copied_frames);
int wdc_adapter_finish(struct wdc_output_adapter *,unsigned int padding_ms);
int wdc_adapter_get_state(struct wdc_output_adapter *,struct wdc_adapter_state *);
/* Drops pending input; no implicit finish. Always releases adapter, not client. */
int wdc_adapter_abort_close(struct wdc_output_adapter **);
#endif
