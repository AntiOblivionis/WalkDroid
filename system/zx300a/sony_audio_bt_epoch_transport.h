#ifndef SONY_AUDIO_BT_EPOCH_TRANSPORT_H
#define SONY_AUDIO_BT_EPOCH_TRANSPORT_H
#include "sony_audio_bt_stream_epochs.h"
/* Only inherited, independently selected AF_UNIX/SOCK_SEQPACKET descriptors.
 * No bind/connect, device selection, RF or owner operation. Preparation uses
 * the existing qualified WBL1 socket setup; WBE2 framing stays separate.
 * Volume may explicitly use the existing mode-wide WBL1 event envelope. No
 * other WBL1 packet is accepted on a WBE2 connection. */
int sony_bt_epoch_transport_prepare(int fd,unsigned *send_buffer);
int sony_bt_epoch_transport_send(int fd,const struct sony_bt_live_header *,const void *);
/* Nonblocking whole record. The legacy PID/UID arguments are not used.
 * Cookie is independently checked here and again by the protocol cursor.
 * All received descriptor rights are closed/rejected. Header output remains
 * unchanged on every failure; scratch may be overwritten. No partial retry. */
int sony_bt_epoch_transport_receive(int fd,pid_t,uid_t,uint64_t cookie,
    struct sony_bt_live_header *,void *scratch,unsigned capacity);
#endif
