/*
 * bg_impact_guard.c — see bg_impact_guard.h.
 *
 * For texture_index >= 0 both modes agree (read the real g_Textures record and
 * skip only HIT_WATER/HIT_SNOW). For texture_index < 0 the retail OOB read is
 * unreproducible in the port, so the default guards (no spawn) and the opt-in
 * spawns unconditionally. See FID-0097.
 */
#include "bg_impact_guard.h"

/* bondconstants.h HIT_TYPE: HIT_WATER == 5, HIT_SNOW == 6. Hardcoded here so the
 * pure TU has no game-header dependency; the runtime call site uses the enums. */
#define BG_IMPACT_HIT_WATER 5
#define BG_IMPACT_HIT_SNOW  6

int bgImpactShouldSpawn(int texture_index, int hit_sound, int reproduce_retail_oob)
{
    if (texture_index >= 0) {
        return (hit_sound != BG_IMPACT_HIT_WATER) && (hit_sound != BG_IMPACT_HIT_SNOW);
    }
    /* texture_index < 0: retail reads g_Textures[-1] (OOB, non-reproducible).
     * Port default guards it; the opt-in reproduces the "retail spawns"
     * hypothesis without reading OOB. */
    return reproduce_retail_oob ? 1 : 0;
}
