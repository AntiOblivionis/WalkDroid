#include "sony_audio_bt_tx_volume.h"
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
struct sony_tx_volume {
 pthread_mutex_t lock;
 struct wva_snapshot state;
 struct sony_tx_volume_ops ops;
 void *context;
 unsigned target;
 uint64_t pending;
};
static int advance(struct sony_tx_volume *v){
 if(v->state.revision==UINT64_MAX){v->state.flags|=WVA_SEALED;return -EOVERFLOW;}
 ++v->state.revision;return 0;
}
static void invalidate_remote(struct sony_tx_volume *v){
 v->state.flags&=~(WVA_REMOTE_KNOWN|WVA_VALUE_KNOWN|WVA_WAITING_ECHO);
 v->state.version=v->state.features=v->state.value=0;v->pending=0;
}
int sony_tx_volume_open(struct sony_tx_volume **out,const struct sony_tx_volume_ops *ops,void *ctx){
 if(!out||*out||!ops||!ops->relative||!ops->absolute||!ops->mode)return -EINVAL;
 struct sony_tx_volume *v=calloc(1,sizeof(*v));if(!v)return -ENOMEM;
 int rc=pthread_mutex_init(&v->lock,NULL);if(rc){free(v);return -rc;}
 v->ops=*ops;v->context=ctx;*out=v;return 0;
}
void sony_tx_volume_connection(struct sony_tx_volume *v,unsigned raw){
 if(!v)return;pthread_mutex_lock(&v->lock);
 if(!(v->state.flags&WVA_SEALED)&&!advance(v)){
  if(raw!=2||!(v->state.flags&WVA_CONNECTION_KNOWN)||v->state.connection!=raw)invalidate_remote(v);
  v->state.connection=raw;v->state.flags|=WVA_CONNECTION_KNOWN;
 }
 pthread_mutex_unlock(&v->lock);
}
void sony_tx_volume_remote(struct sony_tx_volume *v,unsigned version,unsigned features){
 if(!v)return;pthread_mutex_lock(&v->lock);
 if(!(v->state.flags&WVA_SEALED)&&(v->state.flags&WVA_CONNECTION_KNOWN)&&v->state.connection==2&&!advance(v)){
  if(version>65535||features>65535){invalidate_remote(v);}
  else {
   if(!(v->state.flags&WVA_REMOTE_KNOWN)||v->state.version!=version||v->state.features!=features)invalidate_remote(v);
   v->state.version=version;v->state.features=features;v->state.flags|=WVA_REMOTE_KNOWN;
  }
 }
 pthread_mutex_unlock(&v->lock);
}
void sony_tx_volume_current(struct sony_tx_volume *v,unsigned raw){
 if(!v)return;pthread_mutex_lock(&v->lock);
 if(!(v->state.flags&WVA_SEALED)&&(v->state.flags&WVA_CONNECTION_KNOWN)&&v->state.connection==2&&!advance(v)){
  if(raw>127){v->state.value=0;v->state.flags&=~WVA_VALUE_KNOWN;}
  else {
   v->state.value=raw;v->state.flags|=WVA_VALUE_KNOWN;
   if(v->pending&&raw==v->target){v->state.confirmed=v->pending;v->pending=0;v->state.flags&=~WVA_WAITING_ECHO;}
  }
 }
 pthread_mutex_unlock(&v->lock);
}
void sony_tx_volume_mode_observed(struct sony_tx_volume *v,unsigned enabled){
 if(!v)return;pthread_mutex_lock(&v->lock);
 if(!(v->state.flags&WVA_SEALED)&&!advance(v)){
  v->pending=0;v->state.flags&=~WVA_WAITING_ECHO;
  if(enabled>1){v->state.mode=0;v->state.flags&=~WVA_MODE_KNOWN;}
  else {v->state.mode=enabled;v->state.flags|=WVA_MODE_KNOWN;}
 }
 pthread_mutex_unlock(&v->lock);
}
int sony_tx_volume_snapshot(struct sony_tx_volume *v,struct wva_snapshot *out){
 if(!v||!out)return -EINVAL;pthread_mutex_lock(&v->lock);*out=v->state;pthread_mutex_unlock(&v->lock);return 0;
}
int sony_tx_volume_command(struct sony_tx_volume *v,unsigned op,uint64_t expected,int value,struct wva_snapshot *out){
 if(!v||!out)return -EINVAL;pthread_mutex_lock(&v->lock);int rc=0,absolute=0;unsigned target=0;uint64_t command=0;
 (void)expected;
 if(op==WVA_QUERY)goto snapshot;
 if(op!=WVA_STEP&&op!=WVA_MODE){rc=-ENOSYS;goto snapshot;}
 if(v->state.flags&WVA_SEALED){rc=-ESHUTDOWN;goto snapshot;}
 if(v->state.flags&(WVA_CALL_IN_FLIGHT|WVA_WAITING_ECHO)){rc=-EBUSY;goto snapshot;}
 if(!(v->state.flags&WVA_MODE_KNOWN)){rc=-EAGAIN;goto snapshot;}
 if(op==WVA_STEP){
  if(value!=1&&value!=-1){rc=-EINVAL;goto snapshot;}
  if(!(v->state.flags&WVA_CONNECTION_KNOWN)||v->state.connection!=2){rc=-ENOTCONN;goto snapshot;}
  absolute=v->state.mode&&(v->state.flags&WVA_REMOTE_KNOWN)&&v->state.version>=0x104&&(v->state.features&8);
  if(absolute){
   if(!(v->state.flags&WVA_VALUE_KNOWN)){rc=-EAGAIN;goto snapshot;}
   target=value==1?(v->state.value>123?127:v->state.value+4):(v->state.value<4?0:v->state.value-4);
  }
 }else if(value!=0&&value!=1){rc=-EINVAL;goto snapshot;}
 if(v->state.command==UINT64_MAX||(rc=advance(v))){rc=-EOVERFLOW;goto snapshot;}
 command=++v->state.command;v->state.flags|=WVA_CALL_IN_FLIGHT;
 if(op==WVA_STEP){
  v->state.value=0;v->state.flags&=~WVA_VALUE_KNOWN;
  if(absolute){v->pending=command;v->target=target;v->state.flags|=WVA_WAITING_ECHO;}
 }
 // Original calls may synchronously invoke callbacks. No callback lock is
 // held across them, and the pending identity exists before submission.
 pthread_mutex_unlock(&v->lock);
 rc=op==WVA_MODE?v->ops.mode(v->context,(unsigned)value):absolute?v->ops.absolute(v->context,target):v->ops.relative(v->context,value);
 if(rc>0)rc=-EIO;
 pthread_mutex_lock(&v->lock);v->state.flags&=~WVA_CALL_IN_FLIGHT;
 if(v->state.flags&WVA_SEALED&&!rc)rc=-ECANCELED;
 if(rc){
  if(v->pending==command){v->pending=0;v->state.flags&=~WVA_WAITING_ECHO;}
  if(op==WVA_MODE){v->state.mode=0;v->state.flags&=~WVA_MODE_KNOWN;}
 }else if(op==WVA_MODE&&!(v->state.flags&WVA_SEALED)){
  v->state.mode=(unsigned)value;v->state.flags|=WVA_MODE_KNOWN;v->state.confirmed=command;
 }
 if(advance(v)&&!rc)rc=-EOVERFLOW;
snapshot:
 *out=v->state;pthread_mutex_unlock(&v->lock);return rc;
}
void sony_tx_volume_seal(struct sony_tx_volume *v){
 if(!v)return;pthread_mutex_lock(&v->lock);
 if(!(v->state.flags&WVA_SEALED)){advance(v);v->state.flags|=WVA_SEALED;v->state.flags&=~WVA_WAITING_ECHO;v->pending=0;}
 pthread_mutex_unlock(&v->lock);
}
int sony_tx_volume_destroy(struct sony_tx_volume **out){
 if(!out||!*out)return -EINVAL;struct sony_tx_volume *v=*out;pthread_mutex_lock(&v->lock);
 if(!(v->state.flags&WVA_SEALED)||v->state.flags&WVA_CALL_IN_FLIGHT){pthread_mutex_unlock(&v->lock);return -EBUSY;}
 pthread_mutex_unlock(&v->lock);int rc=pthread_mutex_destroy(&v->lock);if(rc)return -rc;free(v);*out=NULL;return 0;
}
