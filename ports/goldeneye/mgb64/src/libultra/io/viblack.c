/*
 * Clean-room libultra-style VI blanking helper for the matching target.
 */

#include <os_internal.h>
#include "viint.h"

void osViBlack(u8 active)
{
    u32 mask;

    mask = __osDisableInt();
    if (active) {
        __osViNext->state |= VI_STATE_BLACK;
    } else {
        __osViNext->state &= (u16)~VI_STATE_BLACK;
    }
    __osRestoreInt(mask);
}
