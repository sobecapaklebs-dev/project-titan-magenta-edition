// ui_bindings.cpp — Controls panel: press-to-rebind keyboard AND gamepad actions,
// plus the three system hotkeys (AUDIT-0024).
#include "ui_launcher.h"
#include "app_theme.h"   // AppTheme::bad() — conflict tint
#include "input_actions.h"
#include "config_schema.h"  // system-hotkey config keys (AUDIT-0024)
#include "ui_common.h"
#include "ui_overlay.h"  // Overlay_gamepadToggleButton: reserved, not capturable
#include "../platform/sys_hotkey.h"  // sysKeyValid / sysKeyMutualConflict / gamepadButtonName

#include "imgui.h"
#include <SDL.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace {

// Status line shown under the tables after a rebind attempt (e.g. a rejected
// conflict, naming the current owner). Cleared on the next successful bind.
char g_bindMsg[192] = {0};
void setBindMsg(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    std::vsnprintf(g_bindMsg, sizeof(g_bindMsg), fmt, ap);
    va_end(ap);
}

// --- Reserved inputs: hardcoded (non-rebindable) in-game roles (see stubs.c).
// Binding a gameplay action onto one would silently double-act, so the Controls
// UI rejects the capture and flags any existing collision [AUDIT-0050/0051/0025].
struct ReservedKey { int sc; const char *role; };
const ReservedKey kReservedKeys[] = {
    {SDL_SCANCODE_F, "Reload"},      {SDL_SCANCODE_BACKSPACE, "Reload"},
    {SDL_SCANCODE_LEFT, "Lean L"},   {SDL_SCANCODE_RIGHT, "Lean R"},
    {SDL_SCANCODE_I, "Aim up"},      {SDL_SCANCODE_K, "Aim down"},
    {SDL_SCANCODE_J, "Aim left"},    {SDL_SCANCODE_L, "Aim right"},
    {SDL_SCANCODE_RETURN, "Accept"}, {SDL_SCANCODE_SPACE, "Accept"},
    {SDL_SCANCODE_TAB, "Watch"},     {SDL_SCANCODE_ESCAPE, "Watch"},
};
const char *keyReservedRole(int sc) {
    for (const ReservedKey &r : kReservedKeys) if (r.sc == sc) return r.role;
    return nullptr;
}
// The rebindable keyboard action (other than `exclude`) currently on `sc`, or -1.
int keyOwner(int sc, int exclude, const char **outLabel) {
    if (sc == 0) return -1;
    int n = inputBindingCount();
    for (int j = 0; j < n; ++j) {
        if (j == exclude) continue;
        if (inputBindingScancode((InputAction)j) == sc) {
            if (outLabel) *outLabel = inputActionLabel((InputAction)j);
            return j;
        }
    }
    return -1;
}
// A keyboard action collides with a reserved key OR another action.
bool keyConflicts(int i, int n) {
    int sc = inputBindingScancode((InputAction)i);
    if (sc == 0) return false;
    if (keyReservedRole(sc)) return true;
    for (int j = 0; j < n; ++j)
        if (j != i && inputBindingScancode((InputAction)j) == sc) return true;
    return false;
}

// Reserved gamepad buttons (encoded values are plain SDL button indices): X is a
// fixed alternate Reload; the overlay-toggle button and Guide are system inputs.
const char *padReservedRole(int encoded) {
    if (encoded == SDL_CONTROLLER_BUTTON_X) return "Reload";
    if (encoded == Overlay_gamepadToggleButton()) return "Menu";
    if (encoded == SDL_CONTROLLER_BUTTON_GUIDE) return "Guide";
    return nullptr;
}
// The gamepad action (other than `exclude`) currently on encoded input `enc`, or
// -1. Delegates to the shared SDL-free ownership primitive (unit-tested) so the
// button AND trigger reject paths share one owner map [AUDIT-0050].
int padOwner(int enc, int exclude, const char **outLabel) {
    int owner = gamepadBindingOwnerOf(enc, exclude);
    if (owner >= 0 && outLabel) *outLabel = gamepadActionLabel((GamepadAction)owner);
    return owner;
}
// A gamepad action collides with a reserved button OR another action.
bool padConflicts(int i, int n) {
    int enc = gamepadBindingEncoded((GamepadAction)i);
    if (padReservedRole(enc)) return true;
    for (int j = 0; j < n; ++j)
        if (j != i && gamepadBindingEncoded((GamepadAction)j) == enc) return true;
    return false;
}

