#ifndef WALKDROID_SONY_EQ_BRIDGE_H
#define WALKDROID_SONY_EQ_BRIDGE_H
/* Original Sony ARM/glibc component boundary. Not an Android Bionic library.
 * The caller supplies the exact audited libSoundServiceFw handle and a working
 * configuration transport. No ALSA/device/USB operations occur in this module.
 */
struct sony_eq_bridge;
struct sony_eq_settings {
    int enabled;
    int bands[10]; /* Original keys -20..20, tested as half-dB increments. */
    int output_device; /* Original enum; hardware mapping remains external. */
    int primary_volume;
    int secondary_volume;
    int se_gain_mode;
    int btl_gain_mode;
};
struct sony_eq_controls {
    int enabled;
    int processing;
    int bands[10];
};

/* Initial implementation: 48000 Hz, stereo, interleaved float32, 512 frames.
 * Other Sony algorithms, formats and full-chain Direct Source remain separate.
 * Return 0 on success. Failure never silently becomes a bypass stream.
 * Caller must validate hardware-context enum/volume/gain values against its
 * selected route profile; this prototype does not establish hardware support.
 * Configuration changes require serialized calls; API is not thread-safe.
 * Close consumes the pointer. On process/update failure caller must stop the
 * stream or supply silence; it must not play stale output as valid audio.
 */
int sony_eq_bridge_open(struct sony_eq_bridge **out, void *original_library,
                        const struct sony_eq_settings *settings);
int sony_eq_bridge_update(struct sony_eq_bridge *, const struct sony_eq_settings *);
int sony_eq_bridge_process(struct sony_eq_bridge *, const float *input,
                          float *output, unsigned int frames);
/* Read original filter GetParam values, not cached client settings. Does not
 * claim ALSA/hardware state or the surrounding full-chain DSP policy. */
int sony_eq_bridge_get_controls(struct sony_eq_bridge *, struct sony_eq_controls *);
int sony_eq_bridge_close(struct sony_eq_bridge *);
#endif
