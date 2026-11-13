#ifndef WALKDROID_SONY_BT_TX_ABI_H
#define WALKDROID_SONY_BT_TX_ABI_H
#include <stdint.h>
/* Original libBtMw/libBtCompIf ARM32 glibc interfaces. Callback
 * values remain raw until separately mapped. Init registers borrowed tables;
 * callers must keep them alive until the original graph is quiescent.
 * Neither these declarations nor a zero SendData result authorize RF or prove
 * data consumption: the original OFF/not-streaming path returns zero too. */
struct sony_bt_tx_callbacks {
 uint32_t bytes;
 void (*connection_status)(unsigned raw);
 void (*connection_information)(unsigned char *address,unsigned char name_bytes,unsigned char *name);
 void (*sound_status)(unsigned codec,unsigned rate,unsigned channel,_Bool native_flag);
 void (*sep_information)(unsigned codec,unsigned short vendor_codec,unsigned char detail);
 void (*pcm_preferred_size)(unsigned short bytes);
};
struct sony_bt_tx_avrcp_callbacks {
 uint32_t bytes;
 void (*connection_status)(unsigned raw);
 void (*receive_command)(unsigned button,_Bool pressed);
 void (*get_play_status)(void);
 void (*get_media_attribute)(unsigned char count,unsigned *attributes);
 void (*current_volume)(unsigned char raw);
 void (*remote_version)(unsigned short first,unsigned short second);
};
#if UINTPTR_MAX == UINT32_MAX
#endif
#endif
