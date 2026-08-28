/*
 * test_stick_range.c — ROM-free verify-ratchet for FID-0060 (stick MAGNITUDE / range).
 *
 * FID-0060 asked: does our SDL stick deliver a HOTTER magnitude to game code than
 * an N64 stick, changing movement/turn saturation from hardware?
 *
 * Trace (clean-negative): the movement map lives in the pure helper
 * pcMapMovementStickN64() (src/platform/radial_deadzone.c). Full SDL deflection
 * (±32767) maps to ±80 — the N64 practical stick range (physical hardware tops out
 * ~±80 of the s8 ±127 theoretical). GoldenEye's own analog tuning divides the
 * (deadzone-trimmed) stick by 70.0f and clamps to ±1.0 — walk (bondview.c:12988,
 * 13082), strafe (12965, 13063), turn (13380) and pitch (13323) all SATURATE at
 * raw stick ≈75. So ±80 sits right at/just past saturation, exactly like a real
 * N64 stick at full deflection. We do NOT deliver a hot ±127-equivalent.
 *
 * This is the equivalent of GoldenRecomp's SDL→×0.65-of-N64-range scale
 * (0.65 × 127 ≈ 82.5 ≈ our 80) — we solved F9 at the mapping constant instead of
 * as a post-scale.
 *
 * This test LOCKS that invariant (verify ratchet): full deflection must map to the
 * N64 target ±80, never a hot ±127. It fails-on-revert if the ±80 clamp/scale in
 * radial_deadzone.c is bumped toward the raw s8 range. It composes with — and does
 * not disturb — FID-0015's radial-deadzone geometry (guarded separately by
 * test_radial_deadzone.c); here every assertion is about MAGNITUDE, not shape.
 */
#include "radial_deadzone.h"

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

/* The N64 practical full-deflection magnitude GoldenEye's tuning was authored
 * against. Anything hotter (toward the s8 theoretical ±127) reaches the game's
 * /70.0f analog saturation earlier → twitchy movement/turn vs hardware. */
#define N64_PRACTICAL 80
#define HOT_S8_RANGE  127

int main(void) {
    int mx, my;

    /* === Full deflection → N64 practical ±80 (NOT hot ±127) === */

    /* radial ON (remaster default): +X full */
    pcMapMovementStickN64(32767, 0, DZ, /*radial=*/1, SQ, &mx, &my);
    CHECK(mx == N64_PRACTICAL, "full +X radial -> N64 practical 80");
    CHECK(mx != HOT_S8_RANGE, "full +X radial is NOT hot 127");
    CHECK(my == 0, "full +X radial -> no Y bleed");

    /* radial ON: forward (SDL up = -y -> N64 +y) */
    pcMapMovementStickN64(0, -32767, DZ, 1, SQ, &mx, &my);
    CHECK(my == N64_PRACTICAL, "full forward radial -> N64 practical 80");
    CHECK(mx == 0, "full forward radial -> no X bleed");

    /* radial OFF (legacy square escape hatch): full deflection ALSO clamps to
     * ±80, so byte-identity under the opt-out equals today's raw range. */
    pcMapMovementStickN64(32767, 0, DZ, /*radial=*/0, SQ, &mx, &my);
    CHECK(mx == N64_PRACTICAL, "full +X legacy -> N64 practical 80");
    CHECK(mx != HOT_S8_RANGE, "full +X legacy is NOT hot 127");

    /* === Negative full deflection → ∓80 (symmetric, not hot) === */
    pcMapMovementStickN64(-32767, 0, DZ, 1, SQ, &mx, &my);
    CHECK(mx == -N64_PRACTICAL, "full -X radial -> -80");
    pcMapMovementStickN64(0, 32767, DZ, 1, SQ, &mx, &my); /* SDL down = +y -> N64 -y */
    CHECK(my == -N64_PRACTICAL, "full back radial -> -80");

    /* === Zero → zero === */
    pcMapMovementStickN64(0, 0, DZ, 1, SQ, &mx, &my);
    CHECK(mx == 0 && my == 0, "zero -> (0,0)");

    /* === Diagonal full deflection: magnitude never exceeds the N64 target on
     *     either axis (the ±80 clamp holds; a hot map would overshoot). === */
    pcMapMovementStickN64(32767, -32767, DZ, 1, SQ, &mx, &my);
    CHECK(mx <= N64_PRACTICAL && mx >= -N64_PRACTICAL, "diagonal X within ±80");
    CHECK(my <= N64_PRACTICAL && my >= -N64_PRACTICAL, "diagonal Y within ±80");
    CHECK(mx > 0 && my > 0, "diagonal preserves direction (both axes live)");

    /* === Linear-region faithfulness: half deflection delivers a proportional
     *     ~40 (legacy linear path), NOT the ~63 a hot ±127 map would. This is
     *     what keeps the analog ramp reaching GE's /70 saturation at the same
     *     physical deflection as N64 hardware. === */
    pcMapMovementStickN64(16384, 0, DZ, /*radial=*/0, SQ, &mx, &my);
    CHECK(mx == 40, "half +X legacy -> ~40 (proportional, not hot ~63)");
    CHECK(mx < 63, "half +X legacy stays below the hot ±127-map value");

    if (g_failures == 0) {
        printf("PASS: stick_range (FID-0060: full deflection -> N64 practical ±80, not hot ±127)\n");
        return 0;
    }
    fprintf(stderr, "%d check(s) failed\n", g_failures);
    return 1;
}
