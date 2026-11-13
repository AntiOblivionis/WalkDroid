/* Experimental healthd-only filter. Never install in a general uevent daemon.
 * The only dropped event is the Sony USB suspend switch's exact change header.
 * All other traffic, including every normal power_supply event, is accepted.
 */
#include <errno.h>
#include <linux/filter.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>

#ifndef ZX9_FILTER_HOST_TEST
#include <stdbool.h>
#include <dlfcn.h>
#include <android/log.h>
#endif

static const unsigned char ignored_header[] =
    "change@/devices/virtual/switch/usb0_suspend_state";

int zx9_healthd_attach_filter(int fd) {
    /* sizeof includes NUL: do not drop names merely sharing this prefix. */
    enum { N = sizeof(ignored_header), COUNT = 2 + 2 * N + 2 };
    struct sock_filter code[COUNT];
    unsigned int i;
    code[0] = (struct sock_filter)BPF_STMT(BPF_LD | BPF_W | BPF_LEN, 0);
    code[1] = (struct sock_filter)BPF_JUMP(BPF_JMP | BPF_JGE | BPF_K,
                                        N, 0, 2 * N + 1);
    for (i = 0; i < N; ++i) {
        code[2 + 2 * i] = (struct sock_filter)BPF_STMT(BPF_LD | BPF_B | BPF_ABS, i);
        code[3 + 2 * i] = (struct sock_filter)BPF_JUMP(
            BPF_JMP | BPF_JEQ | BPF_K, ignored_header[i], 0,
            2 * (N - i - 1) + 1);
    }
    code[2 + 2 * N] = (struct sock_filter)BPF_STMT(BPF_RET | BPF_K, 0);
    code[3 + 2 * N] = (struct sock_filter)BPF_STMT(BPF_RET | BPF_K, UINT32_MAX);
    struct sock_fprog program = { .len = COUNT, .filter = code };
    return setsockopt(fd, SOL_SOCKET, SO_ATTACH_FILTER, &program, sizeof(program));
}

#ifndef ZX9_FILTER_HOST_TEST
/* Scoped LD_PRELOAD for the healthd service only. The original libcutils
 * function still creates/binds the socket and sets credential handling. */
int uevent_open_socket(int buf_sz, bool passcred) {
    typedef int (*open_fn)(int, bool);
    open_fn original = (open_fn)dlsym(RTLD_NEXT, "uevent_open_socket");
    if (!original) {
        __android_log_print(ANDROID_LOG_ERROR, "WalkDroidSleep",
                            "original uevent_open_socket unavailable");
        errno = ENOSYS;
        return -1;
    }
    int fd = original(buf_sz, passcred);
    if (fd < 0) return fd;
    int original_errno = errno;
    int rc = zx9_healthd_attach_filter(fd);
    int filter_errno = errno;
    __android_log_print(rc == 0 ? ANDROID_LOG_INFO : ANDROID_LOG_WARN,
                        "WalkDroidSleep", "healthd USB suspend filter fd=%d rc=%d errno=%d",
                        fd, rc, rc == 0 ? 0 : filter_errno);
    /* Unsupported filtering leaves the original socket usable, with its
     * original wake protection. Never drop battery reporting on attach failure. */
    errno = original_errno;
    return fd;
}
#endif
