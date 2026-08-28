/*
 * test_watchmenu_hand_lifecycle.c — ROM-free fail-on-revert lane for FID-0073
 * (and the shared exit lifecycle reused by FID-0072).
 *
 * Locks the retail behavior: the solo watch-menu renderers set the hand item
 * ONCE and never restore it (faithful == WATCHMENU_HAND_EXIT_NONE for every prev
 * item). The pre-fix port restored/cleared it every frame. If the phantom
 * save/restore is reintroduced, faithful == legacy and this fails.
 */
#include <stdio.h>
#include "watchmenu_hand_lifecycle.h"

int main(void) {
    int failures = 0;
    int prev;
    /* Representative prev items: no-item sentinel, boundary 0, real items, the
     * {30,23}->60 remap value, and a large id. */
    int prevs[] = {-2, -1, 0, 1, 23, 30, 60, 85, 200};
    int n = (int)(sizeof(prevs) / sizeof(prevs[0]));
    int k;

    for (k = 0; k < n; k++) {
        prev = prevs[k];

        /* Faithful: always NONE regardless of prev (retail never restores). */
        if (watchMenuHandExitAction(prev, 0) != WATCHMENU_HAND_EXIT_NONE) {
            printf("FAIL: faithful prev=%d must be NONE\n", prev);
            failures++;
        }

        /* Legacy: RESTORE when prev >= 0, CLEAR otherwise. */
        {
            WatchMenuHandExit want = (prev >= 0) ? WATCHMENU_HAND_EXIT_RESTORE
                                                 : WATCHMENU_HAND_EXIT_CLEAR;
            if (watchMenuHandExitAction(prev, 1) != want) {
                printf("FAIL: legacy prev=%d must be %d\n", prev, (int)want);
                failures++;
            }
        }

        /* Fail-on-revert: faithful must differ from legacy for every prev
         * (NONE != RESTORE and NONE != CLEAR). */
        if (watchMenuHandExitAction(prev, 0) == watchMenuHandExitAction(prev, 1)) {
            printf("FAIL: faithful == legacy at prev=%d\n", prev);
            failures++;
        }
    }

    if (failures == 0) {
        printf("test_watchmenu_hand_lifecycle: PASS "
               "(FID-0073 no phantom save/restore; leave the menu item pinned)\n");
        return 0;
    }
    printf("test_watchmenu_hand_lifecycle: FAIL (%d)\n", failures);
    return 1;
}
