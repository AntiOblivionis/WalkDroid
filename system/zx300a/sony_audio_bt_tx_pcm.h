#ifndef WALKDROID_SONY_BT_TX_PCM_H
#define WALKDROID_SONY_BT_TX_PCM_H
#include <stdint.h>
/* Native-side units are deliberately separate. Android input frames must
 * never be relabeled as codec frames when original Sony SRC is active. */
enum sony_tx_pcm_flags {SONY_TX_PCM_CONVERT=1,SONY_TX_PCM_FINISHED=2,SONY_TX_PCM_CANCELED=4,SONY_TX_PCM_FAILED=8};
struct sony_tx_pcm_stats {
    uint32_t input_rate,codec_rate,input_pending,codec_pending;
    uint64_t input_processed,input_discarded,input_padding;
    uint64_t codec_generated,codec_submitted,codec_padding,codec_discarded,codec_uncertain;
    uint32_t flags,reserved[3];
};
struct sony_tx_pcm;
typedef int (*sony_tx_pcm_send)(void *,const int16_t *,unsigned frames);
int sony_tx_pcm_open(struct sony_tx_pcm **,unsigned input_rate,unsigned codec_rate,
                     unsigned preferred_frames,sony_tx_pcm_send,void *);
int sony_tx_pcm_write(struct sony_tx_pcm *,const int16_t *,unsigned frames);
int sony_tx_pcm_finish(struct sony_tx_pcm *);
void sony_tx_pcm_cancel(struct sony_tx_pcm *);
void sony_tx_pcm_stats(const struct sony_tx_pcm *,struct sony_tx_pcm_stats *);
void sony_tx_pcm_close(struct sony_tx_pcm **);
#endif
