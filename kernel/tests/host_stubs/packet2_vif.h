#ifndef TITAN_HOST_STUB_PACKET2_VIF_H
#define TITAN_HOST_STUB_PACKET2_VIF_H
#include <packet2.h>
void packet2_vif_add_micro_program(packet2_t *packet2, u32 dest, u32 *start, u32 *end);
void packet2_vif_flush(packet2_t *packet2, u8 irq);
void packet2_vif_flushe(packet2_t *packet2, u8 irq);
void packet2_vif_flusha(packet2_t *packet2, u8 irq);
void packet2_vif_mscal(packet2_t *packet2, u32 address, u8 irq);
void packet2_vif_mscalf(packet2_t *packet2, u32 address, u8 irq);
void packet2_vif_pad128(packet2_t *packet2);
#endif
