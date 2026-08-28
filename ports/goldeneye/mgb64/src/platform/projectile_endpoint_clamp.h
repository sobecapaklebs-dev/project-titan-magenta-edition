/*
 * projectile_endpoint_clamp.h — the after-room-loop endpoint pull-back for
 * sticky-projectile deposit motion (handles_projectile_motion, retail ASM
 * src/game/chrobjhandler.c:5657-5717).
 *
 * Pure (ROM-free, SDL-free) mirror of the retail pull-back block, factored out
 * so a ROM-free unit test can guard it (FID-0065). Two independent divergences
 * were found between the retail ASM and the NONMATCHING port:
 *
 *   1. GUARD POLARITY. ASM (chrobjhandler.c:5660) is `bnezl $t5,.L7F0426FC`
 *      with $t5 = result: it SKIPS the block when result != 0, i.e. runs it
 *      ONLY when result == 0 (a wall/background hit was recorded). The port ran
 *      the block on the OPPOSITE branch (`if (result != 0)`), i.e. only on the
 *      no-hit path.
 *
 *   2. BASE OPERAND. ASM reads the base from arg2 (`lwc1 $f6,($s3)`,
 *      chrobjhandler.c:5698; $s3 is arg2 — the 5th arg, `sw $s3,0x10($sp)`
 *      at :5724). On a wall hit arg2 already holds bg_hit_pos (the impact). The
 *      port read the base from dest (= arg1, the raw target endpoint).
 *
 * Net (values passed to projectileFindCollidingProp):
 *   wall hit (result==0): retail clamps BOTH the collision endpoint (dest) and
 *     the reported impact (arg2) to bg_hit - 0.1*travel (just before the wall);
 *     the port instead skipped the clamp on hit (passed arg1 through the wall).
 *   no hit (result!=0): retail passes arg1 straight through; the port applied
 *     the -0.1 shorten here instead.
 * where travel = saved_arg1 - obj->runtime_pos and the pull-back fraction is
 * 0.1/|travel| (or 0.5 when |travel| <= 0.1). See FID-0065.
 */
#ifndef MGB64_PROJECTILE_ENDPOINT_CLAMP_H
#define MGB64_PROJECTILE_ENDPOINT_CLAMP_H

#ifdef __cplusplus
extern "C" {
#endif

struct ProjClampVec3 {
    float x;
    float y;
    float z;
};

/*
 * Compute the retail endpoint pull-back for one motion step.
 *
 *   result : 0  => a wall/background hit was recorded this step (impact holds
 *                  bg_hit_pos); != 0 => no hit.
 *   legacy : 0  => faithful retail behavior; != 0 => reproduce the port defect
 *                  (inverted guard + wrong base) for the A/B negative control.
 *   impact : arg2 — on a wall hit this holds bg_hit_pos (the retail base).
 *   target : dest / arg1 — the raw intended endpoint (the legacy base).
 *   start  : obj->runtime_pos — the motion start (for the travel vector).
 *   out    : receives the clamped point when the pull-back applies.
 *
 * Returns 1 and writes *out (= base - frac*travel) when the pull-back applies;
 * returns 0 and leaves *out untouched otherwise. The caller writes *out to BOTH
 * arg2 and dest (retail copies arg2 -> dest, chrobjhandler.c:5712-5716).
 */
int projectileEndpointPullback(int result, int legacy,
                               const struct ProjClampVec3 *impact,
                               const struct ProjClampVec3 *target,
                               const struct ProjClampVec3 *start,
                               struct ProjClampVec3 *out);

#ifdef __cplusplus
}
#endif

#endif /* MGB64_PROJECTILE_ENDPOINT_CLAMP_H */
