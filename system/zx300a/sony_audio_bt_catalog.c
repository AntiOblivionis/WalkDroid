#define _GNU_SOURCE
#include "sony_audio_bt_catalog.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
int sony_bt_catalog_serve_once_guarded(struct sony_bt_store *store,const unsigned char instance[16],int fd,int allow_selection){
    (void)allow_selection;if(!store||!instance||fd<0)return -EINVAL;int rc=0;
    struct sony_bt_catalog_request q;memset(&q,0,sizeof(q));struct iovec iov={&q,sizeof(q)};
    struct msghdr message={.msg_iov=&iov,.msg_iovlen=1};ssize_t got=recvmsg(fd,&message,MSG_DONTWAIT|MSG_TRUNC);
    if(got<0)return -errno;if(!got)return -ECONNRESET;
    struct sony_bt_catalog_reply *r=calloc(1,sizeof(*r));if(!r)return -ENOMEM;
    r->magic=SONY_BT_CATALOG_MAGIC;r->version=1;r->operation=q.operation;r->request=q.request;
    memcpy(r->instance,instance,16);memcpy(r->nonce,q.nonce,16);r->bytes=offsetof(struct sony_bt_catalog_reply,peers);
    struct sony_bt_snapshot *snapshot=NULL;
    if(q.operation==SONY_BT_CATALOG_STATUS){
        rc=sony_bt_store_read(store,&snapshot);if(!rc)r->publication=SONY_BT_STORE_VISIBLE;
    }else if(q.operation==SONY_BT_CATALOG_SELECT){
        enum sony_bt_store_publication publication=SONY_BT_STORE_NOT_PUBLISHED;
        rc=sony_bt_store_select(store,q.expected_generation,q.peer,q.nonce,&publication,&snapshot);r->publication=publication;
    }else rc=-ENOSYS;
    r->status=rc;
    if(!rc){
        r->generation=sony_bt_snapshot_generation(snapshot);sony_bt_snapshot_selection(snapshot,r->selected);
        struct sony_bt_peer *peers=calloc(SONY_BT_PEER_MAX,sizeof(*peers));if(!peers)rc=-ENOMEM;
        if(!rc)rc=sony_bt_snapshot_catalog(snapshot,peers,&r->count);
        if(!rc)for(unsigned n=0;n<r->count;++n){struct sony_bt_catalog_peer *p=&r->peers[n];
            memcpy(p->address,peers[n].address,6);p->name_bytes=(uint16_t)peers[n].name_bytes;p->cod=peers[n].cod;memcpy(p->name,peers[n].name,p->name_bytes);}
        free(peers);if(rc){r->status=rc;r->count=0;}
    }
    sony_bt_snapshot_close(&snapshot);r->bytes+=r->count*sizeof(r->peers[0]);
    size_t bytes=r->bytes;ssize_t sent=send(fd,r,bytes,MSG_NOSIGNAL|MSG_DONTWAIT);rc=sent==(ssize_t)bytes?0:sent<0?-errno:-EIO;
    /* Public metadata only. No DB payload/digest/key can enter this struct. */
    free(r);return rc;
}
int sony_bt_catalog_serve_once(struct sony_bt_store *store,const unsigned char instance[16],int fd){return sony_bt_catalog_serve_once_guarded(store,instance,fd,1);}
