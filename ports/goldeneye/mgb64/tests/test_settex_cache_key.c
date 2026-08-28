/*
 * test_settex_cache_key.c — ROM-free regression lane for TMEM-2.
 *
 * The G_SETTEX texture cache (fast3d/gfx_pc.c, settex_cache[]) was keyed on the
 * texture number alone, so the same number re-drawn against a different TLUT
 * returned the FIRST palette's decode — a CI texture whose palette was re-stored
 * by a runtime G_LOADTLUT kept stale colours (DAM_RENDER_DEEP_DIVE 2026-07-18,
 * TMEM-2). The fix adds the decode's palette identity to the key; the per-entry
 * HIT/MISS decision is gfx_settex_cache_key_hit() (settex_cache_key.h), which
 * gfx_pc.c uses directly, so this test pins the real behaviour:
 *
 *   - CI entry: hit iff the palette identity is unchanged (a changed palette,
 *     including a NULL grayscale-fallback -> real-palette transition, is a MISS
 *     that forces a re-decode).
 *   - non-CI entry: no palette, so always a hit on a texturenum match
 *     (byte-identical to the pre-TMEM-2 lookup).
 *
 * A revert of the palette component (making CI entries ignore the palette)
 * reddens the "different palette" and "fallback then palette" cases.
 */
#include "settex_cache_key.h"

#include <stdio.h>

static int g_failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); \
        g_failures++; \
    } \
} while (0)

/* GBI image formats (PR/gbi.h) — mirrored locally to keep the test ROM-free. */
#define FMT_RGBA 0u
#define FMT_CI   2u  /* == GFX_SETTEX_KEY_FMT_CI */
#define FMT_IA   3u
#define FMT_I    4u

int main(void)
{
    const uintptr_t palA = (uintptr_t)0x1000;
    const uintptr_t palB = (uintptr_t)0x2000;

    /* The header's CI constant must equal the value gfx_pc.c keys on. */
    CHECK(GFX_SETTEX_KEY_FMT_CI == FMT_CI,
          "GFX_SETTEX_KEY_FMT_CI == G_IM_FMT_CI (2)");

    /* CI: same palette identity -> HIT (byte-identical reuse). */
    CHECK(gfx_settex_cache_key_hit(FMT_CI, palA, palA),
          "CI same palette -> hit");

    /* CI: different palette identity -> MISS (must re-decode the new TLUT). */
    CHECK(!gfx_settex_cache_key_hit(FMT_CI, palA, palB),
          "CI changed palette -> miss");

    /* CI: grayscale fallback (pal_key == 0) then a real palette arrives -> MISS
     * (the FID-0122 CI-without-registered-TLUT class re-decodes once the palette
     * is available). */
    CHECK(!gfx_settex_cache_key_hit(FMT_CI, 0, palA),
          "CI grayscale-fallback then palette -> miss");
    CHECK(!gfx_settex_cache_key_hit(FMT_CI, palA, 0),
          "CI palette then grayscale-fallback -> miss");
    CHECK(gfx_settex_cache_key_hit(FMT_CI, 0, 0),
          "CI no-palette both -> hit (unchanged)");

    /* Non-CI: palette is irrelevant, always a hit on a texturenum match. This is
     * what keeps every non-CI settex texture byte-identical to pre-TMEM-2. */
    CHECK(gfx_settex_cache_key_hit(FMT_I, palA, palB),
          "I ignores palette -> hit");
    CHECK(gfx_settex_cache_key_hit(FMT_IA, palA, palB),
          "IA ignores palette -> hit");
    CHECK(gfx_settex_cache_key_hit(FMT_RGBA, palA, palB),
          "RGBA ignores palette -> hit");
    CHECK(gfx_settex_cache_key_hit(FMT_I, 0, 0),
          "I no-palette -> hit");

    if (g_failures) {
        fprintf(stderr, "test_settex_cache_key: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("test_settex_cache_key: OK\n");
    return 0;
}
