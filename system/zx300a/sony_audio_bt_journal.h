#ifndef SONY_AUDIO_BT_JOURNAL_H
#define SONY_AUDIO_BT_JOURNAL_H
#include <stddef.h>
#include <stdint.h>
/* Bounded diagnostic recording of borrowed native callbacks. Append performs
 * no allocation or I/O, preserves all admitted bytes, and seals on overflow.
 * Whole records reserve their wire order before copying, so a consumer may
 * inspect the data only after close + all admitted appenders have completed.
 * This is not the eventual live DSP queue or a claim of real-time playback.
 * The callback table and journal must outlive EVERY external callback; close
 * does not itself unregister native callbacks or authorize freeing storage.
 */
struct sony_bt_journal;
enum sony_bt_event_kind {SONY_BT_EVENT_SOUND=1,SONY_BT_EVENT_PCM=2,SONY_BT_EVENT_DROP=3,SONY_BT_EVENT_CONNECTION=4,SONY_BT_EVENT_PEER=5,SONY_BT_EVENT_AUDIO_STATE=6};
#define SONY_BT_JOURNAL_MAGIC 0x31454257u /* WBE1 */
#define SONY_BT_JOURNAL_HEADER_BYTES 16u
int sony_bt_journal_create(struct sony_bt_journal **,unsigned capacity);
int sony_bt_journal_append(struct sony_bt_journal *,unsigned kind,const void *,unsigned bytes);
int sony_bt_journal_close(struct sony_bt_journal *);
int sony_bt_journal_view(struct sony_bt_journal *,const void **,unsigned *bytes);
int sony_bt_journal_error(const struct sony_bt_journal *);
/* Caller first independently unregisters/quiesces all external callbacks. */
int sony_bt_journal_destroy(struct sony_bt_journal **);
#endif
