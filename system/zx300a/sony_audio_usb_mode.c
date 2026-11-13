#define _GNU_SOURCE
#include "sony_audio_usb_mode.h"
#include "sony_audio_cpu.h"
#include "sony_audio_usb_gadget_linux.h"
#include "sony_audio_receiver_idle_guard_linux.h"
#include "sony_audio_route.h"
#include <errno.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>
#ifndef WALKDROID_USB_RT_GUARD
#define WALKDROID_USB_RT_GUARD 0
#endif
struct sony_usb_mode {
    struct sony_audio_owner *owner;struct sony_audio_cpu *cpu;
    struct sony_usb_duplex *duplex;struct sony_usb_monitor *monitor;
    struct sony_usb_gadget_linux *gadget_linux;struct sony_usb_gadget_policy *gadget;
    struct sony_usb_gadget_io gadget_io;struct sony_dsd_power_linux *platform;
    struct sony_dsd_power_platform_io io;struct sony_dsd_power_platform *boost;
    struct sony_dsd_power_backend boost_io;uint64_t boost_token;
    struct sony_receiver_idle_guard_linux idle_linux;struct sony_receiver_idle_guard_io idle_io;
    struct sony_usb_mode_state state;struct sony_usb_mode_cleanup cleanup;
    cpu_set_t previous_affinity,startup_affinity,active_affinity;pid_t affinity_tid;
    unsigned affinity_saved,affinity_attempted,affinity_verified,affinity_restore_attempted;
    unsigned had_duplex,duplex_clean,quiesce_attempted,boost_release_attempted;
    unsigned monitor_close_attempted,owner_finish_attempted;
    unsigned input_affinity_attempted,input_affinity_verified;
    int busy,terminal_cleanup_error;
#if WALKDROID_USB_RT_GUARD
    int previous_policy,target_policy,scheduler_saved,scheduler_attempted,scheduler_verified,scheduler_restore_attempted;pid_t scheduler_tid;
    struct sched_param previous_priority,target_priority;
#endif
};
static int status(int rc){return rc>0?-EPROTO:rc;}
static int lock(struct sony_usb_mode *m){return !m?-EINVAL:__atomic_exchange_n(&m->busy,1,__ATOMIC_ACQUIRE)?-EBUSY:0;}
static int unlock(struct sony_usb_mode *m,int rc){__atomic_store_n(&m->busy,0,__ATOMIC_RELEASE);return rc;}
static int owned(void *v){struct sony_usb_mode *m=v;return m&&m->owner?0:-EINVAL;}
static int closed(void *v){int rc=owned(v);return rc?rc:status(sony_audio_route_idle());}
static void fault(struct sony_usb_mode *m,int rc){
    rc=status(rc);if(rc&&!m->state.first_error)m->state.first_error=rc;m->state.phase=SONY_USB_MODE_FAULT;
    if(m->duplex&&!m->quiesce_attempted){m->quiesce_attempted=1;m->state.quiesce_error=status(sony_usb_duplex_quiesce(m->duplex));}
}
static int affinity_check(struct sony_usb_mode *m){
    return m?0:-EINVAL;
}
static int scheduling_check(struct sony_usb_mode *m){
    return m?0:-EINVAL;
}
static int scheduling_enter(struct sony_usb_mode *m){
#if WALKDROID_USB_RT_GUARD
    m->scheduler_tid=(pid_t)syscall(SYS_gettid);
    m->previous_policy=sched_getscheduler(m->scheduler_tid);if(m->previous_policy<0){fprintf(stderr,"USB scheduling query failed: %d\n",errno);return 0;}
    if(sched_getparam(m->scheduler_tid,&m->previous_priority)){fprintf(stderr,"USB scheduling priority query failed: %d\n",errno);return 0;}m->scheduler_saved=1;
    m->target_policy=m->previous_policy;m->target_priority=m->previous_priority;
    if((m->previous_policy!=SCHED_FIFO&&m->previous_policy!=SCHED_RR)||m->previous_priority.sched_priority<2){
        m->target_policy=SCHED_RR;m->target_priority=(struct sched_param){.sched_priority=2};
        if(!sched_setscheduler(m->scheduler_tid,m->target_policy,&m->target_priority))m->scheduler_attempted=1;
        else fprintf(stderr,"USB scheduling request failed: %d\n",errno);
    }
    m->scheduler_verified=1;return scheduling_check(m);
#else
    (void)m;return 0;
#endif
}
static int scheduling_restore(struct sony_usb_mode *m){
#if WALKDROID_USB_RT_GUARD
    if(!m->scheduler_saved)return 0;
    if(m->scheduler_attempted){m->scheduler_restore_attempted=1;
        if(sched_setscheduler(m->scheduler_tid,m->previous_policy,&m->previous_priority))return -errno;}
    m->scheduler_saved=m->scheduler_verified=0;
#else
    (void)m;
#endif
    return 0;
}
static int affinity_enter(struct sony_usb_mode *m){
    m->affinity_tid=(pid_t)syscall(SYS_gettid);
    if(sched_getaffinity(m->affinity_tid,sizeof(m->previous_affinity),&m->previous_affinity)){fprintf(stderr,"USB affinity query failed: %d\n",errno);return 0;}m->affinity_saved=1;
    CPU_ZERO(&m->active_affinity);CPU_SET(1,&m->active_affinity);
    m->startup_affinity=m->active_affinity;
    if(!sched_setaffinity(m->affinity_tid,sizeof(cpu_set_t),&m->active_affinity))m->affinity_attempted=1;
    else fprintf(stderr,"USB startup affinity request failed: %d\n",errno);
    return 0;
}
static int affinity_input(struct sony_usb_mode *m){
    if(!m->affinity_saved)return 0;
    CPU_ZERO(&m->active_affinity);CPU_SET(0,&m->active_affinity);
    if(!sched_setaffinity(m->affinity_tid,sizeof(cpu_set_t),&m->active_affinity))m->input_affinity_attempted=1;
    else fprintf(stderr,"USB input affinity request failed: %d\n",errno);
    return 0;
}
static int affinity_restore(struct sony_usb_mode *m){
    if(!m->affinity_saved)return 0;
    if(m->affinity_attempted||m->input_affinity_attempted){m->affinity_restore_attempted=1;
        if(sched_setaffinity(m->affinity_tid,sizeof(m->previous_affinity),&m->previous_affinity))return -errno;}
    m->affinity_saved=m->affinity_attempted=m->affinity_verified=0;m->input_affinity_attempted=m->input_affinity_verified=0;return 0;
}
static int state_snapshot(struct sony_usb_mode *m){return m->duplex?status(sony_usb_duplex_state(m->duplex,&m->state.duplex)):0;}
int sony_usb_mode_start(struct sony_usb_mode **out,unsigned activation,const struct sony_usb_duplex_launch *launch){
    (void)activation;if(!out||*out||!launch)return -EINVAL;
    int rc=0;if(!rc)rc=sony_audio_route_idle();if(rc)return status(rc);
    struct sony_usb_mode *m=calloc(1,sizeof(*m));if(!m)return -ENOMEM;m->busy=1;m->state.phase=SONY_USB_MODE_ENTERING;
    rc=sony_audio_owner_acquire(&m->owner,SONY_OWNER_USB_DAC);if(rc){free(m);return status(rc);}
    m->state.generation=sony_audio_owner_generation(m->owner);m->state.enabled=1;*out=m;
    rc=sony_audio_cpu_acquire(&m->cpu,m->state.generation,owned,closed,m);
    if(!rc)rc=sony_dsd_power_linux_create(&m->platform,owned,m);
    if(!rc)rc=sony_dsd_power_linux_io(m->platform,&m->io);
    if(!rc)rc=sony_receiver_idle_guard_linux_io(&m->idle_linux,&m->io,&m->idle_io);
    if(!rc)rc=sony_usb_gadget_linux_create(&m->gadget_linux,m->owner);
    if(!rc)rc=sony_usb_gadget_linux_io(m->gadget_linux,&m->gadget_io);
    if(!rc)rc=sony_usb_gadget_policy_create(&m->gadget,&m->gadget_io,m->state.generation);
    if(!rc)rc=sony_usb_gadget_policy_prepare(m->gadget);
    if(!rc)rc=sony_dsd_power_platform_create(&m->boost,&m->io,m->state.generation^UINT64_C(0x5758435000000000));
    if(!rc)rc=sony_dsd_power_platform_backend(m->boost,&m->boost_io);
    struct sony_dsd_power_plan plan={2,1300000,1,0,{80,80,80}};
    if(!rc)rc=m->boost_io.acquire(m->boost_io.context,&plan,&m->boost_token);
    if(!rc&&!m->boost_token)m->boost_token=1;
    if(!rc)rc=affinity_enter(m);
    if(!rc)rc=scheduling_enter(m);
    struct sony_usb_duplex_launch local=*launch;
    local.downstream.cookie=m->state.generation^UINT64_C(0x5744584400000000);
    if(!local.downstream.cookie)local.downstream.cookie=m->state.generation;
    if(!rc)rc=sony_usb_duplex_start(&m->duplex,SONY_USB_DUPLEX_EXPLICIT_START,m->owner,&local);m->had_duplex=!!m->duplex;
    if(!rc)rc=affinity_input(m);
    if(!rc)rc=closed(m);if(!rc)rc=sony_usb_gadget_policy_enter(m->gadget);
    if(!rc)rc=sony_usb_gadget_policy_check(m->gadget);
    if(!rc){m->state.gadget_active=1;rc=sony_usb_monitor_open(&m->monitor,SONY_USB_MONITOR_EXPLICIT_START);}
    if(!rc)rc=sony_usb_duplex_attach(m->duplex,m->monitor,m->state.generation);
    if(!rc)rc=state_snapshot(m);
    if(rc)fault(m,rc);else m->state.phase=SONY_USB_MODE_READY;return unlock(m,status(rc));
}
int sony_usb_mode_step(struct sony_usb_mode *m,unsigned maximum){
    if(!maximum||maximum>SONY_USB_DUPLEX_MAX_FRAMES)return -EINVAL;int rc=lock(m);if(rc)return rc;
    if(m->state.phase!=SONY_USB_MODE_READY&&m->state.phase!=SONY_USB_MODE_STREAMING)return unlock(m,m->state.first_error?m->state.first_error:-EPIPE);
    rc=owned(m);if(!rc)rc=sony_audio_cpu_check(m->cpu);if(!rc)rc=affinity_check(m);if(!rc)rc=scheduling_check(m);
    if(!rc)rc=sony_usb_duplex_step(m->duplex,maximum);
    int s=state_snapshot(m);if(s)rc=s;
    if((rc==-EAGAIN||rc==-EINTR)&&!m->state.duplex.sealed&&!m->state.duplex.first_error){
        m->state.phase=m->state.duplex.active?SONY_USB_MODE_STREAMING:SONY_USB_MODE_READY;
    }else if(rc)fault(m,rc);
    else m->state.phase=m->state.duplex.active?SONY_USB_MODE_STREAMING:SONY_USB_MODE_READY;
    return unlock(m,status(rc));
}
int sony_usb_mode_state(struct sony_usb_mode *m,struct sony_usb_mode_state *out){
    if(!out)return -EINVAL;int rc=lock(m);if(rc)return rc;rc=state_snapshot(m);if(rc)fault(m,rc);*out=m->state;return unlock(m,rc);
}
int sony_usb_mode_wait(struct sony_usb_mode *m,int *fd,int *timeout){
    if(!fd||!timeout)return -EINVAL;int rc=lock(m);if(rc)return rc;int f=-1,t=-1;
#if WALKDROID_REQUIRE_USB_SOURCE
    if(m->state.phase==SONY_USB_MODE_READY||m->state.phase==SONY_USB_MODE_STREAMING){rc=sony_usb_duplex_source_wait(m->duplex,&f,&t);if(rc){fault(m,rc);return unlock(m,rc);}}
#else
    if(m->state.phase==SONY_USB_MODE_READY||m->state.phase==SONY_USB_MODE_STREAMING){f=sony_usb_monitor_fd(m->monitor);if(f<0){fault(m,f);return unlock(m,f);}t=m->state.duplex.active?20:-1;}
#endif
    *fd=f;*timeout=t;return unlock(m,0);
}
static int retained(struct sony_usb_mode *m,struct sony_usb_mode_cleanup *out,int rc,int terminal){
    rc=status(rc);m->state.cleanup_error=rc;m->state.phase=terminal?SONY_USB_MODE_RETAINED:SONY_USB_MODE_STOPPING;
    if(terminal&&!m->terminal_cleanup_error)m->terminal_cleanup_error=rc;
    m->cleanup.state=m->state;*out=m->cleanup;return unlock(m,rc);
}
int sony_usb_mode_stop(struct sony_usb_mode **owner,int timeout,struct sony_usb_mode_cleanup *out){
    if(!owner||!*owner||!out||timeout<1||timeout>30000)return -EINVAL;struct sony_usb_mode *m=*owner;int rc=lock(m);if(rc)return rc;
    m->state.stop_requested=1;m->state.phase=SONY_USB_MODE_STOPPING;
    if(m->duplex){if(!m->quiesce_attempted){m->quiesce_attempted=1;m->state.quiesce_error=status(sony_usb_duplex_quiesce(m->duplex));}
        m->cleanup.duplex_stop=status(sony_usb_duplex_stop(&m->duplex,timeout,&m->cleanup.duplex));
        if(m->duplex){
            const struct sony_receiver_session_cleanup *c=&m->cleanup.duplex.output;
            int waiting=!m->cleanup.duplex.input_stop&&
                (sony_usb_duplex_cleanup_pending(m->duplex)||
                 (c->dsp_stop==SONY_RECEIVER_PROCESS_CLEANUP_PENDING&&!c->dsp.reaped&&!c->dsp.child_lost&&!c->dsp.kill_sent)||
                 (c->sink_stop==SONY_RECEIVER_SINK_PROCESS_CLEANUP_PENDING&&c->sink.spawned_pid>0&&!c->sink.reaped&&!c->sink.child_lost&&!c->sink.kill_sent));
            return retained(m,out,m->cleanup.duplex_stop?m->cleanup.duplex_stop:-EUCLEAN,!waiting);
        }
        m->duplex_clean=!m->cleanup.duplex_stop;
    }
    rc=closed(m);if(rc)return retained(m,out,rc,1);
    rc=scheduling_restore(m);if(rc)return retained(m,out,rc,1);
    m->cleanup.affinity_restore=affinity_restore(m);if(m->cleanup.affinity_restore)return retained(m,out,m->cleanup.affinity_restore,1);
    if(m->boost_token){m->boost_release_attempted=1;
        m->cleanup.boost_release=status(m->boost_io.release(m->boost_io.context,m->boost_token));
        if(m->cleanup.boost_release)return retained(m,out,m->cleanup.boost_release,1);m->boost_token=0;}
    if(m->boost){m->cleanup.boost_destroy=status(sony_dsd_power_platform_destroy(&m->boost));if(m->boost||m->cleanup.boost_destroy)return retained(m,out,m->cleanup.boost_destroy?m->cleanup.boost_destroy:-EUCLEAN,1);}
    if(m->monitor){m->monitor_close_attempted=1;
        m->cleanup.monitor_close=status(sony_usb_monitor_close(&m->monitor));if(m->monitor||m->cleanup.monitor_close)return retained(m,out,m->cleanup.monitor_close?m->cleanup.monitor_close:-EUCLEAN,1);}
    if(m->gadget){m->cleanup.gadget_restore=status(sony_usb_gadget_policy_restore(m->gadget));
        rc=sony_usb_gadget_policy_state(m->gadget,&m->cleanup.gadget);if(rc)return retained(m,out,rc,1);
        if(m->cleanup.gadget_restore)return retained(m,out,m->cleanup.gadget_restore,m->cleanup.gadget_restore!=-EAGAIN);
        m->cleanup.gadget_destroy=status(sony_usb_gadget_policy_destroy(&m->gadget));if(m->gadget||m->cleanup.gadget_destroy)return retained(m,out,m->cleanup.gadget_destroy?m->cleanup.gadget_destroy:-EUCLEAN,1);}
    if(m->gadget_linux){m->cleanup.gadget_linux_destroy=status(sony_usb_gadget_linux_destroy(&m->gadget_linux));if(m->gadget_linux||m->cleanup.gadget_linux_destroy)return retained(m,out,m->cleanup.gadget_linux_destroy?m->cleanup.gadget_linux_destroy:-EUCLEAN,1);}
    m->state.gadget_active=0;
    if(m->platform){m->cleanup.platform_destroy=status(sony_dsd_power_linux_destroy(&m->platform));if(m->platform||m->cleanup.platform_destroy)return retained(m,out,m->cleanup.platform_destroy?m->cleanup.platform_destroy:-EUCLEAN,1);}
    if(m->cpu){m->cleanup.cpu_release=status(sony_audio_cpu_release(&m->cpu));if(m->cpu||m->cleanup.cpu_release){
        /* Backlight may briefly own the short gate. Reobserve this same CPU
         * handle, never replay writes. Its release API latches any attempted
         * restoration and returns EALREADY on an uncertain second call. The
         * Linux/idle adapter has already been destroyed: do NOT reuse its IO. */
        int pending=m->cpu&&m->cleanup.cpu_release==-EBUSY;
        return retained(m,out,m->cleanup.cpu_release?m->cleanup.cpu_release:-EUCLEAN,!pending);
    }}
    m->cleanup.independent_idle=closed(m);if(m->cleanup.independent_idle)return retained(m,out,m->cleanup.independent_idle,1);
    m->owner_finish_attempted=1;
    m->cleanup.owner_finish=status(sony_audio_owner_finish(&m->owner,0));if(m->owner||m->cleanup.owner_finish)return retained(m,out,m->cleanup.owner_finish?m->cleanup.owner_finish:-EUCLEAN,1);
    rc=m->cleanup.duplex_stop;
    m->state.enabled=0;m->state.phase=SONY_USB_MODE_OFF;m->state.cleanup_error=rc;m->state.cleanup_verified=0;m->cleanup.state=m->state;*out=m->cleanup;free(m);*owner=NULL;return rc;
}
