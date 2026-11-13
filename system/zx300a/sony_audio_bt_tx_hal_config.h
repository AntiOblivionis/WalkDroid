#ifndef WALKDROID_SONY_BT_TX_HAL_CONFIG_H
#define WALKDROID_SONY_BT_TX_HAL_CONFIG_H
#include <stdint.h>
/* Only the trusted root route controller may create this RAM-only record.
 * No path, shell command, codec or gain is accepted from AudioParameter. */
#define WBT_HAL_ROUTE_DIR "/dev/walkdroid-bt-tx"
#define WBT_HAL_ROUTE_PATH WBT_HAL_ROUTE_DIR "/route"
#define WBT_HAL_SEED_PATH WBT_HAL_ROUTE_DIR "/seed"
#define WBT_HAL_ROUTE_MAGIC UINT32_C(0x31485457)
struct wbt_hal_route {
    uint32_t magic,version;
    uint64_t generation;
    uint8_t peer[6],reserved[10];
};
int wbt_hal_route_read(struct wbt_hal_route *);
const char *wbt_hal_runtime_path(void);
#endif
