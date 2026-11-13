#ifndef WALKDROID_SONY_DSD_CONTROLS_H
#define WALKDROID_SONY_DSD_CONTROLS_H
/* Original service enums, not GUI item IDs. DSD mode 1 permits native BTL;
 * mode 2 requests the original converter. SE native capability is deliberately
 * not invented here. This audited ZX300A profile uses SE conversion.
 * No worker wire protocol or device deployment is implied by this interface. */
struct sony_dsd_controls { int mode,filter,gain; };
static inline int sony_dsd_controls_valid(const struct sony_dsd_controls *c) {
    return c&&(c->mode==1||c->mode==2)&&(c->filter==1||c->filter==2)&&
        (c->gain==1||c->gain==2);
}
#define SONY_DSD_PACKET_BYTES 32768u
#define SONY_DSD_MAX_OUTPUT_BYTES 131072u
#define SONY_DSD_OUTPUT_PCM_S32LE 1u
#define SONY_DSD_OUTPUT_NATIVE 3u
struct sony_dsd_result {
    unsigned int bytes,rate,format;
};
#endif
