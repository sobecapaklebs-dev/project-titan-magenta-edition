/*
 * weapon_action_sfx.c — see weapon_action_sfx.h.
 *
 * Encodes retail jpt_80054194 (equip) and jpt_80054294 (reload) from
 * src/game/gun.c, entry-for-entry. Index == ITEM_IDS ordinal (bondconstants.h);
 * the boundary literals below are pinned against enum drift by a _Static_assert
 * at the call site in src/game/gun.c.
 *
 * Cue values (matching the retail sndPlaySfx bodies in gun.c):
 *   equip : weapon_switchstyle_NONE=0  weapon_playsfx_knife=233
 *           weapon_playsfx_F2=242  weapon_playsfx_mine=235  weapon_playsfx_gun=232
 *   reload: weapon_reload_none_sfx=0   weapon_reload_gun_sfx=50
 */
#include "weapon_action_sfx.h"

enum {
    WSFX_TABLE_COUNT = 62, /* retail bound: weapon_id < 0x3e == ITEM_BLACKBOX */
    WSFX_ITEM_TOKEN  = 88, /* ITEM_TOKEN: silent in the post-table branch     */
    WSFX_EQUIP_GUN   = 232,
    WSFX_RELOAD_GUN  = 50
};

/* jpt_80054194 — equip cue, index == ITEM_IDS ordinal 0..61. */
static const uint16_t s_equipCue[WSFX_TABLE_COUNT] = {
    /*  0 ITEM_UNARMED            */ 0,
    /*  1 ITEM_FIST               */ 0,
    /*  2 ITEM_KNIFE              */ 233,
    /*  3 ITEM_THROWKNIFE         */ 233,
    /*  4 ITEM_WPPK               */ 232,
    /*  5 ITEM_WPPKSIL            */ 232,
    /*  6 ITEM_TT33               */ 232,
    /*  7 ITEM_SKORPION           */ 232,
    /*  8 ITEM_AK47               */ 232,
    /*  9 ITEM_UZI                */ 232,
    /* 10 ITEM_MP5K               */ 232,
    /* 11 ITEM_MP5KSIL            */ 232,
    /* 12 ITEM_SPECTRE            */ 232,
    /* 13 ITEM_M16                */ 232,
    /* 14 ITEM_FNP90              */ 232,
    /* 15 ITEM_SHOTGUN            */ 232,
    /* 16 ITEM_AUTOSHOT           */ 232,
    /* 17 ITEM_SNIPERRIFLE        */ 232,
    /* 18 ITEM_RUGER              */ 232,
    /* 19 ITEM_GOLDENGUN          */ 232,
    /* 20 ITEM_SILVERWPPK         */ 232,
    /* 21 ITEM_GOLDWPPK           */ 232,
    /* 22 ITEM_LASER              */ 242,
    /* 23 ITEM_WATCHLASER         */ 0,
    /* 24 ITEM_GRENADELAUNCH      */ 232,
    /* 25 ITEM_ROCKETLAUNCH       */ 232,
    /* 26 ITEM_GRENADE            */ 0,
    /* 27 ITEM_TIMEDMINE          */ 235,
    /* 28 ITEM_PROXIMITYMINE      */ 235,
    /* 29 ITEM_REMOTEMINE         */ 235,
    /* 30 ITEM_TRIGGER            */ 0,
    /* 31 ITEM_TASER              */ 0,
    /* 32 ITEM_TANKSHELLS         */ 0,
    /* 33 ITEM_BOMBCASE           */ 0,
    /* 34 ITEM_PLASTIQUE          */ 0,
    /* 35 ITEM_FLAREPISTOL        */ 232,
    /* 36 ITEM_PITONGUN           */ 232,
    /* 37 ITEM_BUNGEE             */ 232,
    /* 38 ITEM_DOORDECODER        */ 232,
    /* 39 ITEM_BOMBDEFUSER        */ 232,
    /* 40 ITEM_CAMERA             */ 0,
    /* 41 ITEM_LOCKEXPLODER       */ 232,
    /* 42 ITEM_DOOREXPLODER       */ 232,
    /* 43 ITEM_BRIEFCASE          */ 232,
    /* 44 ITEM_WEAPONCASE         */ 232,
    /* 45 ITEM_SAFECRACKERCASE    */ 232,
    /* 46 ITEM_KEYANALYSERCASE    */ 232,
    /* 47 ITEM_BUG                */ 0,
    /* 48 ITEM_MICROCAMERA        */ 0,
    /* 49 ITEM_BUGDETECTOR        */ 232,
    /* 50 ITEM_EXPLOSIVEFLOPPY    */ 232,
    /* 51 ITEM_POLARIZEDGLASSES   */ 232,
    /* 52 ITEM_DARKGLASSES        */ 232,
    /* 53 ITEM_CREDITCARD         */ 232,
    /* 54 ITEM_GASKEYRING         */ 232,
    /* 55 ITEM_DATATHIEF          */ 232,
    /* 56 ITEM_WATCHIDENTIFIER    */ 232,
    /* 57 ITEM_WATCHCOMMUNICATOR  */ 232,
    /* 58 ITEM_WATCHGEIGERCOUNTER */ 232,
    /* 59 ITEM_WATCHMAGNETREPEL   */ 232,
    /* 60 ITEM_WATCHMAGNETATTRACT */ 0,
    /* 61 ITEM_GOLDENEYEKEY       */ 0
};

