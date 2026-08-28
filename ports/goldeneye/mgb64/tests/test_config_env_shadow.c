/* test_config_env_shadow.c -- AUDIT-0055 regression guard.
 *
 * A transient GE007_* environment override must affect one launch only and must
 * NEVER erase or change the user's persisted (on-disk) config value. Because
 * configSave() rewrites the WHOLE file atomically, the old code omitted every
 * SETTING_OVERRIDE_ENV key -- and in a full rewrite, omission = deletion, so the
 * next launch fell back to the compiled default (the bug).
 *
 * The fix snapshots each key's durable on-disk value at load (the "shadow") and
 * serializes the shadow -- not the live env value -- for env-overridden keys.
 * A durable edit (configSetValue: --config-set, in-game Apply) clears the ENV
 * marking so the edit persists; the staging live-preview path is contained so a
 * previewed-then-discarded env key does not leak its env value into the save.
 *
 * ROM-free: links only the registry + config layers (config_schema.c /
 * config_pc.c / settings.c / savedir.c) against a private temp savedir. It
 * exercises the real configInit -> settingsApplyEnvOverrides -> configSetValue ->
 * configSave -> reload lifecycle across all five setting types. No SDL/engine.
 */
#include "config_schema.h"   /* configStaging* + mgb_config_set_* (src/app hdr) */
#include "settings.h"        /* settingsRegister* / settingsApplyEnvOverrides   */
#include "config_pc.h"       /* configInit / configSave / configSetValue        */
#include "savedir.h"         /* savedirInit / savedirPath                        */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>   /* mkdtemp */

/* config_schema.c references these from mgb_config_init(), which we never call;
 * stub them so the object links standalone (mirrors test_config_staging.c). */
void platformRegisterConfig(void) {}
void portAudioRegisterConfig(void) {}

/* Live globals the registry binds to (the engine's "real" values). */
static s32  g_int   = 10;
static u32  g_uint  = 20;
static f32  g_float = 1.5f;
static s32  g_enum  = 0;
static char g_str[64] = "def";

static const ConfigEnumOption kLevels[] = {
    { "off",  0 },
    { "low",  1 },
    { "high", 2 },
};

static int g_fails = 0;
#define CHECK(name, cond) do { \
    if (!(cond)) { printf("FAIL: %s\n", (name)); ++g_fails; } \
} while (0)

/* float equality is exact here -- every value is a small literal we set/clamp. */
static int feq(float a, float b) { return a == b; }

/* Read the whole ge007.ini in the temp savedir into a caller buffer. */
static void readConfigFile(char *buf, size_t n) {
    const char *path = savedirPath(CONFIG_FILENAME);
    FILE *f = fopen(path, "r");
    size_t got = 0;
    buf[0] = '\0';
    if (!f) return;
    got = fread(buf, 1, n - 1, f);
    buf[got] = '\0';
    fclose(f);
}

static int fileHas(const char *needle) {
    char buf[8192];
    readConfigFile(buf, sizeof(buf));
    return strstr(buf, needle) != NULL;
}

static void registerAll(void) {
    settingsRegisterInt("Test.IntVal", &g_int, 10, 0, 1000,
                        SETTING_SCOPE_LIVE, "GE007_TEST_INT", NULL, "Int", "t");
    settingsRegisterUInt("Test.UintVal", &g_uint, 20, 0, 1000,
                         SETTING_SCOPE_LIVE, "GE007_TEST_UINT", NULL, "Uint", "t");
    settingsRegisterFloat("Test.FloatVal", &g_float, 1.5f, 0.0f, 100.0f,
                          SETTING_SCOPE_LIVE, "GE007_TEST_FLOAT", NULL, "Float", "t");
    settingsRegisterEnum("Test.EnumVal", &g_enum, 0, kLevels, 3,
                         SETTING_SCOPE_LIVE, "GE007_TEST_ENUM", NULL, "Enum", "t");
    settingsRegisterString("Test.StrVal", g_str, sizeof(g_str), "def",
                           SETTING_SCOPE_LIVE, "GE007_TEST_STR", NULL, "Str", "t");
}

/* Seed the on-disk config with the user's durable values (flags stay NONE). */
static void seedDurable(void) {
    configSetValue("Test.IntVal",   "77");
    configSetValue("Test.UintVal",  "88");
    configSetValue("Test.FloatVal", "3.25");
    configSetValue("Test.EnumVal",  "high");
    configSetValue("Test.StrVal",   "durable/path");
    CHECK("seed save succeeds", configSave());
}

static void setEnvAll(void) {
    setenv("GE007_TEST_INT",   "11",       1);
    setenv("GE007_TEST_UINT",  "22",       1);
    setenv("GE007_TEST_FLOAT", "9.5",      1);
    setenv("GE007_TEST_ENUM",  "low",      1);
    setenv("GE007_TEST_STR",   "env/path", 1);
}

