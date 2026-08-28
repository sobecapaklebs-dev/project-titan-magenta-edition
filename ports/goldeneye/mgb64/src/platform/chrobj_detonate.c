/*
 * chrobj_detonate.c — see chrobj_detonate.h.
 *
 * Faithful reconstruction of three decision points in the retail ASM body of
 * maybe_detonate_object (src/game/chrobjhandler.c:38029-38459, VRAM
 * 0x7F04E108). The arithmetic and branch senses match the ASM exactly so the
 * legacy (opt-out) path stays byte-identical to the pre-fix port.
 */
#include "chrobj_detonate.h"

ChrobjDetonateAction chrobjDetonateGate(int item_is_unarmed,
                                        int is_collectable,
                                        unsigned int flags,
                                        int type,
                                        int weapon_is_explosive,
                                        int ammo_is_explosive,
                                        int legacy)
{
    if (item_is_unarmed) {
        if (is_collectable) {
            /* Collectable-unarmed gate — correct in retail AND the old port
             * (ASM 7F04E188 `sll $t6,$t5,8` + `bltzl`): proceed iff
             * PROPFLAG_00800000 is SET, else skip. */
            if (!(flags & CHROBJ_DETONATE_FLAG_COLLECT_UNARMED)) {
                return CHROBJ_DETONATE_SKIP;
            }
        } else {
            /* D3 — non-collectable unarmed gate. Retail (ASM 7F04E1A4
             * `sll $t9,$t7,7` + `bgezl`) proceeds iff PROPFLAG_01000000 is
             * CLEAR (skip iff SET). The port inverted this (skip iff CLEAR). */
            if (legacy) {
                if (!(flags & CHROBJ_DETONATE_FLAG_UNARMED_DISABLE)) {
                    return CHROBJ_DETONATE_SKIP;
                }
            } else {
                if (flags & CHROBJ_DETONATE_FLAG_UNARMED_DISABLE) {
                    return CHROBJ_DETONATE_SKIP;
                }
            }
        }
        /* Retail branches straight to damage application (ASM -> L7F04E2A8),
         * skipping the objIsMortal gate. */
        return CHROBJ_DETONATE_APPLY;
    }

    /* Armed path (item != ITEM_UNARMED). */

    /* D4 — retail returns first thing on the armed path when PROPFLAG_INVINCIBLE
     * is set (ASM 7F04E1BC `sll $t8,$v1,0xe` + `bltzl` -> bare epilogue), BEFORE
     * the type-7/8 dispatch. The port had no such early-out. */
    if (!legacy && (flags & CHROBJ_DETONATE_FLAG_INVINCIBLE)) {
        return CHROBJ_DETONATE_SKIP;
    }

    if (type == CHROBJ_DETONATE_TYPE_COLLECTABLE) { /* 8 — dropped weapon */
        if (weapon_is_explosive) {
            /* ASM 7F04E224 `sh $zero,0x82` then return. */
            return CHROBJ_DETONATE_ARM_WEAPON;
        }
        /* D5 — retail returns unconditionally for a non-explosive weapon
         * (ASM `bnel $v0,$at,.L7F04E710` -> bare epilogue). The port fell
         * through to objIsMortal+damage. */
        return legacy ? CHROBJ_DETONATE_CHECK_MORTAL : CHROBJ_DETONATE_TYPE78_INERT;
    }

    if (type == CHROBJ_DETONATE_TYPE_MAGAZINE) { /* 7 — single ammo magazine */
        if (ammo_is_explosive) {
            /* ASM 7F04E284 `or $t1,$v1,0x10000000; sw` then return. */
            return CHROBJ_DETONATE_ARM_MAGAZINE;
        }
        /* D5 — retail returns unconditionally for a non-explosive ammotype
         * (ASM `bne $v0,$at,.L7F04E70C` -> bare epilogue). */
        return legacy ? CHROBJ_DETONATE_CHECK_MORTAL : CHROBJ_DETONATE_TYPE78_INERT;
    }

    /* Any other armed type falls to the objIsMortal gate (ASM 7F04E294). */
    return CHROBJ_DETONATE_CHECK_MORTAL;
}

float chrobjArmourCollectAmount(int destroyed, float initialamount,
                                float damage_threshold, float maxdamage_accum,
                                int legacy)
{
    /* Retail (ASM 7F04E688-7F04E6C8): `bnezl` on objGetDestroyedLevel takes the
     * amount=0.0 branch when DESTROYED (delay-slot `mtc1 $zero,$f8`); the
     * fall-through (NOT destroyed) computes
     *   initialamount * (damage - maxdamage) / damage. */
    if (legacy) {
        /* Pre-fix port had the two branches swapped. */
        if (destroyed) {
            return initialamount * (damage_threshold - maxdamage_accum) / damage_threshold;
        }
        return 0.0f;
    }
    if (destroyed) {
        return 0.0f;
    }
    return initialamount * (damage_threshold - maxdamage_accum) / damage_threshold;
}

int chrobjAmmoSalvageSlotUsable(unsigned int quantity, unsigned int modelnum)
{
    /* ASM 7F04E3F0 `blezl` on the zero-extended u16 quantity (fires only at 0),
     * then 7F04E400 `beql` skip when modelnum == 0xFFFF. */
    return quantity != 0u && modelnum != 0xFFFFu;
}

int chrobjAmmoSalvageAmmoType(int slot_index)
{
    /* ASM 7F04E4B0 `addiu $a1,$a0,1`; `bne $a1,2` else `li $t7,1`. */
    int ammotype = slot_index + 1;
    if (ammotype == 2) {
        ammotype = 1;
    }
    return ammotype;
}
