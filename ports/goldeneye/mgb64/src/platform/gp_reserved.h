// gp_reserved.h — pure, SDL-free gamepad binding-reservation predicates shared by
// input_bindings.c and its ROM-free unit test (AUDIT-0025). SDL enum values (the
// configured menu button, GUIDE, SDL_CONTROLLER_BUTTON_MAX) are passed in by the
// caller so this TU needs no SDL/game headers.
#ifndef MGB64_GP_RESERVED_H
#define MGB64_GP_RESERVED_H

#ifdef __cplusplus
extern "C" {
#endif

// 1 if a plain gamepad button index also drives a system role and therefore
// can't be a gameplay binding: the configured overlay-toggle button
// (Input.MenuToggleButton) or the OS/Steam Guide button. Used by gpValid().
int gpButtonReserved(int button, int menu_btn, int guide_btn);

// 1 if an ENCODED binding value collides with the configured menu button, i.e.
// the menu button is a real button in [0, button_max) and `encoded` equals it.
// Axis (>= GB_AXIS_BASE) and None (-1) encodings never collide with a button,
// and an out-of-range / unbound menu_btn clears nothing (guard). Used by the
// menu-wins reconciliation in gamepadBindingReconcileMenu().
int gpMenuConflict(int encoded, int menu_btn, int button_max);

#ifdef __cplusplus
}
#endif

#endif  // MGB64_GP_RESERVED_H
