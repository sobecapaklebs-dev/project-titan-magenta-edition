/* test_config_staging.c — the settings staging layer (config_schema.c).
 *
 * Proves the "make it awesome" post-boot contract: while a staging session is
 * open, the app UI's mgb_config_set_* writes go to a working copy and NEVER
 * touch the live engine globals until Apply; Discard drops the copy; Preview
 * pushes one staged value to the live global and reverts it. The test reads the
 * bound globals DIRECTLY (not via the getter) to prove the live value is
 * untouched independent of the staged overlay the getter reports.
 *
 * Links only the registry + config layers (no SDL/engine), registering a few
 * settings inline so the harness stays light. configSave is suppressed so Apply
 * performs no file I/O. */
#include "config_schema.h"   /* mgb_config_* + configStaging* (src/app) */
#include "settings.h"        /* settingsRegister* (src/platform) */
#include "config_pc.h"       /* configSetSaveSuppressed */

#include <stdio.h>
#include <string.h>

/* config_schema.c references these in mgb_config_init(), which we never call;
 * define stubs so the object links standalone. */
void platformRegisterConfig(void) {}
void portAudioRegisterConfig(void) {}

/* Live globals the registry binds to (the engine's "real" values). */
static f32 g_fov = 60.0f;
static s32 g_fpsOverlay = 1;
static s32 g_windowMode = 0;

static const ConfigEnumOption kWindowModes[] = {
    { "windowed",   0 },
    { "fullscreen", 1 },
    { "borderless", 2 },
};

static int g_fails = 0;
#define CHECK(name, cond) do { \
    if (!(cond)) { printf("FAIL: %s\n", (name)); ++g_fails; } \
} while (0)

/* float equality is exact here — every value is a small literal we set/clamp. */
static int feq(float a, float b) { return a == b; }

int main(void) {
    configSetSaveSuppressed(1);   /* Apply must not write ge007.ini */

    settingsRegisterFloat("Video.FovY", &g_fov, 60.0f, 30.0f, 120.0f,
                          SETTING_SCOPE_LIVE, "GE007_TEST_FOV", "--fov=VALUE",
                          "FOV", "test");
    settingsRegisterInt("Video.FpsOverlay", &g_fpsOverlay, 1, 0, 1,
                        SETTING_SCOPE_LIVE, "GE007_TEST_FPS", "--fps=VALUE",
                        "FPS", "test");
    settingsRegisterEnum("Video.WindowMode", &g_windowMode, 0,
                         kWindowModes, 3, SETTING_SCOPE_LIVE, "GE007_TEST_WM",
                         "--wm=VALUE", "Window mode", "test");

    CHECK("not staging initially", !configStagingActive());

    /* ---- float: staged edit is invisible to the live global ---------------- */
    float before = mgb_config_get_float("Video.FovY", 0.0f);   /* 60 */
    configStagingBegin();
    CHECK("staging active after begin", configStagingActive());
    mgb_config_set_float("Video.FovY", before + 13.0f);        /* -> staged only */
    CHECK("getter shows staged edit",  feq(mgb_config_get_float("Video.FovY", 0.0f), before + 13.0f));
    CHECK("LIVE global untouched while staged", feq(g_fov, before));

    /* ---- discard: live never changed -------------------------------------- */
    configStagingDiscard();
    CHECK("not staging after discard", !configStagingActive());
    CHECK("getter back to live after discard", feq(mgb_config_get_float("Video.FovY", 0.0f), before));
    CHECK("LIVE global still original after discard", feq(g_fov, before));

    /* ---- apply: commits staged -> live ------------------------------------ */
    configStagingBegin();
    mgb_config_set_float("Video.FovY", before + 13.0f);
    CHECK("LIVE still original mid-session", feq(g_fov, before));
    configStagingApply();
    CHECK("not staging after apply", !configStagingActive());
    CHECK("LIVE global applied", feq(g_fov, before + 13.0f));
    CHECK("getter reflects applied live", feq(mgb_config_get_float("Video.FovY", 0.0f), before + 13.0f));

    /* ---- apply clamps out-of-range via configSetValue --------------------- */
    configStagingBegin();
    mgb_config_set_float("Video.FovY", 999.0f);   /* above max 120 */
    configStagingApply();
    CHECK("apply clamps to max", feq(g_fov, 120.0f));

    /* ---- int round-trip ---------------------------------------------------- */
    configStagingBegin();
    mgb_config_set_int("Video.FpsOverlay", 0);
    CHECK("int getter shows staged", mgb_config_get_int("Video.FpsOverlay", -1) == 0);
    CHECK("int LIVE untouched while staged", g_fpsOverlay == 1);
    configStagingApply();
    CHECK("int LIVE applied", g_fpsOverlay == 0);

    /* ---- enum staged by token, getter resolves value ---------------------- */
    configStagingBegin();
    mgb_config_set_enum("Video.WindowMode", 2);   /* "borderless" -> value 2 */
    CHECK("enum getter shows staged value", mgb_config_get_int("Video.WindowMode", -1) == 2);
    CHECK("enum LIVE untouched while staged", g_windowMode == 0);
    configStagingApply();
    CHECK("enum LIVE applied", g_windowMode == 2);

    /* ---- preview: staged value pushed to live, then reverted --------------- */
    float applied = g_fov;                         /* 120 from the clamp test */
    configStagingBegin();
    mgb_config_set_float("Video.FovY", 75.0f);
    CHECK("preview: live unchanged before preview", feq(g_fov, applied));
    configStagingPreview("Video.FovY", 1);
    CHECK("preview on pushes staged to live", feq(g_fov, 75.0f));
    configStagingPreview("Video.FovY", 0);
    CHECK("preview off reverts live", feq(g_fov, applied));
    /* discard while a stale preview flag would be a bug — discard cleans up */
    configStagingPreview("Video.FovY", 1);
    configStagingDiscard();
    CHECK("discard reverts a live preview", feq(g_fov, applied));
    CHECK("not staging after discard", !configStagingActive());

    if (g_fails == 0) { printf("PASS: all config_staging cases\n"); return 0; }
    printf("%d failure(s)\n", g_fails);
    return 1;
}
