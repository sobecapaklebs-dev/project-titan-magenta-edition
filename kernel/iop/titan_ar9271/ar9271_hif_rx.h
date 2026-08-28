#ifndef TITAN_AR9271_HIF_RX_H
#define TITAN_AR9271_HIF_RX_H

#include <types.h>

#define TITAN_HIF_RX_STREAM_TAG 0x4e00u
#define TITAN_HIF_RX_MAX_PACKET 32768
#define TITAN_HIF_RX_STASH_SIZE (TITAN_HIF_RX_MAX_PACKET + 8)

typedef void (*TitanHifPacketFn)(void *ctx, const u8 *packet, int len);

typedef struct TitanHifRx {
    u8 stash[TITAN_HIF_RX_STASH_SIZE] __attribute__((aligned(64)));
    int stash_len;
    u32 packets;
    u32 partial_packets;
    u32 invalid_packets;
    TitanHifPacketFn packet_fn;
    void *packet_ctx;
} TitanHifRx;

void titan_hif_rx_init(TitanHifRx *hif, TitanHifPacketFn fn, void *ctx);
void titan_hif_rx_reset(TitanHifRx *hif);
void titan_hif_rx_feed(TitanHifRx *hif, const u8 *data, int len);

#endif
