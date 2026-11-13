#ifndef WALKDROID_SONY_BT_BROKER_GROUP_H
#define WALKDROID_SONY_BT_BROKER_GROUP_H
/* ZX300A Android init tracks each service in cpuacct /acct/uid_0/pid_PID.
 * Guardian moves itself into a named group before spawning children. An
 * existing group with that name can be reused; other tasks are not moved.
 * No filesystem, ownership, PID or membership checks are performed. */
struct sony_bt_broker_group;
int sony_bt_broker_group_enter(struct sony_bt_broker_group **);
int sony_bt_broker_group_leave(struct sony_bt_broker_group **);
#endif
