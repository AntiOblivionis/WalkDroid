#ifndef WALKDROID_SONY_BT_TX_ROUTE_GATE_H
#define WALKDROID_SONY_BT_TX_ROUTE_GATE_H
#include "sony_audio_bt_tx_hal_config.h"
struct wbt_route_guard;
/* Cooperating HAL streams hold a shared flock; the controller uses exclusive
 * flock while changing the route. No credentials, filesystem, inode or route
 * content are checked. This module does not determine RF or PCM state. */
int wbt_route_guard_acquire_at(struct wbt_route_guard **out,int parent,const char *name,struct wbt_hal_route *route);
int wbt_route_guard_acquire(struct wbt_route_guard **out,struct wbt_hal_route *route);
int wbt_route_guard_check(struct wbt_route_guard *);
/* Take the controller's exclusive lock on the caller-provided gate. */
int wbt_route_guard_quiesce_at(struct wbt_route_guard **out,int parent,const char *name);
/* Release the exclusive lock to admit streams. */
int wbt_route_guard_allow_streams(struct wbt_route_guard *,const struct wbt_hal_route *);
/* Regain the exclusive lock without blocking. */
int wbt_route_guard_quiesce(struct wbt_route_guard *);
void wbt_route_guard_release(struct wbt_route_guard **);
#endif
