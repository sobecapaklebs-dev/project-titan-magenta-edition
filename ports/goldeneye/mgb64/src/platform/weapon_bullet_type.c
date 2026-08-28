/*
 * weapon_bullet_type.c — see weapon_bullet_type.h.
 *
 * Encodes retail jpt_weapon_bullet_type (src/game/gun.c:7076). The dispatch
 * index is item-4, so index 11 == ITEM_SHOTGUN(15) and index 12 == ITEM_AUTOSHOT(16)
 * both select weapon_bullet_type_shotgun_mine; indices 18/19 == ITEM_LASER(22)/
 * ITEM_WATCHLASER(23) select weapon_bullet_type_none; every other in-range index
 * selects weapon_bullet_type_pistol.
 */
#include "weapon_bullet_type.h"

/* ITEM_IDS ordinals (bondconstants.h) covered by this dispatch. Kept as local
 * literals so the unit test compiles ROM-free (no game headers). */
enum {
    WBT_ITEM_WPPK       = 4,
    WBT_ITEM_SHOTGUN    = 15,
    WBT_ITEM_AUTOSHOT   = 16,
    WBT_ITEM_LASER      = 22,
    WBT_ITEM_WATCHLASER = 23
};

enum WeaponBulletType weaponBulletTypeClassify(int item, int legacy_autoshot_pistol)
{
    if (item < WBT_ITEM_WPPK || item > WBT_ITEM_WATCHLASER) {
        return WEAPON_BULLET_TYPE_OTHER;
    }

    switch (item) {
    case WBT_ITEM_SHOTGUN:
        return WEAPON_BULLET_TYPE_SHOTGUN_MINE;
    case WBT_ITEM_AUTOSHOT:
        /* Retail: shotgun_mine. The port defect (FID-0052) classifies it as
         * pistol; the legacy negative control restores that for A/B. */
        return legacy_autoshot_pistol ? WEAPON_BULLET_TYPE_PISTOL
                                      : WEAPON_BULLET_TYPE_SHOTGUN_MINE;
    case WBT_ITEM_LASER:
    case WBT_ITEM_WATCHLASER:
        return WEAPON_BULLET_TYPE_NONE;
    default:
        return WEAPON_BULLET_TYPE_PISTOL;
    }
}
