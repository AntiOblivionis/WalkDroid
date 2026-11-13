#define _GNU_SOURCE
#include "sony_audio_bt_store.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#define STORE_MAGIC UINT32_C(0x31534257)
#define MAX_FILES 128u
#define MAX_ENTRIES 256u
#define MAX_DATA 524288u
#define MAX_FILE 65536u
struct disk_header {
    uint32_t magic,version,header_bytes,entries;
    uint64_t generation,parent,payload_bytes;
    unsigned char nonce[16],selected[6],reserved[34],digest[32];
};
struct disk_entry {char name[160];uint32_t kind,bytes;unsigned char reserved[8];};
#define MAX_PAYLOAD (MAX_ENTRIES*sizeof(struct disk_entry)+MAX_DATA)
struct sony_bt_store {int root,gate;pthread_mutex_t mutex;unsigned char factory[64];};
struct sony_bt_snapshot {struct disk_header h;unsigned char *payload;unsigned char factory[64];};
struct pack_entry {struct disk_entry h;unsigned char *data;};
struct pack {struct pack_entry e[MAX_ENTRIES];unsigned entries,files;uint64_t bytes;};
static void wipe(void *p,size_t n){volatile unsigned char *v=p;while(n--)*v++=0;}
static int zeros(const void *p,size_t n){const unsigned char *v=p;for(size_t i=0;i<n;++i)if(v[i])return 0;return 1;}
static int lock_store(struct sony_bt_store *s,int exclusive){
    int rc=pthread_mutex_lock(&s->mutex);if(rc)return -rc;
    while(flock(s->gate,exclusive?LOCK_EX:LOCK_SH))if(errno!=EINTR){rc=-errno;pthread_mutex_unlock(&s->mutex);return rc;}
    return 0;
}
static int unlock(struct sony_bt_store *s,int rc){if(flock(s->gate,LOCK_UN)&&!rc)rc=-errno;int p=pthread_mutex_unlock(&s->mutex);return !rc&&p?-p:rc;}
static int read_exact(int fd,void *p,size_t bytes,off_t offset){
    size_t at=0;while(at<bytes){ssize_t n=pread(fd,(char*)p+at,bytes-at,offset+(off_t)at);
        if(n<0&&errno==EINTR)continue;if(n<=0)return n?-errno:-ESTALE;at+=(size_t)n;}return 0;
}
static int write_exact(int fd,const void *p,size_t bytes){
    size_t at=0;while(at<bytes){ssize_t n=write(fd,(const char*)p+at,bytes-at);
        if(n<0&&errno==EINTR)continue;if(n<=0)return n?-errno:-EIO;at+=(size_t)n;}return 0;
}
static int valid_name(const char *name){
    size_t len=strnlen(name,160);if(!len||len>=160||name[0]=='/'||name[len-1]=='/')return 0;
    const char *start=name;
    for(const char *p=name;;++p){
        if(*p=='/'||!*p){size_t n=(size_t)(p-start);if(!n||(n==1&&*start=='.')||(n==2&&start[0]=='.'&&start[1]=='.'))return 0;
            if(!*p)break;start=p+1;
        }
    }
    return 1;
}
static const unsigned char *find_file(const struct sony_bt_snapshot *s,const char *name,unsigned *bytes){
    size_t at=0;for(unsigned n=0;n<s->h.entries;++n){struct disk_entry e;memcpy(&e,s->payload+at,sizeof(e));
        if(!strcmp(e.name,name)&&e.kind==2){*bytes=e.bytes;return s->payload+at+sizeof(e);}at+=sizeof(e)+e.bytes;}return NULL;
}
static int contains_peer(const struct sony_bt_snapshot *s,const unsigned char peer[6]){
    struct sony_bt_peer *peers=calloc(SONY_BT_PEER_MAX,sizeof(*peers));if(!peers)return -ENOMEM;
    unsigned bytes=0,count=0;const void *cache=find_file(s,"Bluetooth/devdb/dev_cache",&bytes);int found=0;
    int rc=cache?sony_bt_peer_cache_parse(cache,bytes,peers,&count):0;
    if(!rc)for(unsigned n=0;n<count;++n)if(!memcmp(peers[n].address,peer,6))found=1;
    free(peers);return rc?rc:found;
}
static int validate(const struct sony_bt_snapshot *s){
    const struct disk_header *h=&s->h;
    if(h->entries>MAX_ENTRIES||h->payload_bytes>MAX_PAYLOAD)return -EBADMSG;
    size_t at=0;
    for(unsigned n=0;n<h->entries;++n){
        if(h->payload_bytes-at<sizeof(struct disk_entry))return -EBADMSG;
        /* Disk records may be unaligned after arbitrary-sized file contents. */
        struct disk_entry e;memcpy(&e,s->payload+at,sizeof(e));
        if(!valid_name(e.name)||h->payload_bytes-at-sizeof(e)<e.bytes)return -EBADMSG;
        at+=sizeof(e)+e.bytes;
    }
    return 0;
}
void sony_bt_snapshot_close(struct sony_bt_snapshot **p){if(!p||!*p)return;struct sony_bt_snapshot *s=*p;
    if(s->payload){wipe(s->payload,(size_t)s->h.payload_bytes);free(s->payload);}wipe(s,sizeof(*s));free(s);*p=NULL;}
