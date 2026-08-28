/*
 * test_projectile_endpoint_clamp.c — ROM-free regression lane for FID-0065.
 *
 * Guards the sticky-projectile deposit-motion endpoint pull-back
 * (handles_projectile_motion, retail ASM src/game/chrobjhandler.c:5657-5717).
 * Retail runs the pull-back ONLY on a wall hit (result==0) and measures from
 * arg2 (= bg_hit_pos, the impact); the NONMATCHING port inverted the guard
 * (ran it on the no-hit branch) AND measured from dest (= arg1, the raw
 * target). Both divergences are corrected in projectileEndpointPullback().
 *
 * Fails on revert:
 *   - If the guard polarity is flipped back (block runs on result!=0), the
 *     "wall hit clamps / no-hit does not" assertions go red at legacy=0.
 *   - If the base operand is reverted to the target (dest/arg1), the faithful
 *     wall-hit result moves from impact-0.1*travel to target-0.1*travel and the
 *     coordinate assertion goes red.
 *   - The legacy negative control (legacy=1) must still reproduce the OLD buggy
 *     behavior (clamp on no-hit, base = target) so GE007_NO_PROJECTILE_ENDPOINT
 *     _CLAMP_FIX stays byte-identical to the pre-fix port.
 */
#include "projectile_endpoint_clamp.h"

#include <math.h>
#include <stdio.h>

static int g_failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); \
        g_failures++; \
    } \
} while (0)

static int close_to(float a, float b)
{
    float d = a - b;
    if (d < 0.0f) d = -d;
    return d <= 1e-4f;
}

#define CHECK_VEC(v, ex, ey, ez, msg) do { \
    CHECK(close_to((v).x, (ex)) && close_to((v).y, (ey)) && close_to((v).z, (ez)), (msg)); \
} while (0)

int main(void)
{
    /* Motion from start toward a target 10 units away on +X; a wall hit was
     * recorded at x=4 (impact). travel = target-start = (10,0,0), |travel|=10,
     * frac = 0.1/10 = 0.01, so the pull-back is 0.1 units back along +X. */
    struct ProjClampVec3 start  = { 0.0f, 0.0f, 0.0f };
    struct ProjClampVec3 target = { 10.0f, 0.0f, 0.0f };
    struct ProjClampVec3 impact = { 4.0f, 0.0f, 0.0f };
    struct ProjClampVec3 out;
    int applied;

    /* --- FAITHFUL (legacy=0) --- */

    /* Wall hit (result==0): clamp applies, base = impact -> impact - 0.1*travel. */
    out.x = out.y = out.z = -999.0f;
    applied = projectileEndpointPullback(0, 0, &impact, &target, &start, &out);
    CHECK(applied == 1, "faithful wall-hit: pull-back applies");
    CHECK_VEC(out, 3.9f, 0.0f, 0.0f,
              "faithful wall-hit: base is the impact (arg2), not the target");

    /* No hit (result!=0): retail passes arg1 through -> pull-back does NOT run. */
    out.x = out.y = out.z = -999.0f;
    applied = projectileEndpointPullback(1, 0, &impact, &target, &start, &out);
    CHECK(applied == 0, "faithful no-hit: pull-back does NOT apply (endpoint passes through)");
    CHECK_VEC(out, -999.0f, -999.0f, -999.0f, "faithful no-hit: out untouched");

    /* --- LEGACY negative control (legacy=1) = the pre-fix port defect --- */

    /* No hit (result!=0): buggy port clamped here, base = target (dest/arg1). */
    out.x = out.y = out.z = -999.0f;
    applied = projectileEndpointPullback(1, 1, &impact, &target, &start, &out);
    CHECK(applied == 1, "legacy no-hit: pull-back applies (reproduces port defect)");
    CHECK_VEC(out, 9.9f, 0.0f, 0.0f,
              "legacy no-hit: base is the target (dest/arg1) = pre-fix behavior");

    /* Wall hit (result==0): buggy port skipped the clamp here. */
    out.x = out.y = out.z = -999.0f;
    applied = projectileEndpointPullback(0, 1, &impact, &target, &start, &out);
    CHECK(applied == 0, "legacy wall-hit: pull-back does NOT apply (reproduces port defect)");
    CHECK_VEC(out, -999.0f, -999.0f, -999.0f, "legacy wall-hit: out untouched");

    /* --- Short-travel branch: |travel| <= 0.1 -> frac = 0.5 --- */
    {
        struct ProjClampVec3 s2 = { 0.0f, 0.0f, 0.0f };
        struct ProjClampVec3 t2 = { 0.0f, 0.05f, 0.0f };   /* dist 0.05 <= 0.1 */
        struct ProjClampVec3 i2 = { 0.0f, 0.04f, 0.0f };
        out.x = out.y = out.z = -999.0f;
        applied = projectileEndpointPullback(0, 0, &i2, &t2, &s2, &out);
        CHECK(applied == 1, "short-travel wall-hit applies");
        /* base(impact).y - 0.5*travel.y = 0.04 - 0.5*0.05 = 0.015 */
        CHECK_VEC(out, 0.0f, 0.015f, 0.0f, "short-travel uses frac=0.5 from impact base");
    }

    if (g_failures == 0) {
        printf("test_projectile_endpoint_clamp: OK\n");
        return 0;
    }
    fprintf(stderr, "test_projectile_endpoint_clamp: %d failure(s)\n", g_failures);
    return 1;
}
