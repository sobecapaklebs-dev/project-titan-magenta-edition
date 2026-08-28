/*
 * test_mp_healthbar_gate.c — ROM-free regression lane for FID-0070.
 *
 * Guards the maybe_mp_interface health/armour-bar draw gate
 * (mp_healthbar_gate.c). Retail gates on HealthShowTime && watch-idle only; the
 * port defect ORed in DamageShowTime, drawing the bars during damage flashes and
 * skipping the healthdisplaytime decrement. Pins the retail and legacy truth
 * tables — especially the divergent damage-flash case — so a revert or a
 * GE007_NO_MP_HEALTHBAR_DAMAGE_GATE_FIX flip reddens.
 */
#include "mp_healthbar_gate.h"

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
    /* args: (health_show, damage_show, watch_idle, legacy) */

    /* retail (legacy=0): only HealthShowTime && watch-idle draws. */
    CHECK(mpHealthBarDrawGate(1, 0, 1, 0), "retail: health-show, no damage -> draw");
    CHECK(mpHealthBarDrawGate(1, 1, 1, 0), "retail: health-show + damage -> draw");
    /* THE divergent case: damage flash, no health-show -> retail does NOT draw
     * here (falls to the sub_GAME_7F0C6048 else-branch). */
    CHECK(!mpHealthBarDrawGate(0, 1, 1, 0), "retail: damage flash only -> NO draw (else path)");
    CHECK(!mpHealthBarDrawGate(0, 0, 1, 0), "retail: neither -> no draw");
    CHECK(!mpHealthBarDrawGate(1, 0, 0, 0), "retail: watch open -> no draw");
    CHECK(!mpHealthBarDrawGate(1, 1, 0, 0), "retail: watch open (both timers) -> no draw");

    /* legacy (legacy=1): DamageShowTime ORed back in reproduces the port bug. */
    CHECK(mpHealthBarDrawGate(0, 1, 1, 1), "legacy: damage flash only -> draws (the bug)");
    CHECK(mpHealthBarDrawGate(1, 0, 1, 1), "legacy: health-show only -> draw");
    CHECK(!mpHealthBarDrawGate(0, 0, 1, 1), "legacy: neither -> no draw");
    CHECK(!mpHealthBarDrawGate(0, 1, 0, 1), "legacy: watch open -> no draw");

    /* the fix flips exactly the damage-flash-only, watch-idle case. */
    CHECK(mpHealthBarDrawGate(0, 1, 1, 0) != mpHealthBarDrawGate(0, 1, 1, 1),
          "fix changes exactly the damage-flash-only case");

    if (g_failures == 0) {
        printf("PASS: mp_healthbar_gate\n");
        return 0;
    }
    fprintf(stderr, "%d check(s) failed\n", g_failures);
    return 1;
}
