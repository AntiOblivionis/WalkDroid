#ifndef SONY_AUDIO_BT_COMPONENTS_H
#define SONY_AUDIO_BT_COMPONENTS_H
#include "sony_audio_bt_middleware_abi.h"
#include "sony_audio_bt_avrcp_abi.h"
#include "sony_audio_bt_tx_abi.h"
/* Prepare the original mandatory A2DP/AVRCP graph after CommonInit and before
 * RF. Borrowed native modules and callback storage live until
 * the entire worker exits. No RF, pairing, source data or Android API calls.
 * snk=NULL uses the original component's unbound/no-listener callbacks.
 * mask records each accepted registration, including partial failure. Never
 * retry or unload a partial graph; outer process shutdown owns its lifetime. */
int sony_bt_components_prepare(void *middleware,void *components,
    const struct sony_bt_snk_callbacks *snk,unsigned *mask);
/* Optional original AVRCP sink callbacks for a receiver owner. The existing
 * entry keeps all stock AVRCP callbacks. This only registers the audited raw
 * ABI; it does not apply volume, send remote commands or activate a radio.
 * Both callback objects remain borrowed until the worker exits. */
int sony_bt_components_prepare_receiver(void *middleware,void *components,
    const struct sony_bt_snk_callbacks *snk,
    const struct sony_bt_avrcp_snk_callbacks *avrcp,unsigned *mask);
/* Sender counterpart: register audited TX/AVRCP source callbacks while
 * retaining the original mandatory sink graph for shared power callbacks.
 * This is not RF admission and does not claim an active data path. */
int sony_bt_components_prepare_transmitter(void *middleware,void *components,
    const struct sony_bt_tx_callbacks *source,
    const struct sony_bt_tx_avrcp_callbacks *avrcp,unsigned *mask);
#endif
