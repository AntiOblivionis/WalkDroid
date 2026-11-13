#ifndef WALKDROID_SONY_SNAPSHOT_CONFIG_H
#define WALKDROID_SONY_SNAPSHOT_CONFIG_H
/* Single-threaded worker. Mapped nonpersistent values are held in memory;
 * unknown keys and persistent writes use the already-loaded native provider.
 * A successful provider write can persist according to its own semantics. */
int sony_snapshot_config_bind(const void *snapshot);
void sony_snapshot_config_unbind(void);
int sony_snapshot_config_fault(void);
#endif
