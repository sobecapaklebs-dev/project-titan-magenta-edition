// test_launch_intent.cpp — unit test for the pure argv -> LaunchIntent parser
// (AUDIT-0060 core) and the single-sourced CLI stage tables it resolves against.
//
// ROM-free by construction: parseLaunchIntent + the shared cli_stage_tables
// helpers touch no ROM, SDL, or engine runtime. CHECK-counter harness (no
// assert(): ctest builds Release -DNDEBUG, which strips assert()).
#include "launch_intent.h"
#include "cli_stage_tables.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

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

void checkStr(const char *name, const std::string &got, const std::string &want) {
    if (got != want) {
        std::printf("FAIL: %s -> '%s' (want '%s')\n", name, got.c_str(), want.c_str());
        ++g_failures;
    }
}

// True if `hay` contains `needle` (used to assert error messages name the arg).
bool contains(const std::string &hay, const char *needle) {
    return hay.find(needle) != std::string::npos;
}

// Parse argv[1..] built from `args` (argv[0] is synthesized).
bool run(std::vector<const char *> args, LaunchIntent &out, std::string &err) {
    std::vector<char *> argv;
    argv.push_back(const_cast<char *>("mgb64"));
    for (const char *a : args) argv.push_back(const_cast<char *>(a));
    return parseLaunchIntent((int)argv.size(), argv.data(), out, err);
}

// Convenience: expect success.
LaunchIntent ok(std::vector<const char *> args, const char *label) {
    LaunchIntent li;
    std::string err;
    if (!run(args, li, err)) {
        std::printf("FAIL: %s expected success, got err='%s'\n", label, err.c_str());
        ++g_failures;
    }
    return li;
}

// Convenience: expect failure, and that err mentions `mustMention` (or nullptr).
void bad(std::vector<const char *> args, const char *label, const char *mustMention) {
    LaunchIntent li;
    std::string err;
    if (run(args, li, err)) {
        std::printf("FAIL: %s expected failure, got success\n", label);
        ++g_failures;
        return;
    }
    if (err.empty()) {
        std::printf("FAIL: %s failed but err was empty\n", label);
        ++g_failures;
    }
    if (mustMention && !contains(err, mustMention)) {
        std::printf("FAIL: %s err '%s' must mention '%s'\n", label, err.c_str(), mustMention);
        ++g_failures;
    }
}

}  // namespace

