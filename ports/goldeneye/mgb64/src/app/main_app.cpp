// main_app.cpp — entry point for the portable MGB64 app shell.
//
// Task 1 (smoke): with no CLI args, open an ImGui window; with any args,
// delegate to the unchanged engine path. Task 3 replaces the crude "any args"
// check with a precise automation-flag allow-list (mgb_is_automation_invocation)
// so that non-automation flags (e.g. --rom) route into the launcher instead.
#include "app_config.h"
#include "app_host.h"
#include "app_theme.h"
#include "arg_triage.h"
#include "config_schema.h"
#include "diag_log.h"
#include "engine_entry.h"
#include "launch_intent.h"
#include "ui_launcher.h"
#include "ui_overlay.h"
#include "update_check.h"

#include <SDL.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

// savedirInit lives in the C engine (src/platform/savedir.c). Declared here so
// the app shell can seed the save-dir singleton with its override BEFORE
// mgb_config_init() freezes it to the engine default (AUDIT-0034).
extern "C" int savedirInit(const char *savedir_override);

// Headless validation harness for the update check: runs the real gated check
// (curl subprocess + tag parse + semver compare) once and prints the outcome,
// then exits — no window. Drives the mock via GE007_UPDATE_CHECK_URL and the
// negative controls (GE007_UPDATE_CHECK=0, empty PATH, automation flags). Mirrors
// the MGB64_APP_DUMP_SCHEMA self-check pattern below.
static int updateCheckSelfTest(int argc, char **argv) {
    mgb_config_init();  // registers Game.CheckForUpdates so the toggle gate reads it
    AppConfig::load();  // so a previously dismissed tag suppresses the banner
    UpdateCheck_start(argc, argv);
    for (int i = 0; i < 120 && !UpdateCheck_isDone(); ++i) {
        SDL_Delay(50);  // curl --max-time is 3s; wait a little past it
    }
    char tag[128];
    if (UpdateCheck_bannerTag(tag, sizeof(tag))) {
        std::printf("[selftest] banner: %s\n", tag);
    } else {
        std::printf("[selftest] no banner\n");
    }
    return 0;
}

