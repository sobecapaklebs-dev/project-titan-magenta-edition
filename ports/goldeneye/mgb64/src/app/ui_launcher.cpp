// ui_launcher.cpp — launcher shell: brand + table-driven nav rail + router.
// Individual panels live in ui_rom.cpp / ui_launch.cpp / ui_settings.cpp.
#include "ui_launcher.h"
#include "launch_intent.h"
#include "app_host.h"
#include "app_theme.h"
#include "app_version.h"
#include "config_schema.h"  // mgb_config_get_int (Game.CheckForUpdates status)
#include "ui_common.h"
#include "ui_settings.h"
#include "update_check.h"

#include "imgui.h"

#include <SDL.h>

#include <cstdlib>
#include <cstring>
#include <string>

namespace {

// The releases page a player lands on from the "update available" banner/About.
const char *kReleasesUrl = "https://github.com/akratch/mgb64/releases";

// Wrappers so the panel table has a uniform signature.
void SettingsPanel_draw(LauncherState & /*s*/, LauncherAction & /*out*/) {
    ui::SectionHeader("Settings",
                      "Everything configurable \xE2\x80\x94 generated from the engine's config schema.");
    Settings_draw();
}

void AboutPanel_draw(LauncherState & /*s*/, LauncherAction & /*out*/) {
    ui::SectionHeader("About MGB64", "A native source port built on a faithful decompilation.");
    ImGui::TextWrapped(
        "MGB64 ships no game data \xE2\x80\x94 bring your own ROM. This app is one "
        "portable codebase (Dear ImGui rendered in-process) targeting macOS, "
        "Windows, and Linux.");
    ui::Gap(ui::kGapM);
    ui::TextSubtle("Renderer: OpenGL   \xE2\x80\xA2   License: MIT (first-party)");
    ui::Gap(ui::kGapXS);
    ui::TextSubtle("Overlay: press F1 or the gamepad Back/View button in-game for live settings and quit.");
    ui::Gap(ui::kGapXS);
    ui::TextSubtle("Controller-navigable: D-pad/stick to move, A to select, B to go back.");

    // Honest update status: distinguish checking / up-to-date / available / off,
    // so the version above isn't a dead number the user can't act on.
    ui::Gap(ui::kGapM);
    ImGui::Separator();
    ui::Gap(ui::kGapS);
    char utag[128];
    if (UpdateCheck_bannerTag(utag, sizeof(utag))) {
        ImGui::PushStyleColor(ImGuiCol_Text, AppTheme::accent());
        ImGui::Text("A newer version is available: %s", utag);
        ImGui::PopStyleColor();
        ui::Gap(ui::kGapXS);
        if (ImGui::Button("Open releases page")) SDL_OpenURL(kReleasesUrl);
    } else if (!UpdateCheck_isDone()) {
        ui::TextSubtle("Checking for updates\xE2\x80\xA6");
    } else if (UpdateCheck_didCheck()) {
        ui::TextSubtle("You're on the latest version (v%s).", AppVersion());
    } else if (mgb_config_get_int("Game.CheckForUpdates", 1) == 0) {
        ui::TextSubtle("Update checks are off \xE2\x80\x94 enable in Settings \xE2\x96\xB8 Game.");
    } else {
        ui::TextSubtle("Couldn't reach the update server (offline, or curl unavailable).");
    }
}

struct PanelDef {
    const char *label;
    const char *envKey;  // MGB64_APP_PANEL keyword
    void (*draw)(LauncherState &, LauncherAction &);
};

const PanelDef kPanels[] = {
    {"Game ROM", "rom", RomPanel_draw},
    {"Settings", "settings", SettingsPanel_draw},
    {"Launch Options", "launch", LaunchPanel_draw},
    {"Controls", "controls", BindingsPanel_draw},
    {"Modes & Toggles", "modes", ModesPanel_draw},
    {"Diagnostics", "diag", DiagPanel_draw},
    {"About", "about", AboutPanel_draw},
};
const int kNumPanels = IM_ARRAYSIZE(kPanels);

// Full-width nav item: accent left-bar + primary tint when selected, white label.
bool navButton(const char *label, bool selected) {
    ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.0f, 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 7.0f);
    if (selected) {
        ImVec4 p = AppTheme::primary();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(p.x, p.y, p.z, 0.22f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(p.x, p.y, p.z, 0.30f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(p.x, p.y, p.z, 0.38f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));  // white selected label
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    }
    bool clicked = ImGui::Button(label, ImVec2(-FLT_MIN, 42.0f));
    if (selected) {
        ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
        ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(mn.x, mn.y + 6), ImVec2(mn.x + 3.0f, mx.y - 6),
                                                  ImGui::GetColorU32(AppTheme::accent()), 2.0f);
        ImGui::PopStyleColor(4);
    } else {
        ImGui::PopStyleColor(1);
    }
    ImGui::PopStyleVar(2);
    return clicked;
}

