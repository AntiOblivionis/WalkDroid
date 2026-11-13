#ifndef SONY_AUDIO_BT_RADIO_OWNER_H
#define SONY_AUDIO_BT_RADIO_OWNER_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
struct sony_bt_radio_owner;
enum sony_bt_radio_kind {SONY_BT_RADIO_ANDROID=1,SONY_BT_RADIO_NATIVE_RX=4,SONY_BT_RADIO_NATIVE_TX=5};
/* Separate cooperative controller lease. Releasing it does not itself stop the
 * driver, radio, children or audio. A stale ACTIVE record does not block reuse. */
int sony_bt_radio_acquire(struct sony_bt_radio_owner **,enum sony_bt_radio_kind);
/* Caller-supplied parent directory and path. */
int sony_bt_radio_acquire_at(struct sony_bt_radio_owner **,int parent,const char *name,enum sony_bt_radio_kind);
int sony_bt_radio_check(struct sony_bt_radio_owner *);
uint64_t sony_bt_radio_generation(const struct sony_bt_radio_owner *);
/* verified_off records the caller's conclusion; the lease does not verify it. */
int sony_bt_radio_finish(struct sony_bt_radio_owner **,int verified_off);
#ifdef __cplusplus
}
#endif
#endif
