/*
 * Clean-room libultra-style fixed-angle sine helper for the matching target.
 * The input angle maps the unsigned 16-bit range onto a full turn.
 */

#include "guint.h"
#include "sintable.h"

signed short sins(unsigned short angle)
{
    unsigned short phase;
    unsigned short index;
    signed short value;

    phase = angle >> 4;
    index = phase & 0x03ff;

    if (phase & 0x0400) {
        value = sintable[0x03ff - index];
    } else {
        value = sintable[index];
    }

    if (phase & 0x0800) {
        return (signed short)-value;
    }

    return value;
}
