/*
 * Clean-room native replacements for libultra's fixed-point sins/coss helpers.
 *
 * The N64 SDK versions use a 1024-entry quarter-sine table. For the native port
 * we keep the same unsigned-angle API and quadrant folding, but compute the
 * positive quarter-wave from standard trigonometry. The table values correspond
 * to floor(sin(i * pi / 2046) * 32767) for i=0..1023, so this preserves the
 * native behavior without compiling the historical SDK GU source files.
 */

#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static signed short portGuSineMagnitude(unsigned int index) {
    double radians;
    int value;

    if (index > 0x3ff) index = 0x3ff;

    radians = ((double)index * M_PI) / 2046.0;
    value = (int)(sin(radians) * 32767.0 + 1.0e-9);

    if (value > 32767) value = 32767;
    if (value < 0) value = 0;
    return (signed short)value;
}

signed short sins(unsigned short angle) {
    unsigned int phase = angle >> 4;
    unsigned int index = phase & 0x3ff;
    signed short value;

    if (phase & 0x400) {
        index = 0x3ff - index;
    }

    value = portGuSineMagnitude(index);
    return (phase & 0x800) ? (signed short)-value : value;
}

signed short coss(unsigned short angle) {
    return sins((unsigned short)(angle + 0x4000));
}
