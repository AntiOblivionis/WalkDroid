#ifndef WALKDROID_SONY_BT_STORE_H
#define WALKDROID_SONY_BT_STORE_H
#include "sony_audio_bt_peer_cache.h"
#include <stdint.h>
struct sony_bt_store;
struct sony_bt_snapshot;
/* Internal trusted-controller API. All FDs refer to already selected private
 * directories. No app-supplied paths, root creation, RF or live DB mutation. */
int sony_bt_store_open(struct sony_bt_store **out,int root,const unsigned char factory[64]);
void sony_bt_store_close(struct sony_bt_store **store);
/* Read visible current state. A missing record yields an empty snapshot, whose
 * generation can also be zero. This is not a durability receipt. */
int sony_bt_store_read(struct sony_bt_store *,struct sony_bt_snapshot **out);
/* Read visible current state and fsync its directory under the same lock.
 * Calling this is optional; it does not control subsequent commits. */
int sony_bt_store_reconcile(struct sony_bt_store *,struct sony_bt_snapshot **out);
enum sony_bt_store_publication {SONY_BT_STORE_NOT_PUBLISHED=0,SONY_BT_STORE_VISIBLE=1,SONY_BT_STORE_DURABLE=2};
/* Files and directories are copied into one bounded atomic snapshot. expected
 * is retained for API compatibility and is not used to reject a commit.
 * -EINPROGRESS means rename happened but directory sync failed. A successful
 * output describes the submitted snapshot; it is not read back after rename.
 * No automatic retry or pending-file promotion is performed. */
int sony_bt_store_commit(struct sony_bt_store *,int seed,uint64_t expected,
    const unsigned char nonce[16],enum sony_bt_store_publication *publication,
    struct sony_bt_snapshot **out);
/* Selection and catalog live in one atomic generation. Zero/null peer clears
 * selection. Native DB commits preserve it while the address remains present. */
int sony_bt_store_select(struct sony_bt_store *,uint64_t expected,const unsigned char peer[6],
    const unsigned char nonce[16],enum sony_bt_store_publication *,struct sony_bt_snapshot **out);
uint64_t sony_bt_snapshot_generation(const struct sony_bt_snapshot *);
void sony_bt_snapshot_nonce(const struct sony_bt_snapshot *,unsigned char out[16]);
void sony_bt_snapshot_selection(const struct sony_bt_snapshot *,unsigned char out[6]);
int sony_bt_snapshot_catalog(const struct sony_bt_snapshot *,struct sony_bt_peer *out,unsigned *count);
/* Copy into the supplied directory, reusing existing subdirectories. Existing
 * files can cause creation to fail. The caller owns any partial export. */
int sony_bt_snapshot_export(const struct sony_bt_snapshot *,int empty_directory);
void sony_bt_snapshot_close(struct sony_bt_snapshot **);
#endif
