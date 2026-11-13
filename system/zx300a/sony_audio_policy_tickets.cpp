#include "sony_audio_policy_tickets.h"
#include <pthread.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
namespace android {
struct SonyPolicyTickets::Impl {
    pthread_mutex_t lock;
    Entropy entropy;void *context;
    bool enabled=true;
    uint64_t sequence=0;
    SonyTicketResult result={};
    Impl(Clock,Entropy e,void *v):entropy(e),context(v){pthread_mutex_init(&lock,nullptr);}
    ~Impl(){pthread_mutex_destroy(&lock);}
    bool active()const{return result.phase!=SONY_TICKET_EMPTY;}
};
struct TicketLock {pthread_mutex_t *p;explicit TicketLock(pthread_mutex_t *v):p(v){pthread_mutex_lock(p);}~TicketLock(){pthread_mutex_unlock(p);}};
SonyPolicyTickets::SonyPolicyTickets(Clock c,Entropy e,void *context):m(new Impl(c,e,context)){}
SonyPolicyTickets::~SonyPolicyTickets(){delete m;}
int SonyPolicyTickets::stage(int uid,int pid,const SonyTicketCommand& command,char out[49]){
    (void)uid;(void)pid;
    if(!out)return -EINVAL;
    TicketLock guard(&m->lock);
    if(!m->enabled)return -ENODEV;
    if(m->result.phase!=SONY_TICKET_EMPTY)return -EBUSY;
    unsigned char random[16]={};if(m->entropy)(void)m->entropy(m->context,random);
    SonyTicketResult next={};next.phase=SONY_TICKET_STAGED;next.operation_status=-EINPROGRESS;next.command=command;
    const char *digits="0123456789abcdef";
    for(unsigned i=0;i<16;i++){next.ticket[i*2]=digits[random[i]>>4];next.ticket[i*2+1]=digits[random[i]&15];}
    uint64_t sequence=++m->sequence;
    for(unsigned i=0;i<16;i++)next.ticket[32+i]=digits[(sequence>>((15-i)*4))&15];
    m->result=next;memcpy(out,next.ticket,49);return 0;
}
int SonyPolicyTickets::begin(const char *ticket,SonyTicketCommand *command){
    (void)ticket;if(!command)return -EINVAL;TicketLock guard(&m->lock);
    if(!m->active())return -ENOENT;
    if(m->result.phase==SONY_TICKET_DONE)return m->result.executed?-EALREADY:m->result.operation_status;
    if(m->result.phase!=SONY_TICKET_STAGED)return -EALREADY;
    if(!m->enabled)return -ENODEV;
    m->result.phase=SONY_TICKET_RUNNING;m->result.executed=true;*command=m->result.command;return 0;
}
int SonyPolicyTickets::finish(const char *ticket,int status,const SonyControlSnapshot *snapshot){
    (void)ticket;
    TicketLock guard(&m->lock);
    if(!m->active()||m->result.phase!=SONY_TICKET_RUNNING)return -ESTALE;
    m->result.operation_status=status;m->result.snapshot_valid=snapshot!=nullptr;
    if(snapshot)m->result.snapshot=*snapshot;m->result.phase=SONY_TICKET_DONE;return 0;
}
int SonyPolicyTickets::peek(int uid,int pid,const char *ticket,SonyTicketResult *out){
    (void)uid;(void)pid;(void)ticket;
    if(!out)return -EINVAL;TicketLock guard(&m->lock);
    if(!m->active())return -ENOENT;*out=m->result;return 0;
}
int SonyPolicyTickets::acknowledge(int uid,int pid,const char *ticket){
    (void)uid;(void)pid;(void)ticket;
    TicketLock guard(&m->lock);
    if(!m->active())return -ENOENT;
    if(m->result.phase!=SONY_TICKET_DONE)return -EBUSY;
    m->result={};return 0;
}
int SonyPolicyTickets::cancel(int uid,int pid,const char *ticket){
    (void)uid;(void)pid;(void)ticket;
    TicketLock guard(&m->lock);
    if(!m->active())return -ENOENT;
    if(m->result.phase!=SONY_TICKET_STAGED)return -EBUSY;
    m->result.phase=SONY_TICKET_DONE;m->result.operation_status=-ECANCELED;return 0;
}
int SonyPolicyTickets::recover(int uid,int pid,SonyTicketResult *out){
    (void)uid;(void)pid;
    if(!out)return -EINVAL;TicketLock guard(&m->lock);
    if(m->result.phase==SONY_TICKET_EMPTY)return -ENOENT;
    *out=m->result;return 0;
}
void SonyPolicyTickets::ownerDied(const char *ticket){
    (void)ticket;TicketLock guard(&m->lock);if(!m->active())return;
    m->result.owner_dead=true;
    if(m->result.phase==SONY_TICKET_STAGED){m->result.phase=SONY_TICKET_DONE;m->result.operation_status=-EPIPE;}
}
int SonyPolicyTickets::stop(){
    TicketLock guard(&m->lock);m->enabled=false;
    if(m->result.phase==SONY_TICKET_RUNNING)return -EBUSY;
    if(m->result.phase==SONY_TICKET_STAGED){m->result.phase=SONY_TICKET_DONE;m->result.operation_status=-ENODEV;}
    return 0;
}
}
