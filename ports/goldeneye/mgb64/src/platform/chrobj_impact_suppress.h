/*
 * chrobj_impact_suppress.h — pure (ROM-free, SDL-free) mirror of the
 * bullet-impact-FX suppression gate in the object-hit shot handler
 * sub_GAME_7F04EA68 (retail ASM src/game/chrobjhandler.c:39445-39448,
 * VRAM 0x7F04EBF8). Factored out so a ROM-free unit test can guard the
 * FID-0069 divergence.
 *
 * Retail skips the entire explosionCreateBulletImpact block (and the extra
 * randomGetNext() draws inside it) when the shot's weapon is the Watch Laser:
 *
 *   li  $at, 23                 # 23 == ITEM_WATCHLASER
 *   lw  $a0, 0x18($t5)          # shotdata->weapon
 *   beq $a0, $at, .L7F04ED84    # weapon == 23 -> skip the impact block
 *
 * The pre-fix port gated on ITEM_TASER (31) instead, so the Watch Laser spawned
 * an impact effect + drew an extra randomGetNext() (PRNG-stream shift) while the
 * Taser's impact FX was wrongly suppressed.
 *
 * The `legacy` flag reproduces the pre-fix port defect for the master negative
 * control GE007_NO_WATCHLASER_IMPACT_FIX (byte-identical to the old port).
 */
#ifndef MGB64_CHROBJ_IMPACT_SUPPRESS_H
#define MGB64_CHROBJ_IMPACT_SUPPRESS_H

#ifdef __cplusplus
extern "C" {
#endif

/* Item enum anchors (mirrors of src/bondconstants.h ITEM_IDS; ITEM_UNARMED==0,
 * no explicit initializers). ITEM_LASER==22, ITEM_WATCHLASER==23, …
 * ITEM_TRIGGER==30, ITEM_TASER==31. */
#define CHROBJ_IMPACT_ITEM_WATCHLASER 23
#define CHROBJ_IMPACT_ITEM_TASER      31

/*
 * Whether the object-hit impact-FX block (explosionCreateBulletImpact + its
 * randomGetNext draws) is suppressed for this shot's weapon.
 *
 *   weapon : shotdata->weapon (ITEM_IDS).
 *   legacy : 0 => faithful retail (suppress the Watch Laser, id 23);
 *            != 0 => reproduce the pre-fix port (suppress the Taser, id 31)
 *            for the GE007_NO_WATCHLASER_IMPACT_FIX A/B negative control.
 *
 * Returns nonzero when the impact block must be skipped.
 */
int chrobjImpactFxSuppressed(int weapon, int legacy);

#ifdef __cplusplus
}
#endif

#endif /* MGB64_CHROBJ_IMPACT_SUPPRESS_H */
