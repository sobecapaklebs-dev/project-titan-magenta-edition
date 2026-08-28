// ui_overlay.cpp — see ui_overlay.h.
#include "ui_overlay.h"
#include "app_theme.h"
#include "config_schema.h"  // mgb_config_get_float (UI.Scale)
#include "engine_entry.h"   // AppOverlayHooks, platformSetOverlayHooks
#include "ui_common.h"
#include "ui_settings.h"
#include "../platform/save_status.h"  // saveStatusIsFailure (AUDIT-0036)

#include "imgui.h"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_opengl3.h"

#ifdef MGB64_WEBGPU_BACKEND
#include "gfx_webgpu_imgui.h"
// Render-backend seam (engine) + the surface overlay pass (gfx_webgpu.c),
// resolved at link into ge007. The overlay renders through whichever the
// active backend is.
extern "C" bool  gfx_backend_use_webgpu(void);
extern "C" void *gfx_webgpu_current_overlay_pass(void);
extern "C" void  gfx_webgpu_current_overlay_size(int *w, int *h);
#endif

#include <SDL.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#if !defined(_WIN32)
#include <unistd.h>  // execvp
#endif

namespace {

bool        g_open = false;
bool        g_showSettings = false;
int         g_confirm = 0;   // 0 none, 1 return-to-launcher, 2 quit
bool        g_justOpened = false;  // request initial nav focus on the next render
SDL_Window *g_window = nullptr;
char        g_argv0[1024] = {0};
bool        g_prevRelMouse = false;

// Last input device the user actually acted with, so the overlay shows the right
// control hints (controller glyphs vs keyboard keys). Switched only on decisive
// events (button/key presses, a real stick push) so idle drift/mouse jitter
// doesn't thrash it.
enum LastInputDevice { DEV_KBM, DEV_PAD };
LastInputDevice g_lastInputDevice = DEV_KBM;

void setOpen(bool open) {
    if (open == g_open) return;

    if (!open) {
        // Closing the overlay is a "resume": don't silently lose in-progress
        // settings edits — commit any open staging session. (Cancel already
        // discarded + ended its session, so this only fires when the user
        // resumes with unsaved edits.)
        if (configStagingActive()) {
            MgbConfigSaveResult r = configStagingApply();
            Settings_reportSaveResult(r);  // surface it as the panel status line
            if (saveStatusIsFailure(r)) {
                // A persistence FAILURE must not be silently swallowed by the
                // resume (AUDIT-0036): keep the overlay open on the Settings panel
                // so the user sees the error + Retry. Apply already committed the
                // edits to the live globals, so re-open a staging session to keep
                // them editable / retryable, and abort the close.
                g_showSettings = true;
                configStagingBegin();
                return;
            }
        }
    }

    g_open = open;
    g_confirm = 0;
    if (g_open) {
        g_justOpened = true;  // focus the first control so a pad has an anchor
        g_prevRelMouse = (SDL_GetRelativeMouseMode() == SDL_TRUE);
        SDL_SetRelativeMouseMode(SDL_FALSE);  // free the cursor for the overlay
    } else {
        // Reset the Settings panel so reopening the overlay starts fresh.
        g_showSettings = false;
        SDL_SetRelativeMouseMode(g_prevRelMouse ? SDL_TRUE : SDL_FALSE);
    }
}

void quitToDesktop() {
    SDL_Event q{};  // zero-init: don't queue uninitialized bytes
    q.type = SDL_QUIT;
    SDL_PushEvent(&q);
}

void returnToLauncher() {
#if !defined(_WIN32)
    if (g_argv0[0]) {
        char *argv[] = {g_argv0, nullptr};
        execvp(g_argv0, argv);  // replaces the process image → fresh launcher
    }
#endif
    quitToDesktop();  // fallback (and Windows, wired in Part 3)
}

void onProcessEvent(const void *ev) {
    const SDL_Event *e = (const SDL_Event *)ev;
    ImGui_ImplSDL2_ProcessEvent(e);

    // Track the active device from decisive events only (a key/mouse-button press
    // vs a pad button or a firm stick push), so the control hints follow what the
    // user is really holding without flip-flopping on idle drift.
    switch (e->type) {
        case SDL_KEYDOWN:
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEWHEEL:
            g_lastInputDevice = DEV_KBM;
            break;
        case SDL_CONTROLLERBUTTONDOWN:
        case SDL_JOYBUTTONDOWN:
            g_lastInputDevice = DEV_PAD;
            break;
        case SDL_CONTROLLERAXISMOTION:
            if (e->caxis.value > 8000 || e->caxis.value < -8000) g_lastInputDevice = DEV_PAD;
            break;
        default:
            break;
    }

    // Toggle keys/buttons: the menu opener (default F1 keyboard / Back gamepad)
    // and the FPS-overlay hotkey (default F10) are rebindable settings read live
    // from the config registry, so a user rebind takes effect without a restart.
    // Back is the conventional system-UI button and its old default (weapon-prev)
    // is a port invention, so pad Start stays the N64 Start — GoldenEye's watch is
    // the game's core system menu (review F2 adjudication).
    if (e->type == SDL_KEYDOWN && !e->key.repeat &&
        e->key.keysym.sym == mgb_config_get_int("Input.MenuToggleKey", SDLK_F1)) {
        setOpen(!g_open);
        return;
    }
    // FPS overlay quick-toggle — does NOT open the menu. Flips the live
    // Video.FpsOverlay through the config API (also persists on Save).
    if (e->type == SDL_KEYDOWN && !e->key.repeat &&
        e->key.keysym.sym == mgb_config_get_int("Input.FpsToggleKey", SDLK_F10)) {
        mgb_config_set_int("Video.FpsOverlay",
                           mgb_config_get_int("Video.FpsOverlay", 1) ? 0 : 1);
        return;
    }
    if (e->type == SDL_CONTROLLERBUTTONDOWN) {
        // P1-only: in split-screen, P2-4 Start/Back must reach their own N64
        // pad (pcFillPadFromController), not drive the shared overlay — the
        // input gate would freeze every player. (-1 = no pad in slot 0, and no
        // pad means no controller events, so requiring a match is safe.)
        if ((int)e->cbutton.which != platformGetPad0InstanceId()) return;
        if ((int)e->cbutton.button == Overlay_gamepadToggleButton()) {
            setOpen(!g_open);
        } else if (g_open && e->cbutton.button == SDL_CONTROLLER_BUTTON_B) {
            // B = back one level (cancel confirm -> hide settings -> close). Skip
            // when ImGui itself is consuming B (an open combo/popup), so its own
            // nav-cancel closes that first rather than the whole overlay.
            if (!ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId)) {
                if (g_confirm) g_confirm = 0;
                else if (g_showSettings) g_showSettings = false;
                else setOpen(false);
            }
        }
    }
}

