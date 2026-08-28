/*
 * test_ammo_icon_anchor.c — ROM-free regression lane for FID-0067.
 *
 * Guards the HUD ammo-icon rect-center anchor + mirror math
 * (ammo_icon_anchor.c). Retail draws the icon at a floor(h/2)-px-different
 * anchor on both y branches and never mirrors the left dual-wield icon; the
 * NATIVE_PORT rewrite diverged on all of D1/D2/D3/D4. The test pins the exact
 * fixed values and the legacy (pre-fix) values so a revert of either — or a
 * flip of the GE007_NO_AMMO_ICON_ANCHOR_FIX legacy path — reddens.
 */
#include "ammo_icon_anchor.h"

#include <stdio.h>
#include <math.h>

static int g_failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); \
        g_failures++; \
    } \
} while (0)

/* Values here are exact in IEEE-754 (integers and 0.5 multiples), so an exact
 * compare is correct; keep a tiny epsilon guard for robustness anyway. */
#define EQ(a, b) (fabsf((float)(a) - (float)(b)) < 1e-6f)

int main(void)
{
    /* frac(dim) = dim*0.5 - floor(dim/2): 0 for even, 0.5 for odd. */
    CHECK(EQ(ammoIconHalfFrac(16), 0.0f), "frac(16)==0");
    CHECK(EQ(ammoIconHalfFrac(15), 0.5f), "frac(15)==0.5");
    CHECK(EQ(ammoIconHalfFrac(0), 0.0f), "frac(0)==0");
    CHECK(EQ(ammoIconHalfFrac(1), 0.5f), "frac(1)==0.5");

    /* D1 (y>=0): fixed y - h*0.5; legacy y. */
    CHECK(EQ(ammoIconCenterYTop(180.0f, 16, 0), 172.0f), "D1 fixed 180,16 -> 172");
    CHECK(EQ(ammoIconCenterYTop(180.0f, 15, 0), 172.5f), "D1 fixed 180,15 -> 172.5");
    CHECK(EQ(ammoIconCenterYTop(180.0f, 16, 1), 180.0f), "D1 legacy -> y");

    /* D2 (y<0 bottom-anchored): fixed H+S-frac(h); legacy H+S-h*0.5. */
    CHECK(EQ(ammoIconCenterYBottom(200.0f, 3.0f, 16, 0), 203.0f), "D2 fixed even h -> H+S");
    CHECK(EQ(ammoIconCenterYBottom(200.0f, 3.0f, 15, 0), 202.5f), "D2 fixed odd h -> H+S-0.5");
    CHECK(EQ(ammoIconCenterYBottom(200.0f, 3.0f, 16, 1), 195.0f), "D2 legacy even h -> H+S-8");
    /* the exact floor(h/2) delta the ledger names: fixed - legacy == floor(h/2). */
    CHECK(EQ(ammoIconCenterYBottom(200.0f, 3.0f, 16, 0)
             - ammoIconCenterYBottom(200.0f, 3.0f, 16, 1), 8.0f),
          "D2 fixed-legacy delta == floor(16/2)");

    /* D4 (x center): fixed x + (flip? -frac(w) : frac(w)); legacy x. */
    CHECK(EQ(ammoIconCenterX(100.0f, 15, 0, 0), 100.5f), "D4 fixed odd w no-flip -> x+0.5");
    CHECK(EQ(ammoIconCenterX(100.0f, 15, 1, 0), 99.5f), "D4 fixed odd w flip -> x-0.5");
    CHECK(EQ(ammoIconCenterX(100.0f, 16, 0, 0), 100.0f), "D4 fixed even w -> x");
    CHECK(EQ(ammoIconCenterX(100.0f, 15, 1, 1), 100.0f), "D4 legacy -> x");

    /* D3 (mirror flag): fixed always 0; legacy passes flipX through. */
    CHECK(ammoIconMirrorFlag(1, 0) == 0, "D3 fixed flip -> 0 (no mirror)");
    CHECK(ammoIconMirrorFlag(0, 0) == 0, "D3 fixed no-flip -> 0");
    CHECK(ammoIconMirrorFlag(1, 1) == 1, "D3 legacy flip -> 1 (mirrored)");
    CHECK(ammoIconMirrorFlag(0, 1) == 0, "D3 legacy no-flip -> 0");

    if (g_failures == 0) {
        printf("PASS: ammo_icon_anchor\n");
        return 0;
    }
    fprintf(stderr, "%d check(s) failed\n", g_failures);
    return 1;
}