int main(int argc, char **argv) {
    // Headless update-check validation (before the automation gate so it can be
    // exercised alongside automation flags to prove the internal gate).
    if (std::getenv("MGB64_UPDATE_CHECK_SELFTEST")) {
        return updateCheckSelfTest(argc, argv);
    }

    // Automation/diagnostic invocations bypass the launcher and run the
    // unchanged engine path (byte-identical). Everything else opens the app.
    if (mgb_is_automation_invocation(argc, argv)) {
        return mgb64_headless_main(argc, argv);
    }

    // Non-interactive schema self-check (validates config_schema enumeration +
    // the RX.1 player/advanced curation tiers). Runs BEFORE DiagLog_install (so
    // output reaches stdout, not the tee'd log) and BEFORE opening a window (so
    // it works fully headless -- CI, no display; the config registry needs no
    // GL context). Registers the config itself, then exits.
    if (std::getenv("MGB64_APP_DUMP_SCHEMA")) {
        mgb_config_init();
        int n = mgb_config_count();
        int player = 0, advanced = 0;
        std::printf("[app] config schema: %d settings\n", n);
        for (int i = 0; i < n; ++i) {
            MgbCfgEntry e;
            if (!mgb_config_get(i, &e)) continue;
            if (e.advanced) {
                ++advanced;
                std::printf("[app] advanced: %s\n", e.key);
            } else {
                ++player;
            }
            if (std::strcmp(e.key, "Video.VSync") == 0) {
                std::printf("[app] Video.VSync kind=%d live=%d enum_count=%d cur_idx=%d cur_token=%s\n",
                            (int)e.kind, e.is_live, e.enum_count, e.cur_enum_index,
                            mgb_config_enum_token(e.key, e.cur_enum_index));
            }
        }
        std::printf("[app] tiers: player=%d advanced=%d\n", player, advanced);
        return 0;
    }

    // AUDIT-0060: parse the interactive launch flags that arg_triage routes to
    // the launcher (--rom/--level/--mission/--difficulty/presets/--multiplayer/
    // --players/--savedir) and seed them into launcher state, so a documented
    // direct-launch command actually takes effect instead of being silently
    // dropped in favor of remembered/default UI choices. Automation and --no-ui
    // invocations returned above, so only interactive argv reaches here. An
    // invalid flag or unusable ROM fails fast with an actionable stderr message
    // and a nonzero exit BEFORE any window/device is created (no UI flash). This
    // runs before DiagLog_install so the error lands on the real stderr, not the
    // tee'd in-app log.
    Launcher launcher;
    {
        LaunchIntent intent;
        std::string intentErr;
        if (!parseLaunchIntent(argc, argv, intent, intentErr)) {
            std::fprintf(stderr, "[app] %s\n", intentErr.c_str());
            return 2;
        }
        if (!launcher.seed(intent, intentErr)) {
            std::fprintf(stderr, "[app] %s\n", intentErr.c_str());
            return 2;
        }
    }

    // Tee stdout/stderr to the in-app console + mgb64.log BEFORE host.init, so
    // its fatal init diagnostics ([app] SDL_Init/CreateWindow/... failed) are
    // captured even under the GUI subsystem (-mwindows), where there is no
    // console to catch them. This is interactive-path only — the automation
    // path returned above, so it is never redirected — and the only code before
    // this point (mgb_is_automation_invocation) emits nothing. DiagLog_install
    // uses only SDL_GetPrefPath + a pipe, so it is safe before SDL_Init.
    DiagLog_install();

    AppHost host;
    if (!host.init("MGB64", 1280, 800)) {
        return 1;
    }

    // Seed the save-dir singleton with the app-shell override BEFORE
    // mgb_config_init() freezes it to the engine default, so autoplay/PortMaster
    // boots that pass MGB64_APP_SAVEDIR actually persist there (AUDIT-0034).
    // A NULL/unset env keeps the engine's normal auto-selection; an unusable
    // override fails fast rather than losing every save (AUDIT-0054).
    if (const char *envSave = std::getenv("MGB64_APP_SAVEDIR")) {
        if (envSave[0] && savedirInit(envSave) != 0) {
            std::fprintf(stderr,
                         "[app] MGB64_APP_SAVEDIR '%s' is not usable; aborting.\n", envSave);
            host.shutdown();
            return 1;
        }
    }

    // Register + load the engine config so the launcher's settings panel can
    // enumerate + edit it before a game boots. Idempotent with the engine boot.
    mgb_config_init();

    // (Launcher constructed + seeded above, before window creation — AUDIT-0060.)

    // Boot the engine into the shell's window: adopt the window, then run the
    // shared engine boot path. Blocks until the game exits.
    // Returns the engine's exit status so callers can honor it [AUDIT-0035]:
    // a failed boot (bad/missing ROM, init failure) must not become app exit 0.
    auto play = [&](const MgbBootConfig &cfg) -> int {
        platformSetHostWindow(host.window(), host.glContext());
        // On WebGPU, hand the launcher's device/surface to the engine so the game
        // renders into the SAME window+surface (no force_opengl fallback). On GL,
        // no WebGPU host is registered and the adoption path forces GL as before.
        if (host.usingWebGpu()) {
            platformSetHostWebGpu(host.wgpuInstance(), host.wgpuAdapter(),
                                  host.wgpuDevice(), host.wgpuQueue(),
                                  host.wgpuSurface(), host.wgpuFormat());
        }
        Overlay_install(host.window(), argv[0]);  // in-game overlay (F1)
        return mgb64_engine_boot(&cfg);
    };

    // Validation/CI: immediately boot into the shell window (with
    // MGB64_BOOT_SCREENSHOT_FRAME the engine screenshots + exits), proving the
    // launcher->engine handoff non-interactively.
    if (std::getenv("MGB64_APP_AUTOPLAY")) {
        // Isolate save state (MGB64_APP_SAVEDIR) so the validation boot never
        // pollutes the user's eeprom/ini or the byte-identity harness.
        MgbBootConfig cfg = {nullptr, std::getenv("MGB64_APP_SAVEDIR"), -1, -1, 0, 0, -1};
        cfg.level_slug = std::getenv("MGB64_APP_AUTOPLAY_LEVEL");  // exercises level boot
        int rc = play(cfg);
        host.shutdown();
        return rc;
    }

    // Headless smoke path (also used by CI in Task 11): render a fixed number
    // of frames regardless of window events, then exit with proof. Optionally
    // capture the final frame as a BMP (MGB64_APP_SMOKE_SHOT) for design review.
    const char *smoke = std::getenv("MGB64_APP_SMOKE_FRAMES");
    if (smoke && smoke[0]) {
        int frames = std::atoi(smoke);
        if (frames < 1) frames = 1;
        const char *shot = std::getenv("MGB64_APP_SMOKE_SHOT");
        bool sawQuit = false;
        bool captureOk = true;
        for (int i = 0; i < frames; ++i) {
            if (host.pumpAndShouldQuit()) sawQuit = true;  // drain, don't exit
            host.beginFrame();
            launcher.draw(host);
            bool ok = host.endFrame((i == frames - 1) ? shot : nullptr);
            if (i == frames - 1) captureOk = ok;
        }
        std::printf("[app] smoke: rendered %d frames, drawable %dx%d, sawQuit=%d\n",
                    frames, host.drawableWidth(), host.drawableHeight(), sawQuit ? 1 : 0);
        host.shutdown();
        // AUDIT-0046: a requested capture that was not written must fail the run,
        // so a design-review/CI smoke can't pass without its image.
        if (shot && shot[0] && !captureOk) {
            std::fprintf(stderr, "[app] smoke: requested capture %s was not produced\n", shot);
            return 1;
        }
        return 0;
    }

    // PortMaster / handheld path: skip the launcher entirely and boot straight
    // into the game. ROM is taken from MGB64_ROM env (or engine auto-detection).
    // MGB64_PORTMASTER also gates the 640x480 fullscreen mode in platform_sdl.c.
    if (std::getenv("MGB64_PORTMASTER")) {
        MgbBootConfig cfg = {std::getenv("MGB64_ROM"), std::getenv("MGB64_APP_SAVEDIR"),
                             -1, -1, 0, 0, -1, nullptr, 0};
        int rc = play(cfg);
        host.shutdown();
        return rc;
    }

    // Quiet, once-per-session check for a newer GitHub release on a background
    // thread (never blocks launch). Interactive path only — the automation/smoke/
    // autoplay/schema paths all returned above, so no test lane spawns curl. Also
    // internally gated on the automation flags + Game.CheckForUpdates + env.
    UpdateCheck_start(argc, argv);

    // RX.2: fill the display on handhelds so the launcher doesn't "float" in a
    // small centered window on a 1920x1200 7-inch panel. UI.LauncherFullscreen
    // (auto/on/off) gates it; auto detects a small/high-DPI panel and otherwise
    // leaves the desktop dev window resizable. Interactive path only — the
    // smoke/autoplay/schema paths all returned above, so CI is never forced
    // fullscreen.
    host.applyLauncherFullscreen(mgb_config_get_int("UI.LauncherFullscreen", 0));

    bool running = true;
    int exitCode = 0;  // a failed interactive Play boot propagates to process exit
    while (running) {
        if (host.pumpAndShouldQuit()) break;
        // RX.2: apply the live UI.Scale before drawing (idempotent no-op when
        // unchanged) so moving the slider resizes text/metrics immediately.
        AppTheme::setUiScale(mgb_config_get_float("UI.Scale", 1.0f));
        host.beginFrame();
        LauncherAction action = launcher.draw(host);
        host.endFrame();
        if (action.type == LauncherActionType::Quit) {
            running = false;
        } else if (action.type == LauncherActionType::Play) {
            exitCode = play(action.boot);  // blocks; game renders into this window
            running = false;    // Task 9 adds return-to-launcher (re-exec)
        }
    }
    UpdateCheck_quiesce();  // a check still in flight must not log past teardown
    host.shutdown();
    return exitCode;  // 0 on a clean quit; the engine's status on a failed Play
}
