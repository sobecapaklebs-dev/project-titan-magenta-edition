/*
 * test_radial_deadzone.c — ROM-free regression lane for FID-0015 (M2.1).
 *
 * Guards the movement-stick radial deadzone fix (commit a48c12a): the movement
 * stick must use the SAME radial magnitude deadzone + rescale-from-edge as the
 * aim stick (shared platformApplyRadialDeadzone), not a per-axis square deadzone
 * that zeroes a real diagonal.
 *
 * Asserts the documented mapping:
 *   - magnitude below the deadzone -> (0,0);
 *   - a diagonal (6000,6000) whose radial magnitude clears the deadzone ->
 *     small NONZERO output (a per-axis square deadzone would zero both axes);
 *   - full deflection -> unchanged (+/-80);
 *   - the movement map routes through the exact shared helper (identical output
 *     on sample inputs when the helper is applied by hand).
 *
 * Fails if the fix is reverted (movement stick back to per-axis square): the
 * (6000,6000) diagonal collapses to (0,0) and the diagonal assertion goes red.
 */
#include "radial_deadzone.h"

#include <math.h>
#include <stdio.h>

static int g_failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); \
        g_failures++; \
    } \
} while (0)

/* Match the runtime call site (stubs.c): Input.GamepadDeadzone default 0.15,
 * GAMEPAD_DEADZONE square threshold 8000. */
#define DZ 0.15f
#define SQ 8000

int main(void) {
    int mx, my;
    float nx, ny;

    /* --- helper: magnitude below the deadzone -> (0,0) --- */
    nx = 0.10f; ny = 0.05f;                 /* mag ~0.112 < 0.15 */
    platformApplyRadialDeadzone(&nx, &ny, DZ, 1);
    CHECK(nx == 0.0f && ny == 0.0f, "below-deadzone vector -> (0,0)");

    /* --- helper: no-op when radial disabled --- */
    nx = 0.10f; ny = 0.05f;
    platformApplyRadialDeadzone(&nx, &ny, DZ, 0);
    CHECK(nx == 0.10f && ny == 0.05f, "radial disabled -> untouched");

    /* --- movement map: diagonal (6000,6000) clears the radial deadzone --- */
    /* mag = sqrt(2)*6000/32767 ~= 0.259 > 0.15, so a real deflection. A per-axis
     * square deadzone (8000) would zero BOTH axes; the radial map must not. */
    pcMapMovementStickN64(6000, 6000, DZ, /*radial=*/1, SQ, &mx, &my);
    CHECK(mx != 0 && my != 0, "diagonal (6000,6000) radial -> nonzero (not per-axis-zeroed)");
    CHECK(mx == 7 && my == -7, "diagonal (6000,6000) radial -> (7,-7) documented");

    /* The legacy per-axis square map (radial off) DOES zero it — this is the
     * exact divergence the fix removes. */
    pcMapMovementStickN64(6000, 6000, DZ, /*radial=*/0, SQ, &mx, &my);
    CHECK(mx == 0 && my == 0, "diagonal (6000,6000) square -> (0,0) (legacy)");

    /* --- movement map: full deflection unchanged (+/-80) --- */
    pcMapMovementStickN64(32767, 0, DZ, 1, SQ, &mx, &my);
    CHECK(mx == 80 && my == 0, "full deflection X radial -> 80");
    pcMapMovementStickN64(0, -32767, DZ, 1, SQ, &mx, &my); /* SDL up = -y */
    CHECK(my == 80 && mx == 0, "full deflection forward radial -> 80");

    /* --- below deadzone via the movement map -> (0,0) --- */
    pcMapMovementStickN64(3000, 0, DZ, 1, SQ, &mx, &my); /* mag ~0.0915 < 0.15 */
    CHECK(mx == 0 && my == 0, "small single-axis radial -> (0,0)");

    /* --- identity: movement map routes through the shared helper --- */
    /* For a sweep of raw axes, applying the helper by hand to the normalized
     * vector then mapping to +/-80 must equal pcMapMovementStickN64 exactly —
     * i.e. the movement stick and aim stick share one deadzone implementation. */
    {
        static const int samples[][2] = {
            {6000, 6000}, {10000, -4000}, {-20000, 15000},
            {32767, 32767}, {-32767, 0}, {5000, 5000}, {12000, -30000},
        };
        int i;
        for (i = 0; i < (int)(sizeof(samples) / sizeof(samples[0])); i++) {
            int lx = samples[i][0], ly = samples[i][1];
            int emx, emy;
            float hx = (float)lx / 32767.0f;
            float hy = (float)(-ly) / 32767.0f;
            platformApplyRadialDeadzone(&hx, &hy, DZ, 1);
            emx = (int)(hx * 80.0f);
            emy = (int)(hy * 80.0f);
            if (emx > 80) emx = 80; else if (emx < -80) emx = -80;
            if (emy > 80) emy = 80; else if (emy < -80) emy = -80;
            pcMapMovementStickN64(lx, ly, DZ, 1, SQ, &mx, &my);
            CHECK(mx == emx && my == emy, "movement map == shared helper on sample");
        }
    }

    if (g_failures == 0) {
        printf("PASS: radial_deadzone\n");
        return 0;
    }
    fprintf(stderr, "%d check(s) failed\n", g_failures);
    return 1;
}
