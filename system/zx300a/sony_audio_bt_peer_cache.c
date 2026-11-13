#define _GNU_SOURCE
#include "sony_audio_bt_peer_cache.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static unsigned word(const void *p){unsigned v;memcpy(&v,p,4);return v;}
static void wipe(void *p,size_t bytes){volatile unsigned char *v=p;while(bytes--)*v++=0;}
int sony_bt_peer_cache_parse(const void *data,size_t bytes,struct sony_bt_peer *out,unsigned *count){
    if((!data&&bytes)||!out||!count)return -EINVAL;
    memset(out,0,SONY_BT_PEER_MAX*sizeof(*out));*count=0;
    const unsigned char *p=data;unsigned used=0;
    size_t records=bytes/SONY_BT_PEER_RECORD;
    if(records>SONY_BT_PEER_MAX)records=SONY_BT_PEER_MAX;
    for(size_t n=0;n<records;++n){
        const unsigned char *record=p+n*SONY_BT_PEER_RECORD;
        /* Original libbluetooth.blueangel.so 0x1ceee..0x1cf2c. */
        if(!word(record)&&!record[4]&&!record[6]&&!record[7])continue;
        struct sony_bt_peer *peer=&out[used];
        /* Original pure conversion function 0x1bb30..0x1bb76. */
        const unsigned offsets[]={7,6,4,2,1,0};
        for(unsigned i=0;i<6;++i)peer->address[i]=record[offsets[i]];
        peer->name_bytes=(unsigned)strnlen((const char*)record+8,249);memcpy(peer->name,record+8,peer->name_bytes);
        peer->cod=word(record+0x1fc);++used;
        /* +0x200 contains the link key. All private fields are skipped. */
    }
    *count=used;return 0;
}
int sony_bt_peer_cache_read(int fd,struct sony_bt_peer *out,unsigned *count){
    if(fd<0||!out||!count)return -EINVAL;
    memset(out,0,SONY_BT_PEER_MAX*sizeof(*out));*count=0;
    int rc=0;
    unsigned char *raw=malloc(SONY_BT_PEER_CACHE_BYTES);if(!raw)return -ENOMEM;
    size_t at=0;
    while(at<SONY_BT_PEER_CACHE_BYTES){
        ssize_t n=pread(fd,raw+at,SONY_BT_PEER_CACHE_BYTES-at,(off_t)at);
        if(n<0&&errno==EINTR)continue;if(n<0){rc=-errno;break;}if(!n)break;at+=(size_t)n;
    }
    if(!rc)rc=sony_bt_peer_cache_parse(raw,at,out,count);
    wipe(raw,SONY_BT_PEER_CACHE_BYTES);free(raw);return rc;
}
