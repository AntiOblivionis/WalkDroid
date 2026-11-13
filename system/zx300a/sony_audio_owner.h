#ifndef WALKDROID_SONY_AUDIO_OWNER_H
#define WALKDROID_SONY_AUDIO_OWNER_H
#include <stdint.h>
#include <stddef.h>
struct sony_audio_owner;
struct sony_audio_owner_reader;
enum sony_audio_owner_kind {SONY_OWNER_HAL=1,SONY_OWNER_DSF=2,SONY_OWNER_USB_DAC=3,SONY_OWNER_BT_RX=4,SONY_OWNER_BT_TX=5};
/* Cooperative arbitration for participants using the same lease file. flock is
 * released by the kernel after the last inherited descriptor closes. A stale
 * record does not prevent the next holder from acquiring the lock. The caller
 * remains responsible for stopping hardware and coordinating shared handles. */
int sony_audio_owner_acquire(struct sony_audio_owner **,enum sony_audio_owner_kind);
/* Caller-supplied directory, including for isolated namespaces. */
int sony_audio_owner_acquire_at(struct sony_audio_owner **,int directory,enum sony_audio_owner_kind);
int sony_audio_owner_check(struct sony_audio_owner *);
uint64_t sony_audio_owner_generation(const struct sony_audio_owner *);
/* Values recorded on this handle; an inherited handle exposes the same values. */
enum sony_audio_owner_kind sony_audio_owner_kind(const struct sony_audio_owner *);
/* verified_idle records the caller's conclusion; it does not test hardware.
 * Zero leaves an ACTIVE record, which no longer blocks a later acquisition. */
int sony_audio_owner_finish(struct sony_audio_owner **,int verified_idle);
/* Existing lease files use a nonblocking shared flock. If the file is absent,
 * no lock can be held; a concurrent first writer is not independently detected. */
int sony_audio_owner_read_begin(struct sony_audio_owner_reader **);
int sony_audio_owner_read_begin_at(struct sony_audio_owner_reader **,int directory);
int sony_audio_owner_read_end(struct sony_audio_owner_reader **);
/* Status publication does not verify the publisher, process lifetime, lock or
 * age. A crashed process may leave stale text; it is not proof of hardware state. */
#define SONY_OWNER_STATE_TEXT_BYTES 4096u
struct sony_audio_owner_observation {uint64_t generation,sequence;unsigned kind;};
int sony_audio_owner_publish(struct sony_audio_owner *,const char *);
int sony_audio_owner_retire(struct sony_audio_owner *);
int sony_audio_owner_observe(char *,size_t,struct sony_audio_owner_observation *);
int sony_audio_owner_observe_at(int,char *,size_t,struct sony_audio_owner_observation *);
/* Nonblocking AF_UNIX/SOCK_SEQPACKET channel. Dynamic lease and generation
 * values form its address; they are not used to authenticate a client. */
int sony_audio_owner_channel_listen(struct sony_audio_owner *,int *);
int sony_audio_owner_channel_connect_at(int,uint64_t,enum sony_audio_owner_kind,int *);
int sony_audio_owner_channel_connect(uint64_t,enum sony_audio_owner_kind,int *);
#endif
