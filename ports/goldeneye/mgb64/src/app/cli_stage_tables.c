/*
 * cli_stage_tables.c — see cli_stage_tables.h.
 *
 * The tables and helpers below were MOVED VERBATIM out of
 * src/platform/main_pc.c (they were file-static there). The only edits vs. the
 * originals are mechanical:
 *   - `static` dropped so the symbols have external linkage (they are now
 *     shared by main_pc.c, ui_launch.cpp, and launch_intent.cpp);
 *   - the struct typedefs relocated to cli_stage_tables.h;
 *   - a `kPc*Count` export added per table (consumers can't sizeof() an
 *     incomplete extern array).
 * The table initializer bodies and function bodies are byte-identical to the
 * originals — a verbatim move is the mitigation for the repo's known
 * fidelity-table transcription hazard (docs/audit HARNESS_STRATEGY B1).
 *
 * bondconstants.h is included ONLY for the canonical LEVELID / DIFFICULTY /
 * MP_STAGE / SCENARIO enum values used in the initializers — compile-time
 * constants that emit no symbols and pull in no ROM/SDL data.
 */
#include <stdlib.h>   /* strtol */
#include <strings.h>  /* strcasecmp */

#include "bondconstants.h"      /* LEVELID / DIFFICULTY / MP_STAGE / SCENARIO enums */
#include "cli_stage_tables.h"

const PcStartStage kPcStartStages[] = {
    {  1, LEVELID_DAM,      "dam",       "Dam" },
    {  2, LEVELID_FACILITY, "facility",  "Facility" },
    {  3, LEVELID_RUNWAY,   "runway",    "Runway" },
    {  4, LEVELID_SURFACE,  "surface1",  "Surface 1" },
    {  5, LEVELID_BUNKER1,  "bunker1",   "Bunker 1" },
    {  6, LEVELID_SILO,     "silo",      "Silo" },
    {  7, LEVELID_FRIGATE,  "frigate",   "Frigate" },
    {  8, LEVELID_SURFACE2, "surface2",  "Surface 2" },
    {  9, LEVELID_BUNKER2,  "bunker2",   "Bunker 2" },
    { 10, LEVELID_STATUE,   "statue",    "Statue" },
    { 11, LEVELID_ARCHIVES, "archives",  "Archives" },
    { 12, LEVELID_STREETS,  "streets",   "Streets" },
    { 13, LEVELID_DEPOT,    "depot",     "Depot" },
    { 14, LEVELID_TRAIN,    "train",     "Train" },
    { 15, LEVELID_JUNGLE,   "jungle",    "Jungle" },
    { 16, LEVELID_CONTROL,  "control",   "Control" },
    { 17, LEVELID_CAVERNS,  "caverns",   "Caverns" },
    { 18, LEVELID_CRADLE,   "cradle",    "Cradle" },
    { 19, LEVELID_AZTEC,    "aztec",     "Aztec" },
    { 20, LEVELID_EGYPT,    "egypt",     "Egyptian" },
};
const int kPcStartStagesCount = (int)(sizeof(kPcStartStages) / sizeof(kPcStartStages[0]));

const PcStartStage *pcFindStageByLevelId(int level_id) {
    size_t i;
    for (i = 0; i < sizeof(kPcStartStages) / sizeof(kPcStartStages[0]); i++) {
        if (kPcStartStages[i].level_id == level_id) {
            return &kPcStartStages[i];
        }
    }
    return NULL;
}

/* Exported for the scene-decoration layer (include/decor.h): the per-level
 * decor manifest is keyed by the CLI slug ("surface1", ...). The prototype is
 * repeated here (s32 comes from <ultra64.h> via bondconstants.h) so this TU
 * need not pull in decor.h and its GBI/Gfx dependency. */
const char *pcStageSlugForLevelId(s32 level_id);
const char *pcStageSlugForLevelId(s32 level_id) {
    const PcStartStage *st = pcFindStageByLevelId((int)level_id);
    return st != NULL ? st->slug : NULL;
}

const PcStartStage *pcFindStageByMission(int mission_num) {
    size_t i;
    for (i = 0; i < sizeof(kPcStartStages) / sizeof(kPcStartStages[0]); i++) {
        if (kPcStartStages[i].mission_num == mission_num) {
            return &kPcStartStages[i];
        }
    }
    return NULL;
}

const PcStartStage *pcFindStageByName(const char *name) {
    size_t i;
    if (!name || !*name) return NULL;

    for (i = 0; i < sizeof(kPcStartStages) / sizeof(kPcStartStages[0]); i++) {
        if (strcasecmp(kPcStartStages[i].slug, name) == 0 ||
            strcasecmp(kPcStartStages[i].name, name) == 0) {
            return &kPcStartStages[i];
        }
    }

    if (strcasecmp(name, "surface") == 0) return pcFindStageByLevelId(LEVELID_SURFACE);
    if (strcasecmp(name, "bunker") == 0) return pcFindStageByLevelId(LEVELID_BUNKER1);
    if (strcasecmp(name, "egyptian") == 0) return pcFindStageByLevelId(LEVELID_EGYPT);
    return NULL;
}

