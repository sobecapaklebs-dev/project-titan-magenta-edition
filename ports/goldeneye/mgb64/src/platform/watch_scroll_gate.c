/*
 * watch_scroll_gate.c — see watch_scroll_gate.h.
 *
 * Retail ASM sub_GAME_7F0A5B80 up-scroll gate (src/game/watch.c:1255-1262):
 *   jal joyGetButtonsPressedThisFrame ; li a1, 0x808 (U_CBUTTONS|U_JPAD)
 *   bnez v0, .L7F0A5BC0                ; button pressed -> up action (the "then")
 *   jal joyGetStickY ; slti 0x47       ; else fall through, test stick
 *   bnez ... -> down path              ; stick_y < 0x47 -> down block
 *   (else) -> up action                ; stick_y >= 0x47 -> up action
 * The "bnez A -> then; eval B; if !B -> else; else fall to then" shape is the
 * compiler codegen for `if (A || B)`. See FID-0100.
 */
#include "watch_scroll_gate.h"

int watchInvUpSnapGate(int up_pressed, int stick_y, int legacy)
{
    if (legacy) {
        /* Port defect (src/game/watch.c:1121): `&&` — up-button tap alone with
         * the stick centered no longer snap-scrolls. */
        return (up_pressed != 0) && (stick_y >= 0x47);
    }
    /* Retail: `||` — button press OR stick fully up. Matches the down sibling. */
    return (up_pressed != 0) || (stick_y >= 0x47);
}
