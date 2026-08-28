/*
 * watch_joypad_page.c — pure watch controller-page per-value fixes for FID-0072
 * (D1 button-depress vector, D2 hand slot). See the header for the authoritative
 * VERSION_US ASM. D3 (phantom save/restore) reuses watchmenu_hand_lifecycle.c.
 * ROM-free.
 */
#include "watch_joypad_page.h"

int watchJoypadPageHand(int legacy) {
    if (legacy) {
        return WATCH_JOYPAD_HAND_GUNLEFT; /* pre-fix port: wrong hand */
    }
    /* Retail: move $a0,$zero -> hand 0 (GUNRIGHT). */
    return WATCH_JOYPAD_HAND_GUNRIGHT;
}

void watchJoypadButtonDepress(float *pos_y, float *rot_y, float offset, int legacy) {
    if (legacy) {
        /* Pre-fix port: offset the post-rotation (vertices) vector. */
        *rot_y += offset;
        return;
    }
    /* Retail: add.s pos.y, pos.y, -10 — offset the pre-rotation (table) vector. */
    *pos_y += offset;
}