// Lazily open (refcounted; shared with the engine/ImGui) the first game
// controller so the capture flow can poll it. Never closed — one ref for the
// app's lifetime is fine.
SDL_GameController *appController() {
    static SDL_GameController *gc = nullptr;
    if (gc && SDL_GameControllerGetAttached(gc)) return gc;
    gc = nullptr;
    int n = SDL_NumJoysticks();
    for (int i = 0; i < n; ++i) {
        if (SDL_IsGameController(i)) {
            gc = SDL_GameControllerOpen(i);
            if (gc) break;
        }
    }
    return gc;
}

void drawKeyboardTable() {
    static int capturing = -1;  // action index awaiting a key, or -1

    // While capturing, grab the first held key (Esc cancels). A mouse click
    // entered capture, so no key is down yet — the next press is the choice.
    if (capturing >= 0) {
        const Uint8 *ks = SDL_GetKeyboardState(NULL);
        if (ks[SDL_SCANCODE_ESCAPE]) {
            capturing = -1;
        } else {
            for (int sc = 4; sc < SDL_NUM_SCANCODES; ++sc) {
                if (ks[sc]) {
                    // Move-or-reject policy: reject a reserved key or one already
                    // owned by another action (naming the owner) rather than create
                    // a silent double-action [AUDIT-0050/0051].
                    const char *role = keyReservedRole(sc);
                    const char *ownerLabel = nullptr;
                    int owner = keyOwner(sc, capturing, &ownerLabel);
                    const char *kn = SDL_GetScancodeName((SDL_Scancode)sc);
                    const char *keyName = (kn && kn[0]) ? kn : "That key";
                    if (role) {
                        setBindMsg("%s is reserved for %s and can't be rebound.", keyName, role);
                    } else if (owner >= 0) {
                        setBindMsg("%s is already used by \"%s\" \xE2\x80\x94 rebind that action first.",
                                   keyName, ownerLabel);
                    } else {
                        inputBindingSet((InputAction)capturing, sc);
                        inputBindingSave();
                        g_bindMsg[0] = '\0';
                    }
                    capturing = -1;
                    break;
                }
            }
        }
    }

    const int n = inputBindingCount();
    if (ImGui::BeginTable("##binds", 2, ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, 200.0f);
        ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthFixed, 240.0f);
        for (int i = 0; i < n; ++i) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(inputActionLabel((InputAction)i));

            ImGui::TableSetColumnIndex(1);
            ImGui::PushID(i);
            bool cap = (capturing == i);
            bool conflict = !cap && keyConflicts(i, n);
            char label[64];
            if (cap) {
                std::snprintf(label, sizeof(label), "Press a key...");
            } else {
                const char *kn = SDL_GetScancodeName((SDL_Scancode)inputBindingScancode((InputAction)i));
                // Suffix the conflict as text (not color alone) so it reads for
                // colorblind/controller users; the tint reinforces it.
                std::snprintf(label, sizeof(label), conflict ? "%s  \xE2\x80\x94 conflict" : "%s",
                              (kn && kn[0]) ? kn : "?");
            }
            if (cap) ui::PushPrimaryButtonColors();
            if (conflict) ImGui::PushStyleColor(ImGuiCol_Text, AppTheme::bad());
            if (ImGui::Button(label, ImVec2(220.0f, 0.0f))) capturing = cap ? -1 : i;
            if (conflict) {
                ImGui::PopStyleColor();
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Shares an input with a reserved role or another action (e.g. from an older config) \xE2\x80\x94 rebind it to a free key.");
            }
            if (cap) ui::PopPrimaryButtonColors();
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    ui::Gap(ui::kGapM);
    if (ImGui::Button("Reset Keyboard to Defaults", ImVec2(220.0f, 36.0f))) {
        inputBindingResetDefaults();
        inputBindingSave();
        capturing = -1;
    }
}

