// test_launcher_seed.cpp — unit test for applyLaunchIntent (AUDIT-0060 E1).
//
// applyLaunchIntent is the PURE half of Launcher::seed: it writes each PRESENT
// LaunchIntent field into LauncherState and sets the matching per-panel
// *Initialized flag, so the launcher's lazy *_ensureInit calls do not clobber
// the CLI value. It does NOT touch ROM validation (file IO) — that lives in
// Launcher::seed at the call site, verified process-side. This test is therefore
// ROM-/SDL-/ImGui-free: it links only launch_seed.cpp.
//
// The per-field "override only when present" contract (AUDIT-0060 acceptance
// criterion) is what these cases pin: absent optionals leave state untouched.
// CHECK-counter harness (no assert(): ctest builds Release -DNDEBUG).
#include "ui_launcher.h"
#include "launch_intent.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace {

int g_failures = 0;

void checkBool(const char *name, bool got, bool want) {
    if (got != want) {
        std::printf("FAIL: %s -> %s (want %s)\n", name,
                    got ? "true" : "false", want ? "true" : "false");
        ++g_failures;
    }
}

void checkInt(const char *name, long got, long want) {
    if (got != want) {
        std::printf("FAIL: %s -> %ld (want %ld)\n", name, got, want);
        ++g_failures;
    }
}

void checkStr(const char *name, const char *got, const char *want) {
    if (std::strcmp(got, want) != 0) {
        std::printf("FAIL: %s -> '%s' (want '%s')\n", name, got, want);
        ++g_failures;
    }
}

// A LaunchLevel resolved as if the parser matched --level dam (mission 1).
LaunchLevel dam() {
    LaunchLevel l;
    l.level_id = 4;   // LEVELID_DAM (value irrelevant to the seed mapping)
    l.mission = 1;    // 1-based campaign order -> launchLevelIndex
    l.slug = "dam";
    l.name = "Dam";
    return l;
}

}  // namespace

int main() {
    // --- Each present field -> matching state field + Initialized flag. ---
    {
        LaunchIntent in;
        in.level = dam();
        LauncherState s;
        applyLaunchIntent(s, in);
        checkInt("level.launchLevelIndex", s.launchLevelIndex, 1);
        checkBool("level.launchInitialized", s.launchInitialized, true);
        // Untouched panels stay uninitialized (so their prefs still load later).
        checkBool("level.modesInitialized-untouched", s.modesInitialized, false);
        checkBool("level.romInitialized-untouched", s.romInitialized, false);
    }
    {
        // Mission 14 (train) -> index 14.
        LaunchIntent in;
        LaunchLevel l = dam();
        l.mission = 14;
        l.slug = "train";
        in.level = l;
        LauncherState s;
        applyLaunchIntent(s, in);
        checkInt("mission14.launchLevelIndex", s.launchLevelIndex, 14);
    }
    {
        // 007 difficulty (index 3) must round-trip — the launcher models 0..3.
        LaunchIntent in;
        in.difficulty = 3;
        LauncherState s;
        applyLaunchIntent(s, in);
        checkInt("difficulty.launchDifficulty", s.launchDifficulty, 3);
        checkBool("difficulty.launchInitialized", s.launchInitialized, true);
    }
    {
        LaunchIntent in;
        in.preset = 3;   // remaster
        LauncherState s;
        applyLaunchIntent(s, in);
        checkInt("preset.modePreset", s.modePreset, 3);
        checkBool("preset.modesInitialized", s.modesInitialized, true);
        checkBool("preset.launchInitialized-untouched", s.launchInitialized, false);
    }
    {
        LaunchIntent in;
        in.multiplayer = true;
        in.players = 4;
        LauncherState s;
        applyLaunchIntent(s, in);
        checkBool("mp.launchMultiplayer", s.launchMultiplayer, true);
        checkInt("mp.launchPlayers", s.launchPlayers, 4);
        checkBool("mp.launchInitialized", s.launchInitialized, true);
    }
    {
        LaunchIntent in;
        in.rom_path = std::string("/outside/scan/valid.z64");
        LauncherState s;
        applyLaunchIntent(s, in);
        checkStr("rom.romPath", s.romPath, "/outside/scan/valid.z64");
        checkBool("rom.romInitialized", s.romInitialized, true);
    }
    {
        LaunchIntent in;
        in.savedir = std::string("/custom/saves");
        LauncherState s;
        applyLaunchIntent(s, in);
        checkStr("savedir.savedir", s.savedir, "/custom/saves");
    }
    {
        // --unlock-all-levels demo hatch -> LauncherState.unlockAllLevels; sibling
        // panels untouched (applyModeEnv forwards it to GE007_UNLOCK_ALL_LEVELS).
        LaunchIntent in;
        in.unlockAllLevels = true;
        LauncherState s;
        applyLaunchIntent(s, in);
        checkBool("unlock.unlockAllLevels", s.unlockAllLevels, true);
        checkBool("unlock.modesInitialized-untouched", s.modesInitialized, false);
        checkBool("unlock.launchInitialized-untouched", s.launchInitialized, false);
    }

    // --- Empty intent: nothing written, all flags stay false. ---
    {
        LaunchIntent in;
        LauncherState s;
        // Pre-fill sentinels; an absent field must leave every one untouched.
        s.launchLevelIndex = 7;
        s.launchDifficulty = 2;
        s.launchMultiplayer = true;
        s.launchPlayers = 3;
        s.modePreset = 1;
        s.unlockAllLevels = true;   // remembered/sentinel: absent field must not clear it
        std::snprintf(s.romPath, sizeof(s.romPath), "%s", "/remembered.z64");
        std::snprintf(s.savedir, sizeof(s.savedir), "%s", "/remembered/saves");
        applyLaunchIntent(s, in);
        checkInt("empty.launchLevelIndex", s.launchLevelIndex, 7);
        checkInt("empty.launchDifficulty", s.launchDifficulty, 2);
        checkBool("empty.launchMultiplayer", s.launchMultiplayer, true);
        checkInt("empty.launchPlayers", s.launchPlayers, 3);
        checkInt("empty.modePreset", s.modePreset, 1);
        checkStr("empty.romPath", s.romPath, "/remembered.z64");
        checkStr("empty.savedir", s.savedir, "/remembered/saves");
        checkBool("empty.unlockAllLevels", s.unlockAllLevels, true);
        // No field present => no Initialized flag flipped.
        checkBool("empty.launchInitialized", s.launchInitialized, false);
        checkBool("empty.modesInitialized", s.modesInitialized, false);
        checkBool("empty.romInitialized", s.romInitialized, false);
    }

    // --- Partial intent: only the present field overrides; siblings untouched. ---
    {
        LaunchIntent in;
        in.level = dam();   // only level present
        LauncherState s;
        s.launchDifficulty = 2;   // remembered pref (sentinel)
        s.modePreset = 1;         // remembered pref (sentinel)
        applyLaunchIntent(s, in);
        checkInt("partial.launchLevelIndex", s.launchLevelIndex, 1);   // overridden
        checkInt("partial.launchDifficulty", s.launchDifficulty, 2);   // untouched
        checkInt("partial.modePreset", s.modePreset, 1);               // untouched
    }

    if (g_failures == 0) {
        std::printf("PASS: launcher_seed (all applyLaunchIntent cases)\n");
        return 0;
    }
    std::printf("FAIL: launcher_seed (%d failures)\n", g_failures);
    return 1;
}