static int load(struct sony_bt_store *store,struct sony_bt_snapshot **out){
    *out=NULL;int fd=openat(store->root,"current",O_RDONLY|O_NONBLOCK|O_NOFOLLOW|O_CLOEXEC);
    if(fd<0&&errno!=ENOENT)return -errno;
    struct sony_bt_snapshot *s=calloc(1,sizeof(*s));if(!s){if(fd>=0)close(fd);return -ENOMEM;}
    memcpy(s->factory,store->factory,sizeof(s->factory));
    if(fd<0){*out=s;return 0;}
    struct stat before;int rc=fstat(fd,&before)?-errno:0;
    if(!rc&&(before.st_size<(off_t)sizeof(s->h)||before.st_size>(off_t)(sizeof(s->h)+MAX_PAYLOAD)))rc=-EBADMSG;
    if(!rc)rc=read_exact(fd,&s->h,sizeof(s->h),0);
    if(!rc&&(s->h.payload_bytes>MAX_PAYLOAD||s->h.payload_bytes!=(uint64_t)before.st_size-sizeof(s->h)))rc=-EBADMSG;
    if(!rc){s->payload=malloc(s->h.payload_bytes?(size_t)s->h.payload_bytes:1);if(!s->payload)rc=-ENOMEM;}
    if(!rc)rc=read_exact(fd,s->payload,(size_t)s->h.payload_bytes,sizeof(s->h));
    if(!rc)rc=validate(s);close(fd);
    if(rc){/* A malformed header must never choose the wipe length. */
        if(s->h.payload_bytes>MAX_PAYLOAD)s->h.payload_bytes=0;sony_bt_snapshot_close(&s);return rc;}
    *out=s;return 0;
}
int sony_bt_store_open(struct sony_bt_store **out,int root,const unsigned char factory[64]){
    if(!out||root<0||!factory)return -EINVAL;*out=NULL;int rc=0;
    struct sony_bt_store *s=calloc(1,sizeof(*s));if(!s)return -ENOMEM;s->root=-1;s->gate=-1;
    rc=pthread_mutex_init(&s->mutex,NULL);if(rc){free(s);return -rc;}
    s->root=fcntl(root,F_DUPFD_CLOEXEC,3);if(s->root<0){rc=-errno;pthread_mutex_destroy(&s->mutex);free(s);return rc;}
    memcpy(s->factory,factory,64);
    s->gate=openat(s->root,"gate",O_RDWR|O_CREAT|O_NONBLOCK|O_NOFOLLOW|O_CLOEXEC,0600);
    if(s->gate<0)rc=-errno;
    if(!rc&&fsync(s->gate))rc=-errno;if(!rc&&fsync(s->root))rc=-errno;
    if(rc){sony_bt_store_close(&s);return rc;}*out=s;return 0;
}
void sony_bt_store_close(struct sony_bt_store **p){if(!p||!*p)return;struct sony_bt_store *s=*p;
    if(s->gate>=0)close(s->gate);if(s->root>=0)close(s->root);pthread_mutex_destroy(&s->mutex);wipe(s,sizeof(*s));free(s);*p=NULL;}
