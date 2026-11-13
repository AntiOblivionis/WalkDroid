#ifndef WALKDROID_SONY_BT_PEER_IDENTITY_H
#define WALKDROID_SONY_BT_PEER_IDENTITY_H
#include <sys/types.h>
/* Optional diagnostic PID, or -1 if unavailable; the transport does not use it. */
struct sony_bt_peer;
int sony_bt_peer_open(struct sony_bt_peer **,int socket);
pid_t sony_bt_peer_pid(const struct sony_bt_peer *);
void sony_bt_peer_close(struct sony_bt_peer **);
#endif
