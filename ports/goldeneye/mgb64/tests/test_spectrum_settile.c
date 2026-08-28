/*
 * test_spectrum_settile.c — ROM-free regression lane for FID-0107.
 *
 * spectrum_draw_screen emits two SETTILE commands whose low word (w1) must carry
 * tile field 7 (G_TX_LOADTILE) so they configure the same tile their following
 * LOADTLUT (0x0703C000) and LOADBLOCK (0x073FF200) load into. Retail stores
 * 0x07000000 (spectrum.c:1547/1550/1639); the port transcribed 0x00070000, a
 * digit shift decoding to tile 0 + stray cmt/maskt. Pins the corrected constant
 * and the legacy defect so a revert or a GE007_NO_SPECTRUM_LOADTILE_FIX flip
 * reddens.
 */
#include "spectrum_settile.h"

#include <stdio.h>

static int g_failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); \
        g_failures++; \
    } \
} while (0)

/* SETTILE w1 tile field is bits 26..24. */
static unsigned settile_tile(uint32_t w1) { return (w1 >> 24) & 7u; }

int main(void)
{
    uint32_t faithful = spectrumSettileLoadTileW1(0);
    uint32_t legacy   = spectrumSettileLoadTileW1(1);

    /* Faithful: exact retail constant + tile field 7 = G_TX_LOADTILE. */
    CHECK(faithful == 0x07000000u,
          "faithful w1 == 0x07000000 (retail lui $s1,0x700)");
    CHECK(settile_tile(faithful) == 7u,
          "faithful SETTILE tile field == 7 (G_TX_LOADTILE)");

    /* The faithful SETTILE names the SAME tile (7) the two loads use. */
    CHECK(settile_tile(faithful) == settile_tile(0x0703C000u),
          "faithful tile matches the palette LOADTLUT tile (7)");
    CHECK(settile_tile(faithful) == settile_tile(0x073FF200u),
          "faithful tile matches the per-tile LOADBLOCK tile (7)");

    /* Legacy: the digit-shift defect — tile 0 + stray cmt/maskt bits. */
    CHECK(legacy == 0x00070000u,
          "legacy w1 == 0x00070000 (the port digit-shift defect)");
    CHECK(settile_tile(legacy) == 0u,
          "legacy SETTILE tile field == 0 (mismatches the tile-7 loads)");
    CHECK((legacy & (1u << 18)) != 0u,
          "legacy has the stray cmt bit (18) the retail constant lacks");
    CHECK((legacy & (0xFu << 14)) != 0u,
          "legacy has stray maskt bits (17..14) the retail constant lacks");

    /* Fail-on-revert: the fix must change the emitted SETTILE low word. */
    CHECK(faithful != legacy,
          "fix changes the SETTILE w1 (faithful != legacy)");

    if (g_failures == 0) {
        printf("PASS: spectrum_settile\n");
        return 0;
    }
    fprintf(stderr, "%d check(s) failed\n", g_failures);
    return 1;
}
