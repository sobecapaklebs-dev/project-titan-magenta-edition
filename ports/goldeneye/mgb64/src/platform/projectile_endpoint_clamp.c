/*
 * projectile_endpoint_clamp.c — see projectile_endpoint_clamp.h.
 *
 * Faithful reconstruction of the retail ASM pull-back block
 * (src/game/chrobjhandler.c:5657-5717). The arithmetic order matches the ASM
 * exactly so the legacy (opt-out) path stays byte-identical to the pre-fix
 * port: delta = target/impact base - start, dist = sqrtf(dot(delta)),
 * frac = 0.1/dist (or 0.5 when dist <= 0.1), out = base - frac*delta.
 */
#include "projectile_endpoint_clamp.h"

#include <math.h>

int projectileEndpointPullback(int result, int legacy,
                               const struct ProjClampVec3 *impact,
                               const struct ProjClampVec3 *target,
                               const struct ProjClampVec3 *start,
                               struct ProjClampVec3 *out)
{
    const struct ProjClampVec3 *base;
    float dx, dy, dz;
    float dist;
    float frac;

    /* Divergence 1 — guard polarity. Retail runs the block only on a wall hit
     * (result == 0). The port defect ran it on the no-hit branch (result != 0). */
    if (legacy) {
        if (result == 0) {
            return 0;
        }
    } else {
        if (result != 0) {
            return 0;
        }
    }

    /* Divergence 2 — base operand. Retail measures from arg2 (= bg_hit_pos on a
     * wall hit); the port defect measured from dest (= arg1, the raw target). */
    base = legacy ? target : impact;

    dx = target->x - start->x;
    dy = target->y - start->y;
    dz = target->z - start->z;

    dist = sqrtf(dx * dx + dy * dy + dz * dz);

    if (0.1f < dist) {
        frac = 0.1f / dist;
    } else {
        frac = 0.5f;
    }

    out->x = base->x - frac * dx;
    out->y = base->y - frac * dy;
    out->z = base->z - frac * dz;

    return 1;
}