int onWantsInput() { return g_open ? 1 : 0; }

// MC.7 headless proof hook: scripts the overlay open/close by frame ordinal so a
// non-deterministic single-player run can prove the sim pauses while open and
// resumes after (assert g_ClockTimer/g_GlobalTimer frozen across the window),
// without a human at the keyboard. Off unless MGB64_TEST_OVERLAY_OPEN_FRAME is
// set. Ticked once per rendered frame (onRender runs every frame — it early-
// returns below when the overlay is closed, but this runs before that).
void overlayTestFrameTick() {
    static long s_openFrame = -2;   // -2 = env not yet read
    static long s_closeFrame = -2;
    static long s_frame = 0;

    if (s_openFrame == -2) {
        const char *o = std::getenv("MGB64_TEST_OVERLAY_OPEN_FRAME");
        const char *c = std::getenv("MGB64_TEST_OVERLAY_CLOSE_FRAME");
        s_openFrame = o ? std::strtol(o, nullptr, 10) : -1;
        s_closeFrame = c ? std::strtol(c, nullptr, 10) : -1;
    }
    if (s_openFrame < 0) return;  // hook disabled

    if (s_frame == s_openFrame) {
        setOpen(true);
        std::fprintf(stderr, "[overlay-test] opened at frame %ld\n", s_frame);
    }
    if (s_closeFrame >= 0 && s_frame == s_closeFrame) {
        setOpen(false);
        std::fprintf(stderr, "[overlay-test] closed at frame %ld\n", s_frame);
    }
    s_frame++;
}

// Human name for the currently-bound menu-open key (e.g. "F1", "Escape", "Tab").
const char *menuKeyName() {
    const char *n = SDL_GetKeyName((SDL_Keycode)mgb_config_get_int("Input.MenuToggleKey", SDLK_F1));
    return (n && n[0]) ? n : "F1";
}

