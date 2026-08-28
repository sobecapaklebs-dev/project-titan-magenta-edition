/*
 * weapon_action_sfx.h — weapon equip/reload SFX cue selectors.
 *
 * Pure (ROM-free, SDL-free) mirror of the two retail jump tables embedded in
 * src/game/gun.c: jpt_80054194 (weapon-raise / equip cue) and jpt_80054294
 * (reload cue). Factored out of the state-8 / state-11 handlers so a ROM-free
 * unit test can guard the mapping entry-for-entry [AUDIT-0028].
 *
 * The retail consumers (gun.c ASM at 7F0663C4 equip / 7F066740 reload) select:
 *   if (weapon_id < 0x3e)  -> table[weapon_id]        (indices 0..61)
 *   else if (weapon_id != ITEM_TOKEN(88)) -> gun cue  (232 equip / 50 reload)
 *   else (ITEM_TOKEN)      -> silence
 * 0x3e == 62 == ITEM_BLACKBOX, so the table covers ITEM_GOLDENEYEKEY(61)=silent.
 *
 * Cue ids are sndPlaySfx sound indices; 0 is the silence sentinel (retail
 * weapon_switchstyle_NONE / weapon_reload_none_sfx) and is never a real cue in
 * either table, so the caller must skip sndPlaySfx when the resolver returns 0.
 */
#ifndef MGB64_WEAPON_ACTION_SFX_H
#define MGB64_WEAPON_ACTION_SFX_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Equip (weapon-raise) cue for `weapon_id` per jpt_80054194.
 * Returns 232 (gun), 233 (knife), 235 (mine), 242 (F2/laser), or 0 (silence). */
uint16_t portWeaponEquipCue(int weapon_id);

/* Reload cue for `weapon_id` per jpt_80054294.
 * Returns 50 (gun) or 0 (silence). */
uint16_t portWeaponReloadCue(int weapon_id);

#ifdef __cplusplus
}
#endif

#endif /* MGB64_WEAPON_ACTION_SFX_H */
