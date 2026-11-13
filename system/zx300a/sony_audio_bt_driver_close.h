#ifndef SONY_AUDIO_BT_DRIVER_CLOSE_H
#define SONY_AUDIO_BT_DRIVER_CLOSE_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Consume the supplied descriptor once. Kernel driver release errors may not
 * be observable from close(2). */
int sony_bt_driver_close(int owned_fd);
#ifdef __cplusplus
}
#endif
#endif
