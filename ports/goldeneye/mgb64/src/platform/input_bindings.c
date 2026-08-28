// input_bindings.c — rebindable keyboard registry. Defaults are byte-identical
// to the previously-hardcoded map in stubs.c. See input_actions.h.
#include "../app/input_actions.h"
#include "../app/config_schema.h"  /* mgb_config_get_int: live Input.MenuToggleButton (AUDIT-0025) */
#include "binding_conflict.h"       /* GB_NONE / GB_AXIS_BASE + bindingOwnerOf (AUDIT-0050) */
#include "gp_reserved.h"            /* gpButtonReserved / gpMenuConflict (AUDIT-0025) */
#include "savedir.h"

#include <SDL.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>

static const int kDefault[IB_COUNT] = {
    SDL_SCANCODE_W,       // IB_FORWARD
    SDL_SCANCODE_S,       // IB_BACK
    SDL_SCANCODE_A,       // IB_LEFT
    SDL_SCANCODE_D,       // IB_RIGHT
    SDL_SCANCODE_LSHIFT,  // IB_FIRE
    SDL_SCANCODE_RSHIFT,  // IB_AIM
    SDL_SCANCODE_R,       // IB_RELOAD (F / Backspace remain hardcoded alternates)
    SDL_SCANCODE_Q,       // IB_LEAN_L (Left arrow remains a hardcoded alternate)
    SDL_SCANCODE_E,       // IB_LEAN_R (Right arrow remains a hardcoded alternate)
};

static const char *kName[IB_COUNT] = {
    "forward", "back", "left", "right", "fire", "aim", "reload", "lean_l", "lean_r",
};

static const char *kLabel[IB_COUNT] = {
    "Move Forward", "Move Back", "Strafe Left", "Strafe Right",
    "Fire", "Aim", "Reload / Use", "Lean Left", "Lean Right",
};

static int g_binding[IB_COUNT];
static int g_initialized = 0;
static int g_force = 0;

static void ensureInit(void) {
    if (!g_initialized) {
        memcpy(g_binding, kDefault, sizeof(g_binding));
        g_initialized = 1;
    }
}

int inputBindingCount(void) { return IB_COUNT; }

const char *inputActionLabel(InputAction a) {
    return (a >= 0 && a < IB_COUNT) ? kLabel[a] : "?";
}

int inputBindingScancode(InputAction a) {
    ensureInit();
    if (a < 0 || a >= IB_COUNT) return 0;
    return g_force ? kDefault[a] : g_binding[a];
}

void inputBindingSet(InputAction a, int sc) {
    ensureInit();
    if (a >= 0 && a < IB_COUNT && sc > 0 && sc < SDL_NUM_SCANCODES) g_binding[a] = sc;
}

void inputBindingResetDefaults(void) {
    memcpy(g_binding, kDefault, sizeof(g_binding));
    g_initialized = 1;
}

void inputBindingForceDefaults(int on) { g_force = on; }

/* Atomically persist bindings to `path`: write to path.tmp via `writer`, verify
 * the stream, flush, close, then rename over the live file — so a crash or full
 * disk mid-write can never truncate the existing bindings (AUDIT-0049). Mirrors
 * configSave()/eeprom_save_to_file(). Warns on failure; the live file is left
 * untouched. */
static void bindings_atomic_write(const char *path, const char *label,
                                  void (*writer)(FILE *)) {
    /* +8 so a near-1024-char save-dir path can't truncate the ".tmp" suffix into
     * `path` itself (which would defeat atomicity) — matches config_pc.c /
     * eeprom_save_to_file, which size their temp name the same way. */
    char tmp[1024 + 8];
    FILE *f;
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    f = fopen(tmp, "w");
    if (!f) {
        fprintf(stderr, "[INPUT] Failed to save %s to %s: %s\n", label, tmp, strerror(errno));
        return;
    }
    writer(f);
    if (ferror(f) || fflush(f) != 0) {
        fprintf(stderr, "[INPUT] Failed writing %s to %s: %s\n", label, tmp, strerror(errno));
        fclose(f);
        remove(tmp);
        return;
    }
    if (fclose(f) != 0) {
        fprintf(stderr, "[INPUT] Failed closing %s (%s): %s\n", label, tmp, strerror(errno));
        remove(tmp);
        return;
    }
    if (rename(tmp, path) != 0) {
        fprintf(stderr, "[INPUT] Failed publishing %s to %s: %s\n", label, path, strerror(errno));
        remove(tmp);
    }
}

