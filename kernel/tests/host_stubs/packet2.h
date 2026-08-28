#ifndef TITAN_HOST_STUB_PACKET2_H
#define TITAN_HOST_STUB_PACKET2_H
#include <tamtypes.h>
typedef union qword_t { u64 dw[2]; u32 sw[4]; float fw[4]; } qword_t;
typedef struct packet2_t { qword_t *base; qword_t *next; } packet2_t;
enum Packet2Type { P2_TYPE_NORMAL = 0 };
enum Packet2Mode { P2_MODE_NORMAL = 0, P2_MODE_CHAIN = 1 };
packet2_t *packet2_create(u16 qwords, enum Packet2Type type, enum Packet2Mode mode, u8 tte);
void packet2_reset(packet2_t *packet2, u8 clear_mem);
void packet2_update(packet2_t *packet2, qword_t *next);
unsigned int packet2_get_qw_count(packet2_t *packet2);
#endif
