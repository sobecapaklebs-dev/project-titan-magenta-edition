/*
 * test_weapon_action_sfx.c — ROM-free regression lane for AUDIT-0028.
 *
 * Guards portWeaponEquipCue / portWeaponReloadCue against retail jpt_80054194
 * (equip) and jpt_80054294 (reload) from src/game/gun.c, entry-for-entry.
 *
 * The pre-fix port hand-grouped item names into broad switch cases with an
 * audible default and used an off-by-one guard (weapon_id < ITEM_GOLDENEYEKEY
 * instead of < ITEM_BLACKBOX). This test independently re-encodes the two retail
 * tables (indices 0..61) plus the post-table branch and fails if any resolved
 * cue diverges. No assert(): failures are counted and main() returns nonzero.
 */
#include "weapon_action_sfx.h"

#include <stdio.h>
#include <stdint.h>

/* Retail bound / sentinels (bondconstants.h ITEM_IDS ordinals). */
enum {
    T_ITEM_COUNT = 62, /* < ITEM_BLACKBOX(62): table range */
    T_ITEM_TOKEN = 88, /* ITEM_TOKEN: silent post-table    */
    T_GRENADE = 26, T_REMOTEMINE = 29, T_TANKSHELLS = 32, T_PLASTIQUE = 34,
    T_BOMBDEFUSER = 39, T_CAMERA = 40, T_EXPLOSIVEFLOPPY = 50,
    T_WATCHMAGNETATTRACT = 60, T_GOLDENEYEKEY = 61, T_BLACKBOX = 62,
    T_KNIFE = 2, T_LASER = 22, T_WATCHLASER = 23, T_WPPK = 4, T_ROCKETLAUNCH = 25,
    T_TIMEDMINE = 27
};

/* Authoritative expected tables, transcribed from jpt_80054194 / jpt_80054294
 * (src/game/gun.c). Duplicated here on purpose so a change to the resolver's
 * arrays cannot silently match a co-edited expectation. */
static const uint16_t k_equip[T_ITEM_COUNT] = {
    0,0,233,233,232,232,232,232,232,232,232,232,232,232,232,232,232,232,232,232,
    232,232,242,0,232,232,0,235,235,235,0,0,0,0,0,232,232,232,232,232,0,232,232,
    232,232,232,232,0,0,232,232,232,232,232,232,232,232,232,232,232,0,0
};
static const uint16_t k_reload[T_ITEM_COUNT] = {
    0,0,0,0,50,50,50,50,50,50,50,50,50,50,50,50,50,50,50,50,50,50,0,0,50,50,0,0,0,
    0,0,0,0,0,0,50,50,50,50,50,0,50,50,50,50,50,50,0,0,50,50,50,50,50,50,50,50,50,
    50,50,0,0
};

static int g_failures = 0;

#define CHECK_EQ(got, want, ctx, idx) do { \
    unsigned _g = (unsigned)(got), _w = (unsigned)(want); \
    if (_g != _w) { \
        fprintf(stderr, "FAIL: %s[%d] resolved %u, expected %u (%s:%d)\n", \
                (ctx), (int)(idx), _g, _w, __FILE__, __LINE__); \
        g_failures++; \
    } \
} while (0)

