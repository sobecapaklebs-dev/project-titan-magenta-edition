/*
 * test_sys_hotkey.c — ROM-free/SDL-free unit test for the AUDIT-0024 pure core.
 *
 * Covers the three predicates in sys_hotkey.c:
 *   - sysKeyValid: a system-hotkey keycode must be strictly positive and within
 *     SDL's keycode range [1, SYS_KEY_MAX].
 *   - sysKeyMutualConflict: the menu-toggle and fps-toggle keys share one keycode
 *     namespace and must differ (equality == conflict); invalid inputs are
 *     non-conflicting even when equal (validity is sysKeyValid's job).
 *   - gamepadButtonName: bounds-checked reader over the button-name table
 *     mirrored in sys_hotkey.c; ALL 21 names are pinned as literals copied from
 *     kGpButtonName in input_bindings.c. NOTE this guard is ONE-DIRECTIONAL:
 *     it catches edits to the MIRROR (sys_hotkey.c) only — a change to the
 *     source table in input_bindings.c is invisible here and is guarded by the
 *     SYNC comments at both sites, not by this test.
 *
 * Namespace hazard (baked in): gameplay keyboard bindings are SDL SCANCODES,
 * these system hotkeys are SDL KEYCODES. Cross-namespace keyboard conflict needs
 * SDL_GetScancodeFromKey (an SDL call) and is intentionally out of this pure core.
 *
 * SDL keycode/button values are local literals (stable ABI) so this stays
 * SDL-free. NDEBUG strips assert(), so failures are counted and returned nonzero.
 */
#include "sys_hotkey.h"
#include <stdio.h>
#include <string.h>

/* Real SDLK_* keycode values (SDL 2.x, stable ABI). SDLK_F1 = SDLK_SCANCODE_MASK
 * (0x40000000) | SDL_SCANCODE_F1 (0x3A); SDLK_F10 = mask | SDL_SCANCODE_F10
 * (0x43). These are the shipped Input.MenuToggleKey / Input.FpsToggleKey defaults. */
enum {
    KEY_F1      = 0x4000003A, /* SDLK_F1  — default menu-toggle key */
    KEY_F10     = 0x40000043, /* SDLK_F10 — default fps-toggle key  */
    KEY_ASCII_F = 0x66        /* 'f' — a plain ASCII keycode        */
};

/* SDL_CONTROLLER_BUTTON_MAX in SDL 2.32 — size of the pinned name list. */
enum { BTN_MAX = 21 };

/* Expected names for EVERY button index 0..20, copied verbatim from
 * kGpButtonName in src/platform/input_bindings.c (same byte-fidelity discipline
 * as the mirror in sys_hotkey.c). Mirror-side guard only — see header comment. */
static const char *const kExpectedName[BTN_MAX] = {
    "A", "B", "X", "Y", "Back", "Guide", "Start",
    "Left Stick Click", "Right Stick Click",
    "Left Bumper", "Right Bumper",
    "D-Pad Up", "D-Pad Down", "D-Pad Left", "D-Pad Right",
    "Share",                                        /* MISC1 */
    "Paddle 1", "Paddle 2", "Paddle 3", "Paddle 4", /* Elite paddles */
    "Touchpad",
};

static int g_failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { fprintf(stderr, "FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); g_failures++; } \
} while (0)

int main(void) {
    /* ---- sysKeyValid: SDL keycode-range bounds ---- */
    CHECK(sysKeyValid(0) == 0, "SDLK_UNKNOWN (0) invalid");
    CHECK(sysKeyValid(KEY_F1) == 1, "SDLK_F1 (0x4000003A) valid");
    CHECK(sysKeyValid(SYS_KEY_MAX) == 1, "top of keycode range (0x40000FFF) valid");
    CHECK(sysKeyValid(SYS_KEY_MAX + 1) == 0, "past keycode range (0x40001000) invalid");
    CHECK(sysKeyValid(-1) == 0, "negative keycode invalid");
    CHECK(sysKeyValid(KEY_ASCII_F) == 1, "plain ASCII 'f' (0x66) valid");

    /* ---- sysKeyMutualConflict: menu-key vs fps-key must differ ---- */
    CHECK(sysKeyMutualConflict(KEY_F1, KEY_F1) == 1, "equal keycodes conflict");
    CHECK(sysKeyMutualConflict(KEY_F1, KEY_F10) == 0, "distinct keycodes no conflict");
    /* The shipped defaults (F1 menu, F10 fps) must not collide — either order. */
    CHECK(sysKeyMutualConflict(KEY_F10, KEY_F1) == 0, "default F1/F10 no conflict");
    /* Invalid inputs are non-conflicting even when equal. */
    CHECK(sysKeyMutualConflict(0, 0) == 0, "two invalid (0) keys non-conflicting");

    /* ---- gamepadButtonName: ALL 21 names pinned against the source strings ---- */
    {
        int i;
        for (i = 0; i < BTN_MAX; i++) {
            const char *got = gamepadButtonName(i);
            if (got == NULL || strcmp(got, kExpectedName[i]) != 0) {
                fprintf(stderr, "FAIL: button %d -> \"%s\" (want \"%s\") (%s:%d)\n",
                        i, got ? got : "(null)", kExpectedName[i], __FILE__, __LINE__);
                g_failures++;
            }
        }
    }
    /* Out-of-range policy: "" (never NULL) so printf("%s") is safe. */
    CHECK(gamepadButtonName(-1) != NULL && gamepadButtonName(-1)[0] == '\0',
          "idx -1 -> empty string");
    CHECK(gamepadButtonName(BTN_MAX) != NULL && gamepadButtonName(BTN_MAX)[0] == '\0',
          "idx BUTTON_MAX (21) -> empty string");

    if (g_failures) { fprintf(stderr, "%d check(s) failed\n", g_failures); return 1; }
    printf("all sys_hotkey checks passed\n");
    return 0;
}
