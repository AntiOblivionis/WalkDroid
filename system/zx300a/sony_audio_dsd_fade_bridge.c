#define _GNU_SOURCE
#include "sony_audio_dsd_fade_bridge.h"
#include "sony_audio_dsd_controls.h"
#include <dlfcn.h>
#include <link.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

extern unsigned int sony_audio_native_call4(void *, unsigned int, unsigned int,
                                           unsigned int, unsigned int);
#define PTR(p) ((unsigned int)(uintptr_t)(p))
#define N4(b,off,a,c,d,e) sony_audio_native_call4((void *)((b)->base+(off)+1),PTR(a),PTR(c),PTR(d),PTR(e))

struct sony_dsd_fade_bridge {
    uintptr_t base;
    unsigned long construct,initialize,configure,process,destroy;
    void *component;
    int faulted;
    unsigned char input[32768 + 32], output[32768 + 32];
};

static unsigned long offset(const char *name, unsigned long fallback) {
    const char *value=getenv(name);
    return value ? strtoul(value,NULL,0) : fallback;
}

int sony_dsd_fade_close(struct sony_dsd_fade_bridge *b) {
    if (!b) return 0;
    if (b->component) N4(b,b->destroy,b->component,0,0,0);
    memset(b, 0, sizeof(*b)); free(b); return 0;
}

int sony_dsd_fade_open(struct sony_dsd_fade_bridge **out, void *library, unsigned int bitrate) {
    struct link_map *map = NULL;
    if (!out || !library) return -EINVAL;
    *out = NULL;
    if (dlinfo(library, RTLD_DI_LINKMAP, &map) || !map) return -ELIBBAD;
    uintptr_t base = (uintptr_t)map->l_addr;
    struct sony_dsd_fade_bridge *b = calloc(1, sizeof(*b));
    if (!b) return -ENOMEM;
    b->base = base;
    b->construct=offset("WALKDROID_DSD_FADE_CONSTRUCT",0x8ded8);
    b->initialize=offset("WALKDROID_DSD_FADE_INITIALIZE",0x8e3a8);
    b->configure=offset("WALKDROID_DSD_FADE_CONFIGURE",0x8df78);
    b->process=offset("WALKDROID_DSD_FADE_PROCESS",0x8e460);
    b->destroy=offset("WALKDROID_DSD_FADE_DESTROY",0x8e348);
    unsigned int param[23] = {92,0x20101,0,2,1,bitrate,1};
    if (N4(b,b->construct,0,&b->component,0,0) || !b->component ||
        N4(b,b->initialize,b->component,0,0,0) ||
        N4(b,b->configure,b->component,0,0x7f010007,param)) {
        sony_dsd_fade_close(b); return -EIO;
    }
    *out = b; return 0;
}

int sony_dsd_fade_process(struct sony_dsd_fade_bridge *b, const unsigned char *input,
    unsigned int bytes, unsigned int flags, unsigned char *output,
    unsigned int capacity, unsigned int *written) {
    if (!b || !b->component || !input || !output || !written ||
        !bytes || bytes > 32768) return -EINVAL;
    if (b->faulted) return -EIO;
    memcpy(b->input, input, bytes);
    unsigned int ih[20] = {0}, oh[20] = {0};
    ih[2] = PTR(b->input); ih[3] = bytes; ih[4] = bytes; ih[16] = flags << 12;
    oh[2] = PTR(b->output); oh[3] = 32768;
    void *inport = ih, *outport = oh;
    void *ports[2] = {&inport,&outport};
    unsigned int rc = N4(b,b->process,b->component,0,ports,0);
    if (rc) {b->faulted = 1; return -EIO;}
    unsigned int actual=oh[4];
    if (actual>oh[3])actual=oh[3];
    if (actual>capacity)return -ENOSPC;
    memcpy(output,b->output,actual); *written = actual; return 0;
}
