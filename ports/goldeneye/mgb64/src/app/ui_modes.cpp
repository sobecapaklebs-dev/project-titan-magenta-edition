// ui_modes.cpp — Modes & Toggles: visual preset, gameplay hatches, expert env.
#include "ui_launcher.h"
#include "app_config.h"
#include "env_ownership.h"
#include "ui_common.h"

#include "imgui.h"

#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>

namespace {

void portableSetenv(const char *k, const char *v) {
#if defined(_WIN32)
    _putenv_s(k, v);
#else
    setenv(k, v, 1);
#endif
}

void portableUnsetenv(const char *k) {
#if defined(_WIN32)
    _putenv_s(k, "");  // empty value removes the variable on Windows
#else
    unsetenv(k);
#endif
}

void save(const LauncherState &s) {
    AppConfig::set("mode_preset", std::to_string(s.modePreset));
    AppConfig::set("hatch_shoot_lights", s.shootOutLights ? "1" : "0");
    AppConfig::set("hatch_auto_aim", s.autoAim ? "1" : "0");
    AppConfig::set("advanced_env", s.advancedEnv);
    AppConfig::save();
}

}  // namespace

void ModesPanel_ensureInit(LauncherState &s) {
    if (s.modesInitialized) return;
    s.modesInitialized = true;
    AppConfig::load();
    s.modePreset = std::atoi(AppConfig::get("mode_preset", "0").c_str());
    s.shootOutLights = AppConfig::get("hatch_shoot_lights", "1") != "0";
    s.autoAim = AppConfig::get("hatch_auto_aim", "1") != "0";
    std::snprintf(s.advancedEnv, sizeof(s.advancedEnv), "%s",
                  AppConfig::get("advanced_env", "").c_str());
}

void applyModeEnv(const LauncherState &s) {
    // The launcher is AUTHORITATIVE over the GE007_* keys it owns: Return to
    // Launcher re-execs (ui_overlay.cpp) and the child inherits this environment,
    // so an append-only store would let a re-enabled toggle or a deleted advanced
    // override survive silently (AUDIT-0022). Emit an explicit op for every owned
    // key each apply, and reconcile the advanced box against a persisted record so
    // removed keys are unset/restored — never left stale.

    // Hatches: OFF => "0"; ON => unset (engine default ON; and GE007_AUTO_AIM is a
    // scripted-input frame pattern in the engine, so unset is the only sim-safe
    // "on" — setting "1" would forge input).
    for (const EnvOwnership::EnvOp &op : EnvOwnership::hatchOps(s.shootOutLights, s.autoAim)) {
        if (op.set) portableSetenv(op.key.c_str(), op.value.c_str());
        else        portableUnsetenv(op.key.c_str());
    }

    // Unlock-all-levels demo hatch (CLI-only, default OFF; engine default is
    // locked). Authoritative like the hatches above (AUDIT-0022): emit an
    // explicit op every apply — ON => "1", OFF => unset — so a Return-to-Launcher
    // re-exec that inherits a stale "1" is cleared when the flag is not re-set.
    if (s.unlockAllLevels) portableSetenv("GE007_UNLOCK_ALL_LEVELS", "1");
    else                   portableUnsetenv("GE007_UNLOCK_ALL_LEVELS");

    // Advanced expert overrides: reconcile the current KEY=VALUE lines against the
    // keys the launcher owned last time (persisted across the re-exec in the app
    // prefs). A key dropped from the box is unset, or restored to the external
    // value it displaced when first claimed.
    std::map<std::string, std::string> desired = EnvOwnership::parseAdvanced(s.advancedEnv);
    EnvOwnership::Reconciliation rec = EnvOwnership::reconcile(
        desired, AppConfig::get("advanced_env_owned", ""),
        [](const std::string &k) -> std::optional<std::string> {
            const char *v = std::getenv(k.c_str());
            if (v == nullptr) return std::nullopt;
            return std::string(v);
        });
    for (const EnvOwnership::EnvOp &op : rec.ops) {
        if (op.set) portableSetenv(op.key.c_str(), op.value.c_str());
        else        portableUnsetenv(op.key.c_str());
    }
    AppConfig::set("advanced_env_owned", rec.record);
    AppConfig::save();
}

void ModesPanel_draw(LauncherState &s, LauncherAction &out) {
    ModesPanel_ensureInit(s);
    ui::SectionHeader("Modes & Toggles",
                      "Pick a visual preset and gameplay hatches; expert env overrides below.");

    ImGui::TextUnformatted("Visual preset");
    ui::Gap(ui::kGapXS);
    const char *presets[] = {"Custom (use Settings)", "Faithful (1997 look)",
                             "Faithful HD", "Remaster (modern post-FX)"};
    for (int i = 0; i < 4; ++i) {
        if (ImGui::RadioButton(presets[i], s.modePreset == i)) {
            s.modePreset = i;
            save(s);
        }
    }
    ui::Gap(ui::kGapM);

    ImGui::TextUnformatted("Gameplay toggles");
    ui::Gap(ui::kGapXS);
    if (ImGui::Checkbox("Shoot out lights", &s.shootOutLights)) save(s);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Bullets can destroy light fixtures.");
    if (ImGui::Checkbox("Auto-aim assist", &s.autoAim)) save(s);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("N64 auto-aim targeting assist.");
    ui::Gap(ui::kGapM);

    if (ImGui::CollapsingHeader("Advanced (expert, unsupported)")) {
        ui::TextSubtle("Raw GE007_* overrides, one KEY=VALUE per line. Applied at launch.");
        ui::Gap(ui::kGapXS);
        ImGui::InputTextMultiline("##adv", s.advancedEnv, sizeof(s.advancedEnv),
                                  ImVec2(-FLT_MIN, 140.0f));
        if (ImGui::IsItemDeactivatedAfterEdit()) save(s);
    }

    ui::Gap(ui::kGapL);
    PlayButton_draw(s, out);
}