int pcParseIntArg(const char *arg, int *out_value) {
    char *end = NULL;
    long value;

    if (!arg || !*arg) return 0;
    value = strtol(arg, &end, 10);
    if (end == arg || *end != '\0') return 0;
    *out_value = (int)value;
    return 1;
}

int pcParseDifficultyArg(const char *arg, int *out_value) {
    int parsed;

    if (!arg || !*arg) return 0;

    if (strcasecmp(arg, "agent") == 0) {
        *out_value = DIFFICULTY_AGENT;
        return 1;
    }
    if (strcasecmp(arg, "secret") == 0 ||
        strcasecmp(arg, "secretagent") == 0 ||
        strcasecmp(arg, "sa") == 0) {
        *out_value = DIFFICULTY_SECRET;
        return 1;
    }
    if (strcasecmp(arg, "00") == 0 ||
        strcasecmp(arg, "00agent") == 0 ||
        strcasecmp(arg, "double0") == 0 ||
        strcasecmp(arg, "double0agent") == 0) {
        *out_value = DIFFICULTY_00;
        return 1;
    }
    if (strcasecmp(arg, "007") == 0 ||
        strcasecmp(arg, "007mode") == 0 ||
        strcasecmp(arg, "007-mode") == 0) {
        *out_value = DIFFICULTY_007;
        return 1;
    }

    if (pcParseIntArg(arg, &parsed)) {
        if (parsed >= DIFFICULTY_AGENT && parsed <= DIFFICULTY_007) {
            *out_value = parsed;
            return 1;
        }
        return 0;
    }

    return 0;
}

/* CLI-friendly names for the multiplayer stage table. The mp_stage index is a
 * MP_STAGE_* enum value (an index into front.c's multi_stage_setups[]); we never
 * embed ROM-derived stage data here, only the public enum index plus a slug. */
const PcMpStage kPcMpStages[] = {
    { MP_STAGE_RANDOM,   "random" },
    { MP_STAGE_TEMPLE,   "temple" },
    { MP_STAGE_COMPLEX,  "complex" },
    { MP_STAGE_CAVES,    "caves" },
    { MP_STAGE_LIBRARY,  "library" },
    { MP_STAGE_BASEMENT, "basement" },
    { MP_STAGE_STACK,    "stack" },
    { MP_STAGE_FACILITY, "facility" },
    { MP_STAGE_BUNKER,   "bunker" },
    { MP_STAGE_ARCHIVES, "archives" },
    { MP_STAGE_CAVERNS,  "caverns" },
    { MP_STAGE_EGYPT,    "egypt" },
};
const int kPcMpStagesCount = (int)(sizeof(kPcMpStages) / sizeof(kPcMpStages[0]));

/* CLI-friendly names for the multiplayer scenarios (combat modes). */
const PcMpScenario kPcMpScenarios[] = {
    { SCENARIO_NORMAL, "normal" },
    { SCENARIO_NORMAL, "deathmatch" },
    { SCENARIO_NORMAL, "combat" },
    { SCENARIO_YOLT,   "yolt" },
    { SCENARIO_TLD,    "flagtag" },
    { SCENARIO_TLD,    "tld" },
    { SCENARIO_MWTGG,  "goldengun" },
    { SCENARIO_MWTGG,  "mwtgg" },
    { SCENARIO_LTK,    "licencetokill" },
    { SCENARIO_LTK,    "ltk" },
    { SCENARIO_2v2,    "2v2" },
    { SCENARIO_3v1,    "3v1" },
    { SCENARIO_2v1,    "2v1" },
};
const int kPcMpScenariosCount = (int)(sizeof(kPcMpScenarios) / sizeof(kPcMpScenarios[0]));

int pcParseMpStageArg(const char *arg, int *out_value) {
    size_t i;
    int parsed;

    if (!arg || !*arg) return 0;

    for (i = 0; i < sizeof(kPcMpStages) / sizeof(kPcMpStages[0]); i++) {
        if (strcasecmp(kPcMpStages[i].slug, arg) == 0) {
            *out_value = kPcMpStages[i].mp_stage;
            return 1;
        }
    }

    if (pcParseIntArg(arg, &parsed)) {
        if (parsed > MP_STAGE_RANDOM && parsed < MP_STAGE_SELECTED_MAX) {
            *out_value = parsed;
            return 1;
        }
        return 0;
    }

    return 0;
}

int pcParseMpScenarioArg(const char *arg, int *out_value) {
    size_t i;
    int parsed;

    if (!arg || !*arg) return 0;

    for (i = 0; i < sizeof(kPcMpScenarios) / sizeof(kPcMpScenarios[0]); i++) {
        if (strcasecmp(kPcMpScenarios[i].slug, arg) == 0) {
            *out_value = kPcMpScenarios[i].scenario;
            return 1;
        }
    }

    if (pcParseIntArg(arg, &parsed)) {
        if (parsed >= SCENARIO_NORMAL && parsed < MPSCENARIOS_MAX) {
            *out_value = parsed;
            return 1;
        }
        return 0;
    }

    return 0;
}