static void write_kb_bindings(FILE *f) {
    fprintf(f, "# MGB64 input bindings (action=SDL_scancode)\n");
    for (int i = 0; i < IB_COUNT; i++) fprintf(f, "%s=%d\n", kName[i], g_binding[i]);
}

void inputBindingSave(void) {
    char path[1024];
    ensureInit();
    /* Route through savedirPath() like ge007.ini (config_pc.c): a double-clicked
     * .app has CWD=/, so a CWD-relative file silently fails to persist (review F3).
     * Copy the static savedirPath buffer before appending ".tmp". */
    snprintf(path, sizeof(path), "%s", savedirPath("ge007_bindings.ini"));
    bindings_atomic_write(path, "keyboard bindings", write_kb_bindings);
}

void inputBindingLoad(void) {
    ensureInit();
    FILE *f = fopen(savedirPath("ge007_bindings.ini"), "r");
    /* One-time migration: pre-F3 builds wrote this file CWD-relative. If the
     * savedir copy is absent, read a legacy CWD file so an existing rebind
     * survives the first launch after the fix (the next save rewrites it to
     * the savedir). */
    if (!f) f = fopen("ge007_bindings.ini", "r");
    if (!f) return;
    char line[128];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#') continue;
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        int sc = atoi(eq + 1);
        for (int i = 0; i < IB_COUNT; i++) {
            if (strcmp(line, kName[i]) == 0) {
                if (sc > 0 && sc < SDL_NUM_SCANCODES) g_binding[i] = sc;
                break;
            }
        }
    }
    fclose(f);
}

/* ===== Gamepad bindings (player 1) =====
 * A binding value is encoded as: a button index 0..SDL_CONTROLLER_BUTTON_MAX-1,
 * or GB_AXIS_BASE + axis for a trigger axis (LT/RT), or GB_NONE for unbound.
 * The encoding stays internal to this file + the consumer (stubs.c reads it via
 * gamepadBindingActive), so input_actions.h needs no SDL dependency. */
/* GB_NONE / GB_AXIS_BASE now live in binding_conflict.h (shared with the
 * Controls UI + the conflict unit test); only the deadzone stays local. */
#define GB_TRIG_THRESH 8000   /* == GAMEPAD_DEADZONE in stubs.c */

static const int kGpDefault[GB_COUNT] = {
    GB_AXIS_BASE + SDL_CONTROLLER_AXIS_TRIGGERRIGHT, /* GB_FIRE        */
    GB_AXIS_BASE + SDL_CONTROLLER_AXIS_TRIGGERLEFT,  /* GB_AIM         */
    SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,             /* GB_ALT_FIRE    */
    SDL_CONTROLLER_BUTTON_LEFTSHOULDER,              /* GB_LOOK        */
    SDL_CONTROLLER_BUTTON_A,                         /* GB_JUMP        */
    SDL_CONTROLLER_BUTTON_B,                         /* GB_RELOAD      */
    SDL_CONTROLLER_BUTTON_START,                     /* GB_PAUSE       */
    SDL_CONTROLLER_BUTTON_Y,                         /* GB_WEAPON_NEXT */
    SDL_CONTROLLER_BUTTON_RIGHTSTICK,                /* GB_WEAPON_PREV */
    SDL_CONTROLLER_BUTTON_LEFTSTICK,                 /* GB_CROUCH      */
    SDL_CONTROLLER_BUTTON_DPAD_UP,                   /* GB_LOOK_UP     */
    SDL_CONTROLLER_BUTTON_DPAD_DOWN,                 /* GB_LOOK_DOWN   */
    SDL_CONTROLLER_BUTTON_DPAD_LEFT,                 /* GB_LOOK_LEFT   */
    SDL_CONTROLLER_BUTTON_DPAD_RIGHT,                /* GB_LOOK_RIGHT  */
};