void drawGamepadTable() {
    static int capturing = -1;   // gamepad action awaiting a press, or -1
    static bool waitRelease = false;  // debounce: ignore inputs still held from
                                      // the A-press that armed capture

    SDL_GameController *gc = appController();

    // Nav stays enabled (disabling the global flag risks a soft-lock if the user
    // leaves the panel mid-capture). Instead: (1) waitRelease ignores the A that
    // armed the row until release; (2) committedThisFrame stops the same commit
    // press (A = nav-activate on the focused row) from re-arming it below. The
    // capture poll runs before the row buttons, so it always wins the frame.
    bool committedThisFrame = false;

    if (capturing >= 0) {
        // Esc always cancels (keyboard escape hatch).
        const Uint8 *ks = SDL_GetKeyboardState(NULL);
        if (ks[SDL_SCANCODE_ESCAPE]) {
            capturing = -1;
        } else if (!gc) {
            // No pad attached — nothing to capture; leave the row armed.
        } else {
            bool anyDown = false;
            int b;
            for (b = 0; b < SDL_CONTROLLER_BUTTON_MAX; ++b)
                if (SDL_GameControllerGetButton(gc, (SDL_GameControllerButton)b)) anyDown = true;
            int lt = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_TRIGGERLEFT);
            int rt = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
            bool trigDown = (lt > 8000) || (rt > 8000);

            if (waitRelease) {
                if (!anyDown && !trigDown) waitRelease = false;
            } else if (rt > 8000) {
                // Same move-or-reject policy as the button branch: a trigger
                // already owned by another action is rejected (naming it) rather
                // than silently double-acting in-game [AUDIT-0050].
                const char *ownerLabel = nullptr;
                int owner = padOwner(gamepadTriggerEncoded(SDL_CONTROLLER_AXIS_TRIGGERRIGHT),
                                     capturing, &ownerLabel);
                if (owner >= 0) {
                    setBindMsg("The Right Trigger is already used by \"%s\" \xE2\x80\x94 rebind that action first.",
                               ownerLabel);
                } else {
                    gamepadBindingSetTrigger((GamepadAction)capturing, SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
                    gamepadBindingSave(); g_bindMsg[0] = '\0';
                }
                capturing = -1; committedThisFrame = true;
            } else if (lt > 8000) {
                const char *ownerLabel = nullptr;
                int owner = padOwner(gamepadTriggerEncoded(SDL_CONTROLLER_AXIS_TRIGGERLEFT),
                                     capturing, &ownerLabel);
                if (owner >= 0) {
                    setBindMsg("The Left Trigger is already used by \"%s\" \xE2\x80\x94 rebind that action first.",
                               ownerLabel);
                } else {
                    gamepadBindingSetTrigger((GamepadAction)capturing, SDL_CONTROLLER_AXIS_TRIGGERLEFT);
                    gamepadBindingSave(); g_bindMsg[0] = '\0';
                }
                capturing = -1; committedThisFrame = true;
            } else {
                for (b = 0; b < SDL_CONTROLLER_BUTTON_MAX; ++b) {
                    // The overlay-toggle button and Guide fire their own system
                    // role; never capture them (a Fire=toggle bind would ping-pong
                    // the overlay). Presses are ignored mid-capture.
                    if (b == Overlay_gamepadToggleButton() || b == SDL_CONTROLLER_BUTTON_GUIDE)
                        continue;
                    if (SDL_GameControllerGetButton(gc, (SDL_GameControllerButton)b)) {
                        // Reject X (a fixed alternate Reload) or a button already
                        // owned by another action, naming it [AUDIT-0050/0051/0025].
                        const char *ownerLabel = nullptr;
                        int owner = padOwner(b, capturing, &ownerLabel);
                        if (b == SDL_CONTROLLER_BUTTON_X) {
                            setBindMsg("X is a fixed alternate Reload and can't be rebound.");
                        } else if (owner >= 0) {
                            setBindMsg("That button is already used by \"%s\" \xE2\x80\x94 rebind that action first.",
                                       ownerLabel);
                        } else {
                            gamepadBindingSetButton((GamepadAction)capturing, b);
                            gamepadBindingSave();
                            g_bindMsg[0] = '\0';
                        }
                        capturing = -1; committedThisFrame = true;
                        break;
                    }
                }
            }
        }
    }

    const int n = gamepadBindingCount();
    if (ImGui::BeginTable("##gpbinds", 2, ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, 200.0f);
        ImGui::TableSetupColumn("Button", ImGuiTableColumnFlags_WidthFixed, 240.0f);
        for (int i = 0; i < n; ++i) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(gamepadActionLabel((GamepadAction)i));

            ImGui::TableSetColumnIndex(1);
            ImGui::PushID(1000 + i);  // distinct from the keyboard table IDs
            bool cap = (capturing == i);
            bool conflict = !cap && padConflicts(i, n);
            char label[64];
            if (cap) {
                std::snprintf(label, sizeof(label), "Press a button...");
            } else {
                std::snprintf(label, sizeof(label), conflict ? "%s  \xE2\x80\x94 conflict" : "%s",
                              gamepadBindingName((GamepadAction)i));
            }
            if (cap) ui::PushPrimaryButtonColors();
            if (conflict) ImGui::PushStyleColor(ImGuiCol_Text, AppTheme::bad());
            if (ImGui::Button(label, ImVec2(220.0f, 0.0f)) && !committedThisFrame) {
                if (cap) { capturing = -1; }
                else     { capturing = i; waitRelease = true; }
            }
            if (conflict) {
                ImGui::PopStyleColor();
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Shares an input with a reserved role or another action (e.g. from an older config) \xE2\x80\x94 rebind it to a free button.");
            }
            if (cap) ui::PopPrimaryButtonColors();
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    ui::Gap(ui::kGapM);
    if (ImGui::Button("Reset Gamepad to Defaults", ImVec2(220.0f, 36.0f))) {
        gamepadBindingResetDefaults();
        gamepadBindingSave();
        capturing = -1;
    }

    if (!gc) ui::TextSubtle("No gamepad detected — connect a controller to rebind.");
}

// --- System hotkeys (AUDIT-0024) ---------------------------------------------
// The overlay-open key, the FPS-overlay key (both SDL KEYCODES — a DIFFERENT
// namespace from the gameplay SCANCODE table above) and the overlay-open gamepad
// button. Previously only editable as billion-range Settings sliders; now
// press-to-bind rows that mirror the gameplay-row UX. Keycode discipline: these
// config keys store SDL KEYCODES end-to-end (captured scancode -> keycode via
// SDL_GetKeyFromScancode, displayed via SDL_GetKeyName) — never a scancode.
struct SysKeyRow { const char *label; const char *cfgKey; int defKeycode; };
const SysKeyRow kSysKeyRows[] = {
    {"Open Menu",   "Input.MenuToggleKey", SDLK_F1},   // default F1
    {"FPS Overlay", "Input.FpsToggleKey",  SDLK_F10},  // default F10
};

// Persist a system-hotkey int. In the launcher (no staging session) each Controls
// edit persists immediately, mirroring inputBindingSave() for the gameplay rows;
// under a staging session (defensive — Controls is a launcher-only panel today)
// the value stages and the owning Apply persists it.
void sysPersistInt(const char *key, int value) {
    mgb_config_set_int(key, value);
    if (!configStagingActive()) mgb_config_save();
}

void drawSystemTable() {
    static int  capturingKey = -1;   // index into kSysKeyRows awaiting a key, or -1
    static bool capturingPad = false; // pad row awaiting a button
    static bool padWaitRelease = false; // debounce the A-press that armed the pad row

    bool committedThisFrame = false;

    // --- keyboard capture poll (menu / fps keys) ---
    if (capturingKey >= 0) {
        const Uint8 *ks = SDL_GetKeyboardState(NULL);
        if (ks[SDL_SCANCODE_ESCAPE]) {
            capturingKey = -1;  // Esc cancels
        } else {
            for (int sc = 4; sc < SDL_NUM_SCANCODES; ++sc) {
                if (!ks[sc]) continue;
                // Namespace conversion: the poll yields a SCANCODE, but these keys
                // are stored as KEYCODES — convert before validating/storing.
                int kc = (int)SDL_GetKeyFromScancode((SDL_Scancode)sc);
                const char *kn = SDL_GetKeyName((SDL_Keycode)kc);
                const char *keyName = (kn && kn[0]) ? kn : "That key";
                const SysKeyRow &row = kSysKeyRows[capturingKey];
                const SysKeyRow &other = kSysKeyRows[capturingKey == 0 ? 1 : 0];
                int otherKc = mgb_config_get_int(other.cfgKey, other.defKeycode);
                if (!sysKeyValid(kc)) {
                    setBindMsg("%s can't be used as a system hotkey.", keyName);
                } else if (sysKeyMutualConflict(kc, otherKc)) {
                    setBindMsg("%s is already the %s hotkey \xE2\x80\x94 rebind that action first.",
                               keyName, other.label);
                } else {
                    sysPersistInt(row.cfgKey, kc);
                    g_bindMsg[0] = '\0';
                }
                capturingKey = -1;
                break;
            }
        }
    }

    // --- gamepad capture poll (menu button; buttons only) ---
    if (capturingPad) {
        const Uint8 *ks = SDL_GetKeyboardState(NULL);
        SDL_GameController *gc = appController();
        if (ks[SDL_SCANCODE_ESCAPE]) {
            capturingPad = false;
        } else if (!gc) {
            // No pad attached — leave the row armed.
        } else {
            bool anyDown = false;
            for (int b = 0; b < SDL_CONTROLLER_BUTTON_MAX; ++b)
                if (SDL_GameControllerGetButton(gc, (SDL_GameControllerButton)b)) anyDown = true;
            if (padWaitRelease) {
                if (!anyDown) padWaitRelease = false;  // ignore the A that armed the row
            } else {
                for (int b = 0; b < SDL_CONTROLLER_BUTTON_MAX; ++b) {
                    // Guide is an OS/system button (often swallowed) — never bind
                    // the menu toggle onto it.
                    if (b == SDL_CONTROLLER_BUTTON_GUIDE) continue;
                    if (SDL_GameControllerGetButton(gc, (SDL_GameControllerButton)b)) {
                        sysPersistInt("Input.MenuToggleButton", b);
                        // Menu-wins reconcile (AUDIT-0025): clear any gameplay
                        // binding now colliding with the new menu button, then
                        // persist the cleared gameplay bindings.
                        if (gamepadBindingReconcileMenu() > 0) gamepadBindingSave();
                        g_bindMsg[0] = '\0';
                        capturingPad = false; committedThisFrame = true;
                        break;
                    }
                }
            }
        }
    }

    SDL_GameController *gc = appController();

    if (ImGui::BeginTable("##sysbinds", 2, ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, 200.0f);
        ImGui::TableSetupColumn("Binding", ImGuiTableColumnFlags_WidthFixed, 240.0f);

        // Two keyboard rows (keycodes).
        for (int r = 0; r < 2; ++r) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(kSysKeyRows[r].label);

            ImGui::TableSetColumnIndex(1);
            ImGui::PushID(2000 + r);  // distinct from keyboard(0..)/gamepad(1000..) IDs
            bool cap = (capturingKey == r);
            char label[64];
            if (cap) {
                std::snprintf(label, sizeof(label), "Press a key...");
            } else {
                int kc = mgb_config_get_int(kSysKeyRows[r].cfgKey, kSysKeyRows[r].defKeycode);
                const char *kn = SDL_GetKeyName((SDL_Keycode)kc);
                std::snprintf(label, sizeof(label), "%s", (kn && kn[0]) ? kn : "?");
            }
            if (cap) ui::PushPrimaryButtonColors();
            if (ImGui::Button(label, ImVec2(220.0f, 0.0f))) capturingKey = cap ? -1 : r;
            if (cap) ui::PopPrimaryButtonColors();
            ImGui::PopID();
        }

        // Gamepad menu-button row (button index -> human name).
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Menu Button (Gamepad)");

            ImGui::TableSetColumnIndex(1);
            ImGui::PushID(2100);
            bool cap = capturingPad;
            char label[64];
            if (cap) {
                std::snprintf(label, sizeof(label), "Press a button...");
            } else {
                int bi = mgb_config_get_int("Input.MenuToggleButton", SDL_CONTROLLER_BUTTON_BACK);
                const char *bn = gamepadButtonName(bi);
                std::snprintf(label, sizeof(label), "%s", (bn && bn[0]) ? bn : "?");
            }
            if (cap) ui::PushPrimaryButtonColors();
            if (ImGui::Button(label, ImVec2(220.0f, 0.0f)) && !committedThisFrame) {
                if (cap) { capturingPad = false; }
                else     { capturingPad = true; padWaitRelease = true; }
            }
            if (cap) ui::PopPrimaryButtonColors();
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    ui::Gap(ui::kGapM);
    if (ImGui::Button("Reset System Hotkeys to Defaults", ImVec2(260.0f, 36.0f))) {
        mgb_config_set_int("Input.MenuToggleKey", SDLK_F1);
        mgb_config_set_int("Input.FpsToggleKey", SDLK_F10);
        mgb_config_set_int("Input.MenuToggleButton", SDL_CONTROLLER_BUTTON_BACK);
        if (!configStagingActive()) mgb_config_save();
        if (gamepadBindingReconcileMenu() > 0) gamepadBindingSave();
        capturingKey = -1; capturingPad = false; g_bindMsg[0] = '\0';
    }

    if (!gc) ui::TextSubtle("No gamepad detected — the Menu Button row needs a controller to rebind.");
}

}  // namespace

