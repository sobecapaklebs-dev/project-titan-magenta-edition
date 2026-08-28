// launch_intent.cpp — see launch_intent.h.
//
// Parity contract: for every flag this parser MODELS, its resolution/validation
// mirrors src/platform/main_pc.c's headless parse loop exactly (same
// slug/name/raw-LEVELID resolution incl. the mission-disambiguation guidance,
// same difficulty tokens, same players bounds 2-4). Quirks are mirrored and
// noted inline. Two INTENTIONAL divergences (both documented below): (1) unknown
// flags are rejected here, whereas main_pc.c silently ignores them — the brief
// requires "unknown flag -> false"; (2) MP sub-options the launcher/MgbBootConfig
// cannot represent in this first cut (--mp-stage/--scenario/--mp-timelimit) and
// forced-raw --stage-id are rejected rather than accepted, so nothing is
// silently dropped.
#include "launch_intent.h"

#include "cli_stage_tables.h"

#include <cstring>
#include <string>

namespace {

// Value-taking flags the headless engine accepts but the launcher /
// MgbBootConfig has no slot for in this minimal-faithful first cut. Rejected
// explicitly (never silently dropped) with a pointer at the headless path.
// --mp-stage HAS an MgbBootConfig field but no LaunchIntent field yet, so it
// lives here until the E1 wiring promotes it into the modeled set.
const char *const kUnmodeledFlags[] = {
    "--stage-id",
    "--scenario",
    "--mp-timelimit",
    "--mp-stage",
};

bool isUnmodeledFlag(const char *a) {
    for (const char *f : kUnmodeledFlags) {
        if (std::strcmp(a, f) == 0) return true;
    }
    return false;
}

std::string q(const char *s) { return std::string("'") + (s ? s : "") + "'"; }

void setLevelFromStage(LaunchIntent &out, const PcStartStage *s) {
    LaunchLevel lvl;
    lvl.level_id = s->level_id;
    lvl.mission = s->mission_num;
    lvl.slug = s->slug;
    lvl.name = s->name;
    out.level = lvl;
}

}  // namespace

