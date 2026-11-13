#ifndef WALKDROID_SONY_AI_PAUSED_POWER_H
#define WALKDROID_SONY_AI_PAUSED_POWER_H
#include <stdint.h>
/* AI output intercepts the original power intents and delegates resource
 * management to the parent. These counters do not prove a kernel change. */
int sony_ai_paused_power_bind(uint64_t nonce);
int sony_ai_paused_power_fault(void);
/* Record that native Open returned a cleanup error; no hardware state is inferred. */
void sony_ai_paused_power_failed_open(void);
int sony_ai_paused_power_finish(void);
#endif
