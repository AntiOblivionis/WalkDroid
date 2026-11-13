#ifndef WALKDROID_SONY_BT_TX_VOLUME_H
#define WALKDROID_SONY_BT_TX_VOLUME_H
#include <stdint.h>
/* WVA1 travels on its own inherited SEQPACKET socket, never in the PCM
 * stream or as PCM frame positions. The owning root service supplies the
 * link nonce; no public socket or separate RF instance is created here. */
#define WVA_MAGIC UINT32_C(0x31415657)
enum wva_operation {WVA_EVENT=0,WVA_QUERY=1,WVA_STEP=2,WVA_MODE=3};
enum wva_flags {WVA_CONNECTION_KNOWN=1,WVA_REMOTE_KNOWN=2,WVA_VALUE_KNOWN=4,
 WVA_MODE_KNOWN=8,WVA_CALL_IN_FLIGHT=16,WVA_WAITING_ECHO=32,WVA_SEALED=64};
struct wva_snapshot {
 uint64_t revision;
 uint32_t flags,connection,version,features,value,mode;
 uint64_t command,confirmed;
};
struct wva_request {
 uint32_t magic,version,operation,request;
 uint64_t nonce,expected_revision;
 int32_t value;uint32_t reserved;
};
struct wva_reply {
 uint32_t magic,version,operation,request;
 uint64_t nonce;int32_t status;uint32_t reserved;
 struct wva_snapshot state;
};
struct sony_tx_volume;
struct sony_tx_volume_ops {
 int (*relative)(void *,int direction);
 int (*absolute)(void *,unsigned value);
 /* Zero means original mode write AND exact native mode read-back. */
 int (*mode)(void *,unsigned enabled);
};
int sony_tx_volume_open(struct sony_tx_volume **,const struct sony_tx_volume_ops *,void *);
/* Policy connection enum: 2 means the exact peer was verified connected.
 * MW raw callbacks use another enum and must not be passed through here. */
void sony_tx_volume_connection(struct sony_tx_volume *,unsigned verified_policy_state);
void sony_tx_volume_remote(struct sony_tx_volume *,unsigned version,unsigned features);
void sony_tx_volume_current(struct sony_tx_volume *,unsigned raw);
void sony_tx_volume_mode_observed(struct sony_tx_volume *,unsigned enabled);
int sony_tx_volume_snapshot(struct sony_tx_volume *,struct wva_snapshot *);
int sony_tx_volume_command(struct sony_tx_volume *,unsigned operation,uint64_t expected,int value,struct wva_snapshot *);
/* Seal precedes native teardown. Late callbacks cannot reopen this instance.
 * Destruction requires external callback quiescence as well as no active call. */
void sony_tx_volume_seal(struct sony_tx_volume *);
int sony_tx_volume_destroy(struct sony_tx_volume **);
static inline int wva_snapshot_valid(const struct wva_snapshot *s){
 if(!s||s->flags&~127u||s->confirmed>s->command)return 0;
 if(!(s->flags&WVA_CONNECTION_KNOWN)&&s->connection)return 0;
 if(!(s->flags&WVA_REMOTE_KNOWN)&&(s->version||s->features))return 0;
 if(s->version>65535||s->features>65535||s->value>127||s->mode>1)return 0;
 if(!(s->flags&WVA_VALUE_KNOWN)&&s->value)return 0;
 if(!(s->flags&WVA_MODE_KNOWN)&&s->mode)return 0;
 if(s->flags&WVA_WAITING_ECHO){
  unsigned required=WVA_CONNECTION_KNOWN|WVA_REMOTE_KNOWN|WVA_MODE_KNOWN;
  if((s->flags&required)!=required||s->connection!=2||s->version<0x104||!(s->features&8)||!s->mode||!s->command)return 0;
 }
 return 1;
}
#endif