bool parseLaunchIntent(int argc, char **argv, LaunchIntent &out, std::string &err) {
    out = LaunchIntent{};
    err.clear();
    if (!argv) return true;

    // Consume the value argument for a value-taking flag; false + err if absent.
    auto takeValue = [&](int &i, const char *flag) -> const char * {
        if (i + 1 >= argc || argv[i + 1] == nullptr) {
            err = std::string("missing value for ") + flag +
                  " (expected an argument after it).";
            return nullptr;
        }
        return argv[++i];
    };

    for (int i = 1; i < argc; ++i) {
        const char *a = argv[i];
        if (a == nullptr) continue;

        if (std::strcmp(a, "--rom") == 0) {
            const char *v = takeValue(i, "--rom");
            if (!v) return false;
            out.rom_path = v;

        } else if (std::strcmp(a, "--savedir") == 0) {
            const char *v = takeValue(i, "--savedir");
            if (!v) return false;
            out.savedir = v;

        } else if (std::strcmp(a, "--faithful") == 0) {
            out.preset = 1;   // MgbBootConfig.preset: 1=faithful
        } else if (std::strcmp(a, "--faithful-hd") == 0) {
            out.preset = 2;   // 2=faithful-hd
        } else if (std::strcmp(a, "--remaster") == 0) {
            out.preset = 3;   // 3=remaster
            // NB: main_pc.c no longer forces GE007_RENDERER=metal for --remaster
            // (retired 2026-07-16 — WebGPU carries full remaster post-FX/SSAO at
            // GL parity now, so --remaster runs on whatever backend is already
            // selected, WebGPU by default). Nothing for the launcher to record
            // here beyond the preset.

        } else if (std::strcmp(a, "--unlock-all-levels") == 0) {
            // Demo/showcase hatch (main_pc.c parity): mission-select shows every
            // solo stage. Menu availability only — save file and sim untouched.
            // The launcher-seed path forwards this to GE007_UNLOCK_ALL_LEVELS.
            out.unlockAllLevels = true;

        } else if (std::strcmp(a, "--multiplayer") == 0) {
            out.multiplayer = true;

        } else if (std::strcmp(a, "--players") == 0) {
            const char *v = takeValue(i, "--players");
            if (!v) return false;
            int players = 0;
            // main_pc.c parity: pcParseIntArg + bounds 2..4.
            if (!pcParseIntArg(v, &players) || players < 2 || players > 4) {
                err = "invalid --players value " + q(v) + ". Use 2, 3, or 4.";
                return false;
            }
            out.players = players;
            out.multiplayer = true;   // --players implies multiplayer (main_pc.c parity)

        } else if (std::strcmp(a, "--difficulty") == 0) {
            const char *v = takeValue(i, "--difficulty");
            if (!v) return false;
            int diff = 0;
            if (!pcParseDifficultyArg(v, &diff)) {
                err = "invalid --difficulty value " + q(v) +
                      ". Use agent, secret, 00, 007, or 0-3.";
                return false;
            }
            out.difficulty = diff;

        } else if (std::strcmp(a, "--mission") == 0) {
            const char *v = takeValue(i, "--mission");
            if (!v) return false;
            int mission = 0;
            const PcStartStage *stage = nullptr;
            // main_pc.c parity: pcParseIntArg + pcFindStageByMission(1..20).
            if (!pcParseIntArg(v, &mission) || (stage = pcFindStageByMission(mission)) == nullptr) {
                err = "invalid --mission value " + q(v) +
                      ". Use 1-20 for the solo campaign.";
                return false;
            }
            setLevelFromStage(out, stage);

        } else if (std::strcmp(a, "--level") == 0) {
            const char *v = takeValue(i, "--level");
            if (!v) return false;
            // main_pc.c parity: name/slug first, then raw LEVELID, with the
            // mission-disambiguation guidance when a bare number is really a
            // mission index rather than a raw LEVELID.
            const PcStartStage *stage = pcFindStageByName(v);
            int raw = 0;
            if (stage == nullptr && pcParseIntArg(v, &raw)) {
                stage = pcFindStageByLevelId(raw);
                if (stage == nullptr) {
                    const PcStartStage *m = pcFindStageByMission(raw);
                    if (m) {
                        err = "--level " + std::to_string(raw) +
                              " is a raw LEVELID, not solo mission " + std::to_string(raw) +
                              ". If you meant " + m->name + ", use --mission " +
                              std::to_string(m->mission_num) + " or --level " + m->slug +
                              " (raw LEVELID " + std::to_string(m->level_id) + ").";
                    } else {
                        err = "raw LEVELID " + std::to_string(raw) +
                              " is not a supported direct-boot solo mission "
                              "(the launcher models the 20 campaign stages; use --no-ui "
                              "with --stage-id for a forced internal id).";
                    }
                    return false;
                }
            }
            if (stage == nullptr) {
                err = "unknown level " + q(v) +
                      ". Use a solo stage name like 'facility' or a raw LEVELID like 34.";
                return false;
            }
            setLevelFromStage(out, stage);

        } else if (isUnmodeledFlag(a)) {
            err = std::string(a) +
                  " is not supported by the launcher; run with --no-ui for the "
                  "headless engine.";
            return false;

        } else if (a[0] != '-') {
            // Positional ROM path (main_pc.c parity: argv[i][0] != '-').
            out.rom_path = a;

        } else {
            // Any other "--flag". main_pc.c silently ignores unknown flags; the
            // launcher parser is intentionally STRICTER (brief: unknown flag ->
            // false). Automation/diagnostic flags (--no-ui, --deterministic,
            // --record-tape, ...) also land here in the defensive case and are
            // pointed back at the headless path — in production arg_triage routes
            // them before this parser ever runs.
            err = "unknown or unsupported option " + q(a) +
                  ". The launcher models --rom, --level, --mission, --difficulty, "
                  "--faithful, --faithful-hd, --remaster, --multiplayer, --players, "
                  "--savedir, --unlock-all-levels. Automation/diagnostic flags are handled by the "
                  "headless engine -- run with --no-ui.";
            return false;
        }
    }

    return true;
}
