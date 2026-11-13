#define _GNU_SOURCE
#include "sony_audio_owner_commands.h"
#include <errno.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
struct sony_command_server {
    struct sony_audio_owner *owner;struct sony_command_ops ops;pid_t pid;
    int listener,peer,failed;uint64_t generation,touched;
    uint32_t last_request,operation,base_revision;struct sony_command_reply last;
};
struct sony_command_client {
    int fd,failed;pid_t pid;uint64_t generation;uint32_t sequence,operation;
    struct sony_command_request pending;
    struct wdf_controls staged;uint32_t stage_phase;
};
static int now(uint64_t *out){struct timespec t;if(clock_gettime(CLOCK_BOOTTIME,&t))return -errno;
    *out=(uint64_t)t.tv_sec*1000000000u+(uint64_t)t.tv_nsec;return 0;}
static void drop_peer(struct sony_command_server *s){
    if(s->peer>=0)close(s->peer);
    s->peer=-1;s->last_request=s->operation=s->base_revision=0;memset(&s->last,0,sizeof(s->last));
}
static int failed(struct sony_command_server *s,int error){
    s->failed=error;drop_peer(s);if(s->listener>=0)close(s->listener);s->listener=-1;return error;
}
int sony_command_server_open(struct sony_command_server **out,struct sony_audio_owner *owner,const struct sony_command_ops *ops){
    if(!out||*out||!owner||!ops||!ops->read||!ops->stage)return -EINVAL;
    int rc=sony_audio_owner_check(owner);if(rc)return rc;
    struct sony_command_server *s=calloc(1,sizeof(*s));if(!s)return -ENOMEM;
    s->peer=s->listener=-1;s->owner=owner;s->ops=*ops;s->pid=getpid();s->generation=sony_audio_owner_generation(owner);
    rc=sony_audio_owner_channel_listen(owner,&s->listener);if(rc){free(s);return rc;}*out=s;return 0;
}
static int read_state(struct sony_command_server *s,struct wdf_controls *out){
    struct wdf_controls c;int rc=s->ops.read(s->ops.context,&c);if(rc)return rc;
    *out=c;return 0;
}
static void status(struct sony_command_server *s,struct sony_command_reply *reply){
    if(!s->operation){reply->header.error=-ENODATA;reply->header.phase=SONY_COMMAND_REJECTED;return;}
    if(s->last.header.phase==SONY_COMMAND_APPLYING){
        struct wdf_controls c;int rc=read_state(s,&c);
        if(!rc){s->last.state=c;if(!c.pending)s->last.header.phase=SONY_COMMAND_APPLIED;}
        else{s->last.header.error=rc;s->last.header.phase=SONY_COMMAND_FAILED;s->last.header.bytes=0;}
    }
    reply->header.phase=s->last.header.phase;reply->header.error=s->last.header.error;
    reply->header.bytes=s->last.header.bytes;reply->header.operation=s->operation;reply->state=s->last.state;
}
int sony_command_server_descriptor(const struct sony_command_server *s){
    if(!s)return -EINVAL;
    return s->failed?s->failed:s->peer>=0?s->peer:s->listener;
}
int sony_command_server_poll(struct sony_command_server *s){
    if(!s)return -EINVAL;
    if(s->failed)return s->failed;
    int rc=sony_audio_owner_check(s->owner);if(rc)return failed(s,rc);
    /* Freeze the completed exact-operation receipt before any other local
     * volume event can advance its revision. STATUS can then report that
     * result honestly even after a later, independently admitted mutation. */
    if(s->operation&&s->last.header.phase==SONY_COMMAND_APPLYING){struct sony_command_reply observed={0};status(s,&observed);}
    uint64_t time=0;rc=now(&time);if(rc)return failed(s,rc);
    if(s->peer>=0&&(time<s->touched||time-s->touched>=2000000000u))drop_peer(s);
    if(s->peer<0){
        int fd=accept4(s->listener,NULL,NULL,SOCK_NONBLOCK|SOCK_CLOEXEC);
        if(fd<0)return errno==EAGAIN||errno==EWOULDBLOCK||errno==EINTR?0:failed(s,-errno);
        s->peer=fd;s->touched=time;
    }
    struct pollfd p={s->peer,POLLIN,0};if(poll(&p,1,0)<0)return errno==EINTR?0:failed(s,-errno);
    if(p.revents&(POLLHUP|POLLERR|POLLNVAL)){drop_peer(s);return 1;}
    if(!(p.revents&POLLIN))return 0;
    unsigned char packet[sizeof(struct sony_command_request)+sizeof(struct sony_dsd_live_controls)];
    ssize_t n=recv(s->peer,packet,sizeof(packet),MSG_DONTWAIT|MSG_TRUNC);
    if(n<0&&(errno==EAGAIN||errno==EWOULDBLOCK||errno==EINTR))return 0;
    if(n<(ssize_t)sizeof(struct sony_command_request)||n>(ssize_t)sizeof(packet)){drop_peer(s);return 1;}
    struct sony_command_request q;memcpy(&q,packet,sizeof(q));
    if(q.op<1||q.op>3||q.bytes!=(unsigned)n-sizeof(q)||
       (q.op==SONY_COMMAND_STAGE?q.bytes!=sizeof(struct sony_dsd_live_controls):q.bytes!=0)){drop_peer(s);return 1;}
    s->last_request=q.request;s->touched=time;
    struct sony_command_reply reply={.header={SONY_COMMAND_MAGIC,SONY_COMMAND_VERSION,q.op,q.request,s->generation,0,SONY_COMMAND_READY,0,0,{0,0}}};
    /* Recheck after accepting/decoding; a connection or observed ready state is
     * not a lease. No callback runs after a broken owner record. */
    rc=sony_audio_owner_check(s->owner);if(rc)return failed(s,rc);
    if(q.op==SONY_COMMAND_STATUS)status(s,&reply);
    else if(q.op==SONY_COMMAND_GET){
        rc=read_state(s,&reply.state);if(!rc)reply.header.bytes=sizeof(reply.state);
        else{reply.header.error=rc;reply.header.phase=SONY_COMMAND_REJECTED;}
    }else{
        struct sony_dsd_live_controls target;memcpy(&target,packet+sizeof(q),sizeof(target));
        if(s->operation&&s->last.header.phase==SONY_COMMAND_APPLYING){
            /* A STATUS may collect completion; never overwrite an outstanding
             * receipt merely because another write was requested. */
            reply.header.error=-EBUSY;reply.header.phase=SONY_COMMAND_REJECTED;
        }else{
            struct wdf_controls base;rc=read_state(s,&base);
            if(!rc&&base.pending)rc=-EBUSY;
            if(rc){reply.header.error=rc;reply.header.phase=SONY_COMMAND_REJECTED;}
            else{
                s->operation=q.request;s->base_revision=base.revision;reply.header.operation=q.request;
                rc=s->ops.stage(s->ops.context,s->generation,base.revision,&target,&reply.state);
                reply.header.error=rc;reply.header.phase=rc?SONY_COMMAND_FAILED:reply.state.pending?SONY_COMMAND_APPLYING:SONY_COMMAND_APPLIED;
                if(!rc)reply.header.bytes=sizeof(reply.state);
                s->last=reply;
            }
        }
    }
    rc=sony_audio_owner_check(s->owner);if(rc)return failed(s,rc);
    /* The peer may vanish after STAGE. Preserve the admitted native operation;
     * delivery failure never causes a Set retry or a false rollback. */
    unsigned char response[sizeof(reply.header)+sizeof(reply.state)];memcpy(response,&reply.header,sizeof(reply.header));
    if(reply.header.bytes)memcpy(response+sizeof(reply.header),&reply.state,sizeof(reply.state));
    size_t bytes=sizeof(reply.header)+reply.header.bytes;
    if(send(s->peer,response,bytes,MSG_DONTWAIT|MSG_NOSIGNAL)!=(ssize_t)bytes)drop_peer(s);
    return 1;
}
int sony_command_server_close(struct sony_command_server **out){
    if(!out||!*out)return -EINVAL;
    struct sony_command_server *s=*out;
    drop_peer(s);if(s->listener>=0)close(s->listener);free(s);*out=NULL;return 0;
}
static int adopt(struct sony_command_client **out,int fd,uint64_t generation){
    struct sony_command_client *c=calloc(1,sizeof(*c));if(!c){close(fd);return -ENOMEM;}
    c->fd=fd;c->pid=getpid();c->generation=generation;*out=c;return 0;
}
int sony_command_client_open_at(struct sony_command_client **out,int dir,uint64_t generation){
    return sony_command_client_open_at_kind(out,dir,generation,SONY_OWNER_DSF);
}
int sony_command_client_open_at_kind(struct sony_command_client **out,int dir,uint64_t generation,enum sony_audio_owner_kind kind){
    if(!out||*out)return -EINVAL;
    int fd=-1,rc=sony_audio_owner_channel_connect_at(dir,generation,kind,&fd);
    return rc?rc:adopt(out,fd,generation);
}
int sony_command_client_open(struct sony_command_client **out,uint64_t generation){
    return sony_command_client_open_kind(out,generation,SONY_OWNER_DSF);
}
int sony_command_client_open_kind(struct sony_command_client **out,uint64_t generation,enum sony_audio_owner_kind kind){
    if(!out||*out)return -EINVAL;
    int fd=-1,rc=sony_audio_owner_channel_connect(generation,kind,&fd);
    return rc?rc:adopt(out,fd,generation);
}
static int client_error(struct sony_command_client *c,int error){
    if(c->fd>=0)close(c->fd);
    c->fd=-1;c->failed=error;return error;
}
int sony_command_client_send(struct sony_command_client *c,enum sony_command_op op,unsigned revision,const struct sony_dsd_live_controls *target){
    if(!c||op<1||op>3||(op==SONY_COMMAND_STAGE?!target:target!=NULL))return -EINVAL;
    if(c->failed)return c->failed;
    if(c->pending.request)return -EBUSY;
    if(c->sequence==UINT32_MAX)return client_error(c,-EOVERFLOW);
    struct sony_command_request q={SONY_COMMAND_MAGIC,SONY_COMMAND_VERSION,op,c->sequence+1,c->generation,revision,target?sizeof(*target):0};
    unsigned char packet[sizeof(q)+sizeof(*target)];memcpy(packet,&q,sizeof(q));if(target)memcpy(packet+sizeof(q),target,sizeof(*target));
    ssize_t n=send(c->fd,packet,sizeof(q)+q.bytes,MSG_DONTWAIT|MSG_NOSIGNAL);
    if(n!=(ssize_t)(sizeof(q)+q.bytes))return client_error(c,n<0?-errno:-EIO);
    c->sequence=q.request;c->pending=q;return 0;
}
int sony_command_client_receive(struct sony_command_client *c,struct sony_command_reply *out){
    if(!c||!out)return -EINVAL;
    if(c->failed)return c->failed;
    if(!c->pending.request)return -ENODATA;
    unsigned char packet[sizeof(struct sony_command_response)+sizeof(struct wdf_controls)];
    ssize_t n=recv(c->fd,packet,sizeof(packet),MSG_DONTWAIT|MSG_TRUNC);
    if(n<0&&(errno==EAGAIN||errno==EWOULDBLOCK||errno==EINTR))return -EAGAIN;
    if(n<=0)return client_error(c,n<0?-errno:-EPIPE);
    if(n<(ssize_t)sizeof(struct sony_command_response)||n>(ssize_t)sizeof(packet))return client_error(c,-EPROTO);
    struct sony_command_reply reply={0};memcpy(&reply.header,packet,sizeof(reply.header));
    struct sony_command_response *h=&reply.header;
    if(h->bytes!=(unsigned)n-sizeof(*h)||(h->bytes&&h->bytes!=sizeof(reply.state)))return client_error(c,-EPROTO);
    if(h->bytes)memcpy(&reply.state,packet+sizeof(*h),sizeof(reply.state));
    c->operation=h->operation;c->staged=reply.state;c->stage_phase=h->phase;
    c->pending=(struct sony_command_request){0};*out=reply;return 0;
}
int sony_command_client_close(struct sony_command_client **out){
    if(!out||!*out)return -EINVAL;
    struct sony_command_client *c=*out;
    if(c->fd>=0)close(c->fd);
    free(c);*out=NULL;return 0;
}
