#ifndef WALKDROID_SONY_BT_PEER_CACHE_H
#define WALKDROID_SONY_BT_PEER_CACHE_H
#include <stddef.h>
#include <stdint.h>
#define SONY_BT_PEER_MAX 40u
#define SONY_BT_PEER_RECORD 0x2fcu
#define SONY_BT_PEER_CACHE_BYTES (SONY_BT_PEER_MAX*SONY_BT_PEER_RECORD)
struct sony_bt_peer {uint8_t address[6],name[249];unsigned name_bytes,cod;};
/* Decode up to 40 complete BlueAngel-style records; ignore incomplete tail
 * and empty records. Output copies public fields only, never link keys. */
int sony_bt_peer_cache_parse(const void *data,size_t bytes,struct sony_bt_peer *out,unsigned *count);
/* Read at most 40 records from the supplied FD; an actively modified file may
 * yield a mixture of states. Actual read errors are returned to the caller. */
int sony_bt_peer_cache_read(int fd,struct sony_bt_peer *out,unsigned *count);
#endif
