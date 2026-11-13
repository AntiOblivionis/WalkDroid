#ifndef WALKDROID_SONY_OUTPUT_CONTROLS_H
#define WALKDROID_SONY_OUTPUT_CONTROLS_H
/* WDS3 extension: no change to the frozen 68-byte effect settings payload.
 * Device is the original funcarch enum; balance values are 0.5 dB attenuation.
 * This state describes context, never acknowledges physical routing/settling. */
#define SONY_OUTPUT_BTL 1
#define SONY_OUTPUT_SE 2
struct sony_output_controls {int device,se_gain,btl_gain,balance_l,balance_r;};
#define SONY_OUTPUT_DEFAULT {SONY_OUTPUT_SE,0,0,0,0}
static inline int sony_output_controls_valid(const struct sony_output_controls *c) {
    return c&&(c->device==SONY_OUTPUT_SE||c->device==SONY_OUTPUT_BTL)&&
        c->se_gain>=0&&c->se_gain<=1&&c->btl_gain>=0&&c->btl_gain<=1&&
        c->balance_l>=0&&c->balance_l<=88&&c->balance_r>=0&&c->balance_r<=88;
}
#endif
