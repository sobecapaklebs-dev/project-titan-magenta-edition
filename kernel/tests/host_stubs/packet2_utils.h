#ifndef TITAN_HOST_STUB_PACKET2_UTILS_H
#define TITAN_HOST_STUB_PACKET2_UTILS_H
#include <packet2.h>
void packet2_utils_vu_add_unpack_data(packet2_t *packet2, u32 address, void *data, u32 size, u8 use_top);
void packet2_utils_vu_add_end_tag(packet2_t *packet2);
#endif
