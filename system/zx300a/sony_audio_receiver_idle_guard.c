#include "sony_audio_receiver_idle_guard.h"
#include <errno.h>
#include <stdlib.h>
struct sony_receiver_idle_guard {struct sony_receiver_idle_guard_io io;struct sony_receiver_idle_guard_state s;uint64_t receipt;int busy;};
static int lock(struct sony_receiver_idle_guard *g){return !g?-EINVAL:__atomic_exchange_n(&g->busy,1,__ATOMIC_ACQUIRE)?-EBUSY:0;}
static int unlock(struct sony_receiver_idle_guard *g,int rc){__atomic_store_n(&g->busy,0,__ATOMIC_RELEASE);return rc;}
static int remember(struct sony_receiver_idle_guard *g,int rc){if(rc>0)rc=-EPROTO;if(rc&&!g->s.first_error)g->s.first_error=rc;return rc;}
static int check(struct sony_receiver_idle_guard *g){return g?0:-EINVAL;}
static int inspect(struct sony_receiver_idle_guard *g,struct sony_receiver_idle_observation *o){return remember(g,g->io.inspect(g->io.context,o));}
static int write_timer(struct sony_receiver_idle_guard *g,unsigned timer){
    return remember(g,g->io.write_timer(g->io.context,timer));
}
int sony_receiver_idle_guard_create(struct sony_receiver_idle_guard **out,const struct sony_receiver_idle_guard_io *io,uint64_t receipt){
    if(!out||*out||!io||!io->inspect||!io->write_timer)return -EINVAL;
    struct sony_receiver_idle_guard *g=calloc(1,sizeof(*g));if(!g)return -ENOMEM;g->io=*io;g->receipt=receipt?receipt:1;*out=g;return 0;
}
int sony_receiver_idle_guard_acquire(struct sony_receiver_idle_guard *g){
    int rc=lock(g);if(rc)return rc;
    if(!g->s.used){struct sony_receiver_idle_observation o;rc=inspect(g,&o);if(rc)return unlock(g,rc);g->s.previous_timer=o.timer;g->s.used=1;}
    g->s.token=g->receipt;g->s.write_attempted=1;rc=write_timer(g,SONY_RECEIVER_IDLE_GUARD_TIMER);
    if(!rc)rc=check(g);if(!rc)g->s.acquired=1;return unlock(g,rc);
}
int sony_receiver_idle_guard_assure(struct sony_receiver_idle_guard *g,unsigned raw,unsigned operation){
    (void)raw;(void)operation;return g?0:-EINVAL;
}
int sony_receiver_idle_guard_release(struct sony_receiver_idle_guard *g){
    int rc=lock(g);if(rc)return rc;if(!g->s.token)return unlock(g,0);
    g->s.release_attempted=1;
    if(g->s.write_attempted){g->s.restore_attempted=1;rc=write_timer(g,g->s.previous_timer);}
    if(!rc)rc=check(g);if(!rc){g->s.restored=1;g->s.token=0;}return unlock(g,rc);
}
int sony_receiver_idle_guard_state(struct sony_receiver_idle_guard *g,struct sony_receiver_idle_guard_state *out){if(!out)return -EINVAL;int rc=lock(g);if(rc)return rc;*out=g->s;return unlock(g,0);}
int sony_receiver_idle_guard_destroy(struct sony_receiver_idle_guard **owner){if(!owner||!*owner)return -EINVAL;struct sony_receiver_idle_guard *g=*owner;int rc=sony_receiver_idle_guard_release(g);if(rc)return rc;rc=lock(g);if(rc)return rc;free(g);*owner=NULL;return 0;}
int sony_receiver_mode_policy_assure(void *v,unsigned operation){
    (void)operation;return v?0:-EINVAL;
}
