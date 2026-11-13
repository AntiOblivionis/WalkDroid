#ifndef WALKDROID_SONY_DSEE_CONTROLS_H
#define WALKDROID_SONY_DSEE_CONTROLS_H
/* Native GUI service modes 1..5. Mode 1 currently loads the original embedded
 * HX fallback with the supplied CFW asset; it is not verified DSEE AI. */
struct sony_dsee_controls {int enabled,mode;};
#define SONY_DSEE_DEFAULT {0,1}
static inline int sony_dsee_controls_valid(const struct sony_dsee_controls *c) {
    return c&&(c->enabled==0||c->enabled==1)&&c->mode>=1&&c->mode<=5;
}
#endif
