// input_actions.h — the app<->engine input-binding contract (single source).
//
// Included by the engine (stubs.c reads bindings; main_pc.c loads them / forces
// defaults) AND by the app's Controls panel. Kept collision-free in src/app so
// both sides share ONE definition; NOT gated on MGB64_APP, so the bare engine
// still has a working (default) keymap.
#ifndef MGB64_INPUT_ACTIONS_H
#define MGB64_INPUT_ACTIONS_H

#ifdef __cplusplus
extern "C" {
#endif

// Rebindable keyboard actions. Non-rebindable keys (Enter/Tab/Esc, the arrow
// C-aim, I/J/K/L d-pad, and the alternate reload/lean keys) stay hardcoded.
typedef enum {
    IB_FORWARD,
    IB_BACK,
    IB_LEFT,
    IB_RIGHT,
    IB_FIRE,
    IB_AIM,
    IB_RELOAD,
    IB_LEAN_L,
    IB_LEAN_R,
    IB_COUNT
} InputAction;

int inputBindingCount(void);                 // == IB_COUNT
const char *inputActionLabel(InputAction a);
int  inputBindingScancode(InputAction a);    // current SDL scancode (or default under force)
void inputBindingSet(InputAction a, int sdl_scancode);
void inputBindingResetDefaults(void);
void inputBindingLoad(void);                 // from ge007_bindings.ini (no-op if absent)
void inputBindingSave(void);
void inputBindingForceDefaults(int on);      // automation/deterministic: ignore the user file

// ---- Rebindable gamepad actions (player 1 only) -----------------------------
// Each action binds to an SDL_GameControllerButton OR a trigger axis (LT/RT);
// the on/off encoding is internal to input_bindings.c. Player 2..4 pads keep
// fixed defaults (MP rebinding is out of scope). The pad's Back/View button is
// reserved by the app overlay toggle (MC.1); Start stays the N64 Start (watch),
// so the port-invented weapon-prev binding moved off Back to R-stick click.
typedef enum {
    GB_FIRE,        // default: Right Trigger
    GB_AIM,         // default: Left Trigger
    GB_ALT_FIRE,    // default: Right Bumper
    GB_LOOK,        // default: Left Bumper  (N64 L)
    GB_JUMP,        // default: A            (N64 A)
    GB_RELOAD,      // default: B            (N64 B; X stays a fixed alternate)
    GB_PAUSE,       // default: Start        (N64 Start)
    GB_WEAPON_NEXT, // default: Y
    GB_WEAPON_PREV, // default: Right Stick click
    GB_CROUCH,      // default: Left Stick click
    GB_LOOK_UP,     // default: D-Pad Up
    GB_LOOK_DOWN,   // default: D-Pad Down
    GB_LOOK_LEFT,   // default: D-Pad Left
    GB_LOOK_RIGHT,  // default: D-Pad Right
    GB_COUNT
} GamepadAction;

int         gamepadBindingCount(void);               // == GB_COUNT
const char *gamepadActionLabel(GamepadAction a);
const char *gamepadBindingName(GamepadAction a);     // human name of current binding
int         gamepadBindingEncoded(GamepadAction a);  // raw encoded binding (button idx / GB_AXIS_BASE+axis / GB_NONE) for conflict checks
int         gamepadTriggerEncoded(int sdl_axis);     // encoded value a trigger axis (LT/RT) binds to, for UI conflict checks (AUDIT-0050)
int         gamepadBindingOwnerOf(int encoded, int exclude);  // action bound to `encoded` other than `exclude`, or -1 (AUDIT-0050)
void        gamepadBindingSetButton(GamepadAction a, int sdl_button);  // capture: a button
void        gamepadBindingSetTrigger(GamepadAction a, int sdl_axis);   // capture: LT/RT
void        gamepadBindingResetDefaults(void);
void        gamepadBindingLoad(void);                // from ge007_gp_bindings.ini
void        gamepadBindingSave(void);
void        gamepadBindingForceDefaults(int on);     // automation/deterministic
// Menu-wins reconciliation (AUDIT-0025): clear any gameplay binding that collides
// with the configured overlay-toggle button (Input.MenuToggleButton). Called by
// gamepadBindingLoad(); returns the number of bindings cleared.
int         gamepadBindingReconcileMenu(void);
// Consumer: is action a currently active on controller gc (opaque
// SDL_GameController*)? Button pressed, or trigger past the deadzone. Returns 0
// for a NULL/absent controller. Implemented where SDL is available.
int         gamepadBindingActive(void *sdl_gamecontroller, GamepadAction a);

#ifdef __cplusplus
}
#endif

#endif  // MGB64_INPUT_ACTIONS_H
