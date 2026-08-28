/*
 * ammo_icon_anchor.c — see ammo_icon_anchor.h.
 *
 * Instruction-level anchors are in the header. `dim >> 1` reproduces the ASM
 * `sra` (floor division for the u8 image/height dims, which are always >= 0),
 * so frac(dim) is exactly 0.0 for even dim and 0.5 for odd dim. See FID-0067.
 */
#include "ammo_icon_anchor.h"

float ammoIconHalfFrac(int dim)
{
    return (float)dim * 0.5f - (float)(dim >> 1);
}

float ammoIconCenterX(float x, int w, int flip_x, int legacy)
{
    if (legacy) {
        return x; /* port defect: bare x, no fractional/flip correction */
    }
    /* retail 7F069624-48: x + (flipX ? -frac(w) : +frac(w)) */
    {
        float frac = ammoIconHalfFrac(w);
        return flip_x ? (x - frac) : (x + frac);
    }
}

float ammoIconCenterYTop(float y, int h, int legacy)
{
    if (legacy) {
        return y; /* port defect: bare y */
    }
    return y - (float)h * 0.5f; /* retail 7F069674-78: y - h*0.5 */
}

float ammoIconCenterYBottom(float base_h, float scale_s, int h, int legacy)
{
    if (legacy) {
        /* port defect: subtract the full half-height */
        return base_h + scale_s - (float)h * 0.5f;
    }
    /* retail 7F0696A4-E0: H + S - frac(h) */
    return base_h + scale_s - ammoIconHalfFrac(h);
}

int ammoIconMirrorFlag(int flip_x, int legacy)
{
    if (legacy) {
        return flip_x ? 1 : 0; /* port defect: mirror the left dual-wield icon */
    }
    return 0; /* retail 7F0698A4: flipX slot is the constant 0 */
}
