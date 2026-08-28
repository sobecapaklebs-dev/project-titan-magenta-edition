/*
 * test_fire_rate_authentic.c — ROM-free regression lane for FID-0056.
 *
 * Guards the full-auto fire-cadence tick-scaling fix. On locked-60Hz mgb64 the
 * fire counter field_88C advances once per sim tick and the gate fires every
 * `fire_rate` ticks; on N64 the counter advanced once per rendered frame
 * (~2-3 ticks), so automatics fired 2-3x slower. Input.FireRateAuthentic scales
 * the automatic divisor by the assumed N64 frame cost to reproduce that cadence.
 *
 * Asserts:
 *   - fireRateCounterAdvance is byte-identical to `++` at locked 60Hz
 *     (clock==1) in both flag states; tick-scales (== clock) only when ON.
 *   - fireRateEffectiveAutoRate is a no-op when OFF (byte-identity), and
 *     multiplies by the (clamped) N64 frame cost when ON.
 *   - the simulated sustained-fire cadence over a fixed tick window: OFF equals
 *     the raw 60Hz shot count (window/fire_rate); ON equals ~1/frame_cost of it,
 *     matching the measured 2.95x AK47 divergence (33.3 vs 11.3 shots/100 ticks).
 *
 * FAIL-ON-REVERT: if the divisor scaling is reverted (fireRateEffectiveAutoRate
 * returns fire_rate when ON), the ON cadence equals the OFF cadence and the
 * ratio assertion goes red. If the counter tick-scaling is reverted, the
 * clock==3 advance assertion goes red.
 */
#include "fire_rate_authentic.h"

#include <stdio.h>

static int g_failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); \
        g_failures++; \
    } \
} while (0)

/* Simulate the retail full-auto gate over `ticks` sim ticks at a fixed per-frame
 * clock delta, counting shots. Mirrors gun.c: field_88C advances each frame by
 * fireRateCounterAdvance(clock, authentic); a shot fires when field_88C != 0 and
 * field_88C % effective_rate == 0. Returns the shot count. */
static int simulate_shots(int fire_rate, int authentic, int n64_frame_cost,
                          int clock, int ticks)
{
    int eff = fireRateEffectiveAutoRate(fire_rate, authentic, n64_frame_cost);
    int field_88C = 0;
    int elapsed = 0;
    int shots = 0;
    while (elapsed < ticks) {
        field_88C += fireRateCounterAdvance(clock, authentic);
        if (field_88C != 0 && (field_88C % eff) == 0) {
            shots++;
        }
        elapsed += clock;
    }
    return shots;
}

