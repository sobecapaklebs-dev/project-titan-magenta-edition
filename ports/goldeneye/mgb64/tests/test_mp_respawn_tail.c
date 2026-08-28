/*
 * test_mp_respawn_tail.c — ROM-free regression lane for FID-0103.
 *
 * Guards the object-respawn tail of handle_mp_respawn_and_some_things
 * (mp_respawn_tail.c). Retail runs a tail at US ASM .L7F03C8AC-.L7F03C8E4:
 * (1) for PROPDEF_ARMOUR (obj type byte == 21) it resets armour amount to
 * initialamount (obj[0x84] = obj[0x80]); (2) when the reparent marker s3 == 0 it
 * plays the object-respawn sound (sndPlaySfx 82 + chrobjSndCreatePostEventDefault).
 * The port dropped the whole tail, so regenerating objects respawned SILENTLY and
 * the armour reset was lost. Pins the retail and legacy decisions — especially the
 * type-21 armour case and the always-fire (s3==0) sound in the port — so a revert
 * or a GE007_NO_MP_RESPAWN_TAIL_FIX flip reddens.
 */
#include "mp_respawn_tail.h"

#include <stdio.h>

static int g_failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); \
        g_failures++; \
    } \
} while (0)

int main(void)
{
    /* args: mpRespawnTailPlan(obj_type, reparent_marker_s3, legacy) */

    /* The armour type constant matches the ASM `li $at, 21`. */
    CHECK(MP_RESPAWN_TAIL_ARMOUR_TYPE == 21,
          "armour type constant drifted from ASM li $at,21");

    /* retail (legacy=0), s3==0 (the port's only reachable case). */
    {
        MpRespawnTailPlan armour = mpRespawnTailPlan(21, 0, 0);
        CHECK(armour.copy_armour_amount, "retail: type 21 -> reset armour amount");
        CHECK(armour.play_respawn_sfx,   "retail: s3==0 -> play respawn sound");

        MpRespawnTailPlan other = mpRespawnTailPlan(3 /* PROPDEF_PROP */, 0, 0);
        CHECK(!other.copy_armour_amount, "retail: non-armour type -> no armour reset");
        CHECK(other.play_respawn_sfx,    "retail: s3==0 -> play respawn sound (any type)");
    }

    /* retail (legacy=0), s3==1 (retail reparent path — sound suppressed). */
    {
        MpRespawnTailPlan armour = mpRespawnTailPlan(21, 1, 0);
        CHECK(armour.copy_armour_amount, "retail: type 21 -> reset armour amount (reparent)");
        CHECK(!armour.play_respawn_sfx,  "retail: s3==1 -> NO respawn sound (reparent path)");
    }

    /* legacy (legacy=1): the port defect — the entire tail is dropped. */
    {
        MpRespawnTailPlan armour = mpRespawnTailPlan(21, 0, 1);
        CHECK(!armour.copy_armour_amount, "legacy: armour reset dropped (the bug)");
        CHECK(!armour.play_respawn_sfx,   "legacy: respawn sound dropped (silent, the bug)");

        MpRespawnTailPlan other = mpRespawnTailPlan(3, 0, 1);
        CHECK(!other.copy_armour_amount, "legacy: no armour reset");
        CHECK(!other.play_respawn_sfx,   "legacy: no respawn sound");
    }

    /* the fix flips exactly the port's reachable (s3==0) respawn moment. */
    CHECK(mpRespawnTailPlan(21, 0, 0).play_respawn_sfx != mpRespawnTailPlan(21, 0, 1).play_respawn_sfx,
          "fix changes the respawn-sound decision (s3==0)");
    CHECK(mpRespawnTailPlan(21, 0, 0).copy_armour_amount != mpRespawnTailPlan(21, 0, 1).copy_armour_amount,
          "fix changes the armour-reset decision (type 21)");

    if (g_failures == 0) {
        printf("PASS: mp_respawn_tail\n");
        return 0;
    }
    fprintf(stderr, "%d check(s) failed\n", g_failures);
    return 1;
}
