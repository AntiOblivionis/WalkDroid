#define _GNU_SOURCE
#define _LARGEFILE64_SOURCE
#include "sony_audio_dsf_source.h"
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

struct sony_dsf_source {
    int fd,cancel;
    uint64_t bytes;
    pthread_mutex_t api;
    unsigned char buffer[SONY_DSF_SOURCE_MAX_READ];
};
static int cancelled(struct sony_dsf_source *s){return __atomic_load_n(&s->cancel,__ATOMIC_ACQUIRE);}
static int status(struct sony_dsf_source *s){return cancelled(s)?-ECANCELED:0;}

int sony_dsf_source_open_at(struct sony_dsf_source **out,int directory_fd,const char *name,uint64_t budget){
    (void)budget;
    if(!out||*out||!name||!name[0])return -EINVAL;
    int fd=openat64(directory_fd,name,O_RDONLY|O_CLOEXEC|O_NONBLOCK);
    if(fd<0)return -errno;
    struct stat64 st;if(fstat64(fd,&st)){int e=errno;close(fd);return -e;}
    if(st.st_size<0){close(fd);return -EOVERFLOW;}
    struct sony_dsf_source *s=calloc(1,sizeof(*s));if(!s){close(fd);return -ENOMEM;}
    s->fd=fd;s->bytes=(uint64_t)st.st_size;
    int e=pthread_mutex_init(&s->api,NULL);if(e){close(fd);free(s);return -e;}
    *out=s;return 0;
}
int sony_dsf_source_read_at(void *value,uint64_t offset,uint8_t *data,size_t bytes,size_t *actual){
    struct sony_dsf_source *s=value;
    if(!s||!actual||(!data&&bytes))return -EINVAL;
    if(pthread_mutex_trylock(&s->api))return -EBUSY;
    int rc=status(s);
    size_t at=0;
    while(!rc&&at<bytes){
        if(offset>INT64_MAX||at>(uint64_t)INT64_MAX-offset){rc=-EOVERFLOW;break;}
        size_t part=bytes-at;if(part>sizeof(s->buffer))part=sizeof(s->buffer);
        ssize_t n=pread64(s->fd,s->buffer,part,(off64_t)(offset+at));
        if(n<0&&errno==EINTR){rc=status(s);continue;}
        if(n<0){rc=-errno;break;}
        if(!n)break;
        memcpy(data+at,s->buffer,(size_t)n);at+=(size_t)n;rc=status(s);
    }
    if(!rc)*actual=at;
    pthread_mutex_unlock(&s->api);return rc;
}
int sony_dsf_source_size(struct sony_dsf_source *s,uint64_t *size){
    if(!s||!size)return -EINVAL;if(pthread_mutex_trylock(&s->api))return -EBUSY;
    int rc=status(s);struct stat64 st;
    if(!rc&&fstat64(s->fd,&st))rc=-errno;
    if(!rc&&st.st_size<0)rc=-EOVERFLOW;
    if(!rc){s->bytes=(uint64_t)st.st_size;*size=s->bytes;}
    pthread_mutex_unlock(&s->api);return rc;
}
void sony_dsf_source_cancel(struct sony_dsf_source *s){
    if(s)__atomic_store_n(&s->cancel,1,__ATOMIC_RELEASE);
}
int sony_dsf_source_close(struct sony_dsf_source **owner){
    if(!owner)return -EINVAL;struct sony_dsf_source *s=*owner;if(!s)return 0;
    if(pthread_mutex_trylock(&s->api))return -EBUSY;
    int rc=close(s->fd)?-errno:0;
    pthread_mutex_unlock(&s->api);pthread_mutex_destroy(&s->api);free(s);*owner=NULL;return rc;
}
