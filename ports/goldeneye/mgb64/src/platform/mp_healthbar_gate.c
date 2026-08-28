/*
 * mp_healthbar_gate.c — see mp_healthbar_gate.h.
 *
 * The getters are pure reads, so evaluating both unconditionally here (rather
 * than short-circuiting) has no observable effect; the legacy path stays
 * bit-for-bit equivalent to the pre-fix `(HealthShowTime() || DamageShowTime())
 * && watch==0`. See FID-0070.
 */
#include "mp_healthbar_gate.h"

int mpHealthBarDrawGate(int health_show, int damage_show, int watch_idle, int legacy)
{
    if (legacy) {
        /* port defect: also draw during damage flashes */
        return (health_show || damage_show) && watch_idle;
    }
    /* retail: only the health-show-time path (7F089490-A8) */
    return health_show && watch_idle;
}
