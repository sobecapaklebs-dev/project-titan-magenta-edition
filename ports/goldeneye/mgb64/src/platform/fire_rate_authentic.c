/*
 * fire_rate_authentic.c — full-auto fire-cadence tick-scaling (FID-0056).
 *
 * Pure arithmetic (no ROM, no SDL, no game headers) shared by the gun tick
 * (game/gun.c) and tests/test_fire_rate_authentic.c. See fire_rate_authentic.h
 * for the finding + remediation rationale and the ASM anchors
 * (gun.c:17936 field_88C++, gun.c:18368 field_88C % fire_rate).
 */
#include "fire_rate_authentic.h"

int fireRateCounterAdvance(int clock_timer, int authentic)
{
    /* Legacy: the retail per-frame `field_88C++` — advance by 1 regardless of
     * the tick delta. Authentic: mirror `field_890 += g_ClockTimer` so the fire
     * counter accumulates the full integer tick delta (no remainder dropped).
     * At locked 60Hz clock_timer == 1 -> both return 1 (byte-identical). */
    if (authentic == 0) {
        return 1;
    }
    return clock_timer;
}

int fireRateEffectiveAutoRate(int fire_rate, int authentic, int n64_frame_cost)
{
    /* A non-positive rate is never an automatic gate on the retail path; leave
     * it untouched so callers see identical control flow. */
    if (authentic == 0 || fire_rate <= 0) {
        return fire_rate;
    }

    if (n64_frame_cost < FIRE_RATE_N64_FRAME_COST_MIN) {
        n64_frame_cost = FIRE_RATE_N64_FRAME_COST_MIN;
    } else if (n64_frame_cost > FIRE_RATE_N64_FRAME_COST_MAX) {
        n64_frame_cost = FIRE_RATE_N64_FRAME_COST_MAX;
    }

    return fire_rate * n64_frame_cost;
}
