#include "sony_audio_dsf_session.h"
#include "sony_audio_dsd_render_bridge.h"
#include <errno.h>
#include <stdlib.h>
#include <stdint.h>
struct sony_dsf_session {
    struct sony_dsf_reader *reader;
    struct sony_dsd_render_bridge *render;
    struct sony_dsd_assembler *assembler;
    struct sony_dsf_info info;
    uint64_t source_accepted;
    unsigned preroll_accepted;
    int started,finished,faulted,busy;
    unsigned char raw[65536],rendered[32768];
};
static int overlaps(const void *a,size_t na,const void *b,size_t nb) {
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    if(na>UINTPTR_MAX-x||nb>UINTPTR_MAX-y)return 1;
    return na&&nb&&x<y+nb&&y<x+na;
}
int sony_dsf_session_abort_free(struct sony_dsf_session *s) {
    if(!s)return 0;
    if(s->busy)return -EBUSY;
    int a=sony_dsd_assembler_abort_free(s->assembler);
    int r=sony_dsd_render_close(s->render);
    int f=sony_dsf_reader_close(s->reader);
    free(s);return a?a:r?r:f;
}
int sony_dsf_session_open(struct sony_dsf_session **out,void *library,uint64_t file_bytes,
                         sony_dsf_read_at read,void *file_context,sony_dsd_packet_sink packet,
                         sony_dsd_eos_sink eos,void *sink_context) {
    if(!out||!library||!read||!packet||!eos)return -EINVAL;
    struct sony_dsf_session *s=calloc(1,sizeof(*s));if(!s)return -ENOMEM;
    int rc=sony_dsf_reader_open(&s->reader,file_bytes,read,file_context);
    if(!rc)rc=sony_dsf_reader_info(s->reader,&s->info);
    if(!rc)rc=sony_dsd_render_open(&s->render,library,s->info.rate);
    if(!rc)rc=sony_dsd_assembler_create(&s->assembler,packet,eos,sink_context);
    if(rc){sony_dsf_session_abort_free(s);return rc;}
    *out=s;return 0;
}
static int preroll(void *context,const unsigned char *data,unsigned bytes) {
    struct sony_dsf_session *s=context;
    return sony_dsd_assembler_write(s->assembler,data,bytes);
}
int sony_dsf_session_step(struct sony_dsf_session *s) {
    if(!s)return -EINVAL;
    if(s->busy)return -EBUSY;
    if(s->faulted)return -EPIPE;
    if(s->finished)return 1;
    s->busy=1;int rc;
    if(!s->started) {
        rc=sony_dsd_render_preroll(s->render,preroll,s,&s->preroll_accepted);
        if(rc)goto fail;
        s->started=1;
    }
    struct sony_dsf_block block;
    rc=sony_dsf_reader_next(s->reader,s->raw,sizeof(s->raw),&block);
    if(rc==1) {
        if(s->source_accepted!=(s->info.sample_bits_per_channel>>2)){rc=-EIO;goto fail;}
        rc=sony_dsd_assembler_finish(s->assembler);if(rc)goto fail;
        s->finished=1;s->busy=0;return 1;
    }
    if(rc)goto fail;
    for(size_t off=0;off<block.bytes;) {
        unsigned count=block.bytes-off>32768?32768:(unsigned)(block.bytes-off),written=0;
        rc=sony_dsd_render_process(s->render,s->raw+off,count,s->rendered,sizeof(s->rendered),&written);
        if(rc||written!=count){rc=-EIO;goto fail;}
        rc=sony_dsd_assembler_write(s->assembler,s->rendered,written);if(rc)goto fail;
        s->source_accepted+=count;off+=count;
    }
    s->busy=0;return 0;
fail:
    s->faulted=1;s->busy=0;return rc<0?rc:-EIO;
}
int sony_dsf_session_info(const struct sony_dsf_session *s,struct sony_dsf_info *out) {
    if(!s||!out||overlaps(s,sizeof(*s),out,sizeof(*out)))return -EINVAL;
    if(s->busy)return -EBUSY;
    *out=s->info;return 0;
}
int sony_dsf_session_status(const struct sony_dsf_session *s,struct sony_dsf_session_status *out) {
    if(!s||!out||overlaps(s,sizeof(*s),out,sizeof(*out)))return -EINVAL;
    if(s->busy)return -EBUSY;
    struct sony_dsf_session_status st={0};
    st.canonical_file_bytes_accepted=s->source_accepted;st.preroll_accepted=s->preroll_accepted;
    st.finished=s->finished;st.faulted=s->faulted;
    int rc=sony_dsd_assembler_status(s->assembler,&st.assembly);if(rc)return rc;
    *out=st;return 0;
}
