// launch_intent.h — pure argv -> LaunchIntent parser (AUDIT-0060 core).
//
// The default MGB64_APP shell routes interactive flags (--rom/--level/
// --difficulty/presets/multiplayer/--savedir) to the launcher instead of the
// headless engine (arg_triage.cpp), but nothing parses them into launcher
// state, so they are silently dropped (AUDIT-0060). This is the pure core that
// turns argv into a validated LaunchIntent; the launcher-seed wiring that
// consumes it is a later phase (E1).
//
// Presence is tracked PER FIELD (std::optional) rather than sentinel-vs-default,
// because the acceptance criterion is "explicit CLI values override remembered
// prefs FOR THAT RUN" — seeding must apply only fields the CLI actually set.
//
// SDL-/ROM-free. For every flag it models, resolution/validation semantics match
// the headless engine (src/platform/main_pc.c) exactly, via the shared
// cli_stage_tables helpers.
#ifndef MGB64_LAUNCH_INTENT_H
#define MGB64_LAUNCH_INTENT_H

#include <optional>
#include <string>

// A resolved solo level. Every modeled --level/--mission form maps to a
// kPcStartStages entry, so we carry the full resolution (raw LEVELID + 1-based
// campaign mission + canonical slug/name).
struct LaunchLevel {
    int level_id = -1;   // raw LEVELID enum value
    int mission = -1;    // 1..20 solo campaign order
    std::string slug;    // canonical CLI slug ("dam", ...)
    std::string name;    // display name ("Dam", ...)
};

struct LaunchIntent {
    std::optional<std::string> rom_path;    // --rom <path> or positional
    std::optional<LaunchLevel> level;       // --level / --mission
    std::optional<int>         difficulty;  // --difficulty (0=Agent..3=007)
    std::optional<int>         preset;       // 1=faithful, 2=faithful-hd, 3=remaster
    std::optional<bool>        multiplayer; // --multiplayer / --players
    std::optional<int>         players;     // --players (2..4)
    std::optional<std::string> savedir;     // --savedir <path>
    std::optional<bool>        unlockAllLevels; // --unlock-all-levels (demo hatch)
};

// Parse argv[1..argc) into `out` (argv[0], the program name, is skipped).
// Returns true on success. On any invalid, unknown, unsupported, or value-less
// flag, returns false and writes an actionable message naming the offending
// argument into `err`. `out` is reset on entry; on failure its contents are
// unspecified (callers should ignore it when false is returned).
bool parseLaunchIntent(int argc, char **argv, LaunchIntent &out, std::string &err);

#endif  // MGB64_LAUNCH_INTENT_H
