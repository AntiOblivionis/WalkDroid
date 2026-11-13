#define _GNU_SOURCE
#include "sony_audio_bt_tx_volume_channel.h"
#include "sony_audio_bt_tx_volume_wire.h"
#include <limits.h>
#include <poll.h>
int wva_server_init(struct wva_server *s,int fd,uint64_t nonce,struct sony_tx_volume *v){
 if(!s||!v)return -EINVAL;int rc=wva_socket(fd);if(rc)return rc;
 memset(s,0,sizeof(*s));s->fd=fd;s->nonce=nonce;s->volume=v;s->next=1;return 0;
}
short wva_server_events(const struct wva_server *s){return (s->pending?0:POLLIN)|(s->pending||s->blocked?POLLOUT:0);}
static int flush(struct wva_server *s){
 if(s->pending){
  int rc=wva_send(s->fd,&s->reply,sizeof(s->reply));if(rc)return rc;
  s->pending=0;s->sent_revision=s->reply.state.revision;
 }
 struct wva_reply event={.magic=WVA_MAGIC,.version=1,.nonce=s->nonce};
 int rc=sony_tx_volume_snapshot(s->volume,&event.state);if(rc)return rc;
 if(event.state.revision==s->sent_revision)return 0;
 rc=wva_send(s->fd,&event,sizeof(event));if(!rc)s->sent_revision=event.state.revision;return rc;
}
int wva_server_step(struct wva_server *s,short events){
 if(!s||!s->volume)return -EINVAL;
 if(events&(POLLNVAL|POLLERR))return -EPIPE;
 int rc=flush(s);s->blocked=rc==-EAGAIN;if(rc&&rc!=-EAGAIN)return rc;
 if(!s->pending&&events&(POLLIN|POLLHUP)){
  struct wva_request q;rc=wva_receive(s->fd,&q,sizeof(q));
  if(rc&&rc!=-EAGAIN)return rc;
  if(!rc){
   s->next=q.request+1;s->reply=(struct wva_reply){.magic=WVA_MAGIC,.version=1,.operation=q.operation,.request=q.request,.nonce=s->nonce};
   s->reply.status=sony_tx_volume_command(s->volume,q.operation,q.expected_revision,q.value,&s->reply.state);
   s->pending=1;rc=flush(s);s->blocked=rc==-EAGAIN;if(rc&&rc!=-EAGAIN)return rc;
  }
 }
 return 0;
}
