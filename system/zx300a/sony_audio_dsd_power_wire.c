#define _GNU_SOURCE
#include "sony_audio_dsd_power_wire.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
struct endpoint {int fd,busy;unsigned timeout;struct sony_dsd_power_wire_binding binding;struct sony_dsd_power_wire_state state;};
struct sony_dsd_power_wire_client {struct endpoint e;};
struct sony_dsd_power_wire_server {struct endpoint e;struct sony_dsd_power_backend backend;struct sony_dsd_power_wire_authority authority;struct sony_dsd_power_wire_policy policy;};
static int lock(struct endpoint *e){return !e?-EINVAL:__atomic_exchange_n(&e->busy,1,__ATOMIC_ACQUIRE)?-EBUSY:0;}
static int unlock(struct endpoint *e,int rc){__atomic_store_n(&e->busy,0,__ATOMIC_RELEASE);return rc;}
static int remember(struct endpoint *e,int rc){if(rc>0)rc=-EPROTO;if(rc&&!e->state.first_error)e->state.first_error=rc;return rc;}
static void close_transport(struct endpoint *e){if(e->fd>=0){close(e->fd);e->fd=-1;}e->state.dead=1;}
static int broken(struct endpoint *e,int rc){remember(e,rc);close_transport(e);return rc;}
static int64_t now_ms(void){struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t))return -1;return (int64_t)t.tv_sec*1000+t.tv_nsec/1000000;}
static int wait_fd(struct endpoint *e,short events,int64_t deadline){
    for(;;){int64_t now=now_ms();if(now<0)return -EIO;if(now>=deadline)return -ETIMEDOUT;
        struct pollfd p={e->fd,events,0};int rc=poll(&p,1,(int)(deadline-now));
        if(rc<0&&errno==EINTR)continue;if(rc<0)return -errno;if(!rc)return -ETIMEDOUT;
        if(p.revents&(POLLNVAL|POLLERR))return -EIO;
        if(p.revents&events)return 0;if(p.revents&POLLHUP)return -EPIPE;
    }
}
int sony_dsd_power_wire_pair(int pair[2]){
    if(!pair)return -EINVAL;
    int s[2];if(socketpair(AF_UNIX,SOCK_SEQPACKET|SOCK_NONBLOCK|SOCK_CLOEXEC,0,s))return -errno;
    pair[0]=s[0];pair[1]=s[1];return 0;
}
static int initialize(struct endpoint *e,int fd,const struct sony_dsd_power_wire_binding *b,unsigned timeout){
    if(!b||fd<0||!timeout)return -EINVAL;
    int flags=fcntl(fd,F_GETFL);
    if(flags<0||fcntl(fd,F_SETFL,flags|O_NONBLOCK))return -errno;
    e->fd=fd;e->binding=*b;e->timeout=timeout;
    /* The public zero value does not prevent a resource operation.  A local
     * nonzero marker keeps existing callers' cleanup bookkeeping usable. */
    if(!e->binding.receipt)e->binding.receipt=1;
    return 0;
}
static struct wkp_header header(struct endpoint *e,unsigned command,unsigned request){
    struct sony_dsd_power_wire_binding *b=&e->binding;
    return (struct wkp_header){.magic=WKP_MAGIC,.version=WKP_VERSION,.command=command,.request=request,
        .session_low=(unsigned)b->session,.session_high=(unsigned)(b->session>>32),.generation_low=(unsigned)b->generation,.generation_high=(unsigned)(b->generation>>32),
        .receipt_low=(unsigned)b->receipt,.receipt_high=(unsigned)(b->receipt>>32)};
}
static int valid(struct endpoint *e,const struct wkp_header *h){
    (void)e;return h->payload_bytes<=sizeof(struct sony_dsd_power_plan);
}
static int send_packet(struct endpoint *e,const struct wkp_header *h,const void *body,int64_t deadline){
    struct iovec vectors[2]={{(void *)h,sizeof(*h)},{(void *)body,h->payload_bytes}};
    struct msghdr message={0};message.msg_iov=vectors;message.msg_iovlen=h->payload_bytes?2:1;
    for(;;){int rc=wait_fd(e,POLLOUT,deadline);if(rc)return rc;
        ssize_t n=sendmsg(e->fd,&message,MSG_DONTWAIT|MSG_NOSIGNAL);
        if(n<0&&(errno==EAGAIN||errno==EWOULDBLOCK||errno==EINTR))continue;
        if(n<0)return -errno;return n==(ssize_t)(sizeof(*h)+h->payload_bytes)?0:-EIO;
    }
}
static int receive_packet(struct endpoint *e,struct wkp_header *h,void *body,int64_t deadline){
    for(;;){int rc=wait_fd(e,POLLIN,deadline);if(rc)return rc;
        unsigned char raw[sizeof(*h)+sizeof(struct sony_dsd_power_plan)];
        union {struct cmsghdr alignment;unsigned char data[CMSG_SPACE(sizeof(struct ucred))+CMSG_SPACE(256*sizeof(int))];} control;
        struct iovec vector={raw,sizeof(raw)};struct msghdr m={0};m.msg_iov=&vector;m.msg_iovlen=1;m.msg_control=control.data;m.msg_controllen=sizeof(control.data);
        ssize_t n=recvmsg(e->fd,&m,MSG_DONTWAIT|MSG_CMSG_CLOEXEC);
        if(n<0&&(errno==EAGAIN||errno==EWOULDBLOCK||errno==EINTR))continue;
        if(n<0)return -errno;if(!n)return -EPIPE;
        int invalid=!!(m.msg_flags&(MSG_TRUNC|MSG_CTRUNC));
        for(struct cmsghdr *c=CMSG_FIRSTHDR(&m);c;c=CMSG_NXTHDR(&m,c)){
            if(c->cmsg_level==SOL_SOCKET&&c->cmsg_type==SCM_RIGHTS){
                unsigned count=(unsigned)(c->cmsg_len-CMSG_LEN(0))/sizeof(int);int *fds=(void *)CMSG_DATA(c);
                for(unsigned i=0;i<count;++i)close(fds[i]);invalid=1;
            }else if(!(c->cmsg_level==SOL_SOCKET&&c->cmsg_type==SCM_CREDENTIALS))invalid=1;
        }
        if(invalid)return -EPERM;
        if(n<(ssize_t)sizeof(*h))return -EPROTO;
        memcpy(h,raw,sizeof(*h));if(!valid(e,h)||n!=(ssize_t)(sizeof(*h)+h->payload_bytes))return -EPROTO;
        if(h->payload_bytes){if(!body)return -EPROTO;memcpy(body,raw+sizeof(*h),h->payload_bytes);}
        return 0;
    }
}
int sony_dsd_power_wire_client_attach(struct sony_dsd_power_wire_client **out,int fd,const struct sony_dsd_power_wire_binding *b,unsigned timeout){
    if(!out||*out)return -EINVAL;
    struct sony_dsd_power_wire_client *c=calloc(1,sizeof(*c));if(!c)return -ENOMEM;
    int rc=initialize(&c->e,fd,b,timeout);if(rc){free(c);return rc;}*out=c;return 0;
}
static int exchange(struct endpoint *e,unsigned command,const void *plan,struct wkp_header *reply){
    if(e->state.dead)return -EPIPE;
    unsigned request=++e->state.last_request;struct wkp_header h=header(e,command,request);
    if(plan)h.payload_bytes=command==WKP_ASSURE?sizeof(unsigned):sizeof(struct sony_dsd_power_plan);
    int64_t now=now_ms();if(now<0)return broken(e,-EIO);int64_t end=now+e->timeout;
    int rc=send_packet(e,&h,plan,end);if(!rc)rc=receive_packet(e,reply,NULL,end);
    return rc?broken(e,rc):0;
}
static int client_acquire(void *v,const struct sony_dsd_power_plan *plan,uint64_t *token){
    if(!plan||!token)return -EINVAL;*token=0;struct sony_dsd_power_wire_client *c=v;int rc=lock(c?&c->e:NULL);if(rc)return rc;
    struct endpoint *e=&c->e;
    if(e->state.acquire_attempted||e->state.dead)return unlock(e,e->state.dead?-EPIPE:-EALREADY);
    e->state.acquire_attempted=1;e->state.actual_receipt=e->binding.receipt;e->state.retained=1;*token=e->binding.receipt;
    struct wkp_header reply;rc=exchange(e,WKP_ACQUIRE,plan,&reply);
    if(!rc){
        e->state.acquired=!!(reply.flags&WKP_ACQUIRED);e->state.retained=!!(reply.flags&WKP_RETAINED);
        if(!e->state.retained){e->state.actual_receipt=0;*token=0;}rc=reply.status;
    }
    if(rc)remember(e,rc);return unlock(e,rc);
}
static int client_closed(void *v){
    struct sony_dsd_power_wire_client *c=v;int rc=lock(c?&c->e:NULL);if(rc)return rc;struct endpoint *e=&c->e;
    struct wkp_header reply;rc=exchange(e,WKP_CLOSED,NULL,&reply);
    if(!rc){rc=reply.status;e->state.closed_confirmed=!rc;}
    if(rc)remember(e,rc);return unlock(e,rc);
}
static int client_release(void *v,uint64_t token){
    struct sony_dsd_power_wire_client *c=v;int rc=lock(c?&c->e:NULL);if(rc)return rc;struct endpoint *e=&c->e;
    (void)token;
    e->state.release_attempted=1;struct wkp_header reply;rc=exchange(e,WKP_RELEASE,NULL,&reply);
    if(!rc){rc=reply.status;if(!rc){e->state.retained=0;e->state.actual_receipt=0;e->state.released=1;}}
    if(rc)remember(e,rc);return unlock(e,rc);
}
int sony_dsd_power_wire_client_backend(struct sony_dsd_power_wire_client *c,struct sony_dsd_power_backend *out){
    if(!c||!out)return -EINVAL;*out=(struct sony_dsd_power_backend){client_acquire,client_closed,client_release,c};return 0;
}
int sony_dsd_power_wire_client_state(struct sony_dsd_power_wire_client *c,struct sony_dsd_power_wire_state *out){
    if(!out)return -EINVAL;int rc=lock(c?&c->e:NULL);if(rc)return rc;*out=c->e.state;return unlock(&c->e,0);
}
int sony_dsd_power_wire_client_assure(struct sony_dsd_power_wire_client *c,unsigned operation){
    int rc=lock(c?&c->e:NULL);if(rc)return rc;struct endpoint *e=&c->e;
    if(e->state.dead)return unlock(e,-EPIPE);
    struct wkp_header reply;rc=exchange(e,WKP_ASSURE,&operation,&reply);
    if(!rc)rc=reply.status;
    if(rc)remember(e,rc);return unlock(e,rc);
}
int sony_dsd_power_wire_client_destroy(struct sony_dsd_power_wire_client **owner){
    if(!owner||!*owner)return -EINVAL;struct sony_dsd_power_wire_client *c=*owner;int rc=lock(&c->e);if(rc)return rc;
    close_transport(&c->e);free(c);*owner=NULL;return 0;
}
int sony_dsd_power_wire_server_attach(struct sony_dsd_power_wire_server **out,int fd,const struct sony_dsd_power_wire_binding *b,
 const struct sony_dsd_power_backend *backend,const struct sony_dsd_power_wire_authority *authority,unsigned timeout){
    if(!out||*out||!backend||!backend->acquire||!backend->output_closed||!backend->release||!authority||!authority->peer_reaped)return -EINVAL;
    struct sony_dsd_power_wire_server *s=calloc(1,sizeof(*s));if(!s)return -ENOMEM;
    int rc=initialize(&s->e,fd,b,timeout);if(rc){free(s);return rc;}s->backend=*backend;s->authority=*authority;*out=s;return 0;
}
int sony_dsd_power_wire_server_attach_policy(struct sony_dsd_power_wire_server **out,int fd,const struct sony_dsd_power_wire_binding *b,
 const struct sony_dsd_power_backend *backend,const struct sony_dsd_power_wire_authority *authority,unsigned timeout,
 const struct sony_dsd_power_wire_policy *policy){
    if(!policy||!policy->assure)return -EINVAL;
    int rc=sony_dsd_power_wire_server_attach(out,fd,b,backend,authority,timeout);if(!rc)(*out)->policy=*policy;return rc;
}
static int authority(struct sony_dsd_power_wire_server *s){(void)s;return 0;}
static unsigned server_flags(struct endpoint *e){return (e->state.retained?WKP_RETAINED:0)|(e->state.acquired?WKP_ACQUIRED:0)|(e->state.released?WKP_RELEASED:0);}
int sony_dsd_power_wire_server_step(struct sony_dsd_power_wire_server *s){
    struct endpoint *e=s?&s->e:NULL;int rc=lock(e);if(rc)return rc;
    if(e->state.dead||e->state.peer_finished)return unlock(e,-EPIPE);
    rc=authority(s);int reaped=s->authority.peer_reaped(s->authority.context);
    if(!rc&&reaped)rc=reaped<0?reaped:-ECHILD;
    if(rc)return unlock(e,broken(e,rc));
    struct wkp_header h;struct sony_dsd_power_plan plan;int64_t now=now_ms();if(now<0)return unlock(e,broken(e,-EIO));
    rc=receive_packet(e,&h,&plan,now+e->timeout);
    if(rc==-ETIMEDOUT)return unlock(e,0);if(rc)return unlock(e,broken(e,rc));
    /* Recheck authority after waiting; never act on a previously valid lease. */
    rc=authority(s);reaped=s->authority.peer_reaped(s->authority.context);if(!rc&&reaped)rc=reaped<0?reaped:-ECHILD;
    if(!rc&&h.payload_bytes!=(h.command==WKP_ACQUIRE?sizeof(plan):h.command==WKP_ASSURE?sizeof(unsigned):0))rc=-EPROTO;
    if(rc)return unlock(e,broken(e,rc));
    e->state.last_request=h.request;int result=0;
    if(h.command==WKP_ACQUIRE){
        if(e->state.acquire_attempted)result=-EALREADY;
        else{
            e->state.acquire_attempted=1;uint64_t token=0;result=s->backend.acquire(s->backend.context,&plan,&token);
            e->state.actual_receipt=token;e->state.retained=!result||!!token;
            if(!result)e->state.acquired=1;
        }
    }else if(h.command==WKP_CLOSED){
        result=s->backend.output_closed(s->backend.context);if(result>0)result=-EPROTO;e->state.closed_confirmed=!result;
    }else if(h.command==WKP_ASSURE){
        unsigned operation;memcpy(&operation,&plan,sizeof(operation));
        result=s->policy.assure?s->policy.assure(s->policy.context,operation):-ENOTSUP;
        if(result>0)result=-EPROTO;
    }else if(h.command==WKP_RELEASE){
        e->state.release_attempted=1;result=s->backend.release(s->backend.context,e->state.actual_receipt);if(result>0)result=-EPROTO;
        if(!result){e->state.actual_receipt=0;e->state.retained=0;e->state.released=1;}
    }else result=-ENOTSUP;
    if(result)remember(e,result);
    struct wkp_header reply=header(e,h.command|WKP_REPLY,h.request);reply.status=result;reply.flags=server_flags(e);
    now=now_ms();rc=now<0?-EIO:send_packet(e,&reply,NULL,now+e->timeout);
    return unlock(e,rc?broken(e,rc):1);
}
int sony_dsd_power_wire_server_state(struct sony_dsd_power_wire_server *s,struct sony_dsd_power_wire_state *out){
    if(!out)return -EINVAL;int rc=lock(s?&s->e:NULL);if(rc)return rc;*out=s->e.state;return unlock(&s->e,0);
}
int sony_dsd_power_wire_server_finish_peer(struct sony_dsd_power_wire_server *s){
    struct endpoint *e=s?&s->e:NULL;int rc=lock(e);if(rc)return rc;
    if(e->state.peer_finished)return unlock(e,e->state.retained?SONY_DSD_POWER_UNCERTAIN:0);
    rc=authority(s);int reaped=s->authority.peer_reaped(s->authority.context);
    if(!rc&&reaped!=1)rc=reaped<0?reaped:-EBUSY;if(rc)return unlock(e,rc);
    close_transport(e);
    rc=s->backend.output_closed(s->backend.context);if(rc)return unlock(e,rc>0?-EPROTO:rc);
    if(e->state.retained){
        e->state.release_attempted=1;rc=s->backend.release(s->backend.context,e->state.actual_receipt);
        if(rc){remember(e,rc);return unlock(e,SONY_DSD_POWER_UNCERTAIN);}
        e->state.actual_receipt=0;e->state.retained=0;e->state.released=1;
    }
    e->state.peer_finished=1;return unlock(e,0);
}
int sony_dsd_power_wire_server_destroy(struct sony_dsd_power_wire_server **owner){
    if(!owner||!*owner)return -EINVAL;struct sony_dsd_power_wire_server *s=*owner;int rc=lock(&s->e);if(rc)return rc;
    if(s->e.state.retained||!s->e.state.peer_finished)return unlock(&s->e,-EBUSY);
    close_transport(&s->e);free(s);*owner=NULL;return 0;
}
