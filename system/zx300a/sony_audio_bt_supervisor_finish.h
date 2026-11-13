#ifndef WALKDROID_SONY_AUDIO_BT_SUPERVISOR_FINISH_H
#define WALKDROID_SONY_AUDIO_BT_SUPERVISOR_FINISH_H

#include <errno.h>
#include "sony_audio_owner.h"
#include "sony_audio_bt_radio_owner.h"

/* Release all held leases without claiming external hardware was verified.
 * Preserve the first real operation error while attempting both releases. */
static int sony_bt_supervisor_finish(int rc, int unmounted,
                                     struct sony_bt_radio_owner **radio,
                                     struct sony_audio_owner **audio) {
    if (unmounted && !rc) rc=unmounted;
    if (*radio) { int done=sony_bt_radio_finish(radio,0); if (done && !rc) rc=done; }
    if (*audio) { int done=sony_audio_owner_finish(audio,0); if (done && !rc) rc=done; }
    return rc;
}

static int sony_bt_supervisor_exit_code(int status, int report_error) {
    return ((status && status!=-ECANCELED) || report_error) ? 70 : 0;
}

#endif
