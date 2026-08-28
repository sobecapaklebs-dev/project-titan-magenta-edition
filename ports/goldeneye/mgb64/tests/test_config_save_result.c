/* test_config_save_result.c — AUDIT-0036 core: the config-save boundary must
 * distinguish OK / SUPPRESSED / FAILED, headless and ROM-free.
 *
 * Before this, a faithful-session no-op and a real write were both configSave()
 * == 1, and a real failure was configSave() == 0 with no way to tell it apart
 * from a suppressed save. configSaveResult() now carries the tri-state, with
 * configSave() kept as the historical 0/1 wrapper (OK|SUPPRESSED -> 1, FAILED
 * -> 0). The result threads up through the mgb_config layer:
 * configStagingApply() (the in-game "Apply") and mgb_config_save_result() (the
 * direct "Save Settings") both report the tri-state to the app.
 *
 * Clones the config link set (config_schema.c + config_pc.c + settings.c +
 * savedir.c) from test_config_staging.c against a private temp savedir.
 *
 * CHECK-counter style (NO assert(): ctest builds Release -DNDEBUG, which strips
 * assert() — a bare assert would make every case vacuously pass).
 */
#include "config_schema.h"   /* MgbConfigSaveResult, configStaging*, mgb_config_save_result (src/app) */
#include "settings.h"        /* settingsRegisterInt (src/platform) */
#include "config_pc.h"       /* ConfigSaveResult, configSaveResult, configSave, configSetSaveSuppressed */
#include "savedir.h"         /* savedirInit, savedirPath */

#include <stdio.h>
#include <string.h>
#include <unistd.h>          /* mkdtemp, rmdir */

/* config_schema.c references these from mgb_config_init(), which we never call;
 * stub them so the object links standalone (mirrors test_config_staging.c). */
void platformRegisterConfig(void) {}
void portAudioRegisterConfig(void) {}

static s32 g_val = 5;   /* a registered setting for the staging-commit path */

static int g_fails = 0;
#define CHECK(name, cond) do { \
    if (!(cond)) { printf("FAIL: %s\n", (name)); ++g_fails; } \
} while (0)

static int file_exists(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    fclose(f);
    return 1;
}

/* A real save writes the "# MGB64 ..." header first; proves the file is a genuine
 * config write (parseable), not an empty/partial artifact. */
static int file_starts_with_hash(const char *path) {
    FILE *f = fopen(path, "r");
    int c;
    if (!f) return 0;
    c = fgetc(f);
    fclose(f);
    return c == '#';
}

int main(void) {
    char tmpl[] = "/tmp/mgb64_save_result_XXXXXX";
    const char *dir = mkdtemp(tmpl);
    char ini[1024];

    if (!dir) { printf("FAIL: mkdtemp\n"); return 1; }
    if (savedirInit(dir) != 0) { printf("FAIL: savedirInit(%s)\n", dir); return 1; }

    settingsRegisterInt("Test.Val", &g_val, 5, 0, 1000,
                        SETTING_SCOPE_LIVE, "GE007_TEST_SAVE_RESULT", NULL, "Val", "t");

    /* savedirPath() returns a reused static buffer — snapshot the resolved path. */
    snprintf(ini, sizeof(ini), "%s", savedirPath(CONFIG_FILENAME));

    /* ---- Case OK: writable savedir → OK; file created + parseable; wrapper == 1. */
    remove(ini);
    CHECK("OK: configSaveResult() == CONFIG_SAVE_OK", configSaveResult() == CONFIG_SAVE_OK);
    CHECK("OK: config file created",                  file_exists(ini));
    CHECK("OK: config file parseable (header)",       file_starts_with_hash(ini));
    CHECK("OK: configSave() wrapper returns 1",       configSave() == 1);

    /* ---- Case SUPPRESSED: faithful suppression → SUPPRESSED; wrapper still 1; no file. */
    remove(ini);
    configSetSaveSuppressed(1);
    CHECK("SUPPRESSED: configSaveResult() == CONFIG_SAVE_SUPPRESSED",
          configSaveResult() == CONFIG_SAVE_SUPPRESSED);
    CHECK("SUPPRESSED: configSave() wrapper still returns 1 (compat)", configSave() == 1);
    CHECK("SUPPRESSED: ge007.ini left untouched (nothing written)", !file_exists(ini));
    configSetSaveSuppressed(0);

    /* ---- Staging thread-up (in-game Apply): configStagingApply() reports the
     *      tri-state, and the real staged-commit path still persists on OK. ---- */
    remove(ini);
    configStagingBegin();
    mgb_config_set_int("Test.Val", 42);            /* staged only */
    CHECK("STAGING OK: LIVE untouched while staged", g_val == 5);
    CHECK("STAGING OK: configStagingApply() == MGB_CONFIG_SAVE_OK",
          configStagingApply() == MGB_CONFIG_SAVE_OK);
    CHECK("STAGING OK: staged value committed to live", g_val == 42);
    CHECK("STAGING OK: config file persisted", file_exists(ini));

    remove(ini);
    configSetSaveSuppressed(1);
    configStagingBegin();
    mgb_config_set_int("Test.Val", 7);
    CHECK("STAGING SUPPRESSED: configStagingApply() == MGB_CONFIG_SAVE_SUPPRESSED",
          configStagingApply() == MGB_CONFIG_SAVE_SUPPRESSED);
    CHECK("STAGING SUPPRESSED: staged value still committed to live", g_val == 7);
    CHECK("STAGING SUPPRESSED: no file written", !file_exists(ini));
    configSetSaveSuppressed(0);

    /* ---- Direct thread-up (Save Settings): mgb_config_save_result() tri-state,
     *      while mgb_config_save() keeps its 0/1 contract. ---- */
    remove(ini);
    CHECK("DIRECT: mgb_config_save_result() == MGB_CONFIG_SAVE_OK on writable savedir",
          mgb_config_save_result() == MGB_CONFIG_SAVE_OK);
    CHECK("DIRECT: mgb_config_save() wrapper returns 1 (compat)", mgb_config_save() == 1);
    configSetSaveSuppressed(1);
    CHECK("DIRECT: mgb_config_save_result() == MGB_CONFIG_SAVE_SUPPRESSED when suppressed",
          mgb_config_save_result() == MGB_CONFIG_SAVE_SUPPRESSED);
    CHECK("DIRECT: mgb_config_save() wrapper still 1 when suppressed", mgb_config_save() == 1);
    configSetSaveSuppressed(0);

    /* ---- Case FAILED (last: destroys the savedir): removing the temp dir makes
     *      fopen() of the temp file fail → FAILED; wrapper == 0; no file. ---- */
    remove(ini);
    {
        char tmp[1088];
        snprintf(tmp, sizeof(tmp), "%s.tmp", ini);
        remove(tmp);  /* configSave writes ini.tmp then renames; clear any stray. */
    }
    if (rmdir(dir) != 0) {
        printf("FAIL: FAILED-setup: could not remove savedir '%s' (not empty?)\n", dir);
        ++g_fails;
    } else {
        CHECK("FAILED: configSaveResult() == CONFIG_SAVE_FAILED on missing savedir",
              configSaveResult() == CONFIG_SAVE_FAILED);
        CHECK("FAILED: configSave() wrapper returns 0", configSave() == 0);
        CHECK("FAILED: no config file created", !file_exists(ini));
    }

    if (g_fails == 0) { printf("PASS: all config_save_result cases\n"); return 0; }
    printf("%d failure(s)\n", g_fails);
    return 1;
}
