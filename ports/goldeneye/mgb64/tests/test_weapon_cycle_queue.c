/*
 * test_weapon_cycle_queue.c — ROM-free regression lane for FID-0016 (M2.2).
 *
 * Guards the mouse-wheel weapon-cycle fix (commit 5d90a94): a fast N-notch
 * scroll must produce N weapon switches over N ticks, not collapse to 1.
 * g_pcWeaponCycleForward/Back are queued step counts; pcQueueWeaponCycleSteps
 * accumulates (clamped to 5) and pcDrainWeaponCycleStep drains one per tick.
 *
 * Fails if the fix is reverted (drain zeroes the counter instead of
 * decrementing, i.e. the pre-fix boolean behavior): 3 queued notches then drain
 * to exhaustion would surface only 1 switch, and the "3 over 3 ticks" assertion
 * goes red.
 */
#include "weapon_cycle_queue.h"

#include <stdio.h>

static int g_failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); \
        g_failures++; \
    } \
} while (0)

int main(void) {
    int c;

    /* --- queue accumulates --- */
    c = 0;
    pcQueueWeaponCycleSteps(&c, 3);
    CHECK(c == 3, "queue 3 -> 3");

    /* --- queue clamps at PC_WEAPON_CYCLE_MAX_QUEUED_STEPS (5) --- */
    pcQueueWeaponCycleSteps(&c, 3);   /* 3 + 3 = 6 -> clamp 5 */
    CHECK(c == PC_WEAPON_CYCLE_MAX_QUEUED_STEPS, "queue clamps to 5");
    CHECK(PC_WEAPON_CYCLE_MAX_QUEUED_STEPS == 5, "clamp cap is 5");

    c = 0;
    pcQueueWeaponCycleSteps(&c, 100); /* pathological delta -> clamp 5 */
    CHECK(c == 5, "queue 100 -> clamp 5");

    /* --- non-positive delta is a no-op --- */
    c = 2;
    pcQueueWeaponCycleSteps(&c, 0);
    CHECK(c == 2, "queue 0 -> unchanged");
    pcQueueWeaponCycleSteps(&c, -4);
    CHECK(c == 2, "queue negative -> unchanged");

    /* --- drain one step per tick: 3 queued -> 3 switches over 3 ticks --- */
    c = 0;
    pcQueueWeaponCycleSteps(&c, 3);
    {
        int switches = 0, tick;
        for (tick = 0; tick < 3; tick++) {
            if (pcDrainWeaponCycleStep(&c)) switches++;
        }
        CHECK(switches == 3, "3 queued notches -> 3 cycle steps over 3 ticks (not 1)");
        CHECK(c == 0, "queue exhausted after 3 drains");
    }

    /* --- drain past empty is a no-op / returns 0 --- */
    CHECK(pcDrainWeaponCycleStep(&c) == 0, "drain empty -> 0");
    CHECK(c == 0, "empty queue stays 0");

    /* --- single notch unchanged (queue depth 1 -> exactly one switch) --- */
    c = 0;
    pcQueueWeaponCycleSteps(&c, 1);
    CHECK(pcDrainWeaponCycleStep(&c) == 1, "1 notch -> 1 switch");
    CHECK(pcDrainWeaponCycleStep(&c) == 0, "1 notch -> no second switch");

    if (g_failures == 0) {
        printf("PASS: weapon_cycle_queue\n");
        return 0;
    }
    fprintf(stderr, "%d check(s) failed\n", g_failures);
    return 1;
}
