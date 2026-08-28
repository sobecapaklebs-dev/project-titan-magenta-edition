/*
 * test_weapon_bullet_type.c — ROM-free regression lane for FID-0052.
 *
 * Guards the Automatic Shotgun (AUTOSHOT) firing-dispatch grouping. Retail's
 * jpt_weapon_bullet_type (src/game/gun.c:7076) routes AUTOSHOT to
 * weapon_bullet_type_shotgun_mine (no pre-tail action) exactly like the pump
 * SHOTGUN; the NONMATCHING port put AUTOSHOT in the pistol group
 * (sub_GAME_7F061BF4 + field_8A0++), applying pistol recoil + shot-count the
 * retail Automatic Shotgun never gets.
 *
 * Fails on revert: if weaponBulletTypeClassify() is changed to classify AUTOSHOT
 * as PISTOL at the faithful default (legacy=0), the AUTOSHOT==SHOTGUN_MINE
 * assertion goes red. The legacy negative control (legacy=1) must still
 * reproduce the buggy pistol grouping so the A/B flag stays byte-identical.
 */
#include "weapon_bullet_type.h"

#include <stdio.h>

/* ITEM_IDS ordinals (bondconstants.h). */
enum {
    ITEM_UNARMED = 0, ITEM_FIST, ITEM_KNIFE, ITEM_THROWKNIFE,
    ITEM_WPPK, ITEM_WPPKSIL, ITEM_TT33, ITEM_SKORPION, ITEM_AK47, ITEM_UZI,
    ITEM_MP5K, ITEM_MP5KSIL, ITEM_SPECTRE, ITEM_M16, ITEM_FNP90, ITEM_SHOTGUN,
    ITEM_AUTOSHOT, ITEM_SNIPERRIFLE, ITEM_RUGER, ITEM_GOLDENGUN, ITEM_SILVERWPPK,
    ITEM_GOLDWPPK, ITEM_LASER, ITEM_WATCHLASER, ITEM_GRENADELAUNCH, ITEM_ROCKETLAUNCH
};

static int g_failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); \
        g_failures++; \
    } \
} while (0)

/* Faithful (retail) expected class per item, for the whole dispatch range. */
static enum WeaponBulletType expect_retail(int item)
{
    switch (item) {
    case ITEM_SHOTGUN:
    case ITEM_AUTOSHOT:
        return WEAPON_BULLET_TYPE_SHOTGUN_MINE;
    case ITEM_LASER:
    case ITEM_WATCHLASER:
        return WEAPON_BULLET_TYPE_NONE;
    default:
        if (item >= ITEM_WPPK && item <= ITEM_WATCHLASER)
            return WEAPON_BULLET_TYPE_PISTOL;
        return WEAPON_BULLET_TYPE_OTHER;
    }
}

int main(void)
{
    int item;

    /* --- The fix: AUTOSHOT is shotgun_mine at the faithful default, exactly
     *     like the pump SHOTGUN (retail jpt_weapon_bullet_type[12]==[11]). --- */
    CHECK(weaponBulletTypeClassify(ITEM_AUTOSHOT, 0) == WEAPON_BULLET_TYPE_SHOTGUN_MINE,
          "AUTOSHOT (fix) -> shotgun_mine, not pistol");
    CHECK(weaponBulletTypeClassify(ITEM_SHOTGUN, 0) == WEAPON_BULLET_TYPE_SHOTGUN_MINE,
          "SHOTGUN -> shotgun_mine");

    /* --- The negative control (GE007_NO_AUTOSHOT_BULLETTYPE_FIX): AUTOSHOT
     *     reverts to the buggy pistol grouping so the flag is byte-identical
     *     to the pre-fix port. --- */
    CHECK(weaponBulletTypeClassify(ITEM_AUTOSHOT, 1) == WEAPON_BULLET_TYPE_PISTOL,
          "AUTOSHOT (legacy) -> pistol (reproduces port defect)");
    /* legacy flag must ONLY move AUTOSHOT — SHOTGUN stays shotgun_mine. */
    CHECK(weaponBulletTypeClassify(ITEM_SHOTGUN, 1) == WEAPON_BULLET_TYPE_SHOTGUN_MINE,
          "SHOTGUN unaffected by legacy flag");

    /* --- Spot checks the rest of the table matches retail groupings. --- */
    CHECK(weaponBulletTypeClassify(ITEM_WPPK, 0) == WEAPON_BULLET_TYPE_PISTOL, "WPPK -> pistol");
    CHECK(weaponBulletTypeClassify(ITEM_M16, 0) == WEAPON_BULLET_TYPE_PISTOL, "M16 -> pistol");
    CHECK(weaponBulletTypeClassify(ITEM_RUGER, 0) == WEAPON_BULLET_TYPE_PISTOL, "RUGER -> pistol");
    CHECK(weaponBulletTypeClassify(ITEM_LASER, 0) == WEAPON_BULLET_TYPE_NONE, "LASER -> none");
    CHECK(weaponBulletTypeClassify(ITEM_WATCHLASER, 0) == WEAPON_BULLET_TYPE_NONE, "WATCHLASER -> none");

    /* --- Out of dispatch range -> OTHER (no pre-tail action). --- */
    CHECK(weaponBulletTypeClassify(ITEM_KNIFE, 0) == WEAPON_BULLET_TYPE_OTHER, "KNIFE out of range");
    CHECK(weaponBulletTypeClassify(ITEM_GRENADELAUNCH, 0) == WEAPON_BULLET_TYPE_OTHER,
          "GRENADELAUNCH out of range");
    CHECK(weaponBulletTypeClassify(ITEM_ROCKETLAUNCH, 0) == WEAPON_BULLET_TYPE_OTHER,
          "ROCKETLAUNCH out of range");

    /* --- Whole in-range table matches retail at the faithful default. --- */
    for (item = ITEM_WPPK; item <= ITEM_WATCHLASER; item++) {
        CHECK(weaponBulletTypeClassify(item, 0) == expect_retail(item),
              "in-range item matches retail jpt_weapon_bullet_type");
    }

    /* --- Byte-identity contract: under the legacy opt-out the table must equal
     *     retail for EVERY item except AUTOSHOT (the one reverted defect). A
     *     future change that made the legacy flag perturb any other weapon goes
     *     red here — guards the "opt-out is byte-identical to the pre-fix port"
     *     claim across the whole table, not just AUTOSHOT/SHOTGUN. --- */
    for (item = ITEM_WPPK; item <= ITEM_WATCHLASER; item++) {
        enum WeaponBulletType want =
            (item == ITEM_AUTOSHOT) ? WEAPON_BULLET_TYPE_PISTOL : expect_retail(item);
        CHECK(weaponBulletTypeClassify(item, 1) == want,
              "legacy flag moves ONLY AUTOSHOT; every other item byte-identical");
    }

    if (g_failures == 0) {
        printf("test_weapon_bullet_type: OK\n");
        return 0;
    }
    fprintf(stderr, "test_weapon_bullet_type: %d failure(s)\n", g_failures);
    return 1;
}
