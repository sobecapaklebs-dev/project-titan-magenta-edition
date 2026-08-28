/*
 * cli_stage_tables.h — single-source CLI stage / difficulty / multiplayer
 * tables + pure parse helpers, shared by the headless engine
 * (src/platform/main_pc.c), the launcher UI (src/app/ui_launch.cpp), and the
 * launch-intent parser (src/app/launch_intent.cpp).
 *
 * These were file-static in main_pc.c and separately DUPLICATED in
 * ui_launch.cpp (AUDIT-0060 / HARNESS_STRATEGY B1). Single-sourcing them here
 * kills the duplicate and lets the launcher resolve levels/difficulties with
 * the exact same semantics as the headless CLI.
 *
 * This header is deliberately engine-free: the struct fields are plain `int`,
 * so no consumer is forced to pull in <ultra64.h>/<bondconstants.h>. The
 * IMPLEMENTATION (cli_stage_tables.c) includes bondconstants.h ONLY to seed the
 * table initializers from the canonical LEVELID / DIFFICULTY / MP_STAGE /
 * SCENARIO enum *values* — compile-time constants that emit no symbols and
 * pull in no ROM/SDL. So the tables stay single-sourced against the enum and
 * this header stays cheap to include from C and C++ alike.
 */
#ifndef MGB64_CLI_STAGE_TABLES_H
#define MGB64_CLI_STAGE_TABLES_H

#ifdef __cplusplus
extern "C" {
#endif

/* --- struct types (moved verbatim from main_pc.c) --- */

typedef struct PcStartStage {
    int mission_num;      /* 1-based solo campaign order */
    int level_id;         /* raw LEVELID enum value */
    const char *slug;     /* CLI-friendly name */
    const char *name;     /* display name */
} PcStartStage;

typedef struct PcMpStage {
    int mp_stage;         /* MP_STAGE_* index into multi_stage_setups[] */
    const char *slug;     /* CLI-friendly name */
} PcMpStage;

typedef struct PcMpScenario {
    int scenario;         /* SCENARIO_* (MPSCENARIOS) index */
    const char *slug;     /* CLI-friendly name */
} PcMpScenario;

/* --- the tables (defined in cli_stage_tables.c) ---
 * Counts are exported alongside each array because consumers see only an
 * incomplete array type here (sizeof() is unavailable across the TU boundary);
 * within cli_stage_tables.c the original sizeof() idiom is preserved. */
extern const PcStartStage kPcStartStages[];
extern const int kPcStartStagesCount;
extern const PcMpStage kPcMpStages[];
extern const int kPcMpStagesCount;
extern const PcMpScenario kPcMpScenarios[];
extern const int kPcMpScenariosCount;

/* --- pure helpers (moved verbatim from main_pc.c) ---
 * Return a matching table entry or NULL; the int parsers return 1/0 and write
 * *out_value on success. None touch the ROM, SDL, or engine globals. */
const PcStartStage *pcFindStageByLevelId(int level_id);
const PcStartStage *pcFindStageByMission(int mission_num);
const PcStartStage *pcFindStageByName(const char *name);
int pcParseIntArg(const char *arg, int *out_value);
int pcParseDifficultyArg(const char *arg, int *out_value);
int pcParseMpStageArg(const char *arg, int *out_value);
int pcParseMpScenarioArg(const char *arg, int *out_value);

#ifdef __cplusplus
}
#endif

#endif  /* MGB64_CLI_STAGE_TABLES_H */
