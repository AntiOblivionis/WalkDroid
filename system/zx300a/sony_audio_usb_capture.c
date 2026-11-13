#define _GNU_SOURCE
#include "sony_audio_usb_capture.h"
#include <tinyalsa/asoundlib.h>
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sound/asound.h>
#include <unistd.h>
#define STATUS "/proc/asound/card4/pcm0c/sub0/status"
#define PARAMS "/proc/asound/card4/pcm0c/sub0/hw_params"
struct sony_usb_capture {struct sony_usb_capture_scope scope;struct sony_usb_capture_state state;struct sony_usb_capture_cleanup cleanup;struct pcm *pcm;int fd,busy,closing;
    int (*boundary_snapshot)(void *,struct sony_usb_boundary *);struct sony_usb_capture_boundary_info boundary;};
static int lock(struct sony_usb_capture *p){return !p?-EINVAL:__atomic_exchange_n(&p->busy,1,__ATOMIC_ACQUIRE)?-EBUSY:0;}
static int unlock(struct sony_usb_capture *p,int rc){__atomic_store_n(&p->busy,0,__ATOMIC_RELEASE);return rc;}
static int status(int rc){return rc>0?-EPROTO:rc;}
static int fail(struct sony_usb_capture *p,int rc){rc=status(rc);p->state.sealed=1;if(rc&&!p->state.first_error)p->state.first_error=rc;return rc;}
static int fd_check(struct sony_usb_capture *p,int nonblock){
    if(p->fd<0)return -EBADF;
    int flags=fcntl(p->fd,F_GETFL);if(flags<0)return -errno;
    return nonblock&&!(flags&O_NONBLOCK)&&fcntl(p->fd,F_SETFL,flags|O_NONBLOCK)?-errno:0;
}
static int check(struct sony_usb_capture *p,enum sony_usb_capture_stage stage){
    (void)stage;if(p->state.sealed||p->closing)return p->state.first_error?p->state.first_error:-EPIPE;
    return p->fd>=0?fd_check(p,1):0;
}
static int producer_snapshot(struct sony_usb_capture *p,struct sony_usb_boundary *out){
    struct sony_usb_boundary s;int rc=status(p->boundary_snapshot(p->scope.context,&s));
    if(!rc){p->boundary.last=s;*out=s;}return rc;
}
static int metadata(struct sony_usb_capture *p,const char *expected_state){
    (void)expected_state;unsigned frames=pcm_get_buffer_size(p->pcm);
    p->state.period_frames=p->state.event.stream.rate/100;
    p->state.buffer_frames=frames?frames:p->state.period_frames*50;
    p->state.prefill_frames=(p->state.buffer_frames+2)/5;return 0;
}
static int begin_pcm(struct sony_usb_capture **out,unsigned activation,const struct sony_usb_event *event,const struct sony_usb_capture_scope *scope,int (*producer)(void *,struct sony_usb_boundary *)){
    (void)activation;if(!out||*out||!event||!scope)return -EINVAL;
    struct sony_usb_capture_plan plan;int rc=sony_usb_stream_capture_plan(&event->stream,&plan);if(rc)return rc;
    struct sony_usb_capture *p=calloc(1,sizeof(*p));if(!p)return -ENOMEM;p->scope=*scope;p->boundary_snapshot=producer;p->state.event=*event;p->fd=-1;p->busy=1;p->cleanup.global_mode_cleanup_unverified=1;*out=p;
    struct pcm_config config={0};config.channels=2;config.rate=plan.alsa_rate;config.format=PCM_FORMAT_S32_LE;config.period_size=plan.alsa_rate/100;config.period_count=50;
    p->state.open_attempted=1;p->pcm=pcm_open(4,0,PCM_IN|PCM_MONOTONIC,&config);if(!p->pcm)rc=-ENOMEM;
    if(p->pcm)p->fd=pcm_get_poll_fd(p->pcm);
    if(!rc){int flags=fcntl(p->fd,F_GETFL);if(flags<0||fcntl(p->fd,F_SETFL,flags|O_NONBLOCK))rc=-errno;}
    if(!rc)rc=check(p,SONY_USB_CAPTURE_OPEN);if(!rc)rc=metadata(p,"SETUP");if(!rc)rc=check(p,SONY_USB_CAPTURE_OPEN);
    if(!rc){p->state.prepare_attempted=1;rc=pcm_prepare(p->pcm);if(rc)rc=rc<0?rc:-EIO;else p->state.prepared=1;}
    if(!rc)rc=metadata(p,"PREPARED");if(!rc)rc=check(p,SONY_USB_CAPTURE_OPEN);
    if(!rc){p->state.start_attempted=1;rc=pcm_start(p->pcm);if(rc)rc=rc<0?rc:-EIO;else p->state.started=1;}
    if(!rc)rc=check(p,SONY_USB_CAPTURE_PREFILL);
    if(!rc&&producer){struct sony_usb_boundary s;rc=producer_snapshot(p,&s);if(!rc)rc=sony_usb_boundary_reader_start(&p->boundary.reader,&s,event->sequence,event->stream.rate,event->stream.bit_width,p->state.buffer_frames);if(!rc)p->boundary.ready=1;}
    return unlock(p,rc?fail(p,rc):0);
}
int sony_usb_capture_begin_pcm(struct sony_usb_capture **out,unsigned activation,const struct sony_usb_event *event,const struct sony_usb_capture_scope *scope){return begin_pcm(out,activation,event,scope,NULL);}
int sony_usb_capture_begin_boundary_pcm(struct sony_usb_capture **out,unsigned activation,const struct sony_usb_event *event,const struct sony_usb_capture_boundary_scope *scope){if(!scope||!scope->snapshot)return -EINVAL;return begin_pcm(out,activation,event,&scope->admission,scope->snapshot);}
int sony_usb_capture_boundary_info(struct sony_usb_capture *p,struct sony_usb_capture_boundary_info *out){if(!out)return -EINVAL;int rc=lock(p);if(rc)return rc;if(!p->boundary_snapshot)return unlock(p,-ENOTSUP);*out=p->boundary;return unlock(p,0);}
int sony_usb_capture_poll_fd(struct sony_usb_capture *p){int rc=lock(p);if(rc)return rc;rc=check(p,p->state.prefilled?SONY_USB_CAPTURE_READ:SONY_USB_CAPTURE_PREFILL);if(rc)fail(p,rc);return unlock(p,rc?rc:p->fd);}
int sony_usb_capture_prefill(struct sony_usb_capture *p,unsigned *available){
    if(!available)return -EINVAL;int rc=lock(p);if(rc)return rc;rc=check(p,SONY_USB_CAPTURE_PREFILL);if(!rc&&!p->state.started)rc=-EPIPE;
    struct snd_pcm_status s={0};if(!rc&&ioctl(p->fd,SNDRV_PCM_IOCTL_STATUS,&s))rc=-errno;
    if(!rc&&(s.state!=SNDRV_PCM_STATE_RUNNING||s.avail>p->state.buffer_frames))rc=s.state==SNDRV_PCM_STATE_XRUN?-EPIPE:-EPROTO;
    if(!rc){p->state.last_available=(unsigned)s.avail;if(s.avail>p->state.max_available)p->state.max_available=(unsigned)s.avail;rc=check(p,SONY_USB_CAPTURE_PREFILL);}
    int finalized=0;
    if(!rc&&p->boundary_snapshot){struct sony_usb_boundary b;unsigned limit=0;rc=producer_snapshot(p,&b);if(!rc){int q=sony_usb_boundary_reader_limit(&p->boundary.reader,&b,1,&limit);if(p->boundary.reader.sealed||(q!=0&&q!=-EAGAIN&&q!=SONY_USB_CAPTURE_END))rc=q;else finalized=!!(b.flags&SONY_USB_BOUNDARY_FINAL);}}
    if(!rc){*available=(unsigned)s.avail;if(s.avail<p->state.prefill_frames&&!finalized)rc=-EAGAIN;else p->state.prefilled=1;}
    if(rc&&rc!=-EAGAIN&&rc!=-EINTR)fail(p,rc);return unlock(p,rc);
}
static int boundary_read(struct sony_usb_capture *p,void *data,unsigned capacity,unsigned *out_frames){
    int rc=check(p,SONY_USB_CAPTURE_READ);struct sony_usb_boundary before,after;unsigned requested=0;
    if(!rc&&!p->boundary.ready)rc=-EPROTO;
    if(!rc)rc=producer_snapshot(p,&before);
    if(!rc)rc=sony_usb_boundary_reader_limit(&p->boundary.reader,&before,capacity,&requested);
    if(rc==SONY_USB_CAPTURE_END){p->boundary.finished=1;return rc;}
    if((rc==-EAGAIN||rc==-EINTR)&&!p->boundary.reader.sealed){++p->state.transient_reads;return rc;}
    if(!rc&&p->state.captured_frames>UINT64_MAX-requested)rc=-EOVERFLOW;
    struct snd_pcm_status s={0};if(!rc&&ioctl(p->fd,SNDRV_PCM_IOCTL_STATUS,&s))rc=-errno;
    if(!rc&&(s.state!=SNDRV_PCM_STATE_RUNNING||s.avail>p->state.buffer_frames))rc=s.state==SNDRV_PCM_STATE_XRUN?-EPIPE:-EPROTO;
    struct snd_xferi x={0};x.buf=data;x.frames=requested;
    if(!rc){++p->state.read_attempts;if(ioctl(p->fd,SNDRV_PCM_IOCTL_READI_FRAMES,&x))rc=-errno;else if(x.result<0)rc=(int)x.result;else if(!x.result||(unsigned long)x.result>requested)rc=-EPROTO;
        if(!rc)p->state.captured_frames+=(unsigned)x.result;
    }
    if(!rc)rc=check(p,SONY_USB_CAPTURE_READ);
    if(!rc)rc=producer_snapshot(p,&after);
    if(!rc)rc=sony_usb_boundary_reader_commit(&p->boundary.reader,&before,&after,requested,(unsigned)x.result);
    if(!rc){p->state.returned_frames+=(unsigned)x.result;*out_frames=(unsigned)x.result;return 0;}
    if(x.result>0&&(unsigned long)x.result<=requested)return fail(p,rc);
    if((rc==-EAGAIN||rc==-EINTR)&&!p->boundary.reader.sealed){++p->state.transient_reads;return rc;}
    return fail(p,rc);
}
static int running_available(struct sony_usb_capture *p,unsigned consumed){
    struct snd_pcm_status s={0};if(ioctl(p->fd,SNDRV_PCM_IOCTL_STATUS,&s))return -errno;
    if(s.avail>p->state.max_available)p->state.max_available=(unsigned)s.avail;
    p->state.last_available=(unsigned)s.avail;
    /* This Sony UAC driver can keep RUNNING while overwriting its ring. It
     * does not reliably raise XRUN. Bracket every READ, not only prefill. */
    /* Post-READ avail already excludes the consumed frames. Account for them
     * to reject an overwrite during the copy, even when the later avail is
     * back below the ring size. Never publish a potentially replaced block. */
    if(s.state==SNDRV_PCM_STATE_XRUN||consumed>p->state.buffer_frames||s.avail>p->state.buffer_frames-consumed)return -EPIPE;
    return s.state==SNDRV_PCM_STATE_RUNNING?0:-EPROTO;
}
int sony_usb_capture_read(struct sony_usb_capture *p,void *data,unsigned capacity,unsigned *out_frames){
    if(!data||!out_frames||!capacity)return -EINVAL;int rc=lock(p);if(rc)return rc;
    if(p->state.sealed||p->closing)return unlock(p,p->state.first_error?p->state.first_error:-EPIPE);if(!p->state.prefilled)return unlock(p,-EAGAIN);if(p->boundary_snapshot)return unlock(p,boundary_read(p,data,capacity,out_frames));rc=check(p,SONY_USB_CAPTURE_READ);if(!rc&&p->state.captured_frames>UINT64_MAX-capacity)rc=-EOVERFLOW;
    if(!rc)rc=running_available(p,0);struct snd_xferi x={0};x.buf=data;x.frames=capacity;
    if(!rc){++p->state.read_attempts;if(ioctl(p->fd,SNDRV_PCM_IOCTL_READI_FRAMES,&x))rc=-errno;else if(x.result<0)rc=(int)x.result;else if(!x.result||(unsigned long)x.result>capacity)rc=-EPROTO;
        if(!rc)p->state.captured_frames+=(unsigned)x.result;
    }
    if(!rc)rc=running_available(p,(unsigned)x.result);if(!rc)rc=check(p,SONY_USB_CAPTURE_READ);if(!rc){p->state.returned_frames+=(unsigned)x.result;*out_frames=(unsigned)x.result;}
    /* A post-READ admission failure may carry EINTR/EAGAIN too. Samples have
     * already left ALSA: continuing would hide a source gap. Seal even these
     * statuses once positive data was consumed; only no-data calls may retry. */
    if(rc&&x.result>0&&(unsigned long)x.result<=capacity)fail(p,rc);
    else if(rc==-EAGAIN||rc==-EINTR)++p->state.transient_reads;else if(rc)fail(p,rc);return unlock(p,rc);
}
int sony_usb_capture_state(struct sony_usb_capture *p,struct sony_usb_capture_state *out){if(!out)return -EINVAL;int rc=lock(p);if(rc)return rc;*out=p->state;return unlock(p,0);}
int sony_usb_capture_close(struct sony_usb_capture **owner,struct sony_usb_capture_cleanup *out){
    if(!owner||!*owner||!out)return -EINVAL;struct sony_usb_capture *p=*owner;int rc=lock(p);if(rc)return rc;p->closing=1;
    if(p->pcm){
        if(p->state.start_attempted){p->cleanup.stop_attempted=1;p->cleanup.stop_status=pcm_stop(p->pcm);}
        p->cleanup.close_attempted=1;p->cleanup.close_status=pcm_close(p->pcm);p->pcm=NULL;
    }
    p->cleanup.capture_closed_verified=0;
    p->cleanup.state=p->state;*out=p->cleanup;rc=p->cleanup.close_status?p->cleanup.close_status:p->cleanup.stop_status;free(p);*owner=NULL;return rc;
}
