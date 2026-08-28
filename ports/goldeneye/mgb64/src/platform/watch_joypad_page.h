/*
 * watch_joypad_page.h — pure (ROM-free, SDL-free) mirrors of the two per-value
 * divergences in the watch controller-settings page renderer sub_GAME_7F06359C
 * (retail ASM src/game/gun.c:16812-17840, VRAM 0x7F06359C). Factored out so a
 * ROM-free unit test can guard them (FID-0072). The third divergence (D3 phantom
 * save/restore) reuses watchmenu_hand_lifecycle.h (shared with FID-0073).
 *
 * D1  button-depress −10 offset is added to the y of the FIRST translation
 *     vector (pos, arg0 of sub_GAME_7F06351C — the D_80035D44 table copy, applied
 *     BEFORE the Y/X rotations), not the y of the post-rotation vector (rot,
 *     arg4 — the vertices copy). Retail: `add.s pos.y, pos.y, -10` at each of the
 *     7 face-button cases. The pre-fix port added it to rotN.y.
 *
 * D2  the page drives hand 0 (GUNRIGHT): `move $a0,$zero; li $a1,85 (ITEM_JOYPAD);
 *     jal sub_GAME_7F05DA8C` (7F06361C), and every hand query uses a0=0. The
 *     pre-fix port used GUNLEFT (1) throughout.
 *
 * Every helper takes a `legacy` flag: legacy==0 is faithful retail, legacy!=0
 * reproduces the pre-fix port so GE007_NO_WATCH_JOYPAD_FIX stays byte-identical
 * to the old port.
 */
#ifndef MGB64_WATCH_JOYPAD_PAGE_H
#define MGB64_WATCH_JOYPAD_PAGE_H

#ifdef __cplusplus
extern "C" {
#endif

/* GUNHAND values (mirrors of src/bondconstants.h:1345). */
#define WATCH_JOYPAD_HAND_GUNRIGHT 0
#define WATCH_JOYPAD_HAND_GUNLEFT  1

/*
 * D2 — which hand the controller page operates on.
 *   legacy 0 => GUNRIGHT (0), faithful; != 0 => GUNLEFT (1), pre-fix port.
 */
int watchJoypadPageHand(int legacy);

/*
 * D1 — apply a pressed face button's depress offset.
 *   pos_y  : &posN.y  (arg0 of sub_GAME_7F06351C; the table-copied vector).
 *   rot_y  : &rotN.y  (arg4; the vertices-copied vector).
 *   offset : the depress amount (−10.0f in retail).
 *   legacy : 0 => faithful (*pos_y += offset); != 0 => pre-fix (*rot_y += offset).
 * Exactly one of the two components is modified per call.
 */
void watchJoypadButtonDepress(float *pos_y, float *rot_y, float offset, int legacy);

#ifdef __cplusplus
}
#endif

#endif /* MGB64_WATCH_JOYPAD_PAGE_H */
