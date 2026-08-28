/* sys_hotkey.c — see sys_hotkey.h. Pure, SDL-free implementation of the three
 * system-hotkey predicates (AUDIT-0024 core). No <SDL.h>: the keycode ceiling
 * (SYS_KEY_MAX) and the gamepad button count live here as plain constants. */
#include "sys_hotkey.h"

int sysKeyValid(int keycode) {
    /* SDLK_UNKNOWN (0) and negatives are not usable hotkeys; SYS_KEY_MAX is the
     * top of SDL's keycode range. */
    return keycode > 0 && keycode <= SYS_KEY_MAX;
}

int sysKeyMutualConflict(int candidate_keycode, int other_keycode) {
    /* Menu-toggle and fps-toggle share one keycode namespace, so the only
     * cross-hotkey hazard is binding both to the same key: equality == conflict.
     * The validity guard makes invalid inputs non-conflicting (e.g. two unset 0
     * keys don't collide) — rejecting invalid keys is sysKeyValid()'s job, not
     * this predicate's. Because a conflict requires equality, checking the
     * candidate's validity suffices (an equal `other` has the same validity). */
    return candidate_keycode == other_keycode && sysKeyValid(candidate_keycode);
}

/* Mirror of kGpButtonName in src/platform/input_bindings.c (SDL 2.32:
 * SDL_CONTROLLER_BUTTON_MAX == 21). Duplicated here so the accessor is testable
 * without pulling <SDL.h> (and its transitive input_actions.h / savedir.h) in
 * through input_bindings.c. KEEP IN SYNC with that table on any SDL button-enum
 * bump — the SYNC comments are the guard for source-side edits;
 * tests/test_sys_hotkey.c pins all 21 entries but only against THIS mirror
 * (one-directional: it cannot detect a change to the source table). */
#define SYS_GP_BUTTON_MAX 21
static const char *const kGpButtonNameMirror[SYS_GP_BUTTON_MAX] = {
    "A", "B", "X", "Y", "Back", "Guide", "Start",
    "Left Stick Click", "Right Stick Click",
    "Left Bumper", "Right Bumper",
    "D-Pad Up", "D-Pad Down", "D-Pad Left", "D-Pad Right",
    "Share",                                        /* MISC1 */
    "Paddle 1", "Paddle 2", "Paddle 3", "Paddle 4", /* Elite paddles */
    "Touchpad",
};

const char *gamepadButtonName(int idx) {
    if (idx < 0 || idx >= SYS_GP_BUTTON_MAX) return "";
    return kGpButtonNameMirror[idx];
}
