/*
 * watch_scroll_gate.h — the solo watch-inventory UP snap-scroll gate
 * (sub_GAME_7F0A5B80, retail ASM src/game/watch.c:1255-1262, VERSION_US).
 *
 * Pure (ROM-free, SDL-free) mirror of the retail up-scroll button/stick gate,
 * factored out so a ROM-free unit test can guard it (FID-0100).
 *
 * DIVERGENCE. Retail up-scroll snaps when the up C-button/d-pad is pressed OR
 * the stick is pushed fully up (>= 0x47):
 *
 *     joyGetButtonsPressedThisFrame(PLAYER_1, U_CBUTTONS|U_JPAD) [bnez .L7F0A5BC0]
 *     ... else joyGetStickY(PLAYER_1) [slti 0x47, bnez down] else -> up action
 *
 * i.e. `if (button_pressed || stick_y >= 0x47)`. The NONMATCHING port wrote it
 * with `&&` (src/game/watch.c:1121), so a plain up-button tap with the stick
 * centered no longer snap-scrolls. The symmetric DOWN block (retail ASM
 * L7F0A5C08, C src/game/watch.c:1130) correctly uses `||`, proving the up-block
 * `&&` is an asymmetric decomp transcription error. See FID-0100.
 */
#ifndef MGB64_WATCH_SCROLL_GATE_H
#define MGB64_WATCH_SCROLL_GATE_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Decide whether the watch inventory performs an UP snap-scroll this frame.
 *
 *   up_pressed : joyGetButtonsPressedThisFrame(PLAYER_1, U_CBUTTONS|U_JPAD)
 *                (nonzero => an up C-button/d-pad was pressed this frame).
 *   stick_y    : joyGetStickY(PLAYER_1) (the up-full threshold is >= 0x47).
 *   legacy     : 0 => faithful retail behavior (button OR stick, matches the
 *                down sibling); != 0 => reproduce the port defect (button AND
 *                stick) for the GE007_NO_WATCH_UPSCROLL_FIX A/B negative control.
 *
 * Returns nonzero when the up snap-scroll gate fires.
 */
int watchInvUpSnapGate(int up_pressed, int stick_y, int legacy);

#ifdef __cplusplus
}
#endif

#endif /* MGB64_WATCH_SCROLL_GATE_H */