void drawNavRail(int &active, LauncherAction &action) {
    ImGui::BeginChild("##nav", ImVec2(ui::kNavWidth(), 0), true);

    ui::Gap(ui::kGapXS);
    ImGui::PushFont(AppTheme::fonts().title);
    ImGui::PushStyleColor(ImGuiCol_Text, AppTheme::accent());
    ImGui::TextUnformatted("MGB64");
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImGui::PushFont(AppTheme::fonts().small);
    ui::TextSubtle("Man with the Golden Build");
    ImGui::PopFont();
    ui::Gap(ui::kGapM);

    for (int i = 0; i < kNumPanels; ++i) {
        if (navButton(kPanels[i].label, active == i)) active = i;
        // Seed initial nav focus on the active tab (once, on window appear) so a
        // gamepad/keyboard has an anchor. Does not force a visible highlight
        // until nav is actually engaged, so mouse/keyboard UX is unchanged.
        if (active == i) ImGui::SetItemDefaultFocus();
    }

    // Pin a Quit button + version line to the bottom of the rail. Quit is the
    // only in-app exit on a handheld forced into borderless fullscreen (no window
    // chrome), and it's controller-reachable here (a nav-focusable Button), unlike
    // the OS window-close. Pre-boot, there's nothing to lose, so it exits directly.
    const float quitH = 34.0f;
    const float versionH = ImGui::GetTextLineHeight();
    const float footerH = quitH + ui::kGapXS + versionH;
    float avail = ImGui::GetContentRegionAvail().y;
    if (avail > footerH) ui::Gap(avail - footerH);

    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 7.0f);
    if (ImGui::Button("Quit", ImVec2(-FLT_MIN, quitH))) action.type = LauncherActionType::Quit;
    ImGui::PopStyleVar();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Exit MGB64");
    ui::Gap(ui::kGapXS);

    ImGui::PushFont(AppTheme::fonts().small);
    ui::TextSubtle("v%s \xE2\x80\xA2 GL", AppVersion());
    ImGui::PopFont();

    ImGui::EndChild();
}

// Quiet, dismissible "a newer MGB64 is available" row. Drawn full-width above the
// nav/content split only when the background check found a strictly-newer,
// not-previously-dismissed release. Non-modal: it never steals focus or blocks.
void drawUpdateBanner() {
    char tag[128];
    if (!UpdateCheck_bannerTag(tag, sizeof(tag))) return;

    ImVec4 p = AppTheme::primary();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(p.x, p.y, p.z, 0.16f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 7.0f);
    ImGui::BeginChild("##updatebanner", ImVec2(0, 44), true, ImGuiWindowFlags_NoScrollbar);

    ImGui::AlignTextToFramePadding();
    ImGui::PushStyleColor(ImGuiCol_Text, AppTheme::accent());
    ImGui::Text("MGB64 %s is available", tag);
    ImGui::PopStyleColor();

    const float bwOpen = 168.0f, bwDismiss = 90.0f;
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    float rightX = ImGui::GetWindowContentRegionMax().x - bwOpen - bwDismiss - spacing;
    ImGui::SameLine();
    if (rightX > ImGui::GetCursorPosX()) ImGui::SetCursorPosX(rightX);
    if (ImGui::Button("Open releases page", ImVec2(bwOpen, 0))) SDL_OpenURL(kReleasesUrl);
    ImGui::SameLine();
    if (ImGui::Button("Dismiss", ImVec2(bwDismiss, 0))) UpdateCheck_dismiss(tag);

    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    ui::Gap(ui::kGapS);
}

}  // namespace