int main(void) {
    /* --- counter advance: byte-identical at locked 60Hz (clock==1) --- */
    CHECK(fireRateCounterAdvance(1, 0) == 1, "advance OFF clock=1 -> 1");
    CHECK(fireRateCounterAdvance(1, 1) == 1, "advance ON  clock=1 -> 1 (byte-identical)");

    /* --- counter advance: tick-scales only when ON (frame drop clock==3) --- */
    CHECK(fireRateCounterAdvance(3, 0) == 1, "advance OFF clock=3 -> 1 (legacy per-frame)");
    CHECK(fireRateCounterAdvance(3, 1) == 3, "advance ON  clock=3 -> 3 (tick-scaled)");
    CHECK(fireRateCounterAdvance(2, 1) == 2, "advance ON  clock=2 -> 2");

    /* --- effective auto rate: no-op OFF, x frame_cost ON --- */
    CHECK(fireRateEffectiveAutoRate(3, 0, 3) == 3,  "eff rate OFF -> unchanged");
    CHECK(fireRateEffectiveAutoRate(3, 1, 3) == 9,  "eff rate ON x3 -> 9");
    CHECK(fireRateEffectiveAutoRate(3, 1, 2) == 6,  "eff rate ON x2 -> 6 (30fps nominal)");
    CHECK(fireRateEffectiveAutoRate(3, 1, 4) == 12, "eff rate ON x4 -> 12 (15fps heavy)");

    /* --- frame-cost clamp to [2,4] --- */
    CHECK(fireRateEffectiveAutoRate(3, 1, 1) == 6,  "eff rate ON clamp low -> x2");
    CHECK(fireRateEffectiveAutoRate(3, 1, 9) == 12, "eff rate ON clamp high -> x4");

    /* --- non-positive fire_rate passes through unchanged (not an automatic) --- */
    CHECK(fireRateEffectiveAutoRate(0, 1, 3) == 0,   "eff rate ON fire_rate 0 -> 0");
    CHECK(fireRateEffectiveAutoRate(-5, 1, 3) == -5, "eff rate ON fire_rate <0 -> unchanged");

    /* --- FID-0066: the GUARD full-auto gate (chrlv.c chrlvFireWeaponRelated)
     * reuses this SAME divisor helper — self->firecount[hand] % eff_rate — so the
     * guard path scales symmetrically with the player. KF7 Soviet (the Dam guard
     * rifle) has AutomaticFiringRate 3: OFF fires every 3 ticks, ON every 9.
     * The guard's `< 0` always-fire branch and `firecount % (eff_rate * 2)`
     * sub-flag also flow through this helper unchanged. NB: this only covers the
     * shared arithmetic; the guard call site itself is exercised end-to-end by
     * tools/fidelity/guard_fire_rate_symmetry_smoke.sh. --- */
    CHECK(fireRateEffectiveAutoRate(3, 0, 3) == 3,  "guard KF7 OFF -> divisor 3 (every 3 ticks)");
    CHECK(fireRateEffectiveAutoRate(3, 1, 3) == 9,  "guard KF7 ON  -> divisor 9 (every 9 ticks, 1/3)");
    CHECK((fireRateEffectiveAutoRate(3, 1, 3) * 2) == 18,
          "guard KF7 ON  -> sub-flag divisor eff_rate*2 == 18 (2:1 ratio preserved)");
    CHECK(fireRateEffectiveAutoRate(-1, 1, 3) == -1, "guard non-automatic (rate<0) -> unchanged (always-fire branch)");

    /* --- cadence over a fixed tick window at locked 60Hz (clock==1) --- */
    {
        const int fire_rate = 3;      /* AK47-class automatic (fires ~every 3 ticks) */
        const int cost = 3;           /* measured Dam ~20fps combat frame cost       */
        const int window = 360;       /* 360 ticks = 6 sim-seconds; divisible by 3 & 9 */

        int off = simulate_shots(fire_rate, 0, cost, 1, window);
        int on  = simulate_shots(fire_rate, 1, cost, 1, window);

        /* OFF reproduces today's raw 60Hz count: a shot every fire_rate ticks. */
        CHECK(off == window / fire_rate, "OFF cadence == raw 60Hz count (window/fire_rate)");
        /* ON reproduces the N64 per-frame cadence: fire_rate*cost ticks per shot. */
        CHECK(on == window / (fire_rate * cost), "ON cadence == N64 count (window/(fire_rate*cost))");
        /* The divergence: OFF fires `cost`x more than ON (the 2.95x AK47 result). */
        CHECK(off == on * cost, "OFF/ON cadence ratio == N64 frame cost");
        CHECK(on < off, "authentic mode fires strictly slower than legacy");

        printf("cadence: fire_rate=%d cost=%d window=%d ticks -> OFF=%d shots, ON=%d shots (ratio %.2fx)\n",
               fire_rate, cost, window, off, on, (double)off / (double)on);
    }

    if (g_failures != 0) {
        fprintf(stderr, "test_fire_rate_authentic: %d FAILURE(S)\n", g_failures);
        return 1;
    }
    printf("test_fire_rate_authentic: all checks passed\n");
    return 0;
}
