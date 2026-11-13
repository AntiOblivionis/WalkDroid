#define _GNU_SOURCE
#include "sony_audio_root_verify.h"
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#ifndef SONY_AUDIO_ROOT_VERIFY_ONLY
int sony_audio_root_open(void) {
    int fd = open("/vendor/sony-audio/rootfs", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    return fd < 0 ? -errno : fd;
}
#endif
