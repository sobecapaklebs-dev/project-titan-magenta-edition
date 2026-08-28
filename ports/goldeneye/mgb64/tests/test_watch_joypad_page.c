/*
 * test_watch_joypad_page.c — ROM-free fail-on-revert lane for FID-0072 (watch
 * controller-settings page). Locks the two per-value fixes:
 *   D1 the face-button depress −10 offset lands on posN.y (arg0, table copy),
 *      not rotN.y (arg4, vertices);
 *   D2 the page drives hand 0 (GUNRIGHT), not GUNLEFT.
 * The shared D3 exit lifecycle is covered by test_watchmenu_hand_lifecycle.
 */
#include <stdio.h>
#include "watch_joypad_page.h"

static int approx(float a, float b) {
    float d = a - b;
    if (d < 0) d = -d;
    return d < 1e-6f;
}

int main(void) {
    int failures = 0;

    /* D2 — hand. */
    if (watchJoypadPageHand(0) != WATCH_JOYPAD_HAND_GUNRIGHT) {
        printf("FAIL: faithful hand must be GUNRIGHT (0)\n");
        failures++;
    }
    if (watchJoypadPageHand(1) != WATCH_JOYPAD_HAND_GUNLEFT) {
        printf("FAIL: legacy hand must be GUNLEFT (1)\n");
        failures++;
    }
    if (watchJoypadPageHand(0) == watchJoypadPageHand(1)) {
        printf("FAIL: faithful hand == legacy hand\n");
        failures++;
    }

    /* D1 — faithful moves pos_y, leaves rot_y untouched. */
    {
        float pos_y = 0.0f, rot_y = 0.0f;
        watchJoypadButtonDepress(&pos_y, &rot_y, -10.0f, 0);
        if (!approx(pos_y, -10.0f) || !approx(rot_y, 0.0f)) {
            printf("FAIL: faithful must move pos_y to -10, rot_y stays 0 "
                   "(got pos=%g rot=%g)\n", pos_y, rot_y);
            failures++;
        }
    }

    /* D1 — legacy moves rot_y, leaves pos_y untouched (pre-fix port). */
    {
        float pos_y = 0.0f, rot_y = 0.0f;
        watchJoypadButtonDepress(&pos_y, &rot_y, -10.0f, 1);
        if (!approx(rot_y, -10.0f) || !approx(pos_y, 0.0f)) {
            printf("FAIL: legacy must move rot_y to -10, pos_y stays 0 "
                   "(got pos=%g rot=%g)\n", pos_y, rot_y);
            failures++;
        }
    }

    /* Fail-on-revert: faithful and legacy write different components, so from
     * the same start their (pos_y, rot_y) results must differ. */
    {
        float fp = 0.0f, fr = 0.0f, lp = 0.0f, lr = 0.0f;
        watchJoypadButtonDepress(&fp, &fr, -10.0f, 0);
        watchJoypadButtonDepress(&lp, &lr, -10.0f, 1);
        if (approx(fp, lp) && approx(fr, lr)) {
            printf("FAIL: faithful depress == legacy depress\n");
            failures++;
        }
    }

    if (failures == 0) {
        printf("test_watch_joypad_page: PASS "
               "(FID-0072 depress on pos not rot; hand GUNRIGHT not GUNLEFT)\n");
        return 0;
    }
    printf("test_watch_joypad_page: FAIL (%d)\n", failures);
    return 1;
}
