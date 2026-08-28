// ui_launcher.h — the pre-boot launcher: nav rail + table-driven panel router.
//
// Panels are free functions over a shared LauncherState (like Settings_draw),
// so they compose cleanly and Part 2 can add panels by appending one table row.
#ifndef MGB64_UI_LAUNCHER_H
#define MGB64_UI_LAUNCHER_H

#include "engine_entry.h"   // MgbBootConfig
#include "rom_validate.h"   // RomInfo

#include <string>

class AppHost;
struct LaunchIntent;   // src/app/launch_intent.h (pure CLI parse result)

enum class LauncherActionType { None, Play, Quit };

struct LauncherAction {
    LauncherActionType type = LauncherActionType::None;
    // Valid when type == Play. Defaults: auto-detect ROM, normal boot.
    MgbBootConfig boot = {nullptr, nullptr, -1, -1, 0, 0, -1};
};

// State shared across launcher panels.
struct LauncherState {
    char    romPath[1024] = {0};
    RomInfo romInfo{};
    bool    romInitialized = false;
    int     launchLevelIndex = 0;   // 0 = boot to menu; 1.. = level index + 1
    int     launchDifficulty = 0;   // 0=Agent, 1=Secret Agent, 2=00 Agent, 3=007
    bool    launchMultiplayer = false;
    int     launchPlayers = 2;      // 2..4
    char    savedir[1024] = {0};    // --savedir override (empty => engine default); no pref/ensureInit
    bool    launchInitialized = false;  // launch options loaded from AppConfig

    // Modes & Toggles.
    int     modePreset = 0;         // 0=Custom, 1=faithful, 2=faithful-hd, 3=remaster
    bool    shootOutLights = true;  // GE007_SHOOT_OUT_LIGHTS (engine default ON)
    bool    autoAim = true;         // GE007_AUTO_AIM (engine default ON)
    bool    unlockAllLevels = false; // GE007_UNLOCK_ALL_LEVELS (demo hatch; CLI-only, default OFF)
    char    advancedEnv[2048] = {0};
    bool    modesInitialized = false;

    // A panel can request the shell switch tabs (e.g. the disabled-Play hint
    // jumping to Game ROM). -1 = no request; the shell consumes it after drawing.
    int     requestTab = -1;
};

// Panels (implemented in ui_rom.cpp / ui_launch.cpp / ui_launcher.cpp).
void RomPanel_ensureInit(LauncherState &s);                     // load remembered ROM
void RomPanel_draw(LauncherState &s, LauncherAction &out);
void RomPanel_setRom(LauncherState &s, const char *path);       // drag-and-drop entry (validates)
void LaunchPanel_draw(LauncherState &s, LauncherAction &out);
void LaunchPanel_ensureInit(LauncherState &s);                  // load persisted launch options
void ModesPanel_draw(LauncherState &s, LauncherAction &out);
void ModesPanel_ensureInit(LauncherState &s);                   // load persisted mode selections
void applyModeEnv(const LauncherState &s);                      // setenv hatches + advanced (on Play)
void DiagPanel_draw(LauncherState &s, LauncherAction &out);     // diagnostics: log + export
void BindingsPanel_draw(LauncherState &s, LauncherAction &out); // controls: rebind keyboard

// Shared helpers used by both the ROM and Launch panels.
void PlayButton_draw(LauncherState &s, LauncherAction &out);    // primary Play (or disabled hint)
void fillBoot(const LauncherState &s, MgbBootConfig &boot);     // apply launch options
void launchSummary(const LauncherState &s, char *buf, int n);   // "Dam \xE2\x80\xA2 00 Agent"

// AUDIT-0060: apply a parsed CLI LaunchIntent onto launcher state. Pure (no ROM
// validation, no ensureInit) so it is unit-testable; writes only present fields
// and sets the matching *Initialized flag. Implemented in launch_seed.cpp.
void applyLaunchIntent(LauncherState &s, const LaunchIntent &intent);

class Launcher {
public:
    LauncherAction draw(AppHost &host);

    // AUDIT-0060: seed launcher state from parsed CLI flags BEFORE the first
    // draw, so direct-launch flags actually take effect. Loads remembered prefs
    // first (so absent fields keep their saved values), then overrides only the
    // present CLI fields and validates a CLI-supplied ROM. Returns false with an
    // actionable `err` when the ROM path is unusable (caller exits nonzero before
    // opening a window); true when nothing was supplied or seeding succeeded.
    bool seed(const LaunchIntent &intent, std::string &err);

private:
    LauncherState state_;
    int  active_ = 0;               // index into the panel table
    bool panelEnvChecked_ = false;  // MGB64_APP_PANEL design-review/CI hook
};

#endif  // MGB64_UI_LAUNCHER_H
