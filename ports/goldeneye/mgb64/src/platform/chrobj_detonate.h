/*
 * chrobj_detonate.h — pure (ROM-free, SDL-free) mirror of the pre-damage gate,
 * the armour-collect amount, and the ammo-salvage slot arithmetic inside
 * maybe_detonate_object (retail ASM src/game/chrobjhandler.c:38029-38459,
 * VRAM 0x7F04E108). Factored out so a ROM-free unit test can guard the five
 * FID-0074 divergences the port had against the retail body:
 *
 *   D1  missing PROPDEF_AMMO salvage spawn + its randomGetNext() draw.
 *   D2  armour-collect amount inverted (destroyed<->not-destroyed branches).
 *   D3  unarmed non-collectable gate inverted (proceed iff 0x01000000 CLEAR).
 *   D4  dropped armed-path INVINCIBLE (0x00020000) early-out before dispatch.
 *   D5  type-7/8 non-explosive fall-through to objIsMortal+damage.
 *
 * Every helper takes a `legacy` flag: legacy==0 is faithful retail behavior,
 * legacy!=0 reproduces the pre-fix port defect so the master negative control
 * GE007_NO_DETONATE_OBJECT_FIX stays byte-identical to the old port.
 */
#ifndef MGB64_CHROBJ_DETONATE_H
#define MGB64_CHROBJ_DETONATE_H

#ifdef __cplusplus
extern "C" {
#endif

/* PROPFLAG bits used by the gate (mirrors of src/bondconstants.h). */
#define CHROBJ_DETONATE_FLAG_COLLECT_UNARMED  0x00800000u /* PROPFLAG_00800000 */
#define CHROBJ_DETONATE_FLAG_UNARMED_DISABLE  0x01000000u /* PROPFLAG_01000000 (D3) */
#define CHROBJ_DETONATE_FLAG_INVINCIBLE       0x00020000u /* PROPFLAG_INVINCIBLE (D4) */

/* PROPDEF_TYPE values dispatched on the armed path (mirrors of the enum). */
#define CHROBJ_DETONATE_TYPE_MAGAZINE     7  /* PROPDEF_MAGAZINE */
#define CHROBJ_DETONATE_TYPE_COLLECTABLE  8  /* PROPDEF_COLLECTABLE */

/*
 * Result of the pre-damage gate. The caller executes the side effect (if any)
 * and either returns or falls through to damage application.
 */
typedef enum ChrobjDetonateAction {
    CHROBJ_DETONATE_SKIP,          /* return immediately, no side effect */
    CHROBJ_DETONATE_ARM_WEAPON,    /* weapon->timer = 0; then return */
    CHROBJ_DETONATE_ARM_MAGAZINE,  /* obj->flags |= 0x10000000; then return */
    CHROBJ_DETONATE_TYPE78_INERT,  /* type 7/8 non-explosive: return, no effect (D5) */
    CHROBJ_DETONATE_CHECK_MORTAL,  /* fall to the objIsMortal gate, then damage */
    CHROBJ_DETONATE_APPLY          /* unarmed gate passed: apply damage directly */
} ChrobjDetonateAction;

/*
 * The pre-damage gate (retail ASM 7F04E168-7F04E2A8).
 *
 *   item_is_unarmed     : item == ITEM_UNARMED (0).
 *   is_collectable      : objIsCollectable(obj).
 *   flags               : obj->flags.
 *   type                : obj->type (PROPDEF_TYPE).
 *   weapon_is_explosive : type==8 && weapon_uses_explosive_timer(obj).
 *   ammo_is_explosive   : type==7 && ammo_type_uses_explosive_flag(obj).
 *   legacy              : 0 faithful, !=0 reproduce the port defect (D3/D4/D5).
 */
ChrobjDetonateAction chrobjDetonateGate(int item_is_unarmed,
                                        int is_collectable,
                                        unsigned int flags,
                                        int type,
                                        int weapon_is_explosive,
                                        int ammo_is_explosive,
                                        int legacy);

/*
 * The PROPDEF_ARMOUR collect amount (retail ASM 7F04E688-7F04E6C8, D2).
 *
 *   destroyed        : objGetDestroyedLevel(obj) != 0.
 *   initialamount    : armour->initialamount (obj+0x80).
 *   damage_threshold : obj->damage (obj+0x74).
 *   maxdamage_accum  : obj->maxdamage (obj+0x70, the accumulated damage).
 *
 * Faithful: destroyed -> 0.0; else initialamount*(threshold-accum)/threshold.
 * Legacy (port defect): the two branches swapped.
 */
float chrobjArmourCollectAmount(int destroyed, float initialamount,
                                float damage_threshold, float maxdamage_accum,
                                int legacy);

/*
 * Ammo-salvage slot predicate (retail ASM 7F04E3EC-7F04E404, D1): a slot is
 * usable iff its authored quantity is non-zero and its modelnum is a real
 * model (!= 0xFFFF). Both operands are read from the {u16 modelnum, u16
 * quantity} pair at obj+0x80+i*4.
 */
int chrobjAmmoSalvageSlotUsable(unsigned int quantity, unsigned int modelnum);

/*
 * Ammo-salvage magazine ammotype (retail ASM 7F04E4B0-7F04E4DC, D1): slot i is
 * authored for ammotype i+1, with slot 1 (i+1==2) folded into the shared 9mm
 * pool (ammotype 1).
 */
int chrobjAmmoSalvageAmmoType(int slot_index);

#ifdef __cplusplus
}
#endif

#endif /* MGB64_CHROBJ_DETONATE_H */