static const char *kGpName[GB_COUNT] = {
    "fire", "aim", "alt_fire", "look_l", "jump", "reload", "pause",
    "weapon_next", "weapon_prev", "crouch",
    "look_up", "look_down", "look_left", "look_right",
};

static const char *kGpLabel[GB_COUNT] = {
    "Fire", "Aim", "Alt Fire", "Look/Zoom (N64 L)", "Jump (N64 A)", "Reload / Use",
    "Pause / Watch (N64 Start)", "Next Weapon", "Previous Weapon", "Crouch",
    "Look Up", "Look Down", "Look Left", "Look Right",
};

/* Pretty names for EVERY SDL button index (SDL_CONTROLLER_BUTTON_MAX == 21 in
 * SDL 2.32): the capture scan and gpValid() accept 15..20 (Share/paddles/
 * touchpad — Elite/Ally-class hardware), so a NULL here would feed
 * snprintf("%s", NULL) — UB. Keep this table exhaustive.
 * SYNC: sys_hotkey.c mirrors these strings (kGpButtonNameMirror, read by the
 * SDL-free gamepadButtonName() accessor) so the name table is unit-testable
 * without pulling <SDL.h> in through this TU — keep the two in sync on any SDL
 * button-enum bump. tests/test_sys_hotkey.c pins all 21 mirror entries, but
 * that guard is mirror-side only: edits HERE are invisible to the test, so
 * this comment is the guard — update the mirror + test when editing. */
static const char *kGpButtonName[SDL_CONTROLLER_BUTTON_MAX] = {
    "A", "B", "X", "Y", "Back", "Guide", "Start",
    "Left Stick Click", "Right Stick Click",
    "Left Bumper", "Right Bumper",
    "D-Pad Up", "D-Pad Down", "D-Pad Left", "D-Pad Right",
    "Share",                                        /* MISC1 */
    "Paddle 1", "Paddle 2", "Paddle 3", "Paddle 4", /* Elite paddles */
    "Touchpad",
};

static int g_gpBind[GB_COUNT];
static int g_gpInit = 0;
static int g_gpForce = 0;

static void gpEnsureInit(void) {
    if (!g_gpInit) {
        memcpy(g_gpBind, kGpDefault, sizeof(g_gpBind));
        g_gpInit = 1;
    }
}

/* The gamepad button that opens the in-game overlay, read LIVE from config
 * (Input.MenuToggleButton, default Back). Overlay_gamepadToggleButton() in
 * ui_overlay.cpp is the app-side twin; both resolve the same value so binding
 * validation, startup reconciliation and the overlay agree on one owner
 * (AUDIT-0025). */
static int gpMenuButton(void) {
    return mgb_config_get_int("Input.MenuToggleButton", SDL_CONTROLLER_BUTTON_BACK);
}

