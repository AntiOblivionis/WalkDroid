#ifndef SONY_AUDIO_BT_MIDDLEWARE_ABI_H
#define SONY_AUDIO_BT_MIDDLEWARE_ABI_H
#include <stdint.h>
/* Original libBtMw.so btmw_av_snk callback ABI, ARM32 glibc, not Bionic.
 * The controller coordinates use with Android HCI and supplies the native
 * runtime; this interface does not check its identity or bytes.
 * libBtMw stores this pointer: table and callback code live until callbacks
 * are independently quiescent. The native callback supplies borrowed bytes;
 * copy/admit them before returning, never retain the pointer for later use.
 * Bytes is uint16_t, not a frame count. Decode format comes from sound_status.
 * Unknown state values remain raw; this header supplies no guessed state map.
 */
struct sony_bt_snk_callbacks {
    uint32_t bytes;
    void (*connection_status)(unsigned status);
    void (*connection_information)(unsigned char *bdaddr,unsigned char name_bytes,unsigned char *name);
    void (*sound_status)(unsigned track_status,unsigned codec,unsigned rate,unsigned channel,_Bool native_flag);
    void (*pcm)(unsigned short bytes,unsigned char *data);
    void (*drop_buffer)(void);
    void (*multi_status)(unsigned status);
    void (*bitrate)(unsigned bitrate);
    void (*audio_state)(unsigned state);
    /* Original export is misleadingly named NotifyLdacAppStateCallback.
     * Its application endpoint is GetPlayerState: it receives an OUT pointer,
     * and bit0 means the downstream player is ready to decode. This is not a
     * state notification by value. Leave no uninitialized caller output. */
    void (*get_player_state)(unsigned *state);
};
#if UINTPTR_MAX == UINT32_MAX
#endif
#endif
