/* sys_hotkey.h — SDL-free predicates for the three system hotkeys (AUDIT-0024
 * pure-logic core), plus a bounds-checked reader for the gamepad button-name
 * table. Extracting these keeps the hotkey validity / mutual-exclusion policy
 * unit-testable ROM-free AND SDL-free: the keycode bounds and button count are
 * baked as plain constants, documented against SDL's namespace, so the test TU
 * pulls in no <SDL.h>. See src/platform/platform_sdl.c (the hotkeys are plain
 * ints: Input.MenuToggleKey/FpsToggleKey are SDL KEYCODES; Input.MenuToggleButton
 * is a button index) and src/platform/input_bindings.c (the kGpButtonName table).
 *
 * Namespace hazard: gameplay keyboard bindings are SDL SCANCODES, but these
 * system hotkeys are SDL KEYCODES. A cross-namespace keyboard conflict check
 * (menu/fps key vs a gameplay bind) needs SDL_GetScancodeFromKey — an SDL call —
 * so it is intentionally OUT of this pure core. Only menu-key-vs-fps-key mutual
 * exclusion (both keycodes) is purely testable here; menu-button-vs-gameplay-pad
 * rejection already lives in the pure binding_conflict / gp_reserved modules. */
#ifndef MGB64_SYS_HOTKEY_H
#define MGB64_SYS_HOTKEY_H

#ifdef __cplusplus
extern "C" {
#endif

/* Inclusive top of SDL's keycode space: SDLK_SCANCODE_MASK (0x40000000) OR'd with
 * the largest scancode. SDL scancodes cap well below 0x1000, so 0x40000FFF is a
 * stable ceiling covering every SDLK_* the settings UI can register. Held as a
 * literal so this header/TU stays free of <SDL.h>. */
#define SYS_KEY_MAX 0x40000FFF

/* 1 if `keycode` is a usable system-hotkey key: strictly positive (SDLK_UNKNOWN
 * == 0 and negatives are not hotkeys) and within SDL's keycode range
 * (1 .. SYS_KEY_MAX). */
int sysKeyValid(int keycode);

/* 1 if two system-hotkey keycodes collide — the menu-toggle and fps-toggle keys
 * share one keycode namespace and MUST differ. Only equality is a conflict.
 * Invalid inputs are reported as NON-conflicting (equal-but-invalid, e.g. two
 * unset 0 keys, returns 0): rejecting invalid keys is sysKeyValid()'s job, so a
 * garbage/unset value must not masquerade here as "conflicts". */
int sysKeyMutualConflict(int candidate_keycode, int other_keycode);

/* Pretty name for a raw SDL_GameController button index, bounds-checked. Returns
 * "" (never NULL, so callers can printf("%s", ...) safely) for an out-of-range
 * index. Reads a mirror of kGpButtonName in input_bindings.c — keep both in
 * sync via the SYNC comments at both sites. tests/test_sys_hotkey.c pins all 21
 * names, but only mirror-side: it cannot see source-side edits to kGpButtonName. */
const char *gamepadButtonName(int idx);

#ifdef __cplusplus
}
#endif

#endif /* MGB64_SYS_HOTKEY_H */
