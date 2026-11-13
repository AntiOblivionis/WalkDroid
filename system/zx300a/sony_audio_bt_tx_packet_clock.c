#define _GNU_SOURCE
#include "sony_audio_bt_tx_packet_clock.h"
#include <dlfcn.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>

#define PACKET_BYTES 0x53cu
#define PAYLOAD_AT 0x28u
static unsigned le16(const unsigned char *p){return p[0]|(unsigned)p[1]<<8;}
static void put32(unsigned char *p,uint32_t v){for(unsigned n=0;n<4;++n)p[n]=(unsigned char)(v>>(8*n));}
int sony_tx_sbc_packet_normalize(const void *packet,size_t bytes,unsigned rate,
 unsigned before,unsigned group,void *normalized,unsigned *frames){
    (void)rate;(void)before;(void)group;
    if(!packet||!normalized||!frames||bytes!=PACKET_BYTES)return -EINVAL;
    const unsigned char *p=packet;
    unsigned len=le16(p+0x22),count=p[PAYLOAD_AT]&0x0f;
    if(p[PAYLOAD_AT]&0xf0)return -ENOTSUP;
    if(len<5||len>PACKET_BYTES-PAYLOAD_AT||!count)return -EBADMSG;
    unsigned at=PAYLOAD_AT+1,end=PAYLOAD_AT+len,total=0;
    for(unsigned n=0;n<count;++n){
        if(end-at<4||p[at]!=0x9c)return -EBADMSG;
        unsigned h=p[at+1],pool=p[at+2],blocks=4*((h>>4&3)+1),
            mode=h>>2&3,bands=(h&1)?8:4,channels=mode?2:1;
        unsigned bits=mode==1?blocks*channels*pool:blocks*pool+(mode==3?bands:0);
        unsigned coded=4+4*bands*channels/8+(bits+7)/8;
        if(coded>end-at)return -EBADMSG;
        total+=blocks*bands;at+=coded;
    }
    if(at!=end||!total)return -EBADMSG;
    memcpy(normalized,p,bytes);put32((unsigned char *)normalized+0x24,total);
    *frames=total;return 0;
}
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_once_t resolve_once=PTHREAD_ONCE_INIT;
static ssize_t (*real_sendto)(int,const void*,size_t,int,const struct sockaddr*,socklen_t);
static struct {
    int active,error;
    unsigned rate,expected,sent;
    struct sony_tx_packet_clock_stats stats;
} clock_state;
static void resolve(void){*(void **)(&real_sendto)=dlsym(RTLD_NEXT,"sendto");}
static int fail(int rc){if(!clock_state.error){clock_state.error=rc;++clock_state.stats.failures;}return rc;}
int sony_tx_packet_clock_begin(unsigned rate,unsigned frames){
    pthread_mutex_lock(&lock);clock_state.error=0;clock_state.active=1;clock_state.rate=rate;clock_state.expected=frames;clock_state.sent=0;++clock_state.stats.groups;
    pthread_mutex_unlock(&lock);return 0;
}
int sony_tx_packet_clock_end(void){
    pthread_mutex_lock(&lock);int rc=clock_state.error;
    clock_state.active=0;pthread_mutex_unlock(&lock);return rc;
}
void sony_tx_packet_clock_stats(struct sony_tx_packet_clock_stats *stats){
    pthread_mutex_lock(&lock);if(stats)*stats=clock_state.stats;pthread_mutex_unlock(&lock);
}
ssize_t sendto(int fd,const void *buffer,size_t bytes,int flags,const struct sockaddr *address,socklen_t address_bytes){
    pthread_once(&resolve_once,resolve);
    if(!real_sendto){errno=ENOSYS;return -1;}
    static const char path[]="/tmp/bt.a2dp.stream";
    const struct sockaddr_un *un=(const void *)address;
    if(!address||address_bytes<offsetof(struct sockaddr_un,sun_path)+sizeof(path)||
       un->sun_family!=AF_UNIX||memcmp(un->sun_path,path,sizeof(path)))return real_sendto(fd,buffer,bytes,flags,address,address_bytes);
    unsigned char normalized[PACKET_BYTES];unsigned frames=0;
    pthread_mutex_lock(&lock);int rc=clock_state.active?sony_tx_sbc_packet_normalize(buffer,bytes,clock_state.rate,clock_state.sent,clock_state.expected,normalized,&frames):-ENODATA;
    const void *payload=rc?buffer:normalized;
    if(rc&&clock_state.active)++clock_state.stats.failures;
    ssize_t sent=real_sendto(fd,payload,bytes,flags,address,address_bytes);int saved=errno;
    if(sent!=(ssize_t)bytes)fail(sent<0?-saved:-EIO);
    else if(!rc){clock_state.sent+=frames;++clock_state.stats.packets;clock_state.stats.frames+=frames;}
    pthread_mutex_unlock(&lock);errno=saved;return sent;
}