// Friendly name for the currently-bound menu-open gamepad button.
const char *menuButtonName() {
    switch (mgb_config_get_int("Input.MenuToggleButton", SDL_CONTROLLER_BUTTON_BACK)) {
        case SDL_CONTROLLER_BUTTON_BACK:  return "View";
        case SDL_CONTROLLER_BUTTON_START: return "Start";
        case SDL_CONTROLLER_BUTTON_GUIDE: return "Guide";
        case SDL_CONTROLLER_BUTTON_A:     return "A";
        case SDL_CONTROLLER_BUTTON_B:     return "B";
        case SDL_CONTROLLER_BUTTON_X:     return "X";
        case SDL_CONTROLLER_BUTTON_Y:     return "Y";
        default:                          return "Menu";
    }
}

void onRender() {
    overlayTestFrameTick();
    if (!g_open) return;

#ifdef MGB64_WEBGPU_BACKEND
    const bool wgpu = gfx_backend_use_webgpu();
    if (wgpu) {
        gfx_webgpu_imgui_new_frame();
        ImGui_ImplSDL2_NewFrame();
        // Metal window: imgui_impl_sdl2's SDL_GL_GetDrawableSize returns logical,
        // so set the true high-DPI scale from the surface size (== drawable) —
        // same fix as AppHost. DisplaySize stays logical so the panel lays out at
        // point sizes and geometry fills the surface.
        ImGuiIO &io = ImGui::GetIO();
        int sw = 0, sh = 0, lw = 0, lh = 0;
        gfx_webgpu_current_overlay_size(&sw, &sh);
        if (g_window) SDL_GetWindowSize(g_window, &lw, &lh);
        if (lw > 0 && lh > 0 && sw > 0 && sh > 0) {
            io.DisplaySize = ImVec2((float)lw, (float)lh);
            io.DisplayFramebufferScale = ImVec2((float)sw / (float)lw, (float)sh / (float)lh);
        }
        ImGui::NewFrame();
    } else
#endif
    {
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
    }

    // RX.2: apply the live UI.Scale so the in-game overlay is readable on a
    // handheld too (idempotent no-op when unchanged).
    AppTheme::setUiScale(mgb_config_get_float("UI.Scale", 1.0f));
    const float uiS = AppTheme::uiScale();

    const ImGuiViewport *vp = ImGui::GetMainViewport();
    ImGui::GetBackgroundDrawList()->AddRectFilled(
        vp->Pos, ImVec2(vp->Pos.x + vp->Size.x, vp->Pos.y + vp->Size.y), IM_COL32(8, 9, 11, 180));

    // Grow the fixed overlay panel with the UI scale so scaled text/buttons don't
    // clip, but never exceed the viewport on a small panel.
    float w = (g_showSettings ? 720.0f : 440.0f) * uiS;
    float h = (g_confirm ? 250.0f : (g_showSettings ? 560.0f : 300.0f)) * uiS;
    if (w > vp->Size.x) w = vp->Size.x;
    if (h > vp->Size.y) h = vp->Size.y;
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x * 0.5f, vp->Pos.y + vp->Size.y * 0.5f),
                            ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(w, h), ImGuiCond_Always);
    ImGui::Begin("##overlay", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

    ImGui::PushFont(AppTheme::fonts().title);
    ImGui::PushStyleColor(ImGuiCol_Text, AppTheme::accent());
    ImGui::TextUnformatted("MGB64");
    ImGui::PopStyleColor();
    ImGui::PopFont();
    // MC.7: single-player pauses the sim while the overlay is open (the engine
    // zeroes g_ClockTimer in lvlManageMpGame when platformOverlayWantsInput() is
    // set and getPlayerCount()==1). Multiplayer CANNOT pause — you can't freeze
    // the other players' clocks — so it keeps running and the footer says so.
    // Keep the wording honest per-mode: never claim "Paused" in MP. The control
    // hints follow the last-used device and the actual (rebindable) menu binding.
    const char *resume = (g_lastInputDevice == DEV_PAD) ? menuButtonName() : menuKeyName();
    const char *nav = (g_lastInputDevice == DEV_PAD)
        ? "D-pad move  \xE2\x80\xA2  A select  \xE2\x80\xA2  B back"
        : "arrows move  \xE2\x80\xA2  Enter select  \xE2\x80\xA2  Esc back";
    if (platformGetPlayerCount() >= 2) {
        ui::TextSubtle("Game keeps running (multiplayer)  \xE2\x80\xA2  %s to resume  \xE2\x80\xA2  %s", resume, nav);
    } else {
        ui::TextSubtle("Paused  \xE2\x80\xA2  %s to resume  \xE2\x80\xA2  %s", resume, nav);
    }
    ui::Gap(ui::kGapS);
    ImGui::Separator();
    ui::Gap(ui::kGapM);

    // Give a freshly-opened overlay an initial nav focus so a gamepad/keyboard
    // has an anchor without first hunting with the stick. Focuses the next
    // widget drawn (Resume, or the confirm's primary action).
    if (g_justOpened) {
        ImGui::SetKeyboardFocusHere();
        g_justOpened = false;
    }

    if (g_confirm) {
        ui::TextSubtle(g_confirm == 1 ? "Return to the launcher? This ends the current game."
                                      : "Quit to desktop? This ends the current game.");
        ui::Gap(ui::kGapM);
        if (ui::PrimaryButton(g_confirm == 1 ? "Return to Launcher" : "Quit", ui::kBtnWide())) {
            if (g_confirm == 1) returnToLauncher();
            else quitToDesktop();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ui::kBtnSecondary())) g_confirm = 0;
    } else {
        if (ui::PrimaryButton("Resume", ui::kBtnSecondary())) setOpen(false);
        ImGui::SameLine();
        if (ImGui::Button(g_showSettings ? "Hide Settings" : "Settings", ui::kBtnSecondary())) {
            if (g_showSettings) {
                // Closing the panel from the overlay = cancel any staged edits.
                if (configStagingActive()) configStagingDiscard();
                g_showSettings = false;
            } else {
                g_showSettings = true;
                configStagingBegin();  // edits stage on a working copy until Apply/Resume
            }
        }

        if (g_showSettings) {
            ui::Gap(ui::kGapS);
            ImGui::BeginChild("##ovsettings", ImVec2(0, 340), true);
            // Staged: edits go to a working copy; Apply commits to the running
            // game + saves, Cancel reverts, Resume (above) also commits.
            SettingsResult sr = Settings_draw();
            ImGui::EndChild();
            if (sr == SETTINGS_CANCELLED) {
                g_showSettings = false;    // Cancel already discarded the stage
            } else if (sr == SETTINGS_APPLIED) {
                configStagingBegin();      // keep editing on a fresh staged copy
            }
        }

        // Solo cross-reference: the F1 overlay and the diegetic GoldenEye watch are
        // two different menus. Point solo players at the watch for the things this
        // overlay deliberately doesn't duplicate (objectives, mission options,
        // progress). MP's watch is the scores/pause menu, so only hint in solo.
        if (!g_showSettings && platformGetPlayerCount() < 2) {
            ui::Gap(ui::kGapS);
            ui::TextSubtle("Objectives, mission options & progress are in the in-game watch \xE2\x80\x94 Start / Esc / Tab.");
        }

        ui::Gap(ui::kGapM);
        // "Return to Launcher" re-execs the process; that path isn't wired on
        // Windows yet, so it would silently quit instead. Hide it there rather than
        // mislabel — Windows keeps a truthful "Quit to Desktop".
#if !defined(_WIN32)
        if (ImGui::Button("Return to Launcher", ui::kBtnWide())) g_confirm = 1;
        ImGui::SameLine();
#endif
        if (ImGui::Button("Quit to Desktop", ui::kBtnWide())) g_confirm = 2;
    }

    ImGui::End();

    ImGui::Render();
#ifdef MGB64_WEBGPU_BACKEND
    if (wgpu) {
        void *pass = gfx_webgpu_current_overlay_pass();
        int sw = 0, sh = 0;
        gfx_webgpu_current_overlay_size(&sw, &sh);
        if (pass != nullptr) {
            gfx_webgpu_imgui_render(ImGui::GetDrawData(), pass, sw, sh);
        }
    } else
#endif
    {
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    }
}

}  // namespace

int Overlay_gamepadToggleButton() {
    return mgb_config_get_int("Input.MenuToggleButton", SDL_CONTROLLER_BUTTON_BACK);
}

void Overlay_install(SDL_Window *window, const char *argv0) {
    g_window = window;
    if (argv0) std::snprintf(g_argv0, sizeof(g_argv0), "%s", argv0);
    if (std::getenv("MGB64_APP_OVERLAY_TEST")) g_open = true;  // headless render validation
    static AppOverlayHooks hooks;
    hooks.process_event = onProcessEvent;
    hooks.wants_input = onWantsInput;
    hooks.render = onRender;
    platformSetOverlayHooks(&hooks);
}
