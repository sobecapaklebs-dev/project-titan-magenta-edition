/*
 * aimbone_dispatch.h — the arg0 validity dispatch of the character aim-bone
 * pose function sub_GAME_7F02083C (retail ASM src/game/chr.c:4098-4106,
 * VERSION_US glabel src/game/chr.c:4069).
 *
 * Pure (ROM-free, SDL-free) mirror of the retail arg0 gate, factored out so a
 * ROM-free unit test can guard it (FID-0101).
 *
 * DIVERGENCE. The retail validity dispatch is:
 *     beq  arg0, 2 -> proceed
 *     beq  arg0, 3 -> proceed
 *     beql arg0, 1 -> proceed
 *     bnezl arg0   -> return (L7F020D88)
 * `bnezl` is a branch-LIKELY on arg0 != 0: arg0 == 0 does NOT branch and falls
 * through to the aim block (its delay-slot `lw ra` is nullified). So retail
 * PROCEEDS for arg0 in {0,1,2,3} and returns for anything else. arg0 == 0 runs
 * the dedicated gun-hand aim-bone block (retail ASM L7F020974; C body at
 * src/game/chr.c:3878-3912): gunhand-based aimuprshoulder/aimuplshoulder (or
 * aimupback when hidden & 0x400).
 *
 * The NONMATCHING port wrote `if (arg0==2||arg0==3){} else if (arg0==1){} else
 * { return; }` (src/game/chr.c:3816-3827), which RETURNS for arg0 == 0 — making
 * the port's own arg0 == 0 block DEAD. g_aimBoneCallback is invoked per bone
 * with the bone index as arg0 (model.c:3701 / model.c:4012), and arg0 == 0 is
 * the gun-hand aim bone, so the port never poses the gun-hand/arm to track aim.
 * See FID-0101.
 */
#ifndef MGB64_AIMBONE_DISPATCH_H
#define MGB64_AIMBONE_DISPATCH_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Decide whether sub_GAME_7F02083C proceeds (i.e. does NOT early-return) for a
 * given aim-bone index arg0.
 *
 *   arg0   : the aim-bone index passed by g_aimBoneCallback.
 *   legacy : 0 => faithful retail behavior (proceed for {0,1,2,3}); != 0 =>
 *            reproduce the port defect (proceed for {1,2,3}, return for 0) for
 *            the GE007_NO_GUNHAND_AIMBONE_FIX A/B negative control.
 *
 * Returns nonzero when the function should proceed to pose the bone.
 */
int aimBoneArg0Proceeds(int arg0, int legacy);

#ifdef __cplusplus
}
#endif

#endif /* MGB64_AIMBONE_DISPATCH_H */
