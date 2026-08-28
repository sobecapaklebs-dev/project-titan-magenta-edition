// ui_launch.cpp — Launch Options panel + the shared Play button + boot config.
#include "ui_launcher.h"
#include "app_config.h"
#include "ui_common.h"
#include "cli_stage_tables.h"  // single-sourced level table (was duplicated here — AUDIT-0060)

#include "imgui.h"

#include <cstdio>
#include <cstdlib>
#include <string>

namespace {

// Persist the launch selections so "boot straight to Dam on 00 Agent" survives a
// restart (mirrors the Modes panel). Keys are namespaced to avoid ini collisions.
void saveLaunch(const LauncherState &s) {
    AppConfig::set("launch_level", std::to_string(s.launchLevelIndex));
    AppConfig::set("launch_difficulty", std::to_string(s.launchDifficulty));
    AppConfig::set("launch_multiplayer", s.launchMultiplayer ? "1" : "0");
    AppConfig::set("launch_players", std::to_string(s.launchPlayers));
    AppConfig::save();
}

// The 20 solo campaign level slug/display-name pairs are single-sourced in the
// shared CLI stage table (src/app/cli_stage_tables.h); this panel used to carry
// a private duplicate of that list (AUDIT-0060 / HARNESS_STRATEGY B1). The combo
// shows kPcStartStages[i] at index i+1 (index 0 = "Boot to menu"), and there are
// kPcStartStagesCount of them. Difficulty display names remain launcher-local.
// 0=Agent .. 3=007, matching the engine DIFFICULTY_* enum so a CLI --difficulty
// 007 (AUDIT-0060) round-trips through the launcher instead of reading OOB here.
const char *kDifficulties[] = {"Agent", "Secret Agent", "00 Agent", "007"};
}  // namespace

void fillBoot(const LauncherState &s, MgbBootConfig &b) {
    b.preset = s.modePreset;
    // AUDIT-0060: forward a CLI --savedir override (empty => engine default).
    b.save_dir = s.savedir[0] ? s.savedir : nullptr;
    if (s.launchMultiplayer) {
        b.multiplayer = 1;
        b.players = s.launchPlayers;
    } else if (s.launchLevelIndex > 0 && s.launchLevelIndex <= kPcStartStagesCount) {
        b.level_slug = kPcStartStages[s.launchLevelIndex - 1].slug;
        b.difficulty = s.launchDifficulty;
    }
    applyModeEnv(s);  // setenv the mode hatches + advanced overrides for this boot
}

void launchSummary(const LauncherState &s, char *buf, int n) {
    if (s.launchMultiplayer) {
        std::snprintf(buf, n, "Multiplayer \xE2\x80\xA2 %d players", s.launchPlayers);
    } else if (s.launchLevelIndex > 0 && s.launchLevelIndex <= kPcStartStagesCount) {
        std::snprintf(buf, n, "%s \xE2\x80\xA2 %s", kPcStartStages[s.launchLevelIndex - 1].name,
                      kDifficulties[s.launchDifficulty]);
    } else {
        std::snprintf(buf, n, "Boot to menu");
    }
}

void PlayButton_draw(LauncherState &s, LauncherAction &out) {
    char summary[96];
    launchSummary(s, summary, sizeof(summary));

    if (s.romInfo.valid != 0) {
        if (ui::PrimaryButton("Play", ui::kBtnPrimary())) {
            out.type = LauncherActionType::Play;
            out.boot.rom_path = s.romPath;  // stable: member of the Launcher's state
            fillBoot(s, out.boot);
        }
        ui::Gap(ui::kGapXS);
        ui::TextSubtle("Will boot: %s", summary);
    } else {
        ImGui::BeginDisabled(true);
        ImGui::Button("Play", ui::kBtnPrimary());
        ImGui::EndDisabled();
        ui::Gap(ui::kGapXS);
        ui::TextSubtle("Select a valid ROM first.");
        ImGui::SameLine();
        // Don't dead-end: offer a one-click jump to the Game ROM tab (index 0).
        if (ImGui::SmallButton("Go to Game ROM")) s.requestTab = 0;
    }
}

void LaunchPanel_ensureInit(LauncherState &s) {
    if (s.launchInitialized) return;
    s.launchInitialized = true;
    AppConfig::load();
    s.launchLevelIndex = std::atoi(AppConfig::get("launch_level", "0").c_str());
    s.launchDifficulty = std::atoi(AppConfig::get("launch_difficulty", "0").c_str());
    s.launchMultiplayer = AppConfig::get("launch_multiplayer", "0") != "0";
    s.launchPlayers = std::atoi(AppConfig::get("launch_players", "2").c_str());
    // Clamp to valid ranges in case the ini was hand-edited.
    if (s.launchLevelIndex < 0 || s.launchLevelIndex > kPcStartStagesCount) s.launchLevelIndex = 0;
    if (s.launchDifficulty < 0 || s.launchDifficulty > 3) s.launchDifficulty = 0;
    if (s.launchPlayers < 2 || s.launchPlayers > 4) s.launchPlayers = 2;
}

void LaunchPanel_draw(LauncherState &s, LauncherAction &out) {
    LaunchPanel_ensureInit(s);
    ui::SectionHeader("Launch Options",
                      "Boot to the menu, jump straight to a level, or start multiplayer.");

    if (ImGui::Checkbox("Multiplayer", &s.launchMultiplayer)) saveLaunch(s);
    ui::Gap(ui::kGapS);

    if (s.launchMultiplayer) {
        ImGui::SetNextItemWidth(ui::kControlWidth());
        ImGui::SliderInt("Players", &s.launchPlayers, 2, 4);
        if (ImGui::IsItemDeactivatedAfterEdit()) saveLaunch(s);
        ui::TextSubtle("Split-screen deathmatch on the default stage.");
    } else {
        ImGui::SetNextItemWidth(ui::kControlWidth());
        const char *cur = s.launchLevelIndex == 0 ? "Boot to menu"
                                                  : kPcStartStages[s.launchLevelIndex - 1].name;
        if (ImGui::BeginCombo("Start at", cur)) {
            if (ImGui::Selectable("Boot to menu", s.launchLevelIndex == 0)) { s.launchLevelIndex = 0; saveLaunch(s); }
            for (int i = 0; i < kPcStartStagesCount; ++i) {
                bool sel = (s.launchLevelIndex == i + 1);
                if (ImGui::Selectable(kPcStartStages[i].name, sel)) { s.launchLevelIndex = i + 1; saveLaunch(s); }
                if (sel) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        if (s.launchLevelIndex > 0) {
            ImGui::SetNextItemWidth(ui::kControlWidth());
            if (ImGui::Combo("Difficulty", &s.launchDifficulty, kDifficulties, IM_ARRAYSIZE(kDifficulties)))
                saveLaunch(s);
        }
    }

    ui::Gap(ui::kGapL);
    PlayButton_draw(s, out);
}
