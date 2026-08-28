/*
 * spectrum_settile.c — see spectrum_settile.h (FID-0107).
 */
#include "spectrum_settile.h"

uint32_t spectrumSettileLoadTileW1(int legacy)
{
    /* Retail: lui $s1,0x700 -> 0x07000000; SETTILE tile field (bits 26..24) = 7
     * = G_TX_LOADTILE, matching the LOADTLUT/LOADBLOCK that follow.
     * Legacy port defect: 0x00070000 (tile 0 + stray cmt=1 / maskt=12). */
    return legacy ? 0x00070000u : 0x07000000u;
}
