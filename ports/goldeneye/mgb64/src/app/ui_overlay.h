// ui_overlay.h — in-game overlay (F1): live settings, return-to-launcher, quit.
#ifndef MGB64_UI_OVERLAY_H
#define MGB64_UI_OVERLAY_H

struct SDL_Window;

// Register the overlay hooks with the engine. Call before mgb64_engine_boot().
// argv0 is the app executable path, used to re-exec back to the launcher.
void Overlay_install(SDL_Window *window, const char *argv0);

// The gamepad button reserved for toggling the overlay, as an
// SDL_GameControllerButton value (Back/View — pad Start stays the N64 Start /
// watch, per the MC.1+MC.3 review F2 adjudication). Single source of truth:
// ui_bindings.cpp excludes this button from the rebind capture scan so no game
// action can collide with the toggle.
int Overlay_gamepadToggleButton();

#endif  // MGB64_UI_OVERLAY_H
