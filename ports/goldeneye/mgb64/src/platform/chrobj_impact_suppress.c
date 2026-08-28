/*
 * chrobj_impact_suppress.c — pure object-hit bullet-impact suppression gate for
 * FID-0069. See the header for the authoritative VERSION_US ASM. ROM-free.
 */
#include "chrobj_impact_suppress.h"

int chrobjImpactFxSuppressed(int weapon, int legacy) {
    if (legacy) {
        /* Pre-fix port: gate on ITEM_TASER (31). */
        return weapon == CHROBJ_IMPACT_ITEM_TASER;
    }
    /* Retail: beq $a0, 23 -> skip the impact block for the Watch Laser. */
    return weapon == CHROBJ_IMPACT_ITEM_WATCHLASER;
}
