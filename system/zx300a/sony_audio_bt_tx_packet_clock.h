#ifndef WALKDROID_SONY_TX_PACKET_CLOCK_H
#define WALKDROID_SONY_TX_PACKET_CLOCK_H
#include <stddef.h>
#include <stdint.h>
/* This is an adapter for the Sony SBC producer IPC, not PCM conversion.
 * Native +0x24 is cumulative PCM bytes within one SourceIF call. The daemon
 * uses it unscaled as a per-packet RTP increment. Preserve all SBC bytes.
 * Unrecognized packets are passed through unchanged by the send adapter. */
struct sony_tx_packet_clock_stats {
    uint64_t groups,packets,frames;
    unsigned failures;
};
int sony_tx_sbc_packet_normalize(const void *packet,size_t bytes,unsigned rate,
    unsigned before_frames,unsigned group_frames,void *normalized,unsigned *frames);
int sony_tx_packet_clock_begin(unsigned rate,unsigned frames);
int sony_tx_packet_clock_end(void);
void sony_tx_packet_clock_stats(struct sony_tx_packet_clock_stats *stats);
#endif
