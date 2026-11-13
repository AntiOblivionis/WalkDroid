#include "sony_audio_bt_journal.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
struct sony_bt_journal {unsigned used,capacity,closed,appenders;int error;unsigned char *data;};
static void put32(unsigned char *p,unsigned value){p[0]=(unsigned char)value;p[1]=(unsigned char)(value>>8);p[2]=(unsigned char)(value>>16);p[3]=(unsigned char)(value>>24);}
static int seal(struct sony_bt_journal *j,int error){int expected=0;__atomic_compare_exchange_n(&j->error,&expected,error,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE);__atomic_store_n(&j->closed,1,__ATOMIC_RELEASE);return error;}
int sony_bt_journal_create(struct sony_bt_journal **out,unsigned capacity){
    if(!out||*out||capacity<64||capacity>16u*1024*1024)return -EINVAL;
    struct sony_bt_journal *j=calloc(1,sizeof(*j));if(!j)return -ENOMEM;j->data=calloc(1,capacity);if(!j->data){free(j);return -ENOMEM;}
    j->capacity=capacity;/* Prefault storage before native callbacks start. */
    for(unsigned n=0;n<capacity;n+=4096)((volatile unsigned char *)j->data)[n]=0;
    *out=j;return 0;
}
static int valid(unsigned kind,unsigned bytes){switch(kind){case SONY_BT_EVENT_SOUND:return bytes==20;case SONY_BT_EVENT_PCM:return bytes<=65535;case SONY_BT_EVENT_DROP:return bytes==0;case SONY_BT_EVENT_CONNECTION:case SONY_BT_EVENT_AUDIO_STATE:return bytes==4;case SONY_BT_EVENT_PEER:return bytes==6;default:return 0;}}
int sony_bt_journal_append(struct sony_bt_journal *j,unsigned kind,const void *payload,unsigned bytes){
    if(!j)return -EINVAL;
    __atomic_add_fetch(&j->appenders,1,__ATOMIC_ACQ_REL);int rc=0;
    if(__atomic_load_n(&j->closed,__ATOMIC_ACQUIRE)){rc=-ESHUTDOWN;goto done;}
    if(!valid(kind,bytes)||(bytes&&!payload)){rc=seal(j,-EINVAL);goto done;}
    unsigned needed=SONY_BT_JOURNAL_HEADER_BYTES+bytes,offset=__atomic_load_n(&j->used,__ATOMIC_ACQUIRE),reserved=0;
    for(unsigned tries=0;tries<64;++tries){
        if(offset>j->capacity||needed>j->capacity-offset){rc=seal(j,-EOVERFLOW);goto done;}
        if(__atomic_compare_exchange_n(&j->used,&offset,offset+needed,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE)){reserved=1;break;}
    }
    if(!reserved){rc=seal(j,-EBUSY);goto done;}
    unsigned char *record=j->data+offset;put32(record,SONY_BT_JOURNAL_MAGIC);put32(record+4,kind);put32(record+8,offset);put32(record+12,bytes);if(bytes)memcpy(record+16,payload,bytes);
done:
    __atomic_sub_fetch(&j->appenders,1,__ATOMIC_RELEASE);return rc;
}
int sony_bt_journal_close(struct sony_bt_journal *j){if(!j)return -EINVAL;__atomic_store_n(&j->closed,1,__ATOMIC_RELEASE);return __atomic_load_n(&j->appenders,__ATOMIC_ACQUIRE)?-EAGAIN:0;}
int sony_bt_journal_view(struct sony_bt_journal *j,const void **out,unsigned *bytes){
    if(!j||!out||!bytes)return -EINVAL;
    if(!__atomic_load_n(&j->closed,__ATOMIC_ACQUIRE)||__atomic_load_n(&j->appenders,__ATOMIC_ACQUIRE))return -EBUSY;
    int rc=__atomic_load_n(&j->error,__ATOMIC_ACQUIRE);if(rc)return rc;unsigned used=__atomic_load_n(&j->used,__ATOMIC_ACQUIRE);if(used>j->capacity)return -EOVERFLOW;*out=j->data;*bytes=used;return 0;
}
int sony_bt_journal_error(const struct sony_bt_journal *j){return j?__atomic_load_n(&j->error,__ATOMIC_ACQUIRE):-EINVAL;}
int sony_bt_journal_destroy(struct sony_bt_journal **owned){if(!owned||!*owned)return -EINVAL;struct sony_bt_journal *j=*owned;if(!__atomic_load_n(&j->closed,__ATOMIC_ACQUIRE)||__atomic_load_n(&j->appenders,__ATOMIC_ACQUIRE))return -EBUSY;free(j->data);free(j);*owned=NULL;return 0;}
