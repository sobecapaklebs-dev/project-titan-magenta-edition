/*
 * aimbone_dispatch.c — see aimbone_dispatch.h.
 *
 * Retail proceeds for arg0 in {0,1,2,3}; the port defect returns for arg0 == 0.
 * See FID-0101 (retail ASM src/game/chr.c:4098-4106, `bnezl arg0`).
 */
#include "aimbone_dispatch.h"

int aimBoneArg0Proceeds(int arg0, int legacy)
{
    if (arg0 == 1 || arg0 == 2 || arg0 == 3) {
        return 1;
    }
    if (arg0 == 0) {
        /* Retail falls through (bnezl not taken) and poses the gun-hand aim
         * bone; the port defect early-returns. */
        return legacy ? 0 : 1;
    }
    /* Any other arg0: retail's bnezl branches to the return; the port returns
     * too — no divergence here. */
    return 0;
}