bool Launcher::seed(const LaunchIntent &intent, std::string &err) {
    // No CLI launch flags supplied => leave the no-arg path byte-identical: don't
    // pre-load prefs or validate anything, just let draw()'s lazy init run.
    const bool any = intent.rom_path || intent.level || intent.difficulty ||
                     intent.preset || intent.multiplayer || intent.players ||
                     intent.savedir;
    if (!any) return true;

    // ensureInit-first: load ALL remembered prefs (and set the *Initialized flags)
    // BEFORE overriding present fields. The flags are per-PANEL, not per-field, so
    // if we set launchInitialized for a CLI --level but never loaded the remembered
    // difficulty, that sibling pref would be lost. Loading first, then overriding,
    // preserves the acceptance criterion: present CLI fields win, absent fields
    // keep their remembered values. draw()'s later ensureInit calls are no-ops.
    RomPanel_ensureInit(state_);
    LaunchPanel_ensureInit(state_);
    ModesPanel_ensureInit(state_);

    applyLaunchIntent(state_, intent);

    // A CLI ROM path outside the scan/default locations must be honored exactly,
    // and an unusable one is a launch-time error (not a silent fallback to a
    // remembered ROM). Validate with the same portable header check the ROM panel
    // uses; applyLaunchIntent already marked romInitialized so draw() won't reload.
    if (intent.rom_path) {
        state_.romInfo = mgb_validate_rom(state_.romPath);
        if (!state_.romInfo.valid) {
            err = std::string("--rom '") + state_.romPath + "' is not a usable ROM: " +
                  state_.romInfo.message;
            return false;
        }
    }
    return true;
}

LauncherAction Launcher::draw(AppHost &host) {
    LauncherAction action;

    // RX.4: a file dragged onto the window sets the ROM and jumps to the ROM tab.
    std::string dropped = host.takeDroppedFile();
    if (!dropped.empty()) {
        RomPanel_setRom(state_, dropped.c_str());
        active_ = 0;  // Game ROM panel
    }

    if (!panelEnvChecked_) {
        panelEnvChecked_ = true;
        if (const char *p = std::getenv("MGB64_APP_PANEL")) {
            for (int i = 0; i < kNumPanels; ++i) {
                if (std::strcmp(p, kPanels[i].envKey) == 0) { active_ = i; break; }
            }
        }
    }
    // Load ALL persisted selections up front, regardless of the active panel, so
    // Play from any tab honors the user's saved ROM, launch options, and mode/
    // toggle choices (previously mode+launch state was only loaded if you visited
    // those tabs, so a direct Play used struct defaults).
    RomPanel_ensureInit(state_);
    LaunchPanel_ensureInit(state_);
    ModesPanel_ensureInit(state_);

    const ImGuiViewport *vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                             ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollbar;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ui::kGapM, ui::kGapM));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::Begin("##launcher", nullptr, flags);

    drawUpdateBanner();

    drawNavRail(active_, action);

    ImGui::SameLine();
    ImGui::BeginChild("##content", ImVec2(0, 0), false);
    ui::Gap(2.0f);
    if (active_ >= 0 && active_ < kNumPanels) {
        kPanels[active_].draw(state_, action);
    }
    // A panel may request a tab switch (e.g. disabled-Play → Game ROM).
    if (state_.requestTab >= 0 && state_.requestTab < kNumPanels) {
        active_ = state_.requestTab;
    }
    state_.requestTab = -1;
    ImGui::EndChild();

    ImGui::End();
    ImGui::PopStyleVar(2);
    return action;
}