int main(void)
{
    int i;

    /* --- Whole in-range table matches retail entry-for-entry. --- */
    for (i = 0; i < T_ITEM_COUNT; i++) {
        CHECK_EQ(portWeaponEquipCue(i), k_equip[i], "equip", i);
        CHECK_EQ(portWeaponReloadCue(i), k_reload[i], "reload", i);
    }

    /* --- Named divergences the audit called out (RED on the old switches). --- */
    /* Grenade equip: retail NONE (old port played mine 235). */
    CHECK_EQ(portWeaponEquipCue(T_GRENADE), 0, "equip-grenade", T_GRENADE);
    /* Remote mine equip: retail mine 235 (old port was silent). */
    CHECK_EQ(portWeaponEquipCue(T_REMOTEMINE), 235, "equip-remotemine", T_REMOTEMINE);
    /* Rocket launcher / bomb defuser / explosive floppy equip: retail gun 232
     * (old port was silent). */
    CHECK_EQ(portWeaponEquipCue(T_ROCKETLAUNCH), 232, "equip-rocket", T_ROCKETLAUNCH);
    CHECK_EQ(portWeaponEquipCue(T_BOMBDEFUSER), 232, "equip-bombdefuser", T_BOMBDEFUSER);
    CHECK_EQ(portWeaponEquipCue(T_EXPLOSIVEFLOPPY), 232, "equip-floppy", T_EXPLOSIVEFLOPPY);
    /* WatchLaser / Camera equip: retail NONE (old port defaulted to gun 232). */
    CHECK_EQ(portWeaponEquipCue(T_WATCHLASER), 0, "equip-watchlaser", T_WATCHLASER);
    CHECK_EQ(portWeaponEquipCue(T_CAMERA), 0, "equip-camera", T_CAMERA);
    /* Reload: TankShells / Plastique / Camera / WatchMagnetAttract retail NONE
     * (old port defaulted to gun 50). */
    CHECK_EQ(portWeaponReloadCue(T_TANKSHELLS), 0, "reload-tankshells", T_TANKSHELLS);
    CHECK_EQ(portWeaponReloadCue(T_PLASTIQUE), 0, "reload-plastique", T_PLASTIQUE);
    CHECK_EQ(portWeaponReloadCue(T_CAMERA), 0, "reload-camera", T_CAMERA);
    CHECK_EQ(portWeaponReloadCue(T_WATCHMAGNETATTRACT), 0, "reload-watchmag", T_WATCHMAGNETATTRACT);
    /* Reload: BombDefuser / ExplosiveFloppy retail gun 50 (old port silent). */
    CHECK_EQ(portWeaponReloadCue(T_BOMBDEFUSER), 50, "reload-bombdefuser", T_BOMBDEFUSER);
    CHECK_EQ(portWeaponReloadCue(T_EXPLOSIVEFLOPPY), 50, "reload-floppy", T_EXPLOSIVEFLOPPY);

    /* --- Off-by-one boundary: GOLDENEYEKEY(61) is IN the table = NONE (old port
     *     routed it to the audible else-if branch, 232 equip / 50 reload). --- */
    CHECK_EQ(portWeaponEquipCue(T_GOLDENEYEKEY), 0, "equip-goldeneyekey", T_GOLDENEYEKEY);
    CHECK_EQ(portWeaponReloadCue(T_GOLDENEYEKEY), 0, "reload-goldeneyekey", T_GOLDENEYEKEY);

    /* --- Positive controls retained. --- */
    CHECK_EQ(portWeaponEquipCue(T_KNIFE), 233, "equip-knife", T_KNIFE);
    CHECK_EQ(portWeaponEquipCue(T_LASER), 242, "equip-laser", T_LASER);
    CHECK_EQ(portWeaponEquipCue(T_WPPK), 232, "equip-wppk", T_WPPK);
    CHECK_EQ(portWeaponEquipCue(T_TIMEDMINE), 235, "equip-timedmine", T_TIMEDMINE);
    CHECK_EQ(portWeaponReloadCue(T_WPPK), 50, "reload-wppk", T_WPPK);

    /* --- Post-table branch: >= ITEM_BLACKBOX(62) && != TOKEN -> gun; TOKEN silent. --- */
    CHECK_EQ(portWeaponEquipCue(T_BLACKBOX), 232, "equip-blackbox", T_BLACKBOX);
    CHECK_EQ(portWeaponReloadCue(T_BLACKBOX), 50, "reload-blackbox", T_BLACKBOX);
    CHECK_EQ(portWeaponEquipCue(87), 232, "equip-87", 87);
    CHECK_EQ(portWeaponReloadCue(87), 50, "reload-87", 87);
    CHECK_EQ(portWeaponEquipCue(T_ITEM_TOKEN), 0, "equip-token", T_ITEM_TOKEN);
    CHECK_EQ(portWeaponReloadCue(T_ITEM_TOKEN), 0, "reload-token", T_ITEM_TOKEN);
    CHECK_EQ(portWeaponEquipCue(200), 232, "equip-oob-high", 200);
    CHECK_EQ(portWeaponReloadCue(200), 50, "reload-oob-high", 200);
    /* Negative id: retail signed/unsigned pair routes to gun (never TOKEN). */
    CHECK_EQ(portWeaponEquipCue(-1), 232, "equip-neg", -1);
    CHECK_EQ(portWeaponReloadCue(-1), 50, "reload-neg", -1);

    if (g_failures == 0) {
        printf("test_weapon_action_sfx: OK\n");
        return 0;
    }
    fprintf(stderr, "test_weapon_action_sfx: %d failure(s)\n", g_failures);
    return 1;
}
