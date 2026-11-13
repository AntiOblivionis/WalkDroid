#ifndef WALKDROID_SONY_AUDIO_JACK_H
#define WALKDROID_SONY_AUDIO_JACK_H
struct sony_audio_jack_state { int se, btl; };
/* GET-only reader for the ICX1288 board's three mtgpio inputs. It does not
 * wake the codec, configure pins or change detector mode. GPIO 39 is sampled
 * but only GPIO 53/42 determine the reported SE/BTL states. An open or ioctl
 * failure returns a negative error; there is no repeated-sample stability test. */
int sony_audio_jack_read(struct sony_audio_jack_state *);
#endif
