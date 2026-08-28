/*
 * fp_weapon_perspnorm.c — pure FP-weapon perspnorm value math for FID-0077. See
 * the header for the authoritative VERSION_US ASM sites. ROM-free / SDL-free.
 */
#include "fp_weapon_perspnorm.h"

float fpWeaponPerspNormNearArg(int legacy) {
    /* Retail: mtc1 $zero,$f12 => 0.0f. Pre-fix port: 1.0f. */
    return legacy ? 1.0f : 0.0f;
}

unsigned short fpWeaponPerspNormValue(int legacy) {
    /* Mirror of matrixmath.c sub_GAME_7F05997C(near, 300.0f). */
    float sum = fpWeaponPerspNormNearArg(legacy) + 300.0f;
    unsigned short result;

    if (sum <= 2.0f) {
        result = 0xffff;
    } else {
        result = (unsigned short)(0x20000 / sum);
        if (result == 0) {
            result = 1;
        }
    }
    return result;
}
