#ifndef TITAN_HOST_STUB_LIBCDVD_H
#define TITAN_HOST_STUB_LIBCDVD_H
#include <tamtypes.h>
#define SCECdINIT 0
static inline int sceCdInit(int mode) { (void)mode; return 0; }
static inline int sceCdMV(u8 *version, u32 *status) { if (version) *version = 0; if (status) *status = 0; return 0; }
static inline int sceCdReadNVM(u32 address, u16 *data, u8 *status) { (void)address; if (data) *data = 0; if (status) *status = 0; return 0; }
#endif
