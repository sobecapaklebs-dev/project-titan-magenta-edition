/*
 * mp_respawn_tail.h — the object-respawn tail of handle_mp_respawn_and_some_things
 * (retail ASM src/game/chrprop.c; VERSION_US glabel 7F03C648).
 *
 * Pure (ROM-free, SDL-free) mirror of the retail respawn-tail DECISION, factored
 * so a ROM-free unit test can guard it (FID-0103). The side effects themselves
 * (the body-armour amount reset and the respawn sound) stay at the call site in
 * chrprop.c; this helper only decides WHICH of them retail runs.
 *
 * DIVERGENCE (re-derived instruction-level from the US ASM): when a destructible
 * or regenerating OBJ/WEAPON/DOOR prop crosses its regen threshold (timetoregen
 * > 0, < 0x3C, was-above-threshold == 0), retail runs a tail at labels
 * .L7F03C8AC through .L7F03C8E4 that BOTH respawn branches (the simple-respawn
 * `b .L7F03C8AC` and the maxdamage!=0 else branch's fall-through) converge on:
 *
 *   1. if the object-type byte at obj+3 == 21 (PROPDEF_ARMOUR):
 *        copy the f32 at obj+0x80 into obj+0x84
 *        (BodyArmourRecord: amount = initialamount — reset armour on respawn).
 *      ASM: `li $at,21` / `bne $t8,$at,.L7F03C8C8` / `lwc1 $f6,0x80($s0)` /
 *           `swc1 $f6,0x84($s0)`  (7F03C8B0-7F03C8C4).
 *   2. if the reparent marker s3 == 0:
 *        sndPlaySfx(g_musicSfxBufferPtr, 82, 0), then
 *        chrobjSndCreatePostEventDefault(that handle, &prop->pos)  (prop+8).
 *      ASM: `bnez $s3,.L7F03C8E4` / `jal sndPlaySfx` (a1=82) /
 *           `jal chrobjSndCreatePostEventDefault` (a1=$s1+8)  (7F03C8C8-7F03C8E0).
 *
 * s3 is set to 1 only inside the flags&0x8000 reparent path (.L7F03C838). The
 * NATIVE_PORT body skips that path entirely, so s3 is ALWAYS 0 in the port and
 * the respawn sound always fires. The port dropped the whole tail, so regenerating
 * objects respawn SILENTLY and the type-21 armour reset is lost.
 *
 * The exact sndPlaySfx -> chrobjSndCreatePostEventDefault idiom is already live
 * elsewhere in the port (chrlv.c:6717, sound 0x101), so the machinery exists and
 * only this call site was missing.
 *
 * `legacy` toggle: 0 => faithful retail tail; != 0 => reproduce the port defect
 * (drop the tail) for the GE007_NO_MP_RESPAWN_TAIL_FIX A/B negative control.
 */
#ifndef MGB64_MP_RESPAWN_TAIL_H
#define MGB64_MP_RESPAWN_TAIL_H

#ifdef __cplusplus
extern "C" {
#endif

/* Retail: the object-type byte (ObjectRecord's PropDefHeaderRecord.type, obj+3)
 * whose respawn resets body-armour amount. ASM `li $at, 21` at 7F03C8B0; equals
 * PROPDEF_ARMOUR (verified by a _Static_assert at the chrprop.c call site). */
#define MP_RESPAWN_TAIL_ARMOUR_TYPE 21

typedef struct MpRespawnTailPlan {
    /* obj is PROPDEF_ARMOUR (type 21): armour->amount = armour->initialamount. */
    int copy_armour_amount;
    /* fire the object-respawn sound (sndPlaySfx 82 + chrobjSndCreatePostEventDefault). */
    int play_respawn_sfx;
} MpRespawnTailPlan;

/*
 * Decide the retail respawn tail.
 *   obj_type            : ObjectRecord type byte (obj+3).
 *   reparent_marker_s3  : retail s3 — 1 only after the flags&0x8000 reparent path,
 *                         always 0 in the port (that path is skipped).
 *   legacy              : 0 => retail tail; != 0 => port defect (tail dropped).
 */
MpRespawnTailPlan mpRespawnTailPlan(int obj_type, int reparent_marker_s3, int legacy);

#ifdef __cplusplus
}
#endif

#endif /* MGB64_MP_RESPAWN_TAIL_H */
