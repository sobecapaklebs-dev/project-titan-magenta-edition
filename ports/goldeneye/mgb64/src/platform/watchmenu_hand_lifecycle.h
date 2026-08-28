/*
 * watchmenu_hand_lifecycle.h — pure (ROM-free, SDL-free) mirror of the watch-menu
 * hand-item exit lifecycle shared by the two solo watch-menu renderers:
 *
 *   set_enviro_fog_for_items_in_solo_watch_menu  (FID-0073, VRAM 0x7F063004)
 *   sub_GAME_7F06359C (controller page)          (FID-0072, VRAM 0x7F06359C)
 *
 * In both retail bodies the hand's watch-menu item is set exactly ONCE (a single
 * jal sub_GAME_7F05DA8C) and never restored: every exit path falls to the bare
 * epilogue, and jal sub_GAME_7F05DAE4 appears in ZERO GLOBAL_ASM blocks anywhere.
 * The highlighted item is left pinned in the hand for the frame; teardown happens
 * elsewhere (bondview.c:10258/10347).
 *
 * The pre-fix port captured hands[hand].weaponnum_watchmenu on entry and, on
 * every exit, restored it (sub_GAME_7F05DA8C(hand, prev)) or cleared it
 * (sub_GAME_7F05DAE4(hand)) — a phantom per-frame save/restore that reverted the
 * menu item within the same draw call, so consumers (bondview gun raise/lower
 * model-swap FSM) saw the equipped weapon instead of the menu item.
 *
 * The `legacy` flag reproduces that pre-fix behavior for the negative controls
 * GE007_NO_WATCHMENU_FOG_ITEM_FIX (FID-0073) / GE007_NO_WATCH_JOYPAD_FIX
 * (FID-0072), byte-identically to the old port.
 */
#ifndef MGB64_WATCHMENU_HAND_LIFECYCLE_H
#define MGB64_WATCHMENU_HAND_LIFECYCLE_H

#ifdef __cplusplus
extern "C" {
#endif

/* What the renderer must do with the hand's watch-menu item on exit. */
typedef enum WatchMenuHandExit {
    WATCHMENU_HAND_EXIT_NONE = 0, /* faithful: leave the item pinned (no call) */
    WATCHMENU_HAND_EXIT_RESTORE,  /* legacy: sub_GAME_7F05DA8C(hand, prev_item) */
    WATCHMENU_HAND_EXIT_CLEAR     /* legacy: sub_GAME_7F05DAE4(hand)            */
} WatchMenuHandExit;

/*
 * Decide the exit action for the hand's watch-menu item.
 *
 *   prev_item : the hands[hand].weaponnum_watchmenu captured on entry (signed;
 *               < 0 means "no item was pending").
 *   legacy    : 0 => faithful retail (never restore, always NONE);
 *               != 0 => reproduce the pre-fix port save/restore
 *               (prev_item >= 0 ? RESTORE : CLEAR).
 */
WatchMenuHandExit watchMenuHandExitAction(int prev_item, int legacy);

#ifdef __cplusplus
}
#endif

#endif /* MGB64_WATCHMENU_HAND_LIFECYCLE_H */