static int gpValid(int v) {
    if (v == GB_NONE) return 1;
    if (v >= GB_AXIS_BASE)
        return (v == GB_AXIS_BASE + SDL_CONTROLLER_AXIS_TRIGGERLEFT ||
                v == GB_AXIS_BASE + SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
    /* Reject the reserved buttons from a hand-edited ini, matching the capture-
     * scan exclusion (ui_bindings.cpp:236): the CONFIGURED overlay-toggle button
     * (Input.MenuToggleButton, not a hardcoded Back — a rebind of the menu button
     * must move this reservation with it, AUDIT-0025) reproduces the overlay/fire
     * double-role the F4 commit prevented in the UI; GUIDE is the OS/Steam overlay
     * button (review F5). */
    if (gpButtonReserved(v, gpMenuButton(), SDL_CONTROLLER_BUTTON_GUIDE))
        return 0;
    return (v >= 0 && v < SDL_CONTROLLER_BUTTON_MAX);
}

int gamepadBindingCount(void) { return GB_COUNT; }

const char *gamepadActionLabel(GamepadAction a) {
    return (a >= 0 && a < GB_COUNT) ? kGpLabel[a] : "?";
}

static const char *gpEncodedName(int v) {
    if (v == GB_NONE) return "None";
    if (v == GB_AXIS_BASE + SDL_CONTROLLER_AXIS_TRIGGERLEFT)  return "Left Trigger";
    if (v == GB_AXIS_BASE + SDL_CONTROLLER_AXIS_TRIGGERRIGHT) return "Right Trigger";
    if (v >= 0 && v < SDL_CONTROLLER_BUTTON_MAX && kGpButtonName[v])
        return kGpButtonName[v];  /* NULL guard: an SDL enum bump must not yield UB */
    return "?";
}

const char *gamepadBindingName(GamepadAction a) {
    gpEnsureInit();
    if (a < 0 || a >= GB_COUNT) return "?";
    return gpEncodedName(g_gpForce ? kGpDefault[a] : g_gpBind[a]);
}

void gamepadBindingSetButton(GamepadAction a, int sdl_button) {
    gpEnsureInit();
    if (a >= 0 && a < GB_COUNT && sdl_button >= 0 && sdl_button < SDL_CONTROLLER_BUTTON_MAX)
        g_gpBind[a] = sdl_button;
}

void gamepadBindingSetTrigger(GamepadAction a, int sdl_axis) {
    gpEnsureInit();
    if (a >= 0 && a < GB_COUNT &&
        (sdl_axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT ||
         sdl_axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT))
        g_gpBind[a] = GB_AXIS_BASE + sdl_axis;
}

void gamepadBindingResetDefaults(void) {
    memcpy(g_gpBind, kGpDefault, sizeof(g_gpBind));
    g_gpInit = 1;
}

void gamepadBindingForceDefaults(int on) { g_gpForce = on; }

/* Raw encoded binding for conflict/ownership checks in the Controls UI: a button
 * index 0..MAX-1, GB_AXIS_BASE+axis for LT/RT, or GB_NONE. Callers compare these
 * for equality; a plain SDL button index (e.g. X == 2) can be compared directly. */
int gamepadBindingEncoded(GamepadAction a) {
    gpEnsureInit();
    if (a < 0 || a >= GB_COUNT) return GB_NONE;
    return g_gpForce ? kGpDefault[a] : g_gpBind[a];
}

/* Encoded value a trigger axis (LT/RT) binds to — the single source the Controls
 * UI uses for its trigger conflict check, so it never hardcodes GB_AXIS_BASE.
 * GB_NONE for a non-trigger axis. Mirrors the encoding in gamepadBindingSetTrigger. */
int gamepadTriggerEncoded(int sdl_axis) {
    if (sdl_axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT ||
        sdl_axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT)
        return GB_AXIS_BASE + sdl_axis;
    return GB_NONE;
}

/* Action currently bound to encoded input `enc` other than `exclude`, or -1 —
 * the reverse owner map the Controls UI consults to reject a duplicate capture
 * for buttons AND triggers alike (AUDIT-0050). Respects force-defaults exactly
 * like gamepadBindingEncoded so the UI and runtime agree. */
int gamepadBindingOwnerOf(int enc, int exclude) {
    gpEnsureInit();
    return bindingOwnerOf(g_gpForce ? kGpDefault : g_gpBind, GB_COUNT, enc, exclude);
}

int gamepadBindingActive(void *handle, GamepadAction a) {
    SDL_GameController *gc = (SDL_GameController *)handle;
    int v;
    if (!gc || a < 0 || a >= GB_COUNT) return 0;
    v = g_gpForce ? kGpDefault[a] : g_gpBind[a];
    if (v == GB_NONE) return 0;
    if (v >= GB_AXIS_BASE) {
        int axis = v - GB_AXIS_BASE;
        return SDL_GameControllerGetAxis(gc, (SDL_GameControllerAxis)axis) > GB_TRIG_THRESH;
    }
    return SDL_GameControllerGetButton(gc, (SDL_GameControllerButton)v) ? 1 : 0;
}

static void write_gp_bindings(FILE *f) {
    int i;
    fprintf(f, "# MGB64 gamepad bindings (action=encoded: button index, or %d+axis for LT/RT, %d=none)\n",
            GB_AXIS_BASE, GB_NONE);
    for (i = 0; i < GB_COUNT; i++) fprintf(f, "%s=%d\n", kGpName[i], g_gpBind[i]);
}

void gamepadBindingSave(void) {
    char path[1024];
    gpEnsureInit();
    /* Route through savedirPath() like ge007.ini so rebinds persist for the
     * packaged-app audience (review F3: a .app has CWD=/, so the old CWD-relative
     * file was silently lost on relaunch). Atomic temp+rename (AUDIT-0049). */
    snprintf(path, sizeof(path), "%s", savedirPath("ge007_gp_bindings.ini"));
    bindings_atomic_write(path, "gamepad bindings", write_gp_bindings);
}

/* Menu-wins reconciliation (AUDIT-0025): clear any gameplay binding — including a
 * stock default like Jump=A once the menu button is moved to A — whose encoded
 * value collides with the CONFIGURED overlay-toggle button, so a single press can
 * never both open the overlay and fire a game action. The system button wins; the
 * freed action reads "None" in the Controls UI for the user to rebind. Runs on
 * every load (below) so a hand-edited ini AND the untouched defaults get the same
 * policy. Sim-neutral: gamepad bindings are host input mapping, never sim/tape
 * state. Returns the number of bindings cleared. */
int gamepadBindingReconcileMenu(void) {
    int menu, i, cleared = 0;
    gpEnsureInit();
    menu = gpMenuButton();
    for (i = 0; i < GB_COUNT; i++) {
        if (gpMenuConflict(g_gpBind[i], menu, SDL_CONTROLLER_BUTTON_MAX)) {
            fprintf(stderr,
                    "[INPUT] Gamepad action '%s' was bound to the menu button (%s); "
                    "clearing it so the overlay toggle isn't a double-action "
                    "(AUDIT-0025). Rebind it in Controls.\n",
                    kGpName[i], gpEncodedName(menu));
            g_gpBind[i] = GB_NONE;
            cleared++;
        }
    }
    return cleared;
}

void gamepadBindingLoad(void) {
    FILE *f;
    gpEnsureInit();
    /* Gamepad file is new (no pre-F3 CWD file to migrate). */
    f = fopen(savedirPath("ge007_gp_bindings.ini"), "r");
    if (f) {
        char line[128];
        while (fgets(line, sizeof(line), f)) {
            char *eq;
            int v, i;
            if (line[0] == '#') continue;
            eq = strchr(line, '=');
            if (!eq) continue;
            *eq = '\0';
            v = atoi(eq + 1);
            for (i = 0; i < GB_COUNT; i++) {
                if (strcmp(line, kGpName[i]) == 0) {
                    if (gpValid(v)) g_gpBind[i] = v;
                    break;
                }
            }
        }
        fclose(f);
    }
    /* Reconcile AFTER load so a stock default (e.g. Jump=A when the menu button is
     * moved to A — gpValid gates only file values, never the untouched defaults)
     * and any surviving file value are both held to the menu-wins rule
     * (AUDIT-0025). */
    gamepadBindingReconcileMenu();
}
