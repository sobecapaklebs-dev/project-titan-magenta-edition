/*
 * glass_shot_depth.c — see glass_shot_depth.h.
 *
 * Pure decision for the port's opt-in glass-crack depth tolerance (FID-0083).
 * Retail rejects any object hit whose depth is behind the limit plane; the port
 * offers a positive-tolerance acceptance window for glass-like props only. The
 * faithful default is tolerance 0.0, which makes this return 0 for every input
 * (byte-identical to retail's rejection).
 */
#include "glass_shot_depth.h"

int glassShotDepthAccept(int is_glass_like, float depth, float limit, float tolerance)
{
    /* Not glass-like, or in front of / on the plane: this helper does not apply;
     * the caller's retail path decides (a hit at or before the plane is accepted
     * by retail; a non-glass hit behind the plane is rejected by retail). */
    if (!is_glass_like || depth <= limit) {
        return 0;
    }

    /* Behind the plane on a glass pane: retail rejects. Accept only when the
     * opt-in tolerance is positive and the hit is within the window. At the
     * faithful default (0.0) this is always false — retail-exact rejection. */
    return tolerance > 0.0f && depth <= limit + tolerance;
}