static void unsetEnvAll(void) {
    unsetenv("GE007_TEST_INT");
    unsetenv("GE007_TEST_UINT");
    unsetenv("GE007_TEST_FLOAT");
    unsetenv("GE007_TEST_ENUM");
    unsetenv("GE007_TEST_STR");
}

int main(void) {
    char tmpl[] = "/tmp/mgb64_env_shadow_XXXXXX";
    const char *dir = mkdtemp(tmpl);
    if (!dir) { printf("FAIL: mkdtemp\n"); return 1; }
    if (savedirInit(dir) != 0) { printf("FAIL: savedirInit(%s)\n", dir); return 1; }

    registerAll();
    seedDurable();

    /* ================================================================= *
     * Phase 1 -- staging live-preview must not leak an env value on save.
     * IntVal's override flag is NONE here (fresh from seed).
     * ================================================================= */
    configInit();                       /* reload durable; capture shadow */
    setenv("GE007_TEST_INT", "11", 1);
    settingsApplyEnvOverrides();        /* IntVal live=11, flag ENV */
    CHECK("preview: env applied to live", g_int == 11);

    configStagingBegin();
    mgb_config_set_int("Test.IntVal", 50);   /* staged only */
    configStagingPreview("Test.IntVal", 1);  /* push staged 50 to live */
    CHECK("preview: on pushes staged to live", g_int == 50);
    configStagingPreview("Test.IntVal", 0);  /* revert to env value */
    configStagingDiscard();
    CHECK("preview: reverted to env value", g_int == 11);

    CHECK("preview save succeeds", configSave());
    /* The env override was never a durable edit -- the file must still hold the
     * user's durable 77, not the previewed 50 and not the env 11. */
    CHECK("preview: file keeps durable IntVal=77", fileHas("IntVal=77"));
    CHECK("preview: file does NOT leak env IntVal=11", !fileHas("IntVal=11"));
    CHECK("preview: file does NOT leak preview IntVal=50", !fileHas("IntVal=50"));

    /* Re-seed IntVal durable for the clean-slate matrix below (clears flag). */
    unsetenv("GE007_TEST_INT");
    configSetValue("Test.IntVal", "77");
    CHECK("re-seed save succeeds", configSave());

    /* ================================================================= *
     * Phase 2 -- the two-launch persistence matrix, all five types.
     *   launch A: durable values on disk (seeded above).
     *   launch B: transient env overrides + one durable --config-set edit.
     *   launch C: env removed -> exact durable values restored.
     * ================================================================= */
    configInit();                       /* launch B start: load durable + shadow */
    setEnvAll();
    settingsApplyEnvOverrides();        /* all five live=env, flags ENV */
    CHECK("matrix: env float applied", feq(g_float, 9.5f));

    /* A durable --config-set on an actively env-overridden key must persist the
     * edit (not the shadow, not the env value, and not be silently omitted). */
    configSetValue("Test.IntVal", "99");

    CHECK("matrix: save succeeds under active env override", configSave());

    /* File assertions: env-overridden keys serialize their durable shadow; the
     * durably-edited key serializes its new value. */
    CHECK("matrix: durable edit persists IntVal=99",   fileHas("IntVal=99"));
    CHECK("matrix: shadow persists UintVal=88",        fileHas("UintVal=88"));
    CHECK("matrix: shadow persists FloatVal=3.25",     fileHas("FloatVal=3.25"));
    CHECK("matrix: shadow persists EnumVal=high",      fileHas("EnumVal=high"));
    CHECK("matrix: shadow persists StrVal=durable/path", fileHas("StrVal=durable/path"));
    /* env values must NOT reach disk */
    CHECK("matrix: env UintVal=22 not persisted",  !fileHas("UintVal=22"));
    CHECK("matrix: env FloatVal=9.5 not persisted", !fileHas("FloatVal=9.5"));
    CHECK("matrix: env EnumVal=low not persisted",  !fileHas("EnumVal=low"));
    CHECK("matrix: env StrVal=env/path not persisted", !fileHas("StrVal=env/path"));

    /* launch C: env removed, fresh load -> exact durable values restored. */
    unsetEnvAll();
    g_int = 0; g_uint = 0; g_float = 0.0f; g_enum = -1; g_str[0] = '\0';
    configInit();
    CHECK("restore: IntVal durable edit stuck (99)", g_int == 99);
    CHECK("restore: UintVal exact durable (88)",     g_uint == 88);
    CHECK("restore: FloatVal exact durable (3.25)",  feq(g_float, 3.25f));
    CHECK("restore: EnumVal exact durable (high=2)", g_enum == 2);
    CHECK("restore: StrVal exact durable",           strcmp(g_str, "durable/path") == 0);

    /* ================================================================= *
     * Phase 3 -- a REJECTED edit to an env-overridden key must not clear the
     * override marking. configSetValue() returns "key found" even when the value
     * is rejected and the previous (env) value is kept (NaN float, unknown enum
     * token); clearing the marking there would persist the live env value instead
     * of the durable shadow. Only a value that was actually APPLIED is durable.
     * ================================================================= */
    /* clean slate for FloatVal + EnumVal: reload, ensure their flags are NONE. */
    configSetValue("Test.FloatVal", "3.25");
    configSetValue("Test.EnumVal",  "high");
    CHECK("phase3 re-seed save", configSave());
    configInit();                               /* shadow: Float=3.25, Enum=high */
    setenv("GE007_TEST_FLOAT", "9.5", 1);
    setenv("GE007_TEST_ENUM",  "low", 1);
    settingsApplyEnvOverrides();                /* Float live=9.5, Enum live=low, both ENV */
    CHECK("phase3: env float applied", feq(g_float, 9.5f));

    /* Rejected float (NaN) and rejected enum (unknown token): both keep the live
     * env value and must NOT be treated as durable edits. */
    configSetValue("Test.FloatVal", "nan");
    configSetValue("Test.EnumVal",  "bogus-token");
    CHECK("phase3: save under rejected edits", configSave());
    CHECK("phase3: rejected float keeps durable shadow 3.25", fileHas("FloatVal=3.25"));
    CHECK("phase3: rejected float does NOT leak env 9.5",     !fileHas("FloatVal=9.5"));
    CHECK("phase3: rejected enum keeps durable shadow high",  fileHas("EnumVal=high"));
    CHECK("phase3: rejected enum does NOT leak env low",      !fileHas("EnumVal=low"));

    /* A subsequent VALID edit to the same env key IS durable and persists. */
    configSetValue("Test.FloatVal", "4.5");
    CHECK("phase3: save after valid edit", configSave());
    CHECK("phase3: valid edit persists 4.5", fileHas("FloatVal=4.5"));
    unsetenv("GE007_TEST_FLOAT");
    unsetenv("GE007_TEST_ENUM");

    /* ================================================================= *
     * Phase 4 -- settingsResetAllToDefaults() under an active env override must
     * persist the DEFAULT, not the durable shadow. A --reset-config is a durable
     * action that clears every override marking; without that, saveEntry would
     * take the shadow branch and silently fail to reset an env-overridden key.
     * ================================================================= */
    configSetValue("Test.IntVal", "77");         /* durable seed, flag NONE */
    CHECK("phase4 re-seed save", configSave());
    configInit();                                /* shadow IntVal=77 */
    setenv("GE007_TEST_INT", "11", 1);
    settingsApplyEnvOverrides();                 /* IntVal live=11, flag ENV */
    settingsResetAllToDefaults();                /* IntVal live=10 (default), flag NONE */
    CHECK("phase4: reset sets live default", g_int == 10);
    CHECK("phase4: reset save", configSave());
    CHECK("phase4: reset persists default IntVal=10",   fileHas("IntVal=10"));
    CHECK("phase4: reset does NOT persist shadow 77",   !fileHas("IntVal=77"));
    CHECK("phase4: reset does NOT persist env 11",      !fileHas("IntVal=11"));
    unsetenv("GE007_TEST_INT");

    /* ================================================================= *
     * Phase 5 -- AUDIT-0011: a malformed numeric value is rejected (previous
     * value kept), not silently coerced to 0 / a truncated prefix / a clamped
     * infinity. Valid values still apply.
     * ================================================================= */
    g_int = 42; g_uint = 42; g_float = 4.2f;
    configSetValue("Test.IntVal",   "abc");     CHECK("0011 int 'abc' rejected",         g_int == 42);
    configSetValue("Test.IntVal",   "12xyz");   CHECK("0011 int trailing junk rejected", g_int == 42);
    configSetValue("Test.IntVal",   "");        CHECK("0011 int empty rejected",         g_int == 42);
    configSetValue("Test.UintVal",  "-5");      CHECK("0011 uint negative rejected",     g_uint == 42);
    configSetValue("Test.UintVal",  "1.5");     CHECK("0011 uint non-integer rejected",  g_uint == 42);
    configSetValue("Test.FloatVal", "inf");     CHECK("0011 float inf rejected",         feq(g_float, 4.2f));
    configSetValue("Test.FloatVal", "1.5abc");  CHECK("0011 float trailing junk rejected", feq(g_float, 4.2f));
    /* valid values are still applied */
    configSetValue("Test.IntVal",   "7");       CHECK("0011 int valid applied",   g_int == 7);
    configSetValue("Test.UintVal",  "9");       CHECK("0011 uint valid applied",  g_uint == 9);
    configSetValue("Test.FloatVal", "0.5");     CHECK("0011 float valid applied", feq(g_float, 0.5f));

    if (g_fails == 0) { printf("PASS: all config_env_shadow cases\n"); return 0; }
    printf("%d failure(s)\n", g_fails);
    return 1;
}
