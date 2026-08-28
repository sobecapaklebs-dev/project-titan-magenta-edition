/*
 * watch_ammo_switchstate.h — watch ammo-panel weapon-switch-state early-out for
 * sub_GAME_7F06A334 (src/game/gun.c). Pure (ROM-free / SDL-free) predicate,
 * factored out so a ctest can lock the retail behavior (FID-0084).
 *
 * AUTHORITY (VERSION_US retail ASM, glabel sub_GAME_7F06A334):
 *   7F06A374  lw   $t6, g_CurrentPlayer
 *   7F06A37C  lw   $v0, 0x894($t6)   ; hands[GUNRIGHT].when_detonating_mines_is_0
 *                                    ; (hands[0]@0x870 + 0x24 = 0x894, GUNRIGHT=0)
 *   7F06A380  li   $at, 6  / beq $v0,$at,.L7F06A594   ; return gdl (no draw)
 *   7F06A388  li   $at, 7  / beq $v0,$at,.L7F06A594   ; return gdl (no draw)
 * This early-out sits AFTER the ammo-type check (7F06A368) and BEFORE the
 * WEAPONSTATBITFLAG_HIDE_AMMO_DISPLAY check (7F06A390). Hand states 6/7 are the
 * weapon-switch (unequip->equip) animation (gun.c:18794 comment); retail blanks
 * the watch ammo icon + clip/reserve digits for a hand mid-switch, where the
 * displayed weapon/ammo pair is transiently stale. The NATIVE_PORT rewrite
 * (gun.c:33424-...) dropped this early-out and drew the panel through the switch.
 *
 * `legacy` toggle: 0 => faithful (hide for states 6/7); != 0 => reproduce the
 * pre-fix port (never hide) for the GE007_NO_WATCH_AMMO_SWITCH_EARLYOUT A/B
 * negative control. Same family as FID-0064 (a different dropped ammo-HUD
 * early-out).
 */
#ifndef MGB64_WATCH_AMMO_SWITCHSTATE_H
#define MGB64_WATCH_AMMO_SWITCHSTATE_H

#ifdef __cplusplus
extern "C" {
#endif

/* Returns non-zero if the watch ammo panel must be hidden because the right
 * hand is mid weapon-switch. handState = hands[GUNRIGHT].when_detonating_mines_is_0. */
int watchAmmoPanelHiddenByWeaponSwitch(int handState, int legacy);

#ifdef __cplusplus
}
#endif

#endif /* MGB64_WATCH_AMMO_SWITCHSTATE_H */
