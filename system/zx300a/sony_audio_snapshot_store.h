#ifndef WALKDROID_SONY_SNAPSHOT_STORE_H
#define WALKDROID_SONY_SNAPSHOT_STORE_H
#define SONY_SNAPSHOT_BYTES 28672u
/* POSIX directory backend; no device paths or NVP services.
 * expected is retained for source compatibility and does not gate writes.
 * -117 means rename completed but directory durability is unconfirmed.
 * -16 means another writer holds the cooperative lock.
 * All buffers are exactly SONY_SNAPSHOT_BYTES; caller serializes native cache.
 */
int sony_snapshot_validate(const void *bytes);
int sony_snapshot_read(const char *directory, void *bytes);
int sony_snapshot_commit(const char *directory, const void *expected, const void *bytes);
/* Called after the complete pending file is fsynced, before its rename. The
 * callback may duplicate the borrowed fd to identify this admitted writer's
 * new inode. It must not close it. A negative result cancels before rename. */
typedef int (*sony_snapshot_prepared_fn)(void *context, int pending_fd);
int sony_snapshot_commit_observed(const char *directory, const void *expected,
                                 const void *bytes, sony_snapshot_prepared_fn,
                                 void *context);
#endif