int main() {
    // -------------------------------------------------------------------
    // Table integrity: single-sourced kPcStartStages must match the known
    // canonical LEVELID values (read from src/bondconstants.h at authoring
    // time). A transcription error in the level_id column would corrupt
    // level resolution for every user — this is the load-bearing guard.
    // -------------------------------------------------------------------
    checkInt("stage count", kPcStartStagesCount, 20);
    struct { const char *slug; int level_id; int mission; const char *name; } known[] = {
        {"dam",      33, 1,  "Dam"},
        {"facility", 34, 2,  "Facility"},
        {"runway",   35, 3,  "Runway"},
        {"surface1", 36, 4,  "Surface 1"},   // slug surface1 -> LEVELID_SURFACE
        {"bunker1",   9, 5,  "Bunker 1"},
        {"frigate",  26, 7,  "Frigate"},
        {"surface2", 43, 8,  "Surface 2"},   // LEVELID_SURFACE2, not adjacent to SURFACE
        {"egypt",    32, 20, "Egyptian"},    // display name differs from slug
    };
    for (auto &k : known) {
        const PcStartStage *s = pcFindStageByName(k.slug);
        std::string lbl = std::string("integrity[") + k.slug + "]";
        if (!s) { std::printf("FAIL: %s not found\n", lbl.c_str()); ++g_failures; continue; }
        checkInt((lbl + ".level_id").c_str(), s->level_id, k.level_id);
        checkInt((lbl + ".mission").c_str(), s->mission_num, k.mission);
        checkStr((lbl + ".name").c_str(), s->name, k.name);
        // Round-trip: resolve by the same level_id and get the same slug back.
        const PcStartStage *r = pcFindStageByLevelId(k.level_id);
        if (!r) { std::printf("FAIL: %s reverse not found\n", lbl.c_str()); ++g_failures; continue; }
        checkStr((lbl + ".roundtrip.slug").c_str(), r->slug, k.slug);
    }
    // Missions are dense 1..20 in table order.
    for (int i = 0; i < kPcStartStagesCount; ++i) {
        checkInt("mission dense", kPcStartStages[i].mission_num, i + 1);
    }

    // -------------------------------------------------------------------
    // Empty argv -> success, everything absent.
    // -------------------------------------------------------------------
    {
        LaunchIntent li = ok({}, "empty");
        checkBool("empty.rom absent", li.rom_path.has_value(), false);
        checkBool("empty.level absent", li.level.has_value(), false);
        checkBool("empty.difficulty absent", li.difficulty.has_value(), false);
        checkBool("empty.preset absent", li.preset.has_value(), false);
        checkBool("empty.multiplayer absent", li.multiplayer.has_value(), false);
        checkBool("empty.players absent", li.players.has_value(), false);
        checkBool("empty.savedir absent", li.savedir.has_value(), false);
    }

    // -------------------------------------------------------------------
    // ROM: explicit flag and positional; absent fields stay absent.
    // -------------------------------------------------------------------
    {
        LaunchIntent li = ok({"--rom", "baserom.u.z64"}, "rom flag");
        checkBool("rom.present", li.rom_path.has_value(), true);
        checkStr("rom.value", li.rom_path.value_or(""), "baserom.u.z64");
        checkBool("rom.level absent", li.level.has_value(), false);
        checkBool("rom.difficulty absent", li.difficulty.has_value(), false);
    }
    {
        LaunchIntent li = ok({"mygame.z64"}, "positional rom");
        checkBool("positional.present", li.rom_path.has_value(), true);
        checkStr("positional.value", li.rom_path.value_or(""), "mygame.z64");
    }

    // -------------------------------------------------------------------
    // Level forms: slug, display-name, raw LEVELID.
    // -------------------------------------------------------------------
    {
        LaunchIntent li = ok({"--level", "facility"}, "level slug");
        checkBool("level slug.present", li.level.has_value(), true);
        checkInt("level slug.id", li.level->level_id, 34);
        checkInt("level slug.mission", li.level->mission, 2);
        checkStr("level slug.slug", li.level->slug, "facility");
    }
    {
        LaunchIntent li = ok({"--level", "Surface 1"}, "level display name");
        checkBool("level name.present", li.level.has_value(), true);
        checkInt("level name.id", li.level->level_id, 36);
        checkStr("level name.slug", li.level->slug, "surface1");
    }
    {
        LaunchIntent li = ok({"--level", "34"}, "level raw LEVELID");
        checkBool("level raw.present", li.level.has_value(), true);
        checkInt("level raw.id", li.level->level_id, 34);
        checkStr("level raw.slug", li.level->slug, "facility");
    }
    {
        // Alias exercised by pcFindStageByName ("surface" -> Surface 1).
        LaunchIntent li = ok({"--level", "surface"}, "level alias");
        checkInt("level alias.id", li.level->level_id, 36);
    }
    // Raw-LEVELID that is really a mission number -> disambiguation error.
    bad({"--level", "5"}, "level mission-as-levelid", "5");
    // Unknown level string.
    bad({"--level", "atlantis"}, "level unknown", "atlantis");

    // -------------------------------------------------------------------
    // --mission: valid resolves via the shared table; out-of-range rejected.
    // -------------------------------------------------------------------
    {
        LaunchIntent li = ok({"--mission", "3"}, "mission valid");
        checkBool("mission.present", li.level.has_value(), true);
        checkInt("mission.id", li.level->level_id, 35);   // mission 3 = runway
        checkStr("mission.slug", li.level->slug, "runway");
    }
    bad({"--mission", "0"},  "mission 0",  "mission");
    bad({"--mission", "21"}, "mission 21", "mission");
    bad({"--mission", "x"},  "mission nan", "mission");

    // -------------------------------------------------------------------
    // Difficulty: every token form + numeric + out-of-range.
    // -------------------------------------------------------------------
    struct { const char *tok; int val; } diffs[] = {
        {"agent", 0}, {"secret", 1}, {"secretagent", 1}, {"sa", 1},
        {"00", 2}, {"00agent", 2}, {"double0", 2}, {"double0agent", 2},
        {"007", 3}, {"007mode", 3}, {"007-mode", 3},
        {"0", 0}, {"1", 1}, {"2", 2}, {"3", 3},
    };
    for (auto &d : diffs) {
        LaunchIntent li = ok({"--difficulty", d.tok}, d.tok);
        std::string lbl = std::string("difficulty[") + d.tok + "]";
        checkBool((lbl + ".present").c_str(), li.difficulty.has_value(), true);
        checkInt((lbl + ".value").c_str(), li.difficulty.value_or(-99), d.val);
    }
    bad({"--difficulty", "4"},   "difficulty 4",  "difficulty");
    bad({"--difficulty", "-1"},  "difficulty -1", "difficulty");
    bad({"--difficulty", "hard"},"difficulty hard","difficulty");

    // -------------------------------------------------------------------
    // Presets: faithful / faithful-hd / remaster.
    // -------------------------------------------------------------------
    checkInt("preset faithful",    ok({"--faithful"}, "faithful").preset.value_or(-1), 1);
    checkInt("preset faithful-hd", ok({"--faithful-hd"}, "faithful-hd").preset.value_or(-1), 2);
    checkInt("preset remaster",    ok({"--remaster"}, "remaster").preset.value_or(-1), 3);

    // -------------------------------------------------------------------
    // Multiplayer / players bounds (2,4 accepted; 1,5 rejected).
    // -------------------------------------------------------------------
    {
        LaunchIntent li = ok({"--multiplayer"}, "multiplayer");
        checkBool("mp.present", li.multiplayer.has_value(), true);
        checkBool("mp.value", li.multiplayer.value_or(false), true);
        checkBool("mp.players absent", li.players.has_value(), false);
    }
    {
        LaunchIntent li = ok({"--players", "2"}, "players 2");
        checkInt("players2.value", li.players.value_or(-1), 2);
        checkBool("players2 implies mp", li.multiplayer.value_or(false), true);
    }
    {
        LaunchIntent li = ok({"--players", "4"}, "players 4");
        checkInt("players4.value", li.players.value_or(-1), 4);
    }
    bad({"--players", "1"}, "players 1", "players");
    bad({"--players", "5"}, "players 5", "players");
    bad({"--players", "x"}, "players nan", "players");

    // -------------------------------------------------------------------
    // savedir.
    // -------------------------------------------------------------------
    {
        LaunchIntent li = ok({"--savedir", "/tmp/saves"}, "savedir");
        checkBool("savedir.present", li.savedir.has_value(), true);
        checkStr("savedir.value", li.savedir.value_or(""), "/tmp/saves");
    }

    // -------------------------------------------------------------------
    // --unlock-all-levels: present -> field true; absent -> absent (so the
    // launcher-seed leaves the demo hatch OFF). Native CLI parity with the
    // headless main_pc.c flag (AUDIT-0060).
    // -------------------------------------------------------------------
    {
        LaunchIntent li = ok({"--unlock-all-levels"}, "unlock-all-levels");
        checkBool("unlock.present", li.unlockAllLevels.has_value(), true);
        checkBool("unlock.value", li.unlockAllLevels.value_or(false), true);
        // A bare flag touches nothing else.
        checkBool("unlock.level absent", li.level.has_value(), false);
        checkBool("unlock.preset absent", li.preset.has_value(), false);
    }
    {
        // Absent when not passed (empty parse already checked, but pin it beside
        // a sibling flag so a stray default can't creep in).
        LaunchIntent li = ok({"--faithful"}, "unlock absent w/ other flag");
        checkBool("unlock.absent", li.unlockAllLevels.has_value(), false);
    }

    // -------------------------------------------------------------------
    // Unmodeled flags: explicitly rejected, err names the flag.
    // -------------------------------------------------------------------
    bad({"--stage-id", "5"},      "stage-id",      "--stage-id");
    bad({"--scenario", "yolt"},   "scenario",      "--scenario");
    bad({"--mp-timelimit", "60"}, "mp-timelimit",  "--mp-timelimit");
    bad({"--mp-stage", "temple"}, "mp-stage",      "--mp-stage");

    // Unknown flag rejected.
    bad({"--wat"}, "unknown flag", "--wat");

    // Automation flags never reach here in production, but must reject
    // defensively with a pointer at --no-ui / the headless engine.
    bad({"--no-ui"},        "no-ui defensive",        "--no-ui");
    bad({"--deterministic"},"deterministic defensive","--no-ui");

    // -------------------------------------------------------------------
    // Missing value (flag at argv end) -> false.
    // -------------------------------------------------------------------
    bad({"--rom"},        "rom missing value",        "--rom");
    bad({"--level"},      "level missing value",      "--level");
    bad({"--difficulty"}, "difficulty missing value", "--difficulty");
    bad({"--players"},    "players missing value",    "--players");

    // -------------------------------------------------------------------
    // A realistic combined direct-play invocation.
    // -------------------------------------------------------------------
    {
        LaunchIntent li = ok({"--rom", "ge.z64", "--level", "dam",
                              "--difficulty", "00", "--faithful"}, "combo");
        checkStr("combo.rom", li.rom_path.value_or(""), "ge.z64");
        checkInt("combo.level", li.level->level_id, 33);
        checkInt("combo.difficulty", li.difficulty.value_or(-1), 2);
        checkInt("combo.preset", li.preset.value_or(-1), 1);
        checkBool("combo.mp absent", li.multiplayer.has_value(), false);
    }

    if (g_failures == 0) {
        std::printf("PASS: all launch_intent cases\n");
        return 0;
    }
    std::printf("%d failure(s)\n", g_failures);
    return 1;
}
