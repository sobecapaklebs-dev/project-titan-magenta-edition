/*
 * mp_respawn_tail.c — see mp_respawn_tail.h.
 *
 * Pure decision only; the caller (chrprop.c handle_mp_respawn_and_some_things)
 * performs the two side effects when the returned plan asks for them. See
 * FID-0103.
 */
#include "mp_respawn_tail.h"

MpRespawnTailPlan mpRespawnTailPlan(int obj_type, int reparent_marker_s3, int legacy)
{
    MpRespawnTailPlan plan;

    if (legacy) {
        /* port defect: the entire respawn tail (.L7F03C8AC-.L7F03C8E4) was
         * dropped — silent respawn, armour amount never reset. */
        plan.copy_armour_amount = 0;
        plan.play_respawn_sfx = 0;
        return plan;
    }

    /* retail: the type-21 armour-amount reset, then the s3==0 respawn sound. */
    plan.copy_armour_amount = (obj_type == MP_RESPAWN_TAIL_ARMOUR_TYPE);
    plan.play_respawn_sfx = (reparent_marker_s3 == 0);
    return plan;
}