void BindingsPanel_draw(LauncherState & /*s*/, LauncherAction & /*out*/) {
    ui::SectionHeader("Controls",
                      "Click a binding, then press a key or button to rebind. Esc cancels.");

    if (ImGui::BeginTabBar("##controlsTabs")) {
        if (ImGui::BeginTabItem("Keyboard")) {
            ui::Gap(ui::kGapS);
            drawKeyboardTable();
            ui::Gap(ui::kGapM);
            ui::TextSubtle("Fixed keys: F / Backspace = Reload, Left / Right arrows = lean, "
                           "I/J/K/L = C-aim, Enter = A, Tab = Start, Esc = pause.");
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Gamepad")) {
            ui::Gap(ui::kGapS);
            drawGamepadTable();
            ui::Gap(ui::kGapM);
            ui::TextSubtle("Sticks are fixed: left stick = move, right stick = look/aim. "
                           "X is a fixed alternate for Reload.");
            ui::TextSubtle("The Back/View button opens the in-game overlay (with F1); it and "
                           "the Guide button cannot be bound to game actions. Previous Weapon "
                           "moved to the Right-Stick click.");
            ui::TextSubtle("Player 2\xE2\x80\x93" "4 pads use fixed defaults (multiplayer rebinding "
                           "is out of scope).");
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("System")) {
            ui::Gap(ui::kGapS);
            drawSystemTable();
            ui::Gap(ui::kGapM);
            ui::TextSubtle("Open Menu and FPS Overlay are keyboard hotkeys (F1 / F10 by default); "
                           "the Menu Button opens the same overlay on a gamepad (View/Back by "
                           "default). Press a key or button to rebind; Esc cancels.");
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    // Reject/status line from the last rebind attempt (naming the conflicting owner).
    if (g_bindMsg[0]) {
        ui::Gap(ui::kGapS);
        ImGui::PushStyleColor(ImGuiCol_Text, AppTheme::bad());
        ImGui::TextWrapped("%s", g_bindMsg);
        ImGui::PopStyleColor();
    }
}