int sony_bt_store_read(struct sony_bt_store *s,struct sony_bt_snapshot **out){
    if(!s||!out)return -EINVAL;*out=NULL;int rc=lock_store(s,0);if(rc)return rc;rc=load(s,out);return unlock(s,rc);
}
int sony_bt_store_reconcile(struct sony_bt_store *s,struct sony_bt_snapshot **out){
    if(!s||!out)return -EINVAL;*out=NULL;int rc=lock_store(s,1);if(rc)return rc;
    rc=load(s,out);if(!rc&&fsync(s->root))rc=-errno;if(rc)sony_bt_snapshot_close(out);return unlock(s,rc);
}
static void clear_pack(struct pack *p){for(unsigned n=0;n<p->entries;++n)if(p->e[n].data){wipe(p->e[n].data,p->e[n].h.bytes);free(p->e[n].data);}free(p);}
static int collect(struct pack *p,int dir,const char *prefix,unsigned depth){
    int rc=0;
    /* Separate open description: readdir must not alter the caller FD offset. */
    int copy=openat(dir,".",O_RDONLY|O_DIRECTORY|O_CLOEXEC);if(copy<0)return -errno;
    DIR *stream=fdopendir(copy);if(!stream){close(copy);return -errno;}
    struct dirent *item;
    for(;;){errno=0;item=readdir(stream);if(!item){if(errno&&!rc)rc=-errno;break;}if(rc)break;
        if(!strcmp(item->d_name,".")||!strcmp(item->d_name,".."))continue;
        if(p->entries==MAX_ENTRIES){rc=-E2BIG;break;}
        struct pack_entry *e=&p->e[p->entries];int n=snprintf(e->h.name,sizeof(e->h.name),"%s%s%s",prefix,*prefix?"/":"",item->d_name);
        if(n<0||n>=(int)sizeof(e->h.name)||!valid_name(e->h.name)){rc=-EPERM;break;}
        struct stat path,opened;if(fstatat(dir,item->d_name,&path,AT_SYMLINK_NOFOLLOW)){rc=-errno;break;}
        int is_dir=S_ISDIR(path.st_mode);if(!is_dir&&!S_ISREG(path.st_mode)){rc=-EINVAL;break;}
        int fd=openat(dir,item->d_name,O_RDONLY|O_NONBLOCK|O_NOFOLLOW|O_CLOEXEC|(is_dir?O_DIRECTORY:0));if(fd<0){rc=-errno;break;}
        if(fstat(fd,&opened))rc=-errno;
        e->h.kind=is_dir?1:2;++p->entries;
        if(!rc&&is_dir)rc=collect(p,fd,e->h.name,depth+1);
        else if(!rc){
            if(opened.st_size<0||opened.st_size>MAX_FILE||++p->files>MAX_FILES||p->bytes+(uint64_t)opened.st_size>MAX_DATA)rc=-EFBIG;
            if(!rc){e->h.bytes=(unsigned)opened.st_size;p->bytes+=e->h.bytes;e->data=malloc(e->h.bytes?e->h.bytes:1);if(!e->data)rc=-ENOMEM;}
            if(!rc)rc=read_exact(fd,e->data,e->h.bytes,0);
        }
        close(fd);
    }
    closedir(stream);return rc;
}
static int compare_entry(const void *a,const void *b){return strcmp(((const struct pack_entry*)a)->h.name,((const struct pack_entry*)b)->h.name);}
static int pack_seed(int seed,uint64_t parent,const unsigned char nonce[16],const unsigned char factory[64],struct sony_bt_snapshot **out){
    *out=NULL;struct pack *p=calloc(1,sizeof(*p));if(!p)return -ENOMEM;int rc=collect(p,seed,"",0);
    struct sony_bt_snapshot *s=NULL;if(!rc){s=calloc(1,sizeof(*s));if(!s)rc=-ENOMEM;}
    if(!rc){memcpy(s->factory,factory,sizeof(s->factory));qsort(p->e,p->entries,sizeof(p->e[0]),compare_entry);s->h=(struct disk_header){.magic=STORE_MAGIC,.version=1,.header_bytes=sizeof(s->h),.entries=p->entries,.generation=parent+1,.parent=parent,.payload_bytes=p->bytes+p->entries*sizeof(struct disk_entry)};
        memcpy(s->h.nonce,nonce,16);s->payload=malloc((size_t)s->h.payload_bytes);if(!s->payload)rc=-ENOMEM;}
    if(!rc){size_t at=0;for(unsigned n=0;n<p->entries;++n){memcpy(s->payload+at,&p->e[n].h,sizeof(struct disk_entry));at+=sizeof(struct disk_entry);
            if(p->e[n].h.bytes)memcpy(s->payload+at,p->e[n].data,p->e[n].h.bytes);at+=p->e[n].h.bytes;}
        rc=validate(s);}
    clear_pack(p);if(rc)sony_bt_snapshot_close(&s);else *out=s;return rc;
}
/* Caller holds both the local mutex and current gate's EX flock. */
static int publish(struct sony_bt_store *store,struct sony_bt_snapshot *next,
 enum sony_bt_store_publication *publication,struct sony_bt_snapshot **out){
    int rc=0;memset(next->h.digest,0,sizeof(next->h.digest));const unsigned char *nonce=next->h.nonce;
    char temp[64]="pending-";for(unsigned i=0;i<16;++i)snprintf(temp+8+2*i,3,"%02x",nonce[i]);
    int fd=-1;
    if(!rc){fd=openat(store->root,temp,O_WRONLY|O_CREAT|O_TRUNC|O_NONBLOCK|O_NOFOLLOW|O_CLOEXEC,0600);if(fd<0)rc=-errno;}
    if(!rc)rc=write_exact(fd,&next->h,sizeof(next->h));if(!rc)rc=write_exact(fd,next->payload,(size_t)next->h.payload_bytes);
    if(!rc&&fsync(fd))rc=-errno;if(fd>=0&&close(fd)&&!rc)rc=-errno;
    if(!rc&&renameat(store->root,temp,store->root,"current"))rc=-errno;
    if(!rc){*publication=SONY_BT_STORE_VISIBLE;
        if(fsync(store->root))rc=-EINPROGRESS;
        else{*publication=SONY_BT_STORE_DURABLE;*out=next;}}
    return rc;
}
int sony_bt_store_commit(struct sony_bt_store *store,int seed,uint64_t expected,const unsigned char nonce[16],
 enum sony_bt_store_publication *publication,struct sony_bt_snapshot **out){
    if(!store||seed<0||!nonce||!publication||!out)return -EINVAL;*publication=SONY_BT_STORE_NOT_PUBLISHED;*out=NULL;
    struct sony_bt_snapshot *next=NULL,*current=NULL;int rc=pack_seed(seed,expected,nonce,store->factory,&next);if(rc)return rc;
    rc=lock_store(store,1);if(rc){sony_bt_snapshot_close(&next);return rc;}
    rc=load(store,&current);
    if(!rc){next->h.parent=current->h.generation;next->h.generation=current->h.generation+1;}
    if(!rc&&!zeros(current->h.selected,6)){
        int found=contains_peer(next,current->h.selected);if(found<0)rc=found;else if(found)memcpy(next->h.selected,current->h.selected,6);
    }
    if(!rc){rc=publish(store,next,publication,out);if(!rc)next=NULL;}
    sony_bt_snapshot_close(&current);sony_bt_snapshot_close(&next);return unlock(store,rc);
}
int sony_bt_store_select(struct sony_bt_store *s,uint64_t expected,const unsigned char peer[6],const unsigned char nonce[16],
 enum sony_bt_store_publication *publication,struct sony_bt_snapshot **out){
    if(!s||!nonce||!publication||!out)return -EINVAL;*publication=SONY_BT_STORE_NOT_PUBLISHED;*out=NULL;
    (void)expected;
    unsigned char selected[6]={0};if(peer)memcpy(selected,peer,6);
    int rc=lock_store(s,1);if(rc)return rc;struct sony_bt_snapshot *next=NULL;
    rc=load(s,&next);
    if(!rc){next->h.parent=next->h.generation;next->h.generation++;memcpy(next->h.nonce,nonce,16);memcpy(next->h.selected,selected,6);rc=publish(s,next,publication,out);if(!rc)next=NULL;}
    sony_bt_snapshot_close(&next);return unlock(s,rc);
}
uint64_t sony_bt_snapshot_generation(const struct sony_bt_snapshot *s){return s?s->h.generation:0;}
void sony_bt_snapshot_nonce(const struct sony_bt_snapshot *s,unsigned char out[16]){if(out){if(s)memcpy(out,s->h.nonce,16);else memset(out,0,16);}}
void sony_bt_snapshot_selection(const struct sony_bt_snapshot *s,unsigned char out[6]){if(out){if(s)memcpy(out,s->h.selected,6);else memset(out,0,6);}}
int sony_bt_snapshot_catalog(const struct sony_bt_snapshot *s,struct sony_bt_peer *out,unsigned *count){
    if(!s||!out||!count)return -EINVAL;memset(out,0,SONY_BT_PEER_MAX*sizeof(*out));*count=0;
    unsigned bytes=0;const void *p=find_file(s,"Bluetooth/devdb/dev_cache",&bytes);
    return p?sony_bt_peer_cache_parse(p,bytes,out,count):0;
}
int sony_bt_snapshot_export(const struct sony_bt_snapshot *s,int root){
    if(!s)return -EINVAL;int rc=0;
    int factory_written=0;size_t at=0;for(unsigned n=0;!rc&&n<s->h.entries;++n){struct disk_entry e;memcpy(&e,s->payload+at,sizeof(e));
        char path[160];strcpy(path,e.name);int dir=fcntl(root,F_DUPFD_CLOEXEC,3);if(dir<0){rc=-errno;break;}char *part=path,*slash;
        while((slash=strchr(part,'/'))){*slash=0;
            if(mkdirat(dir,part,0700)&&errno!=EEXIST){rc=-errno;break;}
            int next=openat(dir,part,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);close(dir);dir=next;
            if(dir<0){rc=-errno;break;}part=slash+1;}
        if(!rc){if(e.kind==1){if(mkdirat(dir,part,0700)&&errno!=EEXIST)rc=-errno;}
            else{int fd=openat(dir,part,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);if(fd<0)rc=-errno;
                else{if(!strcmp(e.name,"BT_Addr")){rc=write_exact(fd,s->factory,sizeof(s->factory));factory_written=1;}
                     else rc=write_exact(fd,s->payload+at+sizeof(e),e.bytes);if(close(fd)&&!rc)rc=-errno;}}}
        if(dir>=0)close(dir);at+=sizeof(e)+e.bytes;
    }
    if(!rc&&!factory_written){int fd=openat(root,"BT_Addr",O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC,0600);if(fd<0)rc=-errno;
        else{rc=write_exact(fd,s->factory,sizeof(s->factory));if(close(fd)&&!rc)rc=-errno;}}
    return rc;
}
