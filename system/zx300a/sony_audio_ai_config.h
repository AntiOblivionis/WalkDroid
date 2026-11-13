#ifndef WALKDROID_SONY_AI_CONFIG_H
#define WALKDROID_SONY_AI_CONFIG_H
/* Private AI process only. Key251 is the current in-memory request.
 * Unknown keys and persistent writes are forwarded to an already-loaded
 * original provider; successful persistent writes also update this cache. */
int sony_ai_config_bind(int enabled);
int sony_ai_config_fault(void);
unsigned int sony_ai_config_reads(void);
unsigned int sony_ai_config_mirrors(void);
#endif
