/* Same broker engine and reap-before-CLEAN semantics as USB.
 * Separate fixed endpoint/activation command; never auto-select a transport. */
#define SONY_BT_RECEIVER_BROKER 1
#define WDR_SOCKET "walkdroid.sony.bt.receiver.v1"
#include "sony_audio_receiverd.c"
