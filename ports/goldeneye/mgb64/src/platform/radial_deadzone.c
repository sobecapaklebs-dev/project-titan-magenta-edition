/*
 * radial_deadzone.c — shared analog-stick radial deadzone + N64 mapping.
 *
 * Pure, SDL-free. Factored out of platform_sdl.c (helper) and stubs.c (movement
 * map) so the aim stick and movement stick share one behavior and a ROM-free
 * unit test can guard it (FID-0015 / M2.1). Behavior is byte-identical to the
 * original inline code paths.
 */
#include "radial_deadzone.h"

#include <math.h>

void platformApplyRadialDeadzone(float *nx, float *ny, float deadzone, int radial_enabled) {
    float x, y, mag;
    if (!nx || !ny || !radial_enabled) {
        return;
    }
    x = *nx;
    y = *ny;
    mag = sqrtf(x * x + y * y);
    if (mag <= deadzone || mag <= 0.0f) {
        *nx = 0.0f;
        *ny = 0.0f;
    } else {
        float rescaled = (mag - deadzone) / (1.0f - deadzone);
        float inv;
        if (rescaled > 1.0f) rescaled = 1.0f;
        inv = rescaled / mag;
        *nx = x * inv;
        *ny = y * inv;
    }
}

void pcMapMovementStickN64(int lx, int ly, float deadzone, int radial_enabled,
                           int square_deadzone, int *out_x, int *out_y) {
    int mx = 0, my = 0;

    if (radial_enabled) {
        float nx = (float)lx / 32767.0f;
        float ny = (float)(-ly) / 32767.0f;   /* SDL Y inverted vs N64 */
        platformApplyRadialDeadzone(&nx, &ny, deadzone, 1);
        mx = (int)(nx * 80.0f);
        my = (int)(ny * 80.0f);
    } else {
        if (lx > square_deadzone || lx < -square_deadzone) mx = (lx * 80) / 32767;
        if (ly > square_deadzone || ly < -square_deadzone) my = (-ly * 80) / 32767;
    }
    /* Clamp to the N64 PRACTICAL stick range, ±80 (FID-0060). Real N64 hardware
     * tops out ~±80 of the s8 ±127 theoretical, and GE's analog tuning divides the
     * (deadzone-trimmed) stick by 70.0f then clamps to ±1.0 (bondview.c walk/strafe/
     * turn/pitch), so it saturates at raw ≈75. Mapping full deflection to ±80 (not
     * ±127) keeps movement/turn saturation identical to hardware — the equivalent of
     * GoldenRecomp's SDL→×0.65-of-N64-range scale (0.65×127≈82.5≈80), solved here at
     * the mapping constant. Guarded by tests/test_stick_range.c. Do NOT raise toward
     * ±127: that would reach saturation earlier and play "too hot." */
    if (mx > 80) mx = 80; else if (mx < -80) mx = -80;
    if (my > 80) my = 80; else if (my < -80) my = -80;
    *out_x = mx;
    *out_y = my;
}
