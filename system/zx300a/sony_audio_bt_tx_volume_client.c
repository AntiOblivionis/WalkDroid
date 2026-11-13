#define _GNU_SOURCE
#include "sony_audio_bt_tx_volume_channel.h"
#include "sony_audio_bt_tx_volume_wire.h"
#include <limits.h>
int wva_client_init(struct wva_client *c,int fd,uint64_t nonce){
 if(!c)return -EINVAL;int rc=wva_socket(fd);if(rc)return rc;
 memset(c,0,sizeof(*c));c->fd=fd;c->nonce=nonce;c->creator=getpid();c->next=1;return 0;
}
static int ready(struct wva_client *c){return !c?-EINVAL:c->failed?c->failed:0;}
static int fail(struct wva_client *c,int rc){c->failed=rc;shutdown(c->fd,SHUT_RDWR);return rc;}
int wva_client_submit(struct wva_client *c,unsigned op,uint64_t expected,int value){
 int rc=ready(c);if(rc)return rc;if(c->pending)return -EBUSY;
 if(op<WVA_QUERY||op>WVA_MODE||(op==WVA_QUERY&&(expected||value))||
    (op==WVA_STEP&&value!=1&&value!=-1)||(op==WVA_MODE&&value!=0&&value!=1))return -EINVAL;
 if(c->next==UINT32_MAX)return fail(c,-EOVERFLOW);
 struct wva_request q={.magic=WVA_MAGIC,.version=1,.operation=op,.request=c->next,.nonce=c->nonce,.expected_revision=expected,.value=value};
 rc=wva_send(c->fd,&q,sizeof(q));if(rc)return rc==-EAGAIN?rc:fail(c,rc);
 c->pending=c->next++;c->pending_operation=op;return 0;
}
int wva_client_observe(struct wva_client *c,struct wva_reply *out){
 int rc=ready(c);if(rc)return rc;if(!out)return -EINVAL;
 struct wva_reply r;rc=wva_receive(c->fd,&r,sizeof(r));if(rc)return rc==-EAGAIN?rc:fail(c,rc);
 if(r.operation!=WVA_EVENT){
  c->pending=c->pending_operation=0;
 }
 c->state=r.state;*out=r;return r.operation==WVA_EVENT?0:1;
}
