#ifndef WALKDROID_SONY_BT_RAM_ROUTE_H
#define WALKDROID_SONY_BT_RAM_ROUTE_H
#include "sony_audio_bt_store.h"
#include "sony_audio_bt_tx_route_gate.h"
struct sony_bt_ram_route;
/* Root broker only, fixed trusted parent/name, immutable verified snapshot.
 * An error after mkdir may retain *out so its ORIGINAL owner can retire the
 * partial transaction. Existing roots are never adopted or deleted. */
int sony_bt_ram_route_create(struct sony_bt_ram_route **,const char *parent,const char *name,
    const struct sony_bt_snapshot *,const struct wbt_hal_route *);
const char *sony_bt_ram_route_seed(const struct sony_bt_ram_route *);
int sony_bt_ram_route_publish(struct sony_bt_ram_route *);
int sony_bt_ram_route_invalidate(struct sony_bt_ram_route *);
int sony_bt_ram_route_events(const struct sony_bt_ram_route *);
/* One nonblocking attempt. Caller independently proved its native children
 * reaped/FDs/leases CLEAN (or that no native child was ever created).
 * EBUSY keeps all handles/seed; watch events for the last HAL guard close.
 * Never poll-reset leases or reopen a replacement path to infer quiescence. */
int sony_bt_ram_route_retire(struct sony_bt_ram_route **,int native_quiescent);
#endif
