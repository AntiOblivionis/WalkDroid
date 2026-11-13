#ifndef WALKDROID_SONY_OUTPUT_TIMING_H
#define WALKDROID_SONY_OUTPUT_TIMING_H
/* Fixed ARM32 wire schema. Values are ALSA negotiated capacities, not a
 * presentation timestamp, current queue depth, or proof of DAC consumption.
 * Before the lazy native output opens, all fields including valid are zero. */
struct sony_output_timing {
    unsigned int sample_rate,period_frames,buffer_frames,valid;
};
#endif
