#ifndef TITAN_HOST_STUB_PACKET2_CHAIN_H
#define TITAN_HOST_STUB_PACKET2_CHAIN_H
#include <packet2.h>
void packet2_chain_open_cnt(packet2_t *packet2, u8 irq, u32 pce, u8 spr);
void packet2_chain_close_tag(packet2_t *packet2);
#endif
