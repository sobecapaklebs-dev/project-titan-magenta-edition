#ifndef TITAN_HOST_STUB_GS_PRIVILEGED_H
#define TITAN_HOST_STUB_GS_PRIVILEGED_H
#include <tamtypes.h>
extern volatile u64 *GS_REG_DISPLAY1;
extern volatile u64 *GS_REG_DISPLAY2;
static inline u64 titan_host_gs_set_display(
    int dx,
    int dy,
    unsigned int magh,
    unsigned int magv,
    unsigned int dw,
    unsigned int dh
)
{
    return (u64)(
        (unsigned int)dx ^
        (unsigned int)dy ^
        magh ^ magv ^ dw ^ dh
    );
}
#define GS_SET_DISPLAY(dx, dy, magh, magv, dw, dh) \
    titan_host_gs_set_display((dx), (dy), (magh), (magv), (dw), (dh))
#endif
