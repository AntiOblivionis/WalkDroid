#ifndef WALKDROID_SONY_SRCONV_H
#define WALKDROID_SONY_SRCONV_H
#include <stdint.h>
/* Original Sony software conversion, inside the native processing boundary.
 * This is not an Android mixer, codec, RF sender or presentation clock. */
struct sony_srconv;
int sony_srconv_open(struct sony_srconv **,unsigned input_rate,unsigned output_rate);
int sony_srconv_limits(struct sony_srconv *,unsigned *mandatory,unsigned *output_capacity);
int sony_srconv_process(struct sony_srconv *,const int16_t *input,unsigned frames,
                       int16_t *output,unsigned capacity,unsigned *produced);
int sony_srconv_flush(struct sony_srconv *,int16_t *output,unsigned capacity,unsigned *produced);
void sony_srconv_close(struct sony_srconv **);
#endif
