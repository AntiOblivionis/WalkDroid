/* Same independently verified audio/radio ownership, child supervision and
 * private mount cleanup. Only the explicitly selected worker ABI differs. */
#define SONY_TX_CONTROL_ARGUMENT "--link-fd"
#define SONY_TX_PARENT_DEATH_SIGNAL SIGTERM
#define SONY_TX_CLEAN_ORPHAN_EXIT 1
#define SONY_TX_VOLUME_CONTROL 1
#include "sony_audio_bt_tx_supervisor.c"
