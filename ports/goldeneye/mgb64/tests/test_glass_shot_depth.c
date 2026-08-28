/*
 * test_glass_shot_depth.c — ROM-free regression lane for FID-0083.
 *
 * Guards the glass shot-depth acceptance decision (glass_shot_depth.c). Retail
 * (US ASM sub_GAME_7F04E720 / sub_GAME_7F04E9BC) rejects an object hit whose
 * depth is behind the limit plane; the port offers an OPT-IN glass-crack window.
 * Pins both sides so the faithful default (tolerance 0.0 = retail-exact
 * rejection) and the opt-in mitigation (tolerance > 0 accepts within the window)
 * cannot silently drift — a revert of the default to a positive tolerance, or a
 * change to the window math, reddens.
 */
#include "glass_shot_depth.h"

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
    const float limit = 100.0f;

    /* ---- Faithful default: tolerance 0.0 == retail-exact rejection ---- */
    /* A glass hit behind the plane is REJECTED (matches retail's bc1fl return). */
    CHECK(glassShotDepthAccept(1, limit + 1.0f, limit, 0.0f) == 0,
          "faithful (tol 0): glass hit 1u behind plane is rejected");
    CHECK(glassShotDepthAccept(1, limit + 0.001f, limit, 0.0f) == 0,
          "faithful (tol 0): glass hit just behind plane is rejected");

    /* ---- Opt-in mitigation: tolerance 2.0 (the former default, now opt-in) ---- */
    /* Within the window: ACCEPTED (the deliberate deviation from retail). */
    CHECK(glassShotDepthAccept(1, limit + 1.0f, limit, 2.0f) == 1,
          "opt-in (tol 2): glass hit 1u behind plane is accepted");
    CHECK(glassShotDepthAccept(1, limit + 2.0f, limit, 2.0f) == 1,
          "opt-in (tol 2): glass hit exactly at the window edge is accepted");
    /* Beyond the window: rejected even with the mitigation on. */
    CHECK(glassShotDepthAccept(1, limit + 3.0f, limit, 2.0f) == 0,
          "opt-in (tol 2): glass hit 3u behind plane (beyond window) is rejected");

    /* ---- fail-on-revert: faithful and opt-in must DIFFER on the same hit ---- */
    CHECK(glassShotDepthAccept(1, limit + 1.0f, limit, 0.0f)
              != glassShotDepthAccept(1, limit + 1.0f, limit, 2.0f),
          "fail-on-revert: faithful rejects where opt-in accepts");

    /* ---- Non-glass and in-front hits are never this helper's business ---- */
    CHECK(glassShotDepthAccept(0, limit + 1.0f, limit, 2.0f) == 0,
          "non-glass prop behind the plane is not accepted by the glass window");
    CHECK(glassShotDepthAccept(1, limit - 1.0f, limit, 2.0f) == 0,
          "glass hit in front of the plane defers to the retail (accept) path");
    CHECK(glassShotDepthAccept(1, limit, limit, 2.0f) == 0,
          "glass hit exactly on the plane defers to the retail (accept) path");

    if (g_failures == 0) {
        printf("test_glass_shot_depth: OK\n");
        return 0;
    }
    fprintf(stderr, "test_glass_shot_depth: %d failure(s)\n", g_failures);
    return 1;
}
