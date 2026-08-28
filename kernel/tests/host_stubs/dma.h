#ifndef TITAN_HOST_STUB_DMA_H
#define TITAN_HOST_STUB_DMA_H
#include <packet2.h>
#define DMA_CHANNEL_VIF1 1
#define DMA_CHANNEL_GIF 2
int dma_channel_initialize(int channel, void *handler, int flags);
int dma_channel_wait(int channel, int timeout);
void dma_channel_send_packet2(packet2_t *packet2, int channel, u8 flush_cache);
void dma_reset(void);
#endif
