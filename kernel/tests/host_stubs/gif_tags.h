#ifndef TITAN_HOST_STUB_GIF_TAGS_H
#define TITAN_HOST_STUB_GIF_TAGS_H
#include <tamtypes.h>
#define GIF_FLG_PACKED 0u
#define GIF_REG_RGBAQ 1u
#define GIF_REG_ST 2u
#define GIF_REG_UV 3u
#define GIF_REG_AD 14u
#define GIF_REG_XYZ2 5u
#define GIF_SET_TAG(NLOOP,EOP,PRE,PRIM,FLG,NREG) \
    (((u64)(NREG) << 60) | ((u64)(FLG) << 58) | ((u64)(PRIM) << 47) | \
     ((u64)(PRE) << 46) | ((u64)(EOP) << 15) | (u64)(NLOOP))
#endif
