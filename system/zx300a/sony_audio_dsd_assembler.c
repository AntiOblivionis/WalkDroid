#include "sony_audio_dsd_assembler.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>

struct sony_dsd_assembler {
    struct sony_dsd_assembly_status state;
    sony_dsd_packet_sink packet;
    sony_dsd_eos_sink eos;
    void *context;
    int busy;
    uint8_t buffer[SONY_DSD_PACKET_BYTES];
};

static int overlaps(const void *a,size_t na,const void *b,size_t nb) {
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    if (na>UINTPTR_MAX-x || nb>UINTPTR_MAX-y) return 1;
    return na && nb && x<y+nb && y<x+na;
}

int sony_dsd_assembler_create(struct sony_dsd_assembler **out,
        sony_dsd_packet_sink packet,sony_dsd_eos_sink eos,void *context) {
    struct sony_dsd_assembler *a;
    if (!out || !packet || !eos) return -EINVAL;
    a=calloc(1,sizeof(*a));
    if (!a) return -ENOMEM;
    a->packet=packet;a->eos=eos;a->context=context;
    *out=a;return 0;
}

static int deliver(struct sony_dsd_assembler *a,size_t source,int terminal) {
    size_t padding=SONY_DSD_PACKET_BYTES-source;
    if (a->state.packet_bytes_acknowledged>UINT64_MAX-SONY_DSD_PACKET_BYTES ||
        a->state.padding_bytes_acknowledged>UINT64_MAX-padding) {
        a->state.faulted=1;return -EOVERFLOW;
    }
    if (a->packet(a->context,a->buffer,source,terminal)) {
        a->state.faulted=1;return -EIO;
    }
    a->state.packet_bytes_acknowledged+=SONY_DSD_PACKET_BYTES;
    a->state.padding_bytes_acknowledged+=padding;
    a->state.buffered_bytes=0;
    return 0;
}

int sony_dsd_assembler_write(struct sony_dsd_assembler *a,const uint8_t *data,size_t bytes) {
    size_t remaining=bytes;int rc=0;
    if (!a || (!data && bytes) || bytes>65536 || (bytes&1)) return -EINVAL;
    if (a->busy) return -EBUSY;
    if (a->state.faulted || a->state.finished) return -EPIPE;
    if (overlaps(data,bytes,a,sizeof(*a))) return -EINVAL;
    if (a->state.source_admitted>UINT64_MAX-bytes) return -EOVERFLOW;
    a->busy=1;
    while (remaining) {
        size_t n=SONY_DSD_PACKET_BYTES-a->state.buffered_bytes;
        if (n>remaining) n=remaining;
        memcpy(a->buffer+a->state.buffered_bytes,data,n);
        a->state.buffered_bytes+=n;a->state.source_admitted+=n;
        data+=n;remaining-=n;
        if (a->state.buffered_bytes==SONY_DSD_PACKET_BYTES &&
            (rc=deliver(a,SONY_DSD_PACKET_BYTES,0))) break;
    }
    a->busy=0;return rc;
}

int sony_dsd_assembler_finish(struct sony_dsd_assembler *a) {
    int rc;size_t source;
    if (!a) return -EINVAL;
    if (a->busy) return -EBUSY;
    if (a->state.faulted) return -EPIPE;
    if (a->state.finished) return 1;
    a->busy=1;source=a->state.buffered_bytes;
    memset(a->buffer+source,0x69,SONY_DSD_PACKET_BYTES-source);
    rc=deliver(a,source,1);
    if (!rc && a->eos(a->context)) {a->state.faulted=1;rc=-EIO;}
    if (!rc) a->state.finished=1;
    a->busy=0;return rc;
}

int sony_dsd_assembler_status(const struct sony_dsd_assembler *a,struct sony_dsd_assembly_status *out) {
    if (!a || !out || overlaps(out,sizeof(*out),a,sizeof(*a))) return -EINVAL;
    if (a->busy) return -EBUSY;
    *out=a->state;return 0;
}

int sony_dsd_assembler_abort_free(struct sony_dsd_assembler *a) {
    if (a && a->busy) return -EBUSY;
    free(a);return 0;
}
