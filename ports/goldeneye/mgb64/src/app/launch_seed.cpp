// launch_seed.cpp — pure application of a parsed LaunchIntent onto LauncherState
// (AUDIT-0060 E1). Deliberately ImGui-/SDL-/ROM-free so it is unit-testable
// (tests/test_launcher_seed.cpp); the ROM-validation + ensureInit ordering that
// makes CLI values authoritative over remembered prefs lives in Launcher::seed
// (ui_launcher.cpp), which calls this after loading the prefs.
//
// Contract: write ONLY the fields the CLI actually set (std::optional presence),
// and for each set the matching per-panel *Initialized flag so the launcher's
// lazy *_ensureInit calls become no-ops and cannot clobber the CLI value. Absent
// fields are left exactly as-is — that per-field override-only-when-present
// behavior is the AUDIT-0060 acceptance criterion.
#include "ui_launcher.h"
#include "launch_intent.h"

#include <cstdio>

void applyLaunchIntent(LauncherState &s, const LaunchIntent &in) {
    if (in.rom_path) {
        std::snprintf(s.romPath, sizeof(s.romPath), "%s", in.rom_path->c_str());
        // Validation (mgb_validate_rom, file IO) is Launcher::seed's job; here we
        // only mark the ROM panel seeded so its ensureInit won't reload last_rom.
        s.romInitialized = true;
    }
    if (in.level) {
        // LaunchLevel.mission is the 1-based campaign order; launchLevelIndex uses
        // the same 1-based scheme (0 = boot to menu, i+1 selects kPcStartStages[i],
        // and kPcStartStages[i].mission_num == i+1), so the mission IS the index.
        s.launchLevelIndex = in.level->mission;
        s.launchInitialized = true;
    }
    if (in.difficulty) {
        s.launchDifficulty = *in.difficulty;   // 0=Agent .. 3=007
        s.launchInitialized = true;
    }
    if (in.multiplayer) {
        s.launchMultiplayer = *in.multiplayer;
        s.launchInitialized = true;
    }
    if (in.players) {
        s.launchPlayers = *in.players;
        s.launchInitialized = true;
    }
    if (in.preset) {
        s.modePreset = *in.preset;   // 1=faithful, 2=faithful-hd, 3=remaster
        s.modesInitialized = true;
    }
    if (in.unlockAllLevels) {
        // Demo hatch: menu availability only. applyModeEnv forwards this to
        // GE007_UNLOCK_ALL_LEVELS at Play. No persisted pref / ensureInit, so no
        // Initialized flag — the field defaults OFF and only the CLI sets it.
        s.unlockAllLevels = *in.unlockAllLevels;
    }
    if (in.savedir) {
        // No persisted savedir pref / ensureInit, so no Initialized flag: fillBoot
        // forwards this straight into MgbBootConfig.save_dir at Play.
        std::snprintf(s.savedir, sizeof(s.savedir), "%s", in.savedir->c_str());
    }
}
