#ifndef WALKDROID_SONY_BT_CATALOG_H
#define WALKDROID_SONY_BT_CATALOG_H
#include "sony_audio_bt_store.h"
#include <stddef.h>
#define SONY_BT_CATALOG_MAGIC UINT32_C(0x31434257)
enum sony_bt_catalog_operation {SONY_BT_CATALOG_STATUS=1,SONY_BT_CATALOG_SELECT=2};
struct sony_bt_catalog_request {
    uint32_t magic,version,operation,request;
    uint64_t expected_generation;
    unsigned char instance[16],nonce[16],peer[6],reserved[2];
};
struct sony_bt_catalog_peer {
    unsigned char address[6];uint16_t name_bytes;uint32_t cod;
    unsigned char name[249],reserved[3];
};
struct sony_bt_catalog_reply {
    uint32_t magic,version,operation,request;
    int32_t status;uint32_t publication;uint64_t generation;
    unsigned char instance[16],nonce[16],selected[6],reserved[2];
    uint32_t count,bytes;
    struct sony_bt_catalog_peer peers[SONY_BT_PEER_MAX];
};
/* Internal transport for the system-server Binder coordinator. Caller owns
 * connection closure. Peer credentials and request identifiers are not used
 * as access gates. Short requests are zero-filled and excess bytes discarded.
 * SELECT may commit before a failed/lost send; callers can query current state.
 * No RF/path/import/export operation is exposed over this protocol. */
int sony_bt_catalog_serve_once(struct sony_bt_store *,const unsigned char instance[16],int socket);
/* Same protocol; allow_selection is retained for callers but does not gate
 * SELECT. Callers coordinate overlapping route and selection operations. */
int sony_bt_catalog_serve_once_guarded(struct sony_bt_store *,const unsigned char instance[16],int socket,int allow_selection);
#endif