/* jpt_80054294 — reload cue, index == ITEM_IDS ordinal 0..61. */
static const uint16_t s_reloadCue[WSFX_TABLE_COUNT] = {
    /*  0 ITEM_UNARMED            */ 0,
    /*  1 ITEM_FIST               */ 0,
    /*  2 ITEM_KNIFE              */ 0,
    /*  3 ITEM_THROWKNIFE         */ 0,
    /*  4 ITEM_WPPK               */ 50,
    /*  5 ITEM_WPPKSIL            */ 50,
    /*  6 ITEM_TT33               */ 50,
    /*  7 ITEM_SKORPION           */ 50,
    /*  8 ITEM_AK47               */ 50,
    /*  9 ITEM_UZI                */ 50,
    /* 10 ITEM_MP5K               */ 50,
    /* 11 ITEM_MP5KSIL            */ 50,
    /* 12 ITEM_SPECTRE            */ 50,
    /* 13 ITEM_M16                */ 50,
    /* 14 ITEM_FNP90              */ 50,
    /* 15 ITEM_SHOTGUN            */ 50,
    /* 16 ITEM_AUTOSHOT           */ 50,
    /* 17 ITEM_SNIPERRIFLE        */ 50,
    /* 18 ITEM_RUGER              */ 50,
    /* 19 ITEM_GOLDENGUN          */ 50,
    /* 20 ITEM_SILVERWPPK         */ 50,
    /* 21 ITEM_GOLDWPPK           */ 50,
    /* 22 ITEM_LASER              */ 0,
    /* 23 ITEM_WATCHLASER         */ 0,
    /* 24 ITEM_GRENADELAUNCH      */ 50,
    /* 25 ITEM_ROCKETLAUNCH       */ 50,
    /* 26 ITEM_GRENADE            */ 0,
    /* 27 ITEM_TIMEDMINE          */ 0,
    /* 28 ITEM_PROXIMITYMINE      */ 0,
    /* 29 ITEM_REMOTEMINE         */ 0,
    /* 30 ITEM_TRIGGER            */ 0,
    /* 31 ITEM_TASER              */ 0,
    /* 32 ITEM_TANKSHELLS         */ 0,
    /* 33 ITEM_BOMBCASE           */ 0,
    /* 34 ITEM_PLASTIQUE          */ 0,
    /* 35 ITEM_FLAREPISTOL        */ 50,
    /* 36 ITEM_PITONGUN           */ 50,
    /* 37 ITEM_BUNGEE             */ 50,
    /* 38 ITEM_DOORDECODER        */ 50,
    /* 39 ITEM_BOMBDEFUSER        */ 50,
    /* 40 ITEM_CAMERA             */ 0,
    /* 41 ITEM_LOCKEXPLODER       */ 50,
    /* 42 ITEM_DOOREXPLODER       */ 50,
    /* 43 ITEM_BRIEFCASE          */ 50,
    /* 44 ITEM_WEAPONCASE         */ 50,
    /* 45 ITEM_SAFECRACKERCASE    */ 50,
    /* 46 ITEM_KEYANALYSERCASE    */ 50,
    /* 47 ITEM_BUG                */ 0,
    /* 48 ITEM_MICROCAMERA        */ 0,
    /* 49 ITEM_BUGDETECTOR        */ 50,
    /* 50 ITEM_EXPLOSIVEFLOPPY    */ 50,
    /* 51 ITEM_POLARIZEDGLASSES   */ 50,
    /* 52 ITEM_DARKGLASSES        */ 50,
    /* 53 ITEM_CREDITCARD         */ 50,
    /* 54 ITEM_GASKEYRING         */ 50,
    /* 55 ITEM_DATATHIEF          */ 50,
    /* 56 ITEM_WATCHIDENTIFIER    */ 50,
    /* 57 ITEM_WATCHCOMMUNICATOR  */ 50,
    /* 58 ITEM_WATCHGEIGERCOUNTER */ 50,
    /* 59 ITEM_WATCHMAGNETREPEL   */ 50,
    /* 60 ITEM_WATCHMAGNETATTRACT */ 0,
    /* 61 ITEM_GOLDENEYEKEY       */ 0
};

uint16_t portWeaponEquipCue(int weapon_id)
{
    if (weapon_id >= 0 && weapon_id < WSFX_TABLE_COUNT) {
        return s_equipCue[weapon_id];
    }
    if (weapon_id != WSFX_ITEM_TOKEN) {
        return WSFX_EQUIP_GUN;
    }
    return 0u;
}

uint16_t portWeaponReloadCue(int weapon_id)
{
    if (weapon_id >= 0 && weapon_id < WSFX_TABLE_COUNT) {
        return s_reloadCue[weapon_id];
    }
    if (weapon_id != WSFX_ITEM_TOKEN) {
        return WSFX_RELOAD_GUN;
    }
    return 0u;
}
