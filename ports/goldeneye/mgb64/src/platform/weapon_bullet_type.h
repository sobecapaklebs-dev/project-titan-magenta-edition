/*
 * weapon_bullet_type.h — per-shot "bullet type" dispatch class for the
 * first-person fire path (handles_firing_or_throwing_weapon_in_hand).
 *
 * Pure (ROM-free, SDL-free) mirror of the retail jump table jpt_weapon_bullet_type
 * (src/game/gun.c:7076, dispatch idx = item-4, bounds < 0x14), factored out so a
 * ROM-free unit test can guard the grouping (FID-0052). The retail table routes:
 *   - weapon_bullet_type_pistol       : sub_GAME_7F061BF4(hand) + field_8A0++
 *   - weapon_bullet_type_shotgun_mine : neither (straight to the common fire tail)
 *   - weapon_bullet_type_none         : sub_GAME_7F061BF4(hand) only
 * The NONMATCHING port placed the Automatic Shotgun (AUTOSHOT) in the pistol
 * group, so it applied pistol recoil (sub_GAME_7F061BF4) and the shot counter
 * (field_8A0++) that retail — which maps AUTOSHOT to shotgun_mine like the pump
 * SHOTGUN — does not. See FID-0052.
 */
#ifndef MGB64_WEAPON_BULLET_TYPE_H
#define MGB64_WEAPON_BULLET_TYPE_H

#ifdef __cplusplus
extern "C" {
#endif

enum WeaponBulletType {
    WEAPON_BULLET_TYPE_OTHER = 0,    /* outside the dispatch range: no pre-tail action */
    WEAPON_BULLET_TYPE_PISTOL,       /* sub_GAME_7F061BF4(hand) + field_8A0++          */
    WEAPON_BULLET_TYPE_SHOTGUN_MINE, /* no pre-tail action                              */
    WEAPON_BULLET_TYPE_NONE          /* sub_GAME_7F061BF4(hand) only                    */
};

/*
 * Classify `item` (an ITEM_IDS ordinal from bondconstants.h) the way retail's
 * jpt_weapon_bullet_type does for item in [ITEM_WPPK(4), ITEM_WATCHLASER(23)].
 * Items outside that range return WEAPON_BULLET_TYPE_OTHER.
 *
 * `legacy_autoshot_pistol` != 0 reproduces the port defect (AUTOSHOT classified
 * as PISTOL) for A/B negative control; 0 gives the faithful retail grouping
 * (AUTOSHOT == SHOTGUN_MINE).
 */
enum WeaponBulletType weaponBulletTypeClassify(int item, int legacy_autoshot_pistol);

#ifdef __cplusplus
}
#endif

#endif /* MGB64_WEAPON_BULLET_TYPE_H */
