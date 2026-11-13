#ifndef SONY_AUDIO_BT_COMMON_ABI_H
#define SONY_AUDIO_BT_COMMON_ABI_H
#include <stdint.h>
/* Original ARM32/glibc libBtMw Common interface. The exact 92-byte table is
 * at libBtCompIf.so:0x13004. Enum values stay raw; no Android state mapping.
 * Pointers are borrowed, including link keys: never print or retain key data.
 * The owner of this table must outlive every native callback. CommonInit is
 * separate from RF enable and must run in the pinned private radio root.
 */
struct sony_bt_common_callbacks {
    uint32_t bytes;
    void (*status)(unsigned);
    void (*searched)(void *,unsigned,unsigned char,unsigned char *);
    void (*passkey)(void *,unsigned,unsigned char,unsigned char *);
    void (*numeric_comparison)(void *,unsigned,unsigned char,unsigned char *,unsigned);
    void (*pairing_complete)(unsigned,void *,void *,unsigned,unsigned char,unsigned char *);
    void (*support_iap)(void *,_Bool);
    void (*paired_device)(void *,void *,unsigned,unsigned char,unsigned char *,unsigned);
    void (*paired_complete)(void);
    void (*deleted_linkkey)(void *);
    void (*deleted_complete)(void);
    void (*my_device)(void *,unsigned,unsigned char,unsigned char *,unsigned char,unsigned char *,unsigned,void *);
    void (*error)(unsigned char);
    void (*acl_disconnected)(void *,_Bool);
    void (*factory_reset)(unsigned);
    void (*coex_ratio)(unsigned short,unsigned short);
    void (*remote_service)(void *,unsigned char,void *);
    void (*device_id)(void *,unsigned short,unsigned short,unsigned short);
    void (*rssi)(int);
    void (*switch_device)(void);
    void (*acl_state)(void *,unsigned);
    void (*ssp_request)(void *,unsigned char,unsigned char *,unsigned,unsigned,unsigned);
    void (*service_classes)(void *,unsigned char,unsigned short *);
};
#if UINTPTR_MAX == UINT32_MAX
#endif
#endif
