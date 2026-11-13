#ifndef SONY_AUDIO_BT_AVRCP_ABI_H
#define SONY_AUDIO_BT_AVRCP_ABI_H
#include <stdint.h>
/* Original44-byte libBtCompIf AVRCP sink table. Raw protocol events only;
 * no mapping between protocol volume and Sony0..120 policy is implied. The
 * middleware accepts the full byte range; an owning policy must validate it.
 * Pointers are borrowed. No service/RF activation belongs in this declaration. */
struct sony_bt_avrcp_snk_callbacks {
    uint32_t bytes;
    void (*connection)(unsigned);
    void (*play_status)(unsigned,unsigned,unsigned);
    void (*volume_down)(void);
    void (*volume_up)(void);
    void (*set_volume)(unsigned char);
    void (*remote_version)(unsigned short);
    void (*track_number)(unsigned short,unsigned char *);
    void (*media)(unsigned,unsigned short,unsigned char *);
    void (*error)(int);
    void (*register_absolute_volume)(void);
};
#if UINTPTR_MAX==UINT32_MAX
#endif
#endif
