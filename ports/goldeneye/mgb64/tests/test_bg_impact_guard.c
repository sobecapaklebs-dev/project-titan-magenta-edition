/*
 * test_bg_impact_guard.c — ROM-free regression lane for FID-0097.
 *
 * Guards the background-only bullet-impact spawn gate (bg_impact_guard.c). The
 * port keeps a `texture_index >= 0` guard around the retail g_Textures[-1] OOB
 * read as the memory-safe faithful default; GE007_BG_IMPACT_RETAIL_OOB opts into
 * spawning for texture_index < 0 without reading OOB.
 *
 * Fails if the default guard reverts to spawning for index < 0, or the
 * water/snow skip is lost.
 */
#include "bg_impact_guard.h"

#include <stdio.h>

static int g_failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); \
        g_failures++; \
    } \
} while (0)

int main(void)
{
    /* texture_index >= 0: both modes agree (read real record; skip water/snow) */
    CHECK(bgImpactShouldSpawn(0, 0 /*HIT_DEFAULT*/, 0) != 0, "index>=0, HIT_DEFAULT -> spawn");
    CHECK(bgImpactShouldSpawn(7, 3 /*HIT_METAL*/,  0) != 0, "index>=0, HIT_METAL -> spawn");
    CHECK(bgImpactShouldSpawn(7, 5 /*HIT_WATER*/,  0) == 0, "index>=0, HIT_WATER -> no spawn");
    CHECK(bgImpactShouldSpawn(7, 6 /*HIT_SNOW*/,   0) == 0, "index>=0, HIT_SNOW -> no spawn");
    /* the water/snow skip is index-mode-independent */
    CHECK(bgImpactShouldSpawn(7, 5, 1) == 0, "retail-oob mode: index>=0 HIT_WATER still no spawn");
    CHECK(bgImpactShouldSpawn(7, 6, 1) == 0, "retail-oob mode: index>=0 HIT_SNOW still no spawn");
    CHECK(bgImpactShouldSpawn(7, 3, 1) != 0, "retail-oob mode: index>=0 HIT_METAL -> spawn");

    /* texture_index < 0: default guards (no spawn); opt-in spawns */
    CHECK(bgImpactShouldSpawn(-1, -1, 0) == 0, "index<0, default -> NO spawn (port guard)");
    CHECK(bgImpactShouldSpawn(-1, -1, 1) != 0, "index<0, retail-oob opt-in -> spawn");

    if (g_failures == 0) {
        printf("PASS: bg_impact_guard\n");
        return 0;
    }
    fprintf(stderr, "%d check(s) failed\n", g_failures);
    return 1;
}
