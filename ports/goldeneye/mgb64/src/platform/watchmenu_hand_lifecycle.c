/*
 * watchmenu_hand_lifecycle.c — pure watch-menu hand-item exit lifecycle for
 * FID-0073 (set_enviro_fog_for_items_in_solo_watch_menu) and FID-0072
 * (sub_GAME_7F06359C). See the header for the authoritative VERSION_US ASM.
 * ROM-free.
 */
#include "watchmenu_hand_lifecycle.h"

WatchMenuHandExit watchMenuHandExitAction(int prev_item, int legacy) {
    if (legacy) {
        /* Pre-fix port: restore the captured item, or clear if none was set. */
        return prev_item >= 0 ? WATCHMENU_HAND_EXIT_RESTORE
                              : WATCHMENU_HAND_EXIT_CLEAR;
    }
    /* Retail: the single sub_GAME_7F05DA8C set stands; every exit is the bare
     * epilogue with no restore. Leave the item pinned. */
    return WATCHMENU_HAND_EXIT_NONE;
}
