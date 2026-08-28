/**
 * platform_sdl.c — SDL2 window, OpenGL context, and frame timing.
 *
 * Provides the display window and frame pacing for the PC port.
 * The N64's VI retrace interrupt is replaced by SDL frame timing.
 */
#include <ultra64.h>
#include <sched.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <time.h>     /* nanosleep for the native pacer's sub-ms sleep tail */
#include <SDL.h>
#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#include <emscripten/em_js.h>
#include <emscripten/html5.h>   /* WEB-016: emscripten_get_element_css_size (undo relative-motion rescale) */
/* Block (via Asyncify) until the browser's next animation frame. The wake is
 * display-synced — unlike emscripten_sleep()'s setTimeout, which Chrome clamps
 * to 1-4ms granularity and which never aligns with vsync. Used by the frame
 * pacer below; the rAF wake also lets pending WebGPU/timer callbacks run. */
EM_ASYNC_JS(void, platformWaitAnimationFrame, (void), {
    await new Promise((resolve) => requestAnimationFrame(resolve));
});
/* PERF-037 (d): the per-frame rAF wait above is the main loop's ONLY Asyncify
 * suspend site and sits on the narrow ASYNCIFY_ADD spine (PERF-031). It keeps its
 * `new Promise(...requestAnimationFrame)` shape DELIBERATELY: a persistent
 * rAF-bridge callback would trade a minor-GC win (one Promise/frame, which the
 * generational collector already absorbs) for churn on the exact suspend chain
 * PERF-031 just narrowed. Left as-is.
 *
 * PERF-037 (a/b/c): the other steady-state browser-state queries used to cross
 * wasm->JS every frame — document.hidden per pacer-wait iteration (2-3x/frame),
 * document.pointerLockElement once/frame, and the canvas CSS size once/frame via
 * getComputedStyle. Mirror all three into plain JS globals kept live by
 * visibilitychange / pointerlockchange listeners and a single ResizeObserver
 * (installed once by platformWebInstallStateMirrors), and read them via cheap
 * SYNCHRONOUS EM_JS. No new suspend site is introduced (these are all sync
 * reads, no emscripten_sleep / await), so the Asyncify spine is untouched. */
EM_JS(int, platformTabHidden, (void), {
    return Module.__mgb64_hidden ? 1 : 0;
});
EM_JS(int, platformPointerLocked, (void), {
    return Module.__mgb64_plock ? 1 : 0;
});
/* Returns 1 (and clears the flag) exactly on frames where the ResizeObserver
 * saw the canvas CSS box change; 0 otherwise. The C side then re-queries the CSS
 * size (the SAME emscripten_get_element_css_size call — same VALUE) ONLY on those
 * frames and reuses its cache every other frame, so getComputedStyle no longer
 * runs per frame. If ResizeObserver is unavailable (ancient browser), __mgb64_noRO
 * forces always-dirty, i.e. the exact old per-frame-query behavior. */
EM_JS(int, platformCanvasSizeDirty, (void), {
    if (Module.__mgb64_noRO) return 1;
    if (Module.__mgb64_sizeDirty) { Module.__mgb64_sizeDirty = 0; return 1; }
    return 0;
});
/* Install the browser-state mirrors ONCE, after the canvas exists (called at the
 * end of platformInitSDL). Seeds every global with its current value so frame 0
 * reads correct state, then keeps them live via listeners. Idempotent. */
static void platformWebInstallStateMirrors(void) {
    EM_ASM({
        if (Module.__mgb64_mirrors) return;
        Module.__mgb64_mirrors = 1;
        Module.__mgb64_hidden = document.hidden ? 1 : 0;
        Module.__mgb64_plock  = document.pointerLockElement ? 1 : 0;
        Module.__mgb64_sizeDirty = 0;
        document.addEventListener('visibilitychange', function() {
            Module.__mgb64_hidden = document.hidden ? 1 : 0;
        });
        document.addEventListener('pointerlockchange', function() {
            Module.__mgb64_plock = document.pointerLockElement ? 1 : 0;
        });
        var canvas = document.querySelector('#canvas');
        if (canvas && typeof ResizeObserver !== 'undefined') {
            var ro = new ResizeObserver(function() { Module.__mgb64_sizeDirty = 1; });
            ro.observe(canvas);
        } else {
            Module.__mgb64_noRO = 1;   /* no RO: fall back to per-frame CSS query */
        }
    });
}
#endif

/* PERF-035: cooperative yield at load boundaries. Time-gated so liberal
 * placement is free when phases are fast; only unwinds to the event loop when a
 * phase ran long, bounding the browser long-task span. HARD no-op on native and
 * under --deterministic (tape gate byte-identical). Uses a BARE emscripten_sleep(0)
 * (unwinds to the JS event loop, resets the long-task timer, lets DOM listeners
 * run) — NOT WGPU_COMPAT_PUMP, which would fire WebGPU futures mid-load and
 * disturb the renderer state texReset is rebuilding. */
void portLoadYield(void) {
#ifdef __EMSCRIPTEN__
    extern int g_deterministic;
    if (g_deterministic) return;
    static double s_lastYieldMs = 0.0;
    double now = emscripten_get_now();
    if (now - s_lastYieldMs < 50.0) return;   /* budget ~50ms */
    s_lastYieldMs = now;
    emscripten_sleep(0);
#endif
}

#ifdef MGB64_PORTMASTER_GLES
#include <GLES3/gl32.h>
#elif defined(__APPLE__)
#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl3.h>
#else
#include <glad/glad.h>
#endif
#include "config_pc.h"
#include "savedir.h"
#include "host_window.h"
#include "app_overlay_hooks.h"
#include "gfx_pc.h"
#include "gfx_uniforms.h"   /* enforce these definitions match the shared declarations */
#include "settings.h"
#include "fire_rate_authentic.h"
#include "frame_stats.h"
#include "port_env.h"
#include "game/front.h"
#include "game/initmenus.h"
#include "game/title.h"

/* Forward declarations */
void platformShutdownSDL(void);
void platformSetWindowTitle(const char *title);

/* ===== Gamepad state ===== */
/* Up to MAXCONTROLLERS pads are opened and bound to fixed player slots.
 * The mapping is stable: a pad keeps its slot for its lifetime, and a
 * hot-unplug frees only that slot (no reshuffling of other players). Slot 0
 * doubles as the keyboard/mouse player, so a pad opened first lands in slot 0
 * but the data[0] path still merges keyboard/mouse on top of it (see stubs.c).
 *
 * g_gameController/g_gameControllerID are kept as aliases for slot 0 so the
 * existing single-pad call sites (right-stick read, shutdown) keep working. */
typedef struct {
    SDL_GameController *handle;      /* NULL when the slot is free */
    SDL_JoystickID      instance_id; /* -1 when the slot is free */
    int                 slot;        /* fixed player slot, == array index */
} PlatformPad;

#define PLATFORM_MAX_PADS 4 /* matches N64 MAXCONTROLLERS */

static PlatformPad g_pads[PLATFORM_MAX_PADS];

SDL_GameController *g_gameController = NULL;
static SDL_JoystickID g_gameControllerID = -1;

/* Refresh the legacy single-pad aliases from slot 0. */
static void platformSyncPad0Alias(void) {
    g_gameController   = g_pads[0].handle;
    g_gameControllerID = g_pads[0].instance_id;
}

/* Open an SDL game controller (by joystick device index) into the first free
 * slot. Returns the slot used, or -1 if no slot is free or the open failed. */
static int platformOpenPad(int deviceIndex) {
    SDL_GameController *gc;
    SDL_JoystickID id;
    int slot;

    if (!SDL_IsGameController(deviceIndex)) {
        return -1;
    }

    id = SDL_JoystickGetDeviceInstanceID(deviceIndex);
    /* Reject duplicates: a controller already opened in some slot. */
    for (slot = 0; slot < PLATFORM_MAX_PADS; slot++) {
        if (g_pads[slot].handle && g_pads[slot].instance_id == id) {
            return -1;
        }
    }

    for (slot = 0; slot < PLATFORM_MAX_PADS; slot++) {
        if (!g_pads[slot].handle) {
            break;
        }
    }
    if (slot >= PLATFORM_MAX_PADS) {
        return -1; /* all slots full */
    }

    gc = SDL_GameControllerOpen(deviceIndex);
    if (!gc) {
        return -1;
    }

    g_pads[slot].handle      = gc;
    g_pads[slot].instance_id = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(gc));
    g_pads[slot].slot        = slot;
    printf("[SDL] Gamepad connected (P%d): %s\n", slot + 1, SDL_GameControllerName(gc));

    platformSyncPad0Alias();
    return slot;
}

/* Free the slot whose pad has the given instance id, without disturbing the
 * other slots (stable mapping survives hot-unplug). */
static void platformClosePadByInstance(SDL_JoystickID id) {
    int slot;
    for (slot = 0; slot < PLATFORM_MAX_PADS; slot++) {
        if (g_pads[slot].handle && g_pads[slot].instance_id == id) {
            printf("[SDL] Gamepad disconnected (P%d)\n", slot + 1);
            SDL_GameControllerClose(g_pads[slot].handle);
            g_pads[slot].handle      = NULL;
            g_pads[slot].instance_id = -1;
            break;
        }
    }
    platformSyncPad0Alias();
}

/* ===== Community controller mappings (MC.2) =====
 * SDL ships built-in mappings for common pads, but the community
 * SDL_GameControllerDB (lib/sdl_gamecontrollerdb/gamecontrollerdb.txt) covers
 * thousands more — exotic/hybrid/handheld devices SDL misses. Load it BEFORE
 * opening any controller so the bundled mappings apply. Missing file is not an
 * error: SDL's built-ins still work, so a stripped install degrades to those.
 *
 * Resolution order (later files override earlier for the same GUID, matching
 * SDL_GameControllerAddMappingsFromFile's last-write-wins semantics):
 *   1. next to the executable / in the app bundle (SDL_GetBasePath) — the
 *      shipped copy that packaging drops beside the binary,
 *   2. the current directory — dev/portable runs from the source tree,
 *   3. the savedir — a user-dropped copy that refreshes/overrides the bundle.
 * Each candidate is loaded independently; a hit in one does not skip the
 * others, so the savedir override composes on top of the bundled base. */
static int platformAddMappingsFrom(const char *path, const char *label) {
    int n;
    if (!path || !path[0]) {
        return 0;
    }
    n = SDL_GameControllerAddMappingsFromFile(path);
    if (n <= 0) {
        /* n < 0: not present/unreadable (expected for absent candidates).
         * n == 0: present but no mapping matched this platform. Either way it
         * contributes nothing, so stay quiet and let the caller's total decide. */
        return 0;
    }
    printf("[SDL] Loaded %d controller mapping(s) from %s (%s)\n", n, path, label);
    return n;
}

static void platformLoadControllerMappings(void) {
    int total = 0;
    char *base = SDL_GetBasePath(); /* SDL-malloc'd; NULL if unsupported */
    if (base) {
        char bundled[1100];
        int w = snprintf(bundled, sizeof(bundled), "%sgamecontrollerdb.txt", base);
        if (w > 0 && (size_t)w < sizeof(bundled)) {
            total += platformAddMappingsFrom(bundled, "bundle/exe dir");
        }
        SDL_free(base);
    }
    total += platformAddMappingsFrom("gamecontrollerdb.txt", "cwd");
    total += platformAddMappingsFrom(savedirPath("gamecontrollerdb.txt"), "savedir override");

    if (total == 0) {
        printf("[SDL] No gamecontrollerdb.txt found; using SDL built-in mappings\n");
    }
}

/* ===== Rumble (MC.4) =====
 * The game's Rumble Pak signal (joy.c joyRumblePakStart/Stop, raised on stock
 * events: weapon fire gun.c, Bond damage bondview.c) is routed here and mapped
 * to SDL_GameControllerRumble on the addressed player's pad. Works on any
 * XInput/DualShock/handheld pad. OUTPUT ONLY: reads no sim state, mutates no
 * game/RNG state, and is a hard no-op under --deterministic so the sim-state
 * hash is byte-identical with rumble on or off. */
extern int g_deterministic; /* platform_sdl.c global: --deterministic run */
extern s32 g_pcRumble;
extern s32 g_pcRumbleIntensity;

/* GE007_TRACE_RUMBLE=1: log each rumble request (player, duration, mapped
 * strength/ms) to stderr for call-path validation. Logging is output-only and
 * runs even under --deterministic (before the actuation gate) so the game
 * event -> SDL path can be proven muted/headless where there is no pad. */
static int platformRumbleTraceEnabled(void) {
    static int cached = -1;
    if (cached < 0) {
        const char *env = getenv("GE007_TRACE_RUMBLE");
        cached = (env != NULL && atoi(env) != 0) ? 1 : 0;
    }
    return cached;
}

void platformRumblePlayer(s32 player, f32 durationSecs) {
    if (durationSecs < 0.0f) durationSecs = 0.0f;

    /* N64 rumble is essentially binary on/off; the game supplies the duration.
     * Map on -> a single strength (scaled by intensity) held for the duration. */
    Uint16 strength;
    Uint32 ms;
    if (g_pcRumbleIntensity < 0)   g_pcRumbleIntensity = 0;
    if (g_pcRumbleIntensity > 100) g_pcRumbleIntensity = 100;
    strength = (Uint16)((65535.0f * (float)g_pcRumbleIntensity) / 100.0f + 0.5f);
    ms = (Uint32)(durationSecs * 1000.0f + 0.5f);
    if (ms == 0) ms = 1;

    if (platformRumbleTraceEnabled()) {
        fprintf(stderr,
                "[RUMBLE] player=%d duration=%.3fs strength=%u ms=%u "
                "(rumble=%d intensity=%d det=%d pad=%s)\n",
                (int)player, (double)durationSecs, (unsigned)strength, (unsigned)ms,
                (int)g_pcRumble, (int)g_pcRumbleIntensity, g_deterministic,
                (player >= 0 && player < PLATFORM_MAX_PADS && g_pads[player].handle)
                    ? "yes" : "none");
    }

    /* Actuation gates (never reached in deterministic/automation runs). */
    if (g_deterministic) return;         /* output-only: no haptics under determinism */
    if (!g_pcRumble) return;             /* Input.Rumble off */
    if (g_pcRumbleIntensity <= 0) return;
    if (player < 0 || player >= PLATFORM_MAX_PADS) return;
    if (!g_pads[player].handle) return;  /* no pad in that player slot (e.g. dual-control 2nd id) */

#if SDL_VERSION_ATLEAST(2, 0, 9)
    SDL_GameControllerRumble(g_pads[player].handle, strength, strength, ms);
#endif
}

void platformRumbleStopAll(void) {
    int i;
    if (g_deterministic) return;
#if SDL_VERSION_ATLEAST(2, 0, 9)
    for (i = 0; i < PLATFORM_MAX_PADS; i++) {
        if (g_pads[i].handle) {
            SDL_GameControllerRumble(g_pads[i].handle, 0, 0, 0);
        }
    }
#else
    (void)i;
#endif
}

/* ===== Window state ===== */
SDL_Window   *g_sdlWindow  = NULL;  /* non-static: fast3d needs access for swap/dimensions */
static SDL_GLContext  g_glContext  = NULL;
/* Backend selectors (fast3d/gfx_backend.h). Both are always defined — each
 * returns false unless its backend is compiled in and selected — so they are
 * declared unconditionally: the screenshot readback path compiles on non-Apple
 * WebGPU builds and references both. */
extern bool gfx_backend_use_metal(void);
extern bool gfx_backend_use_webgpu(void);
extern void gfx_backend_force_opengl(void);
#ifdef __APPLE__
SDL_MetalView g_metalView = NULL;          /* non-static: gfx_metal reads its CAMetalLayer */
#endif
static int g_sdlQuit = 0;
static int g_forceNoVsync = 0;
static int g_backgroundWindow = 0;
static int g_disableInputGrab = 0;
static int g_traceRequested = -1;

/* ===== Frame timing ===== */
static u32 g_lastFrameTime = 0;
/* Absolute frame-pacing deadline in SDL performance-counter ticks (0 = unarmed).
 * See platformFrameSync: sub-ms deadline pacing replaces the integer-ms cap. */
static Uint64 g_paceDeadline = 0;
static int g_frameSyncCallCount = 0;
static int g_perfTraceEnabled = -1;
static int g_perfTraceAfterFrame = 0;
static int g_perfTraceBudget = 0;
static Uint64 g_perfLastFrameStart = 0;

static int platformPerfTraceEnabled(void) {
    if (g_perfTraceEnabled < 0) {
        const char *env = getenv("GE007_PERF_TRACE");
        const char *after_env = getenv("GE007_PERF_TRACE_AFTER_FRAME");
        const char *budget_env = getenv("GE007_PERF_TRACE_BUDGET");

        g_perfTraceEnabled = env != NULL && atoi(env) != 0;
        g_perfTraceAfterFrame = after_env != NULL ? atoi(after_env) : 0;
        g_perfTraceBudget = budget_env != NULL ? atoi(budget_env) : 600;
        if (g_perfTraceBudget < 0) {
            g_perfTraceBudget = 0;
        }
    }

    return g_perfTraceEnabled;
}

static double platformPerfCounterMs(Uint64 start, Uint64 end) {
    Uint64 freq = SDL_GetPerformanceFrequency();
    if (freq == 0 || end < start) {
        return 0.0;
    }
    return (double)(end - start) * 1000.0 / (double)freq;
}

static int platformTraceRequested(void) {
    extern const char *g_traceStatePath;

    if (g_traceRequested >= 0) {
        return g_traceRequested;
    }

    g_traceRequested =
        g_traceStatePath != NULL
        || getenv("GE007_GUARD_ORACLE_TRACE") != NULL
        || getenv("GE007_DUMP_MISSION_GATES") != NULL
        || getenv("GE007_DUMP_STAGE_PADS") != NULL
        || getenv("GE007_DUMP_STAGE_CHRS") != NULL;

    return g_traceRequested;
}

/* ===== Fly camera state ===== */
/* Initial position (stan-space coords, overridden by gameplay camera sync) */
float g_pcCamX = 0.0f, g_pcCamY = 50.0f, g_pcCamZ = -400.0f;
float g_pcCamYaw = 0.0f, g_pcCamPitch = 0.0f;
static int g_mouseGrabbed = 0;
#ifdef __EMSCRIPTEN__
/* WEB-007: latch that turns 1 only once the browser has CONFIRMED pointer lock
 * while we believe we're grabbed. A confirmed→unlocked transition is a genuine
 * in-gameplay lock loss (the browser eats Esc as its exit-lock gesture and
 * delivers no SDL keydown), which we treat as the Esc press. The latch avoids
 * false-firing during the async acquisition window after a regrab-click (the
 * b1525bf cooldown path requests lock but the browser grants it a few frames
 * later — pointerLockElement is null meanwhile, yet that is not a lock LOSS). */
static int g_pcWebLockConfirmed = 0;
/* WEB-016: per-frame cached factors that UNDO SDL's emscripten relative-motion
 * rescale, ONE PER AXIS. SDL's emscripten backend scales the two axes
 * INDEPENDENTLY before delivering event.motion.xrel/yrel:
 *     xrel *= (SDL window_w / canvas css_w),  yrel *= (window_h / css_h).
 * With the SDL window fixed at Video.WindowWidth×Height (1440×810) and the canvas
 * CSS-pinned to the viewport, mouse-look feel changes with every browser resize
 * and never matches native — and a single width-only unscale would leave Y feel
 * aspect-dependent (~10% weak at 16:10, ~31% hot at 21:9). Undoing each axis with
 * its OWN inverse makes one hardware count equal one game count on both axes:
 *     X: (window_w/css_w)·(css_w/window_w) = 1
 *     Y: (window_h/css_h)·(css_h/window_h) = 1
 * Recomputed at most once per frame in platformPollEvents (never per motion
 * event); 1.0 = identity until first read. INDEPENDENT of Input.MouseSensitivity:
 * this normalizes the browser's coordinate scaling; MouseSensitivity is the user's
 * separate feel preference applied later in lvl.c. The two multiply. */
static float g_pcWebMouseUnscaleX = 1.0f;
static float g_pcWebMouseUnscaleY = 1.0f;
#endif
int g_pcDebugFlyCamera = 0;  /* 0 = gameplay camera, 1 = fly cam. Toggle with F1. */
#define FLY_SPEED 50.0f
#define MOUSE_SENSITIVITY 0.003f
f32 g_pcVideoGamma = 1.0f;
/* PERF-030: web (WebGPU surface sized at CSS x devicePixelRatio) joins handheld
 * GLES in defaulting to native res. On a DPR>1 canvas, 2x SSAA is a ~5x pixel
 * load the compositor then downscales, and a DPR>1 canvas is already effectively
 * supersampled for 320x240 content. Users with GPU headroom raise it via
 * Video.RenderScale (--config-override). */
#if defined(MGB64_PORTMASTER_GLES) || defined(__EMSCRIPTEN__)
f32 g_pcRenderScale = 1.0f;   /* native res: R36S 2x SSAA = 1280x960 on a Mali-G31; web canvas is already CSS x DPR */
#else
f32 g_pcRenderScale = 2.0f;   /* desktop remaster default: 2x SSAA (clean edges; raise to 4x for max IQ) */
#endif
s32 g_pcMsaaSamples = 0;       /* remaster default: OFF by design. The AA stack is 2x SSAA (RenderScale) + FXAA; MSAA stacked on a supersampled scene buffer is redundant geometry AA at real cost. When the user does enable MSAA, alpha-to-coverage (gfx_opengl.c) engages to feather cutout edges SSAA cannot. */
int g_pcTextureAnisotropy = 16; /* Video.AnisotropicFiltering: remaster default 16 (--faithful pins 1). 1 = faithful (N64 nearest + in-shader 3-point filter); >1 routes 3-point (bilerp) materials through a hardware linear + anisotropic sampler so grazing-angle surfaces (e.g. Dam canyon walls at the frame edge) stop streaking. Only the WebGPU backend consumes it; GL/Metal ignore it. 1 is byte-identical to prior behavior. */
f32 g_pcFovY = 50.0f;            /* default: classic GoldenEye feel — 60deg vertical balloons to a fisheye ~90deg horizontal on 16:9; 50 keeps the original ~75deg horizontal. Slider still goes 45..105. */
f32 g_pcCutsceneFovY = 60.0f;    /* D6/T16: authored cinematics render at the N64's fixed 60deg vertical FOV regardless of gameplay Video.FovY; 0 = follow Video.FovY. */
f32 g_pcVideoSaturation = 1.15f; /* remaster default: subtly richer palette */
f32 g_pcVideoContrast = 1.08f;   /* remaster default: gentle contrast pop */
f32 g_pcVideoBrightness = 0.04f;  /* kept neutral (brightness offset is taste-sensitive) */
s32 g_pcOutputDither = 1;        /* remaster default: on (anti-banding under the grade) */
f32 g_pcVignette = 0.15f;        /* remaster default: soft edge falloff for depth */
#ifdef MGB64_PORTMASTER_GLES
s32 g_pcBloom = 0;
#else
s32 g_pcBloom = 1;               /* remaster default: on (light bleed on emitters/sky) */
#endif
f32 g_pcBloomThreshold = 0.8f;
f32 g_pcBloomIntensity = 0.5f;
s32 g_pcSsao = 0;                /* default OFF (identity-first landing; flip on at the review checkpoint) */
s32 g_pcSsaoMode = 1;            /* W3.E2: 1=planar v1 (all backends), 2=hemisphere v2 (Metal-only) */
f32 g_pcSsaoRadius = 0.5f;       /* AO sample radius, fraction of screen height */
f32 g_pcSsaoIntensity = 1.0f;    /* occlusion darkening strength */
f32 g_pcSsaoBias = 0.15f;        /* v2 tangent-plane angle threshold — rejects grazing self-occlusion (W3.E2.T2 tuned) */
f32 g_pcSsaoPower = 3.0f;        /* AO contrast exponent (W3.E2.T2 tuned) */
f32 g_pcSsaoFarCutoff = 800.0f;  /* view-Z beyond which AO fades to 0 — GE world scale (W3.E2.T2 tuned) */
f32 g_pcSsaoNearCut = 0.02f;     /* window-depth <= this = viewmodel/near: no AO */
f32 g_pcSsaoSkyCut = 0.9999f;    /* window-depth >= this = sky: no AO */
s32 g_pcSsaoHalfRes = 0;         /* render AO at half scene res (P1a-perf) */
s32 g_pcSsaoBlur = 0;            /* separable bilateral blur pass (P1a-perf) */
f32 g_pcSsaoBlurDepthSharp = 8.0f; /* bilateral blur depth-weight sharpness */
#ifdef MGB64_PORTMASTER_GLES
s32 g_pcFxaa = 0;
#else
s32 g_pcFxaa = 1;                /* remaster default: on (sprite/alpha/HUD edge cleanup atop SSAA) */
#endif
s32 g_pcSmaa = 0;                /* W3.E4: subpixel morphological AA (Metal-only); default OFF. When on, replaces FXAA on the output pass (mutually exclusive). --remaster keeps FXAA until the ◆ preset decision (E4.T3). */
f32 g_pcSharpen = 0.15f;          /* remaster default: mild CAS sharpen (no-op at 0; pairs with SSAA) */
f32 g_pcFogDensity = 1.0f;
s32 g_pcSmoothSky = 0;           /* R5 Video.SmoothSky: default OFF (faithful). When on, skip the N64 5-bit
                                  * (& 0xf8) sky-colour quantization in fog.c so the full 8-bit interpolated
                                  * sky reaches the renderer and the output dither smooths the gradient.
                                  * Cosmetic, draw-only; default OFF keeps the masks -> byte-identical. */
s32 g_pcEnvSmoothNormals = 0;    /* W1.E1: default OFF (identity-first; flip on at the E1 taste checkpoint) */
f32 g_pcEnvRelightBlend = 0.6f;  /* W1.E1: 0 = keep baked luma, 1 = full Lambert replace; the seam-vs-mood dial */
s32 g_pcSunShadow = 0;           /* W1.E3: default OFF (identity-first). Capture-and-replay sun shadow map (§4.5). */
s32 g_pcSunShadowRes = 2048;     /* W1.E3: shadow-map resolution (1024/2048/4096 recommended). */
f32 g_pcSunShadowRadius = 500.0f; /* W1.E3: camera-centered ortho half-extent in GAME-LOGIC units (scaled by
                                   * room_data_float2 to world units in gfx_compute_shadow_mat; the plan's "60
                                   * world units" was mis-scaled vs the landed E2 world coords — see T2 notes). */
f32 g_pcSunShadowBias = 0.0015f;  /* W1.E3.T4: receiver depth-compare bias (peter-panning vs acne dial). */
f32 g_pcSunShadowUmbra = 0.55f;   /* W1.E3.T4: shadowed-surface darkening (1.0 = no darken, 0 = black). */
s32 g_pcPerPixelLight = 0;       /* W1.E4: default OFF (identity-first). Per-pixel geometric-normal (dFdx) directional sun on room surfaces; supersedes E1 CPU relight when on. */
s32 g_pcSceneDecor = 0;          /* W9: default OFF (identity-first). Render-only imported 3D models over the untouched sim (decor_native.c). */
s32 g_pcFontUpscale = 3;         /* M4.1: Video.FontUpscale — HUD/menu glyph supersample factor (1-8, default 3). GE007_FONT_UPSCALE overrides via the settings env path. */
char g_pcSceneDecorDir[1024] = "assets/decor"; /* Video.SceneDecorDir: per-level <slug>.decor.txt manifests + glTF models. */
f32 g_pcViewmodelFov = 50.0f;    /* remaster default: weapon rendered at a fixed reference FOV (matches the 50deg world default) regardless of world FOV so the gun does not stretch at wide FOV. 0.0 = follow world FOV (vanilla coupling, A/B identity). */
s32 g_pcGradePresets = 1;        /* remaster default: on (subtle per-level mood grade atop the global grade) */
#ifdef MGB64_PORTMASTER_GLES
s32 g_pcTonemap = 0;
s32 g_pcRemasterFX = 0;
#else
s32 g_pcTonemap = 1;             /* remaster default: on (gentle filmic highlight rolloff for a cinematic look) */
s32 g_pcRemasterFX = 1;          /* MASTER faithful switch: 0 = bypass ALL remaster post-FX (grade/tonemap/bloom/vignette/sharpen/dither/FXAA) for the original look. HD textures + SSAA (fidelity, not look) stay via their own settings. */
#endif
s32 g_pcFpsOverlay = 1;          /* T11: app-level FPS/frame-time/1%-low overlay. Pinned to 0 by the --faithful/--faithful-hd presets [AUDIT-0010] (a HUD debug widget, not part of the original LOOK); the normal/remaster default is on. Force-suppressed (zero DL bytes) under --deterministic / GE007_BACKGROUND / --screenshot-frame sessions regardless of this setting — see src/game/pc_fps_overlay.c. */
char g_pcTexturePack[1024] = ""; /* Video.TexturePack: dir of an HD texture pack (textures/tok####.png). Empty = off (stock, byte-identical). */
f32 g_pcGradeLevelSat = 1.0f;    /* renderer-internal: per-level saturation mult (identity until set by table) */
f32 g_pcGradeLevelCon = 1.0f;    /* renderer-internal: per-level contrast mult */
f32 g_pcGradeLevelTintR = 1.0f;  /* renderer-internal: per-level tint R */
f32 g_pcGradeLevelTintG = 1.0f;  /* renderer-internal: per-level tint G */
f32 g_pcGradeLevelTintB = 1.0f;  /* renderer-internal: per-level tint B */

/* ===== Window mode state ===== */
typedef enum PlatformWindowMode {
    PLATFORM_WINDOW_MODE_WINDOWED = 0,
    PLATFORM_WINDOW_MODE_BORDERLESS = 1,
    PLATFORM_WINDOW_MODE_EXCLUSIVE = 2
} PlatformWindowMode;

static s32 g_windowMode = PLATFORM_WINDOW_MODE_WINDOWED;
/* RX.3 Fix B: the fullscreen mode the Alt+Enter runtime toggle returns to. Tracks
 * the last non-windowed mode actually applied (borderless or exclusive from
 * Video.WindowMode), so an exclusive user toggles back into exclusive rather than
 * being forced to borderless. Defaults to borderless when the game boots windowed. */
static s32 g_preferredFullscreenMode = PLATFORM_WINDOW_MODE_BORDERLESS;
static const ConfigEnumOption k_windowModeOptions[] = {
    { "windowed", PLATFORM_WINDOW_MODE_WINDOWED },
    { "borderless", PLATFORM_WINDOW_MODE_BORDERLESS },
    { "exclusive", PLATFORM_WINDOW_MODE_EXCLUSIVE },
};

/* ===== App-shell (launcher) UI settings (RX.2) =====
 * Consumed by the in-process ImGui app shell (src/app), NOT the engine renderer.
 * UI.LauncherFullscreen decides whether the pre-boot launcher window fills the
 * display (handhelds) or floats in a resizable window (desktop dev). Registered
 * here in the shared registry so they persist to ge007.ini, appear in the
 * settings menu, and dump like every other setting; the engine binary just
 * ignores them. Storage is trivially small and the defaults are identity. */
typedef enum PlatformLauncherFullscreen {
    PLATFORM_LAUNCHER_FS_AUTO = 0,  /* fill on small/high-DPI panels, float on desktop */
    PLATFORM_LAUNCHER_FS_ON = 1,    /* always fill */
    PLATFORM_LAUNCHER_FS_OFF = 2    /* always windowed/resizable */
} PlatformLauncherFullscreen;
static s32 g_uiLauncherFullscreen = PLATFORM_LAUNCHER_FS_AUTO;
static const ConfigEnumOption k_launcherFullscreenOptions[] = {
    { "auto", PLATFORM_LAUNCHER_FS_AUTO },
    { "on", PLATFORM_LAUNCHER_FS_ON },
    { "off", PLATFORM_LAUNCHER_FS_OFF },
};
/* UI.Scale: app-shell font/metric scale (1.0 = default; handhelds ~1.25-1.5). */
static f32 g_uiScale = 1.0f;

typedef enum PlatformVSyncMode {
    PLATFORM_VSYNC_OFF = 0,
    PLATFORM_VSYNC_ON = 1,
    PLATFORM_VSYNC_ADAPTIVE = 2
} PlatformVSyncMode;

/* remaster default: ADAPTIVE by design (NOT a hard "on"). Adaptive is tear-free
 * at/above the cap and drops to tear-when-slow to avoid latency spikes, with a
 * hard-vsync fallback on unsupported drivers. A literal "on" would change sim
 * substep distribution (substep count is wall-clock-derived via g_ClockTimer),
 * a pacing risk with no headless coverage, for no benefit on adaptive-capable GPUs. */
static s32 g_vsyncMode = PLATFORM_VSYNC_ADAPTIVE;
static const ConfigEnumOption k_vsyncOptions[] = {
    { "off", PLATFORM_VSYNC_OFF },
    { "on", PLATFORM_VSYNC_ON },
    { "adaptive", PLATFORM_VSYNC_ADAPTIVE },
};

typedef enum PlatformFrameCapMode {
    PLATFORM_FRAME_CAP_DISPLAY = 0,
    PLATFORM_FRAME_CAP_30 = 30,
    PLATFORM_FRAME_CAP_60 = 60
} PlatformFrameCapMode;

static s32 g_frameCapMode = PLATFORM_FRAME_CAP_60;
static const ConfigEnumOption k_frameCapOptions[] = {
    { "30", PLATFORM_FRAME_CAP_30 },
    { "60", PLATFORM_FRAME_CAP_60 },
    { "display", PLATFORM_FRAME_CAP_DISPLAY },
};

static const ConfigEnumOption k_msaaOptions[] = {
    { "0", 0 },
    { "2", 2 },
    { "4", 4 },
    { "8", 8 },
};

typedef enum PlatformRetroFilterMode {
    PLATFORM_RETRO_FILTER_AUTO = 0,
    PLATFORM_RETRO_FILTER_OFF = 1,
    PLATFORM_RETRO_FILTER_ON = 2
} PlatformRetroFilterMode;

s32 g_pcRetroFilterMode = PLATFORM_RETRO_FILTER_AUTO;
static const ConfigEnumOption k_retroFilterOptions[] = {
    { "auto", PLATFORM_RETRO_FILTER_AUTO },
    { "off", PLATFORM_RETRO_FILTER_OFF },
    { "on", PLATFORM_RETRO_FILTER_ON },
};

/* SSAO reconstruction mode (W3.E2). planar = the depth-only v1 wash (all
 * backends); hemisphere = view-space reconstruction + cosine-hemisphere AO
 * (Metal-only; GL logs one warn and falls back to planar). Numeric values are
 * load-bearing: the Metal filter reads g_pcSsaoMode directly (1/2). */
static const ConfigEnumOption k_ssaoModeOptions[] = {
    { "planar", 1 },
    { "hemisphere", 2 },
};

/* ===== Configurable window/display settings ===== */
static s32 g_cfgWindowW = 1440;
static s32 g_cfgWindowH = 810;
static s32 g_cfgWindowX = -1;
static s32 g_cfgWindowY = -1;
static s32 g_cfgDisplayIndex = 0;
static s32 g_cfgHiDpi = 0;
static s32 g_cfgFullscreenW = 0;
static s32 g_cfgFullscreenH = 0;
static s32 g_cfgFullscreenRefresh = 0;

int platformPrintDisplays(FILE *f)
{
    int initialized_here = 0;
    int display_count;

    if (f == NULL) {
        f = stdout;
    }

    if ((SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) == 0) {
        if (SDL_InitSubSystem(SDL_INIT_VIDEO) < 0) {
            fprintf(stderr, "[SDL] Failed to initialize video for display listing: %s\n",
                    SDL_GetError());
            return 0;
        }
        initialized_here = 1;
    }

    display_count = SDL_GetNumVideoDisplays();
    if (display_count < 0) {
        fprintf(stderr, "[SDL] Failed to enumerate displays: %s\n", SDL_GetError());
        if (initialized_here) {
            SDL_QuitSubSystem(SDL_INIT_VIDEO);
        }
        return 0;
    }

    fprintf(f, "SDL displays (%d):\n", display_count);
    for (int i = 0; i < display_count; i++) {
        const char *name = SDL_GetDisplayName(i);
        SDL_Rect bounds = {0, 0, 0, 0};
        SDL_Rect usable = {0, 0, 0, 0};
        SDL_DisplayMode current = {0};
        int mode_count;

        SDL_GetDisplayBounds(i, &bounds);
        SDL_GetDisplayUsableBounds(i, &usable);
        SDL_GetCurrentDisplayMode(i, &current);

        fprintf(f,
                "  [%d] %s bounds=%dx%d+%d+%d usable=%dx%d+%d+%d current=%dx%d@%dHz\n",
                i,
                (name != NULL && name[0] != '\0') ? name : "(unnamed)",
                bounds.w, bounds.h, bounds.x, bounds.y,
                usable.w, usable.h, usable.x, usable.y,
                current.w, current.h, current.refresh_rate);

        mode_count = SDL_GetNumDisplayModes(i);
        if (mode_count < 0) {
            fprintf(f, "      modes: unavailable (%s)\n", SDL_GetError());
            continue;
        }

        for (int m = 0; m < mode_count; m++) {
            SDL_DisplayMode mode = {0};

            if (SDL_GetDisplayMode(i, m, &mode) != 0) {
                continue;
            }
            fprintf(f, "      mode[%d] %dx%d@%dHz %s\n",
                    m,
                    mode.w,
                    mode.h,
                    mode.refresh_rate,
                    SDL_GetPixelFormatName(mode.format));
        }
    }

    if (initialized_here) {
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
    }
    return 1;
}

/* ===== Screenshot support ===== */
#define SCREENSHOT_W 640
#define SCREENSHOT_H 480
static int g_screenshotRequested = 0;
static int g_screenshotCounter = 0;
int g_autoScreenshotFrame = -1;  /* frame number to auto-capture (-1 = disabled) */
int g_autoScreenshotGameTimer = -1; /* g_GlobalTimer value to auto-capture (-1 = disabled) */
int g_autoScreenshotExit = 0;    /* exit after auto-screenshot */
/* T11: sticky (never cleared) flag for "this process was launched with
 * --screenshot-frame". g_autoScreenshotFrame itself resets to -1 once it
 * fires, so it can't be used as a whole-session suppression latch — this
 * can. Set once at CLI parse time (main_pc.c); read by the FPS overlay's
 * suppression gate so headless capture harnesses stay byte-identical even
 * if a tool omits --deterministic. */
int g_screenshotFrameSessionActive = 0;
static char g_screenshotLabelStorage[96];
static const char *g_screenshotLabel = NULL; /* label for screenshot filename */
int g_freezeInput = 0;           /* zero all controller input for deterministic screenshots */

extern s32 g_diagDisplayCastLastIndex;
extern s32 g_diagDisplayCastLastAnimIndex;
extern s32 g_diagDisplayCastLastCameraPreset;
extern s32 g_diagDisplayCastLastTimer;

/* [Task 5b] The screenshot canvas is a FIXED 640x480 (4:3). When the source
 * framebuffer is not 4:3 (the shipped default window is 1440x810 = 16:9), the resample below
 * letterboxes/pillarboxes the frame to preserve its aspect — it ADDS black bars
 * that the engine never rendered. That is correct for the capture, but it makes a
 * raw pixel comparison against a 4:3 hardware/emulator reference invalid: the
 * scene content is uniformly scaled (16:9 -> 0.75x vertically) and offset inside
 * the canvas. Task 5's geometric leg was confounded exactly this way (native's
 * band measured 75px vs stock's 20px, all of it capture-added). Emit a one-shot
 * disclosure so the confound can never again be silent in a capture log. FID-0135
 * additionally gives hardware-reference harnesses a fail-closed contract:
 * GE007_FIDELITY_CAPTURE=1 refuses a non-4:3 drawable before readback. */
static void platformWarnScreenshotAspectOnce(int src_w, int src_h, int dst_w, int dst_h) {
    static int warned = 0;
    if (warned || src_w <= 0 || src_h <= 0 || dst_w <= 0 || dst_h <= 0) {
        return;
    }
    if ((int64_t)src_w * dst_h == (int64_t)dst_w * src_h) {
        return;  /* same aspect: pure rescale, no bars added */
    }
    warned = 1;
    fprintf(stderr,
            "[SDL] screenshot: source framebuffer %dx%d has a different aspect than the %dx%d "
            "capture canvas; the capture ADDS letterbox/pillarbox bars and rescales "
            "the scene. Do NOT pixel-compare this against a 4:3 hardware reference — "
            "re-capture with a 4:3 window (e.g. GE007_WINDOW_WIDTH=1440 "
            "GE007_WINDOW_HEIGHT=1080).\n",
            src_w, src_h, dst_w, dst_h);
}

static int platformFidelityScreenshotAspectIsValid(int source_size_known,
                                                   int src_w, int src_h,
                                                   int dst_w, int dst_h) {
    if (!port_env_bool("GE007_FIDELITY_CAPTURE", 0,
                       "Fail hardware-reference screenshots when the readback source is not 4:3.")) {
        return 1;
    }
    if (source_size_known && src_w > 0 && src_h > 0 && dst_w > 0 && dst_h > 0 &&
        (int64_t)src_w * dst_h == (int64_t)dst_w * src_h) {
        return 1;
    }

    if (!source_size_known) {
        fprintf(stderr,
                "[SDL] fidelity capture refused: source framebuffer size is unavailable; "
                "GE007_FIDELITY_CAPTURE=1 requires a verified 4:3 readback source.\n");
        return 0;
    }

    fprintf(stderr,
            "[SDL] fidelity capture refused: source framebuffer %dx%d is not 4:3; "
            "GE007_FIDELITY_CAPTURE=1 requires a 4:3 hardware-reference source "
            "(use Video.WindowWidth=640 and Video.WindowHeight=480).\n",
            src_w, src_h);
    return 0;
}

static void platformResampleFramebufferToScreenshot(const unsigned char *src,
                                                    int src_w, int src_h,
                                                    unsigned char *dst,
                                                    int dst_w, int dst_h) {
    int scaled_w;
    int scaled_h;
    int offset_x;
    int offset_y;

    memset(dst, 0, (size_t)dst_w * (size_t)dst_h * 3);

    if (src == NULL || src_w <= 0 || src_h <= 0 || dst == NULL || dst_w <= 0 || dst_h <= 0) {
        return;
    }

    if ((int64_t)dst_w * src_h <= (int64_t)dst_h * src_w) {
        scaled_w = dst_w;
        scaled_h = (src_h * dst_w + src_w / 2) / src_w;
    } else {
        scaled_h = dst_h;
        scaled_w = (src_w * dst_h + src_h / 2) / src_h;
    }

    if (scaled_w < 1) {
        scaled_w = 1;
    } else if (scaled_w > dst_w) {
        scaled_w = dst_w;
    }

    if (scaled_h < 1) {
        scaled_h = 1;
    } else if (scaled_h > dst_h) {
        scaled_h = dst_h;
    }

    offset_x = (dst_w - scaled_w) / 2;
    offset_y = (dst_h - scaled_h) / 2;

    for (int y = 0; y < scaled_h; y++) {
        int src_y = ((y * 2 + 1) * src_h) / (scaled_h * 2);

        if (src_y < 0) {
            src_y = 0;
        } else if (src_y >= src_h) {
            src_y = src_h - 1;
        }

        for (int x = 0; x < scaled_w; x++) {
            int src_x = ((x * 2 + 1) * src_w) / (scaled_w * 2);
            int dst_index;
            int src_index;

            if (src_x < 0) {
                src_x = 0;
            } else if (src_x >= src_w) {
                src_x = src_w - 1;
            }

            dst_index = ((y + offset_y) * dst_w + (x + offset_x)) * 3;
            src_index = (src_y * src_w + src_x) * 3;
            dst[dst_index + 0] = src[src_index + 0];
            dst[dst_index + 1] = src[src_index + 1];
            dst[dst_index + 2] = src[src_index + 2];
        }
    }
}

static void platformResizeRgbBilinear(const unsigned char *src,
                                      int src_w, int src_h,
                                      unsigned char *dst,
                                      int dst_w, int dst_h) {
    if (src == NULL || dst == NULL || src_w <= 0 || src_h <= 0 || dst_w <= 0 || dst_h <= 0) {
        return;
    }

    for (int y = 0; y < dst_h; y++) {
        float src_y = ((float)y + 0.5f) * (float)src_h / (float)dst_h - 0.5f;
        int y0 = (int)floorf(src_y);
        float fy = src_y - (float)y0;

        if (y0 < 0) {
            y0 = 0;
            fy = 0.0f;
        } else if (y0 >= src_h - 1) {
            y0 = src_h - 1;
            fy = 0.0f;
        }

        int y1 = y0 + 1;
        if (y1 >= src_h) {
            y1 = y0;
        }

        for (int x = 0; x < dst_w; x++) {
            float src_x = ((float)x + 0.5f) * (float)src_w / (float)dst_w - 0.5f;
            int x0 = (int)floorf(src_x);
            float fx = src_x - (float)x0;

            if (x0 < 0) {
                x0 = 0;
                fx = 0.0f;
            } else if (x0 >= src_w - 1) {
                x0 = src_w - 1;
                fx = 0.0f;
            }

            int x1 = x0 + 1;
            if (x1 >= src_w) {
                x1 = x0;
            }

            int dst_index = (y * dst_w + x) * 3;
            int idx00 = (y0 * src_w + x0) * 3;
            int idx10 = (y0 * src_w + x1) * 3;
            int idx01 = (y1 * src_w + x0) * 3;
            int idx11 = (y1 * src_w + x1) * 3;

            for (int c = 0; c < 3; c++) {
                float v0 = (float)src[idx00 + c] + ((float)src[idx10 + c] - (float)src[idx00 + c]) * fx;
                float v1 = (float)src[idx01 + c] + ((float)src[idx11 + c] - (float)src[idx01 + c]) * fx;
                float v = v0 + (v1 - v0) * fy;
                int out = (int)(v + 0.5f);

                if (out < 0) {
                    out = 0;
                } else if (out > 255) {
                    out = 255;
                }
                dst[dst_index + c] = (unsigned char)out;
            }
        }
    }
}

static void platformApplyScreenshotViFilter(unsigned char *pixels, int width, int height) {
    const char *env = getenv("GE007_DIAG_SCREENSHOT_VI_FILTER");
    int filter_w = 0;
    int filter_h = 0;
    unsigned char *small = NULL;
    unsigned char *filtered = NULL;

    if (env == NULL || env[0] == '\0' || strcmp(env, "0") == 0) {
        return;
    }

    if (sscanf(env, "%dx%d", &filter_w, &filter_h) != 2 ||
        filter_w <= 0 || filter_h <= 0 ||
        filter_w > width || filter_h > height) {
        fprintf(stderr,
                "[SDL] Ignoring invalid GE007_DIAG_SCREENSHOT_VI_FILTER=%s "
                "(expected WxH within %dx%d)\n",
                env, width, height);
        return;
    }

    small = (unsigned char *)malloc((size_t)filter_w * (size_t)filter_h * 3);
    filtered = (unsigned char *)malloc((size_t)width * (size_t)height * 3);
    if (small == NULL || filtered == NULL) {
        free(small);
        free(filtered);
        return;
    }

    platformResizeRgbBilinear(pixels, width, height, small, filter_w, filter_h);
    platformResizeRgbBilinear(small, filter_w, filter_h, filtered, width, height);
    memcpy(pixels, filtered, (size_t)width * (size_t)height * 3);
    printf("[SDL] Screenshot VI filter %dx%d -> %dx%d (GE007_DIAG_SCREENSHOT_VI_FILTER)\n",
           filter_w, filter_h, width, height);

    free(small);
    free(filtered);
}

void platformSetScreenshotLabel(const char *label) {
    size_t out = 0;

    if (label == NULL || *label == '\0') {
        g_screenshotLabelStorage[0] = '\0';
        g_screenshotLabel = NULL;
        return;
    }

    while (*label != '\0' && out < sizeof(g_screenshotLabelStorage) - 1) {
        char c = *label++;
        if ((c >= 'a' && c <= 'z')
            || (c >= 'A' && c <= 'Z')
            || (c >= '0' && c <= '9')
            || c == '-'
            || c == '_'
            || c == '.') {
            g_screenshotLabelStorage[out++] = c;
        }
    }

    g_screenshotLabelStorage[out] = '\0';
    g_screenshotLabel = out > 0 ? g_screenshotLabelStorage : NULL;
}

/* Set to 1 whenever the most recent screenshot save did not fully persist a valid
 * BMP (alloc, readback, open, write, or close failure). Read by the auto-exit path
 * so an automation capture that silently failed exits nonzero [AUDIT-0043]. */
static int s_lastScreenshotFailed = 0;

/* [AUDIT-0040] "Is a screenshot pending for the frame currently being composited?"
 * Queried from gfx_run_dl (GLES only) to decide whether to stash the composited
 * default framebuffer BEFORE the swap. It is a deliberate SUPERSET of the actual
 * capture conditions consumed in platformFrameSync: over-arming only costs an
 * extra pre-swap readback (the freshest stash is always the one consumed, so a
 * few early stashes are harmless), while under-arming would miss the target
 * frame. Proximity windows keep automation runs from reading back every frame
 * from boot. Side-effect-free: it must NOT call the display-cast/menu due-state
 * machines (those consume delay state); instead it latches on their env presence.
 * Defined unconditionally (compiles in every config) but only called on GLES. */
int platformScreenshotCapturePendingForGles(void) {
    extern s32 g_GlobalTimer;
    static int diag_latch = -1;

    if (g_screenshotRequested) {
        return 1;
    }
    if (g_autoScreenshotFrame >= 0 && g_frameSyncCallCount + 3 >= g_autoScreenshotFrame) {
        return 1;
    }
    if (g_autoScreenshotGameTimer >= 0 && g_GlobalTimer + 3 >= g_autoScreenshotGameTimer) {
        return 1;
    }
    if (diag_latch < 0) {
        diag_latch = (getenv("GE007_DIAG_DISPLAYCAST_SCREENSHOT_TIMER") != NULL ||
                      getenv("GE007_DIAG_MENU_SCREENSHOT_MENU") != NULL) ? 1 : 0;
    }
    return diag_latch;
}

void platformSaveScreenshot(void) {
    int w = SCREENSHOT_W, h = SCREENSHOT_H;
    s_lastScreenshotFailed = 1;  /* pessimistic: cleared only on full success below */
    int src_w = w;
    int src_h = h;
    int row_size;
    int data_size;
    int file_size;
    unsigned char *pixels = NULL;
    unsigned char *source_pixels = NULL;
    int native_size_screenshot = getenv("GE007_DIAG_SCREENSHOT_NATIVE_SIZE") != NULL;
    int source_size_known = 0;

    if (g_sdlWindow != NULL) {
        if (gfx_backend_use_metal() || gfx_backend_use_webgpu()) {
            /* Metal/WebGPU read from an offscreen render target whose size can
             * differ from CAMetalLayer's Retina backing size (RenderScale and
             * WebGPU surface configuration both make SDL's drawable query the
             * wrong authority). Ask the active backend for the exact texture
             * read_framebuffer_rgb will copy. */
            source_size_known = gfx_backend_get_framebuffer_size(&src_w, &src_h);
        } else
        {
            SDL_GL_GetDrawableSize(g_sdlWindow, &src_w, &src_h);
            source_size_known = src_w > 0 && src_h > 0;
        }
    }

    /* FID-0135: an oracle must never silently turn a widescreen framebuffer
     * into a 4:3 "reference" by adding capture-only bars. It must also reject
     * an unavailable source size rather than accepting the 640x480 fallback.
     * s_lastScreenshotFailed is already set, so the auto-exit path reports 4. */
    if (!platformFidelityScreenshotAspectIsValid(source_size_known,
                                                 src_w, src_h, w, h)) {
        return;
    }

    if (src_w < 1) {
        src_w = w;
    }

    if (src_h < 1) {
        src_h = h;
    }

    if (native_size_screenshot) {
        w = src_w;
        h = src_h;
    }

    row_size = ((w * 3 + 3) & ~3);  /* BMP rows must be 4-byte aligned */
    data_size = row_size * h;
    file_size = 54 + data_size;

    source_pixels = (unsigned char *)malloc((size_t)src_w * (size_t)src_h * 3);
    pixels = (unsigned char *)malloc((size_t)w * (size_t)h * 3);

    if (!source_pixels || !pixels) {
        free(source_pixels);
        free(pixels);
        return;
    }

#if defined(__APPLE__) || defined(MGB64_WEBGPU_BACKEND)
    if (gfx_backend_use_metal() || gfx_backend_use_webgpu()) {
        /* Metal/WebGPU: readback of the last composited scene via the backend
         * (returns GL-convention bottom-left RGB, so downstream BMP/VI handling
         * is unchanged). There is no GL context, so the glReadPixels path below
         * must be skipped. The WebGPU readback is stubbed until Task 5 and
         * returns false — the graceful "readback failed" path fires. */
        extern bool gfx_backend_read_framebuffer_rgb(int, int, int, int, unsigned char *);
        if (!gfx_backend_read_framebuffer_rgb(0, 0, src_w, src_h, source_pixels)) {
            fprintf(stderr, "[gfx] screenshot readback failed\n");
            free(source_pixels);
            free(pixels);
            return;
        }
    } else
#endif
    {
#ifdef MGB64_PORTMASTER_GLES
        /* [AUDIT-0040] GLES has no readable GL_FRONT for the default framebuffer,
         * and the back buffer is undefined here (post-swap). Instead of reading the
         * stale back buffer, consume the composited final frame that gfx_run_dl
         * stashed BEFORE the swap (gfx_opengl_capture_default_framebuffer). Same
         * bottom-up RGB convention as the front-buffer path below, so the BMP flip
         * downstream is unchanged. Fail closed (leave s_lastScreenshotFailed=1) if
         * no valid frame was stashed — never emit a stale/garbage image. */
        {
            extern bool gfx_opengl_get_captured_frame(int *, int *, const unsigned char **);
            int cap_w = 0, cap_h = 0;
            const unsigned char *cap_px = NULL;
            if (!gfx_opengl_get_captured_frame(&cap_w, &cap_h, &cap_px) || cap_px == NULL) {
                fprintf(stderr, "[gfx] GLES screenshot: no composited frame stashed (capture skipped)\n");
                free(source_pixels);
                free(pixels);
                return;
            }
            if (cap_w != src_w || cap_h != src_h) {
                fprintf(stderr, "[gfx] GLES screenshot: stashed frame %dx%d != drawable %dx%d\n",
                        cap_w, cap_h, src_w, src_h);
                free(source_pixels);
                free(pixels);
                return;
            }
            memcpy(source_pixels, cap_px, (size_t)src_w * (size_t)src_h * 3);
        }
#else
        /* Read from the FRONT buffer: this runs at the top of platformFrameSync,
         * BEFORE the current frame's swap (handled later in gfx_end_frame). The BACK
         * buffer is undefined right after the previous frame's SDL_GL_SwapWindow, so a
         * default-read-buffer (GL_BACK) glReadPixels here captures stale/garbage pixels
         * — corrupting every parity/oracle/contact-sheet capture. GL_FRONT holds the
         * last fully-presented frame (deterministic). Restore GL_BACK after. */
        glReadBuffer(GL_FRONT);
        glReadPixels(0, 0, src_w, src_h, GL_RGB, GL_UNSIGNED_BYTE, source_pixels);
        glReadBuffer(GL_BACK);
#endif
    }

    if (src_w == w && src_h == h) {
        memcpy(pixels, source_pixels, (size_t)w * (size_t)h * 3);
    } else {
        platformWarnScreenshotAspectOnce(src_w, src_h, w, h);
        platformResampleFramebufferToScreenshot(source_pixels, src_w, src_h, pixels, w, h);
    }
    platformApplyScreenshotViFilter(pixels, w, h);

    char filename[128];
    if (g_screenshotLabel) {
        snprintf(filename, sizeof(filename), "screenshot_%s.bmp", g_screenshotLabel);
    } else {
        snprintf(filename, sizeof(filename), "screenshot_%03d.bmp", g_screenshotCounter++);
    }
    FILE *f = fopen(filename, "wb");
    if (!f) {
        fprintf(stderr, "[SDL] Failed to save screenshot %s: %s\n",
                filename, strerror(errno));
        free(source_pixels);
        free(pixels);
        return;
    }

    /* BMP header (54 bytes) */
    unsigned char hdr[54] = {0};
    hdr[0] = 'B'; hdr[1] = 'M';
    hdr[2] = file_size; hdr[3] = file_size >> 8; hdr[4] = file_size >> 16; hdr[5] = file_size >> 24;
    hdr[10] = 54;  /* data offset */
    hdr[14] = 40;  /* DIB header size */
    hdr[18] = w; hdr[19] = w >> 8;
    hdr[22] = h; hdr[23] = h >> 8;
    hdr[26] = 1;   /* planes */
    hdr[28] = 24;  /* bpp */
    hdr[34] = data_size; hdr[35] = data_size >> 8; hdr[36] = data_size >> 16; hdr[37] = data_size >> 24;
    bool write_ok = (fwrite(hdr, 1, 54, f) == 54);

    /* BMP stores rows bottom-to-top (OpenGL gives us bottom-to-top), RGB→BGR */
    unsigned char *row_buf = (unsigned char *)malloc(row_size);
    if (!row_buf) {
        write_ok = false;
    } else {
        for (int y = 0; y < h; y++) {
            unsigned char *src = pixels + y * w * 3;
            memset(row_buf, 0, row_size);
            for (int x = 0; x < w; x++) {
                row_buf[x * 3 + 0] = src[x * 3 + 2];  /* B */
                row_buf[x * 3 + 1] = src[x * 3 + 1];  /* G */
                row_buf[x * 3 + 2] = src[x * 3 + 0];  /* R */
            }
            if (fwrite(row_buf, 1, row_size, f) != (size_t)row_size) {
                write_ok = false;
            }
        }
        free(row_buf);
    }
    if (fclose(f) != 0) {
        write_ok = false;
    }
    free(source_pixels);
    free(pixels);
    if (write_ok) {
        printf("[SDL] Screenshot saved: %s\n", filename);
        s_lastScreenshotFailed = 0;  /* full success */
    } else {
        fprintf(stderr, "[SDL] Failed to write screenshot %s (short write or I/O error); removing truncated file.\n",
                filename);
        remove(filename);
    }
}

static int platformParseDiagS32Env(const char *name, s32 *out) {
    const char *env = getenv(name);
    char *end = NULL;
    long value;

    if (env == NULL || env[0] == '\0') {
        return 0;
    }

    value = strtol(env, &end, 10);
    if (end == env) {
        fprintf(stderr, "[SDL] Ignoring invalid %s=%s\n", name, env);
        return 0;
    }
    if (value < INT_MIN) value = INT_MIN;
    if (value > INT_MAX) value = INT_MAX;
    *out = (s32)value;
    return 1;
}

static int platformParseDiagF32Env(const char *name, f32 *out) {
    const char *env = getenv(name);
    char *end = NULL;
    double value;

    if (env == NULL || env[0] == '\0') {
        return 0;
    }

    value = strtod(env, &end);
    if (end == env) {
        fprintf(stderr, "[SDL] Ignoring invalid %s=%s\n", name, env);
        return 0;
    }
    *out = (f32)value;
    return 1;
}

static void platformApplyWindowSizeEnv(void) {
    const char *env = getenv("GE007_WINDOW_SIZE");
    char *end = NULL;
    long width;
    long height;

    if (env == NULL || env[0] == '\0') {
        return;
    }

    width = strtol(env, &end, 10);
    if (end == env || (*end != 'x' && *end != 'X')) {
        fprintf(stderr, "[SDL] Ignoring invalid GE007_WINDOW_SIZE=%s\n", env);
        return;
    }

    height = strtol(end + 1, &end, 10);
    if (height <= 0 || width <= 0 || (*end != '\0' && *end != ' ' && *end != '\t')) {
        fprintf(stderr, "[SDL] Ignoring invalid GE007_WINDOW_SIZE=%s\n", env);
        return;
    }

    if (width < 320) width = 320;
    if (width > 3840) width = 3840;
    if (height < 240) height = 240;
    if (height > 2160) height = 2160;
    g_cfgWindowW = (s32)width;
    g_cfgWindowH = (s32)height;
    fprintf(stderr,
            "[SDL] Window size override %dx%d (GE007_WINDOW_SIZE)\n",
            g_cfgWindowW, g_cfgWindowH);
}

static int platformFloatWithinTolerance(f32 actual, f32 expected, f32 tolerance) {
    return fabsf(actual - expected) <= tolerance;
}

static int platformDiagDisplayCastScreenshotDue(void) {
    static int checked;
    static int done;
    static s32 target_timer = INT_MIN;
    static s32 target_index = INT_MIN;
    static s32 target_anim_index = INT_MIN;
    static s32 target_camera_preset = INT_MIN;
    static s32 capture_delay = 0;
    static s32 pending_delay = -1;

    if (done) {
        return 0;
    }

    if (pending_delay >= 0) {
        if (pending_delay > 0) {
            pending_delay--;
        }
        if (pending_delay == 0) {
            pending_delay = -1;
            done = 1;
            fprintf(stderr,
                    "[SDL] DIAG DISPLAYCAST SCREENSHOT capture frame=%d timer=%d index=%d anim=%d camera=%d\n",
                    g_frameSyncCallCount,
                    g_diagDisplayCastLastTimer,
                    g_diagDisplayCastLastIndex,
                    g_diagDisplayCastLastAnimIndex,
                    g_diagDisplayCastLastCameraPreset);
            return 1;
        }
        return 0;
    }

    if (!checked) {
        checked = 1;
        if (!platformParseDiagS32Env("GE007_DIAG_DISPLAYCAST_SCREENSHOT_TIMER", &target_timer)) {
            return 0;
        }
        platformParseDiagS32Env("GE007_DIAG_DISPLAYCAST_SCREENSHOT_INDEX", &target_index);
        platformParseDiagS32Env("GE007_DIAG_DISPLAYCAST_SCREENSHOT_ANIM_INDEX", &target_anim_index);
        platformParseDiagS32Env("GE007_DIAG_DISPLAYCAST_SCREENSHOT_CAMERA_PRESET", &target_camera_preset);
        platformParseDiagS32Env("GE007_DIAG_DISPLAYCAST_SCREENSHOT_DELAY", &capture_delay);
        if (capture_delay < 0) {
            capture_delay = 0;
        }
        fprintf(stderr,
                "[SDL] DIAG DISPLAYCAST SCREENSHOT armed timer=%d index=%d anim=%d camera=%d delay=%d\n",
                target_timer, target_index, target_anim_index, target_camera_preset, capture_delay);
    }

    if (target_timer == INT_MIN || g_diagDisplayCastLastTimer != target_timer) {
        return 0;
    }
    if (target_index != INT_MIN && g_diagDisplayCastLastIndex != target_index) {
        return 0;
    }
    if (target_anim_index != INT_MIN && g_diagDisplayCastLastAnimIndex != target_anim_index) {
        return 0;
    }
    if (target_camera_preset != INT_MIN && g_diagDisplayCastLastCameraPreset != target_camera_preset) {
        return 0;
    }

    fprintf(stderr,
            "[SDL] DIAG DISPLAYCAST SCREENSHOT matched frame=%d timer=%d index=%d anim=%d camera=%d\n",
            g_frameSyncCallCount,
            g_diagDisplayCastLastTimer,
            g_diagDisplayCastLastIndex,
            g_diagDisplayCastLastAnimIndex,
            g_diagDisplayCastLastCameraPreset);
    if (capture_delay > 0) {
        pending_delay = capture_delay;
        return 0;
    }

    done = 1;
    return 1;
}

static int platformDiagMenuScreenshotDue(void) {
    static int checked;
    static int done;
    static s32 target_menu = INT_MIN;
    static s32 target_timer = INT_MIN;
    static s32 timer_min = INT_MIN;
    static s32 timer_max = INT_MIN;
    static s32 target_wave = INT_MIN;
    static f32 target_title_x = 0.0f;
    static f32 target_title_y = 0.0f;
    static f32 target_rare_rotation = 0.0f;
    static f32 target_nintendo_rotation = 0.0f;
    static f32 target_nintendo_scale = 0.0f;
    static f32 tolerance = 0.5f;
    static int has_title_x;
    static int has_title_y;
    static int has_rare_rotation;
    static int has_nintendo_rotation;
    static int has_nintendo_scale;
    static s32 capture_delay = 0;
    static s32 pending_delay = -1;

    if (done) {
        return 0;
    }

    if (pending_delay >= 0) {
        if (pending_delay > 0) {
            pending_delay--;
        }
        if (pending_delay == 0) {
            pending_delay = -1;
            done = 1;
            fprintf(stderr,
                    "[SDL] DIAG MENU SCREENSHOT capture frame=%d menu=%d timer=%d "
                    "title=(x=%.4f y=%.4f rare=%.4f nintendo=%.4f scale=%.6f wave=%d)\n",
                    g_frameSyncCallCount,
                    (int)current_menu,
                    (int)g_MenuTimer,
                    g_TitleX,
                    g_TitleY,
                    D_8002A89C,
                    ninLogoRotRate,
                    ninLogoScale,
                    (int)word_CODE_bss_80069584);
            return 1;
        }
        return 0;
    }

    if (!checked) {
        checked = 1;
        if (!platformParseDiagS32Env("GE007_DIAG_MENU_SCREENSHOT_MENU", &target_menu)) {
            return 0;
        }
        if (platformParseDiagS32Env("GE007_DIAG_MENU_SCREENSHOT_TIMER", &target_timer)) {
            timer_min = target_timer;
            timer_max = target_timer;
        }
        platformParseDiagS32Env("GE007_DIAG_MENU_SCREENSHOT_TIMER_MIN", &timer_min);
        platformParseDiagS32Env("GE007_DIAG_MENU_SCREENSHOT_TIMER_MAX", &timer_max);
        platformParseDiagS32Env("GE007_DIAG_MENU_SCREENSHOT_WAVE", &target_wave);
        platformParseDiagF32Env("GE007_DIAG_MENU_SCREENSHOT_TOLERANCE", &tolerance);
        has_title_x = platformParseDiagF32Env("GE007_DIAG_MENU_SCREENSHOT_TITLE_X", &target_title_x);
        has_title_y = platformParseDiagF32Env("GE007_DIAG_MENU_SCREENSHOT_TITLE_Y", &target_title_y);
        has_rare_rotation = platformParseDiagF32Env("GE007_DIAG_MENU_SCREENSHOT_RARE_ROTATION", &target_rare_rotation);
        has_nintendo_rotation = platformParseDiagF32Env("GE007_DIAG_MENU_SCREENSHOT_NINTENDO_ROTATION", &target_nintendo_rotation);
        has_nintendo_scale = platformParseDiagF32Env("GE007_DIAG_MENU_SCREENSHOT_NINTENDO_SCALE", &target_nintendo_scale);
        platformParseDiagS32Env("GE007_DIAG_MENU_SCREENSHOT_DELAY", &capture_delay);
        if (capture_delay < 0) {
            capture_delay = 0;
        }
        if (tolerance < 0.0f) {
            tolerance = 0.0f;
        }
        fprintf(stderr,
                "[SDL] DIAG MENU SCREENSHOT armed menu=%d timer=%d min=%d max=%d "
                "rare=%s nintendo=%s scale=%s tolerance=%.4f delay=%d\n",
                (int)target_menu,
                (int)target_timer,
                (int)timer_min,
                (int)timer_max,
                has_rare_rotation ? "set" : "-",
                has_nintendo_rotation ? "set" : "-",
                has_nintendo_scale ? "set" : "-",
                tolerance,
                (int)capture_delay);
    }

    if (target_menu == INT_MIN || (s32)current_menu != target_menu) {
        return 0;
    }
    if (timer_min != INT_MIN && g_MenuTimer < timer_min) {
        return 0;
    }
    if (timer_max != INT_MIN && g_MenuTimer > timer_max) {
        return 0;
    }
    if (target_wave != INT_MIN && (s32)word_CODE_bss_80069584 != target_wave) {
        return 0;
    }
    if (has_title_x && !platformFloatWithinTolerance(g_TitleX, target_title_x, tolerance)) {
        return 0;
    }
    if (has_title_y && !platformFloatWithinTolerance(g_TitleY, target_title_y, tolerance)) {
        return 0;
    }
    if (has_rare_rotation && !platformFloatWithinTolerance(D_8002A89C, target_rare_rotation, tolerance)) {
        return 0;
    }
    if (has_nintendo_rotation && !platformFloatWithinTolerance(ninLogoRotRate, target_nintendo_rotation, tolerance)) {
        return 0;
    }
    if (has_nintendo_scale && !platformFloatWithinTolerance(ninLogoScale, target_nintendo_scale, tolerance)) {
        return 0;
    }

    fprintf(stderr,
            "[SDL] DIAG MENU SCREENSHOT matched frame=%d menu=%d timer=%d "
            "title=(x=%.4f y=%.4f rare=%.4f nintendo=%.4f scale=%.6f wave=%d)\n",
            g_frameSyncCallCount,
            (int)current_menu,
            (int)g_MenuTimer,
            g_TitleX,
            g_TitleY,
            D_8002A89C,
            ninLogoRotRate,
            ninLogoScale,
            (int)word_CODE_bss_80069584);
    if (capture_delay > 0) {
        pending_delay = capture_delay;
        return 0;
    }

    done = 1;
    return 1;
}

static void platformFinishAutoScreenshotIfRequested(void) {
    if (g_autoScreenshotExit) {
        /* Emit the sim-state invariance hash BEFORE any teardown, while the
         * pool/globals are intact (remaster P0.2 gate). No-op unless requested. */
        {
            extern void simStateHashEmitIfRequested(int frame, const char *replay);
            extern const char *g_pcStartRamrom;
            simStateHashEmitIfRequested(g_frameSyncCallCount, g_pcStartRamrom);
        }
        extern int g_crashRecoveryCount;
        if (g_crashRecoveryCount > 0) {
            printf("[GE007-PC] Auto-screenshot complete, but %d crash recoveries occurred; build needed recovery, exiting with error.\n",
                   g_crashRecoveryCount);
            platformShutdownSDL();
            exit(3);
        }
        if (s_lastScreenshotFailed) {
            fprintf(stderr, "[GE007-PC] Auto-screenshot did NOT persist a valid file; exiting with error.\n");
            platformShutdownSDL();
            exit(4);  /* automation must see a failed capture as failure [AUDIT-0043] */
        }
        printf("[GE007-PC] Auto-screenshot complete, exiting.\n");
        platformShutdownSDL();
        exit(0);
    }
}

/* ===== Mouse delta for N64 controller emulation ===== */
static int g_mouseDeltaX = 0;
static int g_mouseDeltaY = 0;

/* ===== PC input state (consumed by game-side NATIVE_PORT blocks) ===== */
static int g_mouseWheelY = 0;       /* scroll wheel accumulator */
int g_pcEscapePressed = 0;          /* Escape key edge-detect flag */
int g_pcCrouchToggle = 0;           /* C/Ctrl edge-detect flag */
int g_pcMouseRegrabFrame = 0;       /* suppress mouse buttons for 1 frame after regrab */
float g_pcMouseSensitivity = 0.15f; /* configurable mouse sensitivity */
float g_pcMouseSensAim = 0.05f;     /* aim-mode mouse sensitivity */
int g_pcInvertY = 0;                /* invert Y axis */
float g_pcGamepadLookSpeed = 8.0f;  /* gamepad right stick scaling */
/* Opt-in right-stick feel knobs (all default to vanilla-identity). */
float g_pcGamepadLookCurve = 1.5f;  /* remaster default: mild ease-in for fine aim (1.0 = linear) */
float g_pcGamepadDeadzone = 0.15f;  /* remaster default: tighter modern deadzone (paired with radial) */
int g_pcGamepadRadialDeadzone = 1;  /* remaster default: true radial rescale-from-edge */
int g_pcGamepadFpsScale = 1;        /* remaster default: frame-rate-independent look (no-op at 60fps) */
s32 g_pcSteadyView = 1;             /* Input.SteadyView       keep world camera upright while moving */

/* Rumble (MC.4). GoldenEye is a Rumble Pak title: the game raises its motor
 * signal via joyRumblePakStart()/joyRumblePakStop() on faithful events (weapon
 * fire, Bond damage). joy.c calls platformRumblePlayer()/platformRumbleStopAll()
 * (below) to actuate the host pad. Output-only: never reads/writes sim state,
 * consumes no RNG, and is force-suppressed under --deterministic so the
 * sim-state hash is identical rumble-on vs rumble-off. */
s32 g_pcRumble = 1;                  /* Input.Rumble           controller vibration master, ON */
s32 g_pcRumbleIntensity = 100;      /* Input.RumbleIntensity  strength 0-100 (%) */

/* Rebindable in-game menu / FPS-overlay toggles. These are host-side UI keys
 * (read by the app-shell overlay via mgb_config_get_int), not sim input, so they
 * never touch determinism. Defaults preserve the shipped bindings: F1 / gamepad
 * Back open the menu, F10 toggles the FPS box. */
s32 g_cfgMenuToggleKey    = SDLK_F1;                    /* Input.MenuToggleKey    keyboard opener */
s32 g_cfgMenuToggleButton = SDL_CONTROLLER_BUTTON_BACK; /* Input.MenuToggleButton gamepad opener */
s32 g_cfgFpsToggleKey     = SDLK_F10;                   /* Input.FpsToggleKey     FPS overlay hotkey */

/* ADS (aim-down-sights) feature flags. Master flag g_pcAdsEnabled ships OFF;
 * when 0 every ADS branch is bypassed and behavior is byte-identical to vanilla.
 * Consumers declare these inline as `extern` at their use sites. */
s32 g_pcAdsEnabled       = 0;     /* Input.AdsEnabled        master, OFF by default */
/* Full-auto fire-rate authenticity (FID-0056). g_pcFireRateAuthentic ships ON
 * (owner decision 2026-07-10: gameplay accuracy is the goal; the ~3x automatic
 * overspeed is a canonical fidelity defect, so the faithful cadence is the
 * default). ON scales the automatic fire gate to the N64 per-rendered-frame
 * cadence; set Input.FireRateAuthentic=0 (GE007_FIRE_RATE_AUTHENTIC=0) to opt
 * OUT and restore the legacy locked-60Hz fast fire. g_pcFireRateN64FrameCost =
 * assumed N64 sim-ticks per rendered frame (3 = the measured Dam ~20fps combat
 * rate; 2 = 30fps nominal; 4 = ~15fps heavy). */
s32 g_pcFireRateAuthentic   = 1;  /* Input.FireRateAuthentic   ON (faithful) by default */
s32 g_pcFireRateN64FrameCost = FIRE_RATE_N64_FRAME_COST_DEFAULT; /* Input.FireRateN64FrameCost */
f32 g_pcAdsSensitivity   = 1.0f;  /* Input.AdsSensitivity    flat aimed look mult   */
s32 g_pcAdsFovCoupleSens = 1;     /* Input.AdsFovCoupleSens  FOV-coupled slow-look   */
s32 g_pcAdsCenterCrosshair = 1;   /* Input.AdsCenterCrosshair stick center-pull      */
s32 g_pcAdsSpreadEnabled = 1;     /* Input.AdsSpreadEnabled  per-weapon spread mult  */
s32 g_pcAdsMovePenalty   = 1;     /* Input.AdsMovePenalty    aimed movement penalty  */
f32 g_pcAdsMoveScale     = 1.0f;  /* Input.AdsMoveScale      forward-speed trim      */
f32 g_pcAdsStrafeScale   = 1.0f;  /* Input.AdsStrafeScale    strafe-speed trim       */
s32 g_pcAdsFaithfulZoom  = 0;     /* Input.AdsFaithfulZoom   disable mild-iron clamp */
s32 g_pcAdsModelPose     = 1;     /* Input.AdsModelPose      sighted model pose      */
s32 g_pcAdsModernReticle = 1;     /* Input.AdsModernReticle  modern dot+ticks reticle */
s32 g_pcAdsSteadyView    = 1;     /* Input.AdsSteadyView     damp walk/strafe head-bob in ADS */
f32 g_pcAdsBobFloor      = 0.15f; /* Input.AdsBobFloor       residual ADS weapon bob (0=rigid) */
f32 g_pcAdsRecoilReduce  = 0.0f;  /* Input.AdsRecoilReduce   cosmetic recoil cut     */
f32 g_pcViewmodelSway    = 1.0f;  /* Input.ViewmodelSway     remaster default: subtle weapon sway ON */
s32 g_pcModernCrosshair  = 1;     /* Input.ModernCrosshair   remaster default: crisp hip-fire reticle ON */
s32 g_pcHitMarkers       = 1;     /* Input.HitMarkers        remaster default: on-hit marker ON */
s32 g_pcReticleTargetFeedback = 1; /* Input.ReticleTargetFeedback remaster default: tint reticle on-target ON */
s32 g_pcMinimapEnabled   = 1;     /* Input.MinimapEnabled    remaster tactical minimap ON */
s32 g_pcMinimapMode      = 0;     /* Input.MinimapMode       0 local north-up, 1 overview, 2 local rotating */
s32 g_pcMinimapObjectives = 1;    /* Input.MinimapObjectives objective layer ON once implemented */
s32 g_pcMinimapEnemyFireReveal = 1; /* Input.MinimapEnemyFireReveal red pings on audible guard fire */
s32 g_pcMinimapShowAllEnemies = 0; /* Input.MinimapShowAllEnemies debug/accessibility assist */
f32 g_pcMinimapOpacity   = 0.85f; /* Input.MinimapOpacity    overlay alpha */
f32 g_pcMinimapSize      = 1.0f;  /* Input.MinimapSize       overlay scale */
s32 g_pcMinimapSharpOverlay = 1;  /* Input.MinimapSharpOverlay native post-filter overlay */

/* D3/T8: level-intro/outro skip semantics. 0 (default) = stock staged
 * handlers only -- a NEW press of one of six buttons (A/B/Z/START/L/R)
 * advances the static shot to the swirl one stage at a time, the swirl only
 * acts in its late fade window, and the analog stick never skips anything.
 * 1 = the native any-button/any-stick instant full skip
 * (bondviewNativeIntroSkipRequested in src/game/bondview.c), preserved as an
 * opt-in for players who want the old one-press-skips-everything behavior.
 * Consumer declares this inline as `extern` at both call sites, matching the
 * g_pcSteadyView / g_deterministic convention. */
s32 g_pcIntroSkipStyle = 0;      /* Game.IntroSkipStyle     0=stock staged (default), 1=instant any-input */
s32 g_pcCheckForUpdates = 1;     /* Game.CheckForUpdates    1=check GitHub Releases at launcher start (default) */

extern int g_pcScriptedMouseDeltaX;
extern int g_pcScriptedMouseDeltaY;
#ifdef MACOS_APP_BUNDLE
extern int g_pcBridgeRightStickX;
extern int g_pcBridgeRightStickY;
#endif

static int platformEnvFlagEnabled(const char *name)
{
    const char *value = getenv(name);

    if (value == NULL || value[0] == '\0') {
        return 0;
    }

    if (value[0] == '0' && value[1] == '\0') {
        return 0;
    }

    return 1;
}

#define AUTO_MUTE_TOGGLE_MAX 16

static int g_audioMuted = -1;
static int g_autoMuteToggleParsed = 0;
static int g_autoMuteToggleCount = 0;
static int g_autoMuteToggleFrames[AUTO_MUTE_TOGGLE_MAX];
static int g_autoMuteToggleFired[AUTO_MUTE_TOGGLE_MAX];

static int platformAudioMuteTraceEnabled(void)
{
    return platformEnvFlagEnabled("GE007_AUDIO_MUTE_TRACE");
}

static void platformToggleAudioMute(const char *source)
{
    extern SDL_AudioDeviceID portAudioGetDevice(void);
    extern SDL_AudioDeviceID portAiGetDevice(void);
    extern int portAudioShouldStartMuted(void);
    SDL_AudioDeviceID audio_dev;
    SDL_AudioDeviceID ai_dev;

    if (g_audioMuted < 0) {
        g_audioMuted = portAudioShouldStartMuted();
    }

    g_audioMuted = !g_audioMuted;
    audio_dev = portAudioGetDevice();
    ai_dev = portAiGetDevice();

    /* WEB-045: soft-mute via a queued-PCM gain ramp instead of pausing the
     * device. SDL_PauseAudioDevice left up to one full queue (~167 ms) of
     * pre-mute audio buffered, which replayed as a stale burst on unmute, and
     * the hard cut clicked. Ramping the queued samples to/from silence while the
     * device keeps running fixes both; the still-draining queue stays bounded by
     * AI_QUEUE_LIMIT_FRAMES either way. audio_dev/ai_dev are kept only for the
     * trace line below (they are the same unified device). */
    {
        extern void portAudioSetMuted(int muted);
        portAudioSetMuted(g_audioMuted);
    }

    printf("[AUDIO] %s (%s)\n", g_audioMuted ? "MUTED" : "UNMUTED", source);
    if (platformAudioMuteTraceEnabled()) {
        printf("[AUDIO_MUTE_TRACE] frame=%d source=%s muted=%d audio_dev=%u ai_dev=%u unified=%d\n",
               g_frameSyncCallCount,
               source,
               g_audioMuted ? 1 : 0,
               (unsigned int)audio_dev,
               (unsigned int)ai_dev,
               (audio_dev != 0 && ai_dev == audio_dev) ? 1 : 0);
    }
}

static void platformAddAutoMuteToggleFrame(long frame)
{
    if (frame < 0 || g_autoMuteToggleCount >= AUTO_MUTE_TOGGLE_MAX) {
        return;
    }

    g_autoMuteToggleFrames[g_autoMuteToggleCount] = (int)frame;
    g_autoMuteToggleFired[g_autoMuteToggleCount] = 0;
    g_autoMuteToggleCount++;
}

static void platformParseAutoMuteToggleValue(const char *value)
{
    const char *cursor = value;

    while (cursor != NULL && *cursor != '\0' && g_autoMuteToggleCount < AUTO_MUTE_TOGGLE_MAX) {
        char *end = NULL;
        long frame;

        while (*cursor == ' ' || *cursor == '\t' || *cursor == ',' || *cursor == ';') {
            cursor++;
        }
        if (*cursor == '\0') {
            break;
        }

        frame = strtol(cursor, &end, 10);
        if (end == cursor) {
            break;
        }

        platformAddAutoMuteToggleFrame(frame);
        cursor = end;
    }
}

static void platformParseAutoMuteToggles(void)
{
    g_autoMuteToggleParsed = 1;
    platformParseAutoMuteToggleValue(getenv("GE007_AUTO_MUTE_TOGGLE_FRAME"));
    platformParseAutoMuteToggleValue(getenv("GE007_AUTO_MUTE_TOGGLE_SCRIPT"));
}

static void platformApplyAutoMuteToggles(void)
{
    int i;

    if (!g_autoMuteToggleParsed) {
        platformParseAutoMuteToggles();
    }

    for (i = 0; i < g_autoMuteToggleCount; i++) {
        if (!g_autoMuteToggleFired[i] && g_frameSyncCallCount == g_autoMuteToggleFrames[i]) {
            g_autoMuteToggleFired[i] = 1;
            platformToggleAudioMute("auto mute toggle");
        }
    }
}

static Uint32 platformFullscreenFlagForWindowMode(s32 mode)
{
    switch (mode) {
        case PLATFORM_WINDOW_MODE_BORDERLESS:
            return SDL_WINDOW_FULLSCREEN_DESKTOP;
        case PLATFORM_WINDOW_MODE_EXCLUSIVE:
            return SDL_WINDOW_FULLSCREEN;
        case PLATFORM_WINDOW_MODE_WINDOWED:
        default:
            return 0;
    }
}

static void platformSyncWindowSizeForRenderer(void)
{
    int w;
    int h;

    if (!g_sdlWindow) {
        return;
    }

    SDL_GetWindowSize(g_sdlWindow, &w, &h);
    gfx_set_window_size(w, h);
}

static int platformConfiguredDisplayIndex(void)
{
    int display_count = SDL_GetNumVideoDisplays();

    if (display_count <= 0) {
        return 0;
    }
    if (g_cfgDisplayIndex < 0 || g_cfgDisplayIndex >= display_count) {
        fprintf(stderr,
                "[SDL] Display index %d is not available; using display 0 of %d.\n",
                g_cfgDisplayIndex, display_count);
        return 0;
    }

    return (int)g_cfgDisplayIndex;
}

static int platformGetDisplayBounds(int display_index, SDL_Rect *bounds)
{
    if (!bounds) {
        return 0;
    }
    if (SDL_GetDisplayUsableBounds(display_index, bounds) == 0) {
        return 1;
    }
    return SDL_GetDisplayBounds(display_index, bounds) == 0;
}

static int platformClampIntToRange(int value, int min_value, int max_value)
{
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

static void platformGetConfiguredWindowPosition(int *x_out, int *y_out)
{
    int display_index = platformConfiguredDisplayIndex();

    if (g_cfgWindowX < 0 || g_cfgWindowY < 0) {
        int centered_pos = SDL_WINDOWPOS_CENTERED_DISPLAY(display_index);
        if (x_out) {
            *x_out = centered_pos;
        }
        if (y_out) {
            *y_out = centered_pos;
        }
        return;
    }

    SDL_Rect bounds;
    if (platformGetDisplayBounds(display_index, &bounds)) {
        int max_rel_x = bounds.w > g_cfgWindowW ? bounds.w - g_cfgWindowW : 0;
        int max_rel_y = bounds.h > g_cfgWindowH ? bounds.h - g_cfgWindowH : 0;
        int rel_x = platformClampIntToRange(g_cfgWindowX, 0, max_rel_x);
        int rel_y = platformClampIntToRange(g_cfgWindowY, 0, max_rel_y);

        if (x_out) {
            *x_out = bounds.x + rel_x;
        }
        if (y_out) {
            *y_out = bounds.y + rel_y;
        }
        return;
    }

    if (x_out) {
        *x_out = g_cfgWindowX;
    }
    if (y_out) {
        *y_out = g_cfgWindowY;
    }
}

static void platformRememberWindowGeometry(void)
{
    int x;
    int y;
    int w;
    int h;
    int display_index;
    SDL_Rect bounds;

    if (!g_sdlWindow || g_windowMode != PLATFORM_WINDOW_MODE_WINDOWED) {
        return;
    }

    SDL_GetWindowPosition(g_sdlWindow, &x, &y);
    SDL_GetWindowSize(g_sdlWindow, &w, &h);
    display_index = SDL_GetWindowDisplayIndex(g_sdlWindow);
    if (display_index < 0) {
        display_index = platformConfiguredDisplayIndex();
    }

    g_cfgWindowW = w;
    g_cfgWindowH = h;
    g_cfgDisplayIndex = display_index;

    if (platformGetDisplayBounds(display_index, &bounds)) {
        g_cfgWindowX = x - bounds.x;
        g_cfgWindowY = y - bounds.y;
    } else {
        g_cfgWindowX = x;
        g_cfgWindowY = y;
    }
}

static void platformMoveWindowToConfiguredDisplay(void)
{
    int x;
    int y;

    if (!g_sdlWindow) {
        return;
    }

    platformGetConfiguredWindowPosition(&x, &y);
    SDL_SetWindowPosition(g_sdlWindow, x, y);
}

static int platformFindConfiguredFullscreenMode(SDL_DisplayMode *out_mode)
{
    int display_index;
    int mode_count;

    if (out_mode == NULL || g_cfgFullscreenW <= 0 || g_cfgFullscreenH <= 0) {
        return 0;
    }

    display_index = platformConfiguredDisplayIndex();
    mode_count = SDL_GetNumDisplayModes(display_index);
    if (mode_count <= 0) {
        return 0;
    }

    for (int i = 0; i < mode_count; i++) {
        SDL_DisplayMode mode = {0};

        if (SDL_GetDisplayMode(display_index, i, &mode) != 0) {
            continue;
        }
        if (mode.w != g_cfgFullscreenW || mode.h != g_cfgFullscreenH) {
            continue;
        }
        if (g_cfgFullscreenRefresh > 0 &&
            mode.refresh_rate != g_cfgFullscreenRefresh) {
            continue;
        }

        *out_mode = mode;
        return 1;
    }

    fprintf(stderr,
            "[SDL] Fullscreen mode %dx%d@%dHz is not available on display %d; using SDL default.\n",
            g_cfgFullscreenW,
            g_cfgFullscreenH,
            g_cfgFullscreenRefresh,
            display_index);
    return 0;
}

/* Selects the SDL display mode used by exclusive fullscreen (SDL_WINDOW_FULLSCREEN).
 * Returns 0 on success, -1 if an exclusive mode-set was requested but could not be
 * satisfied (the caller then falls back to borderless desktop fullscreen). For
 * non-exclusive modes the display mode is reset to NULL and 0 is returned. */
static int platformApplyFullscreenDisplayMode(void)
{
    SDL_DisplayMode mode;
    int display_index;

    if (!g_sdlWindow) {
        return 0;
    }

    if (g_windowMode != PLATFORM_WINDOW_MODE_EXCLUSIVE) {
        /* Windowed / borderless-desktop: SDL manages the mode (desktop resolution). */
        SDL_SetWindowDisplayMode(g_sdlWindow, NULL);
        return 0;
    }

    if (!platformFindConfiguredFullscreenMode(&mode)) {
        /* RX.3 Fix C: no explicit Video.FullscreenWidth/Height configured (or the
         * requested mode is unavailable). Passing NULL to SDL_SetWindowDisplayMode
         * makes exclusive fullscreen adopt the small *window* size instead of the
         * display's native resolution. Default to the chosen display's desktop mode
         * so exclusive comes up at native res; explicit width/height/refresh
         * overrides still take precedence via platformFindConfiguredFullscreenMode. */
        display_index = platformConfiguredDisplayIndex();
        if (SDL_GetDesktopDisplayMode(display_index, &mode) != 0) {
            fprintf(stderr,
                    "[SDL] Could not query desktop display mode for display %d: %s\n",
                    display_index,
                    SDL_GetError());
            return -1;
        }
    }

    if (SDL_SetWindowDisplayMode(g_sdlWindow, &mode) < 0) {
        fprintf(stderr,
                "[SDL] Failed to set fullscreen mode %dx%d@%dHz: %s\n",
                mode.w,
                mode.h,
                mode.refresh_rate,
                SDL_GetError());
        return -1;
    }

    return 0;
}

static void platformApplyWindowMode(void)
{
    Uint32 fullscreen_flag;

    if (!g_sdlWindow) {
        return;
    }

    /* Remember the last non-windowed mode applied so the Alt+Enter toggle can
     * return to it (RX.3 Fix B). Seeds from Video.WindowMode at first apply. */
    if (g_windowMode != PLATFORM_WINDOW_MODE_WINDOWED) {
        g_preferredFullscreenMode = g_windowMode;
    }

    fullscreen_flag = platformFullscreenFlagForWindowMode(g_windowMode);
    platformMoveWindowToConfiguredDisplay();

    if (g_windowMode == PLATFORM_WINDOW_MODE_EXCLUSIVE) {
        /* RX.3 Fix C robustness: true exclusive fullscreen is best-effort. The
         * mode-set and/or SDL_WINDOW_FULLSCREEN can fail (Wayland can't
         * arbitrary-mode-set, the mode may be unavailable, or the driver refuses).
         * On any failure fall back to borderless desktop fullscreen rather than
         * leaving the window in a broken/black exclusive state. */
        if (platformApplyFullscreenDisplayMode() != 0 ||
            SDL_SetWindowFullscreen(g_sdlWindow, SDL_WINDOW_FULLSCREEN) < 0) {
            fprintf(stderr,
                    "[SDL] Exclusive fullscreen unavailable (%s); falling back to "
                    "borderless desktop fullscreen.\n",
                    SDL_GetError());
            if (SDL_SetWindowFullscreen(g_sdlWindow,
                                        SDL_WINDOW_FULLSCREEN_DESKTOP) < 0) {
                fprintf(stderr, "[SDL] Borderless fallback also failed: %s\n",
                        SDL_GetError());
            }
        }
    } else {
        /* Windowed (flag 0) or borderless desktop fullscreen — the safe
         * cross-platform default. */
        platformApplyFullscreenDisplayMode();
        if (SDL_SetWindowFullscreen(g_sdlWindow, fullscreen_flag) < 0) {
            fprintf(stderr, "[SDL] Failed to apply window mode %d: %s\n",
                    g_windowMode, SDL_GetError());
            return;
        }
    }

    platformSyncWindowSizeForRenderer();
}

static void platformApplyVSync(void)
{
    /* Metal: SDL_GL_SetSwapInterval is a GL-only no-op; the layer equivalent is
     * CAMetalLayer.displaySyncEnabled. Applied via a backend hook that stores
     * the value if the layer doesn't exist yet (init ordering). Adaptive maps
     * to plain sync — CAMetalLayer has no adaptive mode. */
#ifdef __APPLE__
    if (gfx_backend_use_metal()) {
        extern void gfx_metal_set_vsync(int enabled);
        gfx_metal_set_vsync(!g_forceNoVsync && g_vsyncMode != PLATFORM_VSYNC_OFF);
    }
#endif
    if (g_forceNoVsync) {
        SDL_GL_SetSwapInterval(0);
        return;
    }

    switch (g_vsyncMode) {
        case PLATFORM_VSYNC_OFF:
            SDL_GL_SetSwapInterval(0);
            break;
        case PLATFORM_VSYNC_ON:
            SDL_GL_SetSwapInterval(1);
            break;
        case PLATFORM_VSYNC_ADAPTIVE:
        default:
            if (SDL_GL_SetSwapInterval(-1) < 0) {
                SDL_GL_SetSwapInterval(1);
            }
            break;
    }
}

static double platformFrameCapPeriodMs(void)
{
    switch (g_frameCapMode) {
        case PLATFORM_FRAME_CAP_30:
            return 1000.0 / 30.0;
        case PLATFORM_FRAME_CAP_DISPLAY:
            /* The sim is a fixed 60 Hz integer-tick model with no render interpolation, and
             * waitForNextFrame()'s round-to-nearest tick counter (N64-faithful: the +387937
             * bias is half a frame, so nextFrameTime = round(elapsed/frame)) treats any
             * frame >= ~1/120 s as one full tick — a floor the N64 RSP/RDP guaranteed in
             * hardware. With the pacer disabled here (the old `return 0.0` when vsync was
             * on), a >60 Hz panel (75/90/120 Hz monitors, 90 Hz Steam Deck OLED) let the
             * loop run every ~1/120 s and advanced the sim 1.25x-2x too fast — movement,
             * enemies, timers and audio all sped up. Pace to the 1/60 s floor so the sim can
             * never outrun 60 Hz; vsync (if on) still aligns the present to a vblank. Until
             * render interpolation lands, "display" behaves like "60" — correct speed beats
             * frame-duplicated over-speed. */
            return 1000.0 / 60.0;
        case PLATFORM_FRAME_CAP_60:
        default:
            return 1000.0 / 60.0;
    }
}

/* Register platform settings with the config system.
 * Called from main_pc.c before configInit(). */
void platformRegisterConfig(void)
{
    settingsRegisterInt("Video.WindowWidth", &g_cfgWindowW, 1440, 320, 3840,
                        SETTING_SCOPE_RESTART, "GE007_WINDOW_WIDTH",
                        "--config-override Video.WindowWidth=VALUE",
                        "Window width",
                        "Initial SDL window width in pixels.");
    settingsRegisterInt("Video.WindowHeight", &g_cfgWindowH, 810, 240, 2160,
                        SETTING_SCOPE_RESTART, "GE007_WINDOW_HEIGHT",
                        "--config-override Video.WindowHeight=VALUE",
                        "Window height",
                        "Initial SDL window height in pixels.");
    settingsRegisterInt("Video.WindowX", &g_cfgWindowX, -1, -1, 32767,
                        SETTING_SCOPE_RESTART, "GE007_WINDOW_X",
                        "--config-override Video.WindowX=VALUE",
                        "Window X",
                        "Window X position relative to the selected display; -1 centers it.");
    settingsRegisterInt("Video.WindowY", &g_cfgWindowY, -1, -1, 32767,
                        SETTING_SCOPE_RESTART, "GE007_WINDOW_Y",
                        "--config-override Video.WindowY=VALUE",
                        "Window Y",
                        "Window Y position relative to the selected display; -1 centers it.");
    settingsRegisterInt("Video.Display", &g_cfgDisplayIndex, 0, 0, 31,
                        SETTING_SCOPE_RESTART, "GE007_DISPLAY",
                        "--config-override Video.Display=VALUE",
                        "Display",
                        "Zero-based SDL display index for window and fullscreen placement.");
    settingsRegisterInt("Video.HiDPI", &g_cfgHiDpi, 0, 0, 1,
                        SETTING_SCOPE_RESTART, "GE007_HIDPI",
                        "--config-override Video.HiDPI=0|1",
                        "HiDPI",
                        "Render at your display's full Retina/high-DPI resolution (sharper, more GPU cost). Off renders at the window size for steadier performance.");
    settingsRegisterInt("Video.FullscreenWidth", &g_cfgFullscreenW, 0, 0, 7680,
                        SETTING_SCOPE_RESTART, "GE007_FULLSCREEN_WIDTH",
                        "--config-override Video.FullscreenWidth=VALUE",
                        "Fullscreen width",
                        "Exclusive fullscreen mode width; 0 uses SDL/default.");
    settingsRegisterInt("Video.FullscreenHeight", &g_cfgFullscreenH, 0, 0, 4320,
                        SETTING_SCOPE_RESTART, "GE007_FULLSCREEN_HEIGHT",
                        "--config-override Video.FullscreenHeight=VALUE",
                        "Fullscreen height",
                        "Exclusive fullscreen mode height; 0 uses SDL/default.");
    settingsRegisterInt("Video.FullscreenRefresh", &g_cfgFullscreenRefresh, 0, 0, 1000,
                        SETTING_SCOPE_RESTART, "GE007_FULLSCREEN_REFRESH",
                        "--config-override Video.FullscreenRefresh=VALUE",
                        "Fullscreen refresh",
                        "Exclusive fullscreen refresh rate in Hz; 0 accepts any/default.");
    settingsRegisterEnum("Video.WindowMode", &g_windowMode, PLATFORM_WINDOW_MODE_WINDOWED,
                         k_windowModeOptions,
                         (s32)(sizeof(k_windowModeOptions) / sizeof(k_windowModeOptions[0])),
                         SETTING_SCOPE_LIVE, "GE007_WINDOW_MODE",
                         "--config-override Video.WindowMode=VALUE",
                         "Window mode",
                         "SDL display mode: windowed, borderless, or exclusive.");
    settingsRegisterEnum("Video.VSync", &g_vsyncMode, PLATFORM_VSYNC_ADAPTIVE,
                         k_vsyncOptions,
                         (s32)(sizeof(k_vsyncOptions) / sizeof(k_vsyncOptions[0])),
                         SETTING_SCOPE_LIVE, "GE007_VSYNC",
                         "--config-override Video.VSync=VALUE",
                         "VSync",
                         "Swap interval: off, on, or adaptive.");
    settingsRegisterEnum("Video.FrameCap", &g_frameCapMode, PLATFORM_FRAME_CAP_60,
                         k_frameCapOptions,
                         (s32)(sizeof(k_frameCapOptions) / sizeof(k_frameCapOptions[0])),
                         SETTING_SCOPE_LIVE, "GE007_FRAME_CAP",
                         "--config-override Video.FrameCap=VALUE",
                         "Frame cap",
                         "Frame pacing cap: 30 or 60. 'display' is currently a 60 Hz compatibility "
                         "alias (the sim runs at a fixed 60 Hz; true high-refresh presentation needs "
                         "the render-interpolation path, not yet enabled), so it behaves like 60.");
    settingsRegisterInt("Video.FpsOverlay", &g_pcFpsOverlay, 1, 0, 1,
                        SETTING_SCOPE_LIVE, "GE007_FPS_OVERLAY",
                        "--config-override Video.FpsOverlay=VALUE",
                        "FPS overlay",
                        "Small top-right overlay showing the current FPS, frame time (ms), and 1%-low "
                        "FPS. 0 = off. (Automatically hidden during benchmark and screenshot runs.)");
    settingsRegisterFloat("Video.Gamma", &g_pcVideoGamma, 1.0f, 0.5f, 2.5f,
                          SETTING_SCOPE_LIVE, "GE007_GAMMA",
                          "--config-override Video.Gamma=VALUE",
                          "Gamma",
                          "Output gamma correction. 1.0 leaves colors unchanged.");
    settingsRegisterFloat("Video.Saturation", &g_pcVideoSaturation, 1.15f, 0.0f, 2.0f,
                          SETTING_SCOPE_LIVE, "GE007_SATURATION",
                          "--config-override Video.Saturation=VALUE",
                          "Saturation",
                          "Output color saturation. 1.0 leaves colors unchanged.");
    settingsRegisterFloat("Video.Contrast", &g_pcVideoContrast, 1.08f, 0.5f, 2.0f,
                          SETTING_SCOPE_LIVE, "GE007_CONTRAST",
                          "--config-override Video.Contrast=VALUE",
                          "Contrast",
                          "Output color contrast. 1.0 leaves colors unchanged.");
    settingsRegisterFloat("Video.Brightness", &g_pcVideoBrightness, 0.04f, -0.5f, 0.5f,
                          SETTING_SCOPE_LIVE, "GE007_BRIGHTNESS",
                          "--config-override Video.Brightness=VALUE",
                          "Brightness",
                          "Output brightness offset. 0.0 leaves colors unchanged.");
    settingsRegisterInt("Video.OutputDither", &g_pcOutputDither, 1, 0, 1,
                        SETTING_SCOPE_LIVE, "GE007_OUTPUT_DITHER",
                        "--config-override Video.OutputDither=VALUE",
                        "Output dither",
                        "Adds a subtle dither pattern that smooths out color banding in skies and fades. 0 = off.");
    settingsRegisterFloat("Video.Vignette", &g_pcVignette, 0.15f, 0.0f, 1.0f,
                          SETTING_SCOPE_LIVE, "GE007_VIGNETTE",
                          "--config-override Video.Vignette=VALUE",
                          "Vignette",
                          "Soft darkening toward screen edges. 0.0 = off.");
    settingsRegisterInt("Video.Bloom", &g_pcBloom, 1, 0, 1,
                        SETTING_SCOPE_LIVE, "GE007_BLOOM",
                        "--config-override Video.Bloom=VALUE",
                        "Bloom",
                        "Soft glow that blooms around bright lights and highlights. 0 = off.");
    settingsRegisterFloat("Video.BloomThreshold", &g_pcBloomThreshold, 0.8f, 0.0f, 1.0f,
                          SETTING_SCOPE_LIVE, "GE007_BLOOM_THRESHOLD",
                          "--config-override Video.BloomThreshold=VALUE",
                          "Bloom threshold",
                          "Luma above which pixels contribute to bloom.");
    settingsRegisterFloat("Video.BloomIntensity", &g_pcBloomIntensity, 0.5f, 0.0f, 2.0f,
                          SETTING_SCOPE_LIVE, "GE007_BLOOM_INTENSITY",
                          "--config-override Video.BloomIntensity=VALUE",
                          "Bloom intensity",
                          "Strength of the bloom halo added to the image.");
    settingsRegisterInt("Video.Ssao", &g_pcSsao, 0, 0, 1,
                        SETTING_SCOPE_LIVE, "GE007_SSAO",
                        "--config-override Video.Ssao=VALUE",
                        "Ambient occlusion",
                        "Adds soft contact shadows in corners and where objects meet the "
                        "ground, for a greater sense of depth. 0 = off.");
    settingsRegisterEnum("Video.SsaoMode", &g_pcSsaoMode, 1,
                         k_ssaoModeOptions,
                         (s32)(sizeof(k_ssaoModeOptions) / sizeof(k_ssaoModeOptions[0])),
                         SETTING_SCOPE_LIVE, "GE007_SSAO_MODE",
                         "--config-override Video.SsaoMode=planar|hemisphere",
                         "SSAO mode",
                         "AO reconstruction: planar (depth-only v1, all backends) or "
                         "hemisphere (view-space v2, Metal-only; GL falls back to planar).");
    settingsRegisterFloat("Video.SsaoRadius", &g_pcSsaoRadius, 0.5f, 0.05f, 2.0f,
                          SETTING_SCOPE_LIVE, "GE007_SSAO_RADIUS",
                          "--config-override Video.SsaoRadius=VALUE",
                          "SSAO radius",
                          "AO sample radius as a fraction of screen height.");
    settingsRegisterFloat("Video.SsaoIntensity", &g_pcSsaoIntensity, 1.0f, 0.0f, 2.0f,
                          SETTING_SCOPE_LIVE, "GE007_SSAO_INTENSITY",
                          "--config-override Video.SsaoIntensity=VALUE",
                          "SSAO intensity",
                          "Strength of the ambient-occlusion darkening.");
    settingsRegisterFloat("Video.SsaoBias", &g_pcSsaoBias, 0.15f, 0.0f, 0.5f,
                          SETTING_SCOPE_LIVE, "GE007_SSAO_BIAS",
                          "--config-override Video.SsaoBias=VALUE",
                          "SSAO bias",
                          "v2 tangent-plane angle threshold; rejects grazing self-occlusion.");
    settingsRegisterFloat("Video.SsaoPower", &g_pcSsaoPower, 3.0f, 0.5f, 4.0f,
                          SETTING_SCOPE_LIVE, "GE007_SSAO_POWER",
                          "--config-override Video.SsaoPower=VALUE",
                          "SSAO power",
                          "AO contrast exponent (higher = punchier creases).");
    settingsRegisterFloat("Video.SsaoFarCutoff", &g_pcSsaoFarCutoff, 800.0f, 8.0f, 4096.0f,
                          SETTING_SCOPE_LIVE, "GE007_SSAO_FAR_CUTOFF",
                          "--config-override Video.SsaoFarCutoff=VALUE",
                          "SSAO far cutoff",
                          "World-space distance beyond which AO fades to 0 (noisy-depth zone).");
    settingsRegisterFloat("Video.SsaoNearCut", &g_pcSsaoNearCut, 0.02f, 0.0f, 0.5f,
                          SETTING_SCOPE_LIVE, "GE007_SSAO_NEAR_CUT",
                          "--config-override Video.SsaoNearCut=VALUE",
                          "SSAO near cut",
                          "Window-depth at/below which pixels (viewmodel) get no AO.");
    settingsRegisterFloat("Video.SsaoSkyCut", &g_pcSsaoSkyCut, 0.9999f, 0.9f, 1.0f,
                          SETTING_SCOPE_LIVE, "GE007_SSAO_SKY_CUT",
                          "--config-override Video.SsaoSkyCut=VALUE",
                          "SSAO sky cut",
                          "Window-depth at/above which pixels (sky) get no AO.");
    settingsRegisterInt("Video.SsaoHalfRes", &g_pcSsaoHalfRes, 0, 0, 1,
                        SETTING_SCOPE_LIVE, "GE007_SSAO_HALF_RES",
                        "--config-override Video.SsaoHalfRes=VALUE",
                        "SSAO half-res",
                        "Render AO at half the internal scene resolution (cheaper).");
    settingsRegisterInt("Video.SsaoBlur", &g_pcSsaoBlur, 0, 0, 1,
                        SETTING_SCOPE_LIVE, "GE007_SSAO_BLUR",
                        "--config-override Video.SsaoBlur=VALUE",
                        "SSAO blur",
                        "Separable depth-aware (bilateral) blur of the AO buffer.");
    settingsRegisterFloat("Video.SsaoBlurDepthSharp", &g_pcSsaoBlurDepthSharp, 8.0f, 0.5f, 64.0f,
                          SETTING_SCOPE_LIVE, "GE007_SSAO_BLUR_DEPTH_SHARP",
                          "--config-override Video.SsaoBlurDepthSharp=VALUE",
                          "SSAO blur depth sharpness",
                          "Bilateral blur depth-weight sharpness (higher = edge-preserving).");
    settingsRegisterInt("Video.EnvSmoothNormals", &g_pcEnvSmoothNormals, 0, 0, 1,
                        SETTING_SCOPE_LIVE, "GE007_ENV_SMOOTH_NORMALS",
                        "--config-override Video.EnvSmoothNormals=VALUE",
                        "Smooth env normals",
                        "Relight static room surfaces with CPU-averaged, position-merged smooth "
                        "normals against the level sun to erase per-quad baked-lighting seams. 0 = off (identity).");
    settingsRegisterFloat("Video.EnvRelightBlend", &g_pcEnvRelightBlend, 0.6f, 0.0f, 1.0f,
                          SETTING_SCOPE_LIVE, "GE007_ENV_RELIGHT_BLEND",
                          "--config-override Video.EnvRelightBlend=VALUE",
                          "Env relight blend",
                          "Strength of the smooth-normal relight: 0 keeps the baked luma, 1 fully "
                          "replaces it with the recomputed Lambert (seam fix vs authored mood dial).");
    settingsRegisterInt("Video.FontUpscale", &g_pcFontUpscale, 3, 1, 8,
                        SETTING_SCOPE_RESTART, "GE007_FONT_UPSCALE",
                        "--config-override Video.FontUpscale=VALUE",
                        "Font upscale",
                        "Supersample factor for HUD and menu text glyphs (1-8). Higher is sharper "
                        "at more GPU cost; 3 is the faithful default. Takes effect on restart.");
    settingsRegisterInt("Video.SunShadow", &g_pcSunShadow, 0, 0, 1,
                        SETTING_SCOPE_LIVE, "GE007_SUN_SHADOW",
                        "--config-override Video.SunShadow=VALUE",
                        "Sun shadows",
                        "Real-time shadows cast by the level's sunlight onto rooms and characters, "
                        "replacing the original blurry blob shadow. 0 = off.");
    settingsRegisterInt("Video.SunShadowRes", &g_pcSunShadowRes, 2048, 1024, 4096,
                        SETTING_SCOPE_LIVE, "GE007_SUN_SHADOW_RES",
                        "--config-override Video.SunShadowRes=VALUE",
                        "Sun shadow resolution",
                        "Depth-map resolution for the sun shadow pass (1024/2048/4096). Higher = sharper, more cost.");
    settingsRegisterFloat("Video.SunShadowRadius", &g_pcSunShadowRadius, 500.0f, 32.0f, 4000.0f,
                          SETTING_SCOPE_LIVE, "GE007_SUN_SHADOW_RADIUS",
                          "--config-override Video.SunShadowRadius=VALUE",
                          "Sun shadow radius",
                          "Camera-centered ortho half-extent (GAME-LOGIC units, scaled to world by the "
                          "level's room_data_float2) for the sun shadow map. Larger covers more, softer detail.");
    settingsRegisterFloat("Video.SunShadowBias", &g_pcSunShadowBias, 0.0015f, 0.0f, 0.02f,
                          SETTING_SCOPE_LIVE, "GE007_SUN_SHADOW_BIAS",
                          "--config-override Video.SunShadowBias=VALUE",
                          "Sun shadow bias",
                          "Depth-compare bias for the sun shadow receiver. Higher hides acne but risks "
                          "peter-panning (detached shadows); default 0.0015.");
    settingsRegisterFloat("Video.SunShadowUmbra", &g_pcSunShadowUmbra, 0.55f, 0.0f, 1.0f,
                          SETTING_SCOPE_LIVE, "GE007_SUN_SHADOW_UMBRA",
                          "--config-override Video.SunShadowUmbra=VALUE",
                          "Sun shadow darkness",
                          "How dark shadowed surfaces get (multiplier): 1.0 = no darkening, 0 = black. Default 0.55.");
    settingsRegisterInt("Video.PerPixelLight", &g_pcPerPixelLight, 0, 0, 1,
                        SETTING_SCOPE_LIVE, "GE007_PERPIXEL_LIGHT",
                        "--config-override Video.PerPixelLight=VALUE",
                        "Per-pixel directional light",
                        "Per-pixel geometric-normal (dFdx) directional sun on room surfaces: "
                        "recomputes Lambert per fragment from the world-position derivative and "
                        "luma-replaces the baked directional shading (reuses Video.EnvRelightBlend "
                        "as the strength dial). Supersedes Video.EnvSmoothNormals when on. 0 = off (identity).");
    settingsRegisterInt("Video.SceneDecor", &g_pcSceneDecor, 0, 0, 1,
                        SETTING_SCOPE_LIVE, "GE007_SCENE_DECOR",
                        "--config-override Video.SceneDecor=VALUE",
                        "3D scene decoration",
                        "Adds extra 3D scenery -- like real trees and props -- layered on top of "
                        "the original levels for a richer look. Purely cosmetic; never affects "
                        "gameplay. 0 = off.");
    settingsRegisterString("Video.SceneDecorDir", g_pcSceneDecorDir, sizeof(g_pcSceneDecorDir),
                           "assets/decor",
                           SETTING_SCOPE_LIVE, "GE007_SCENE_DECOR_DIR",
                           "--config-override Video.SceneDecorDir=PATH",
                           "Scene decoration folder",
                           "Folder the 3D scene decoration is loaded from. The default is fine "
                           "unless you are authoring your own scenery.");
    /* Authoring-only path knob -- players never set it; hide behind Advanced.
     * The 3D-scene-decoration on/off toggle (Video.SceneDecor) stays player-facing. */
    settingsMarkAdvanced("Video.SceneDecorDir");
    settingsRegisterInt("Video.Fxaa", &g_pcFxaa, 1, 0, 1,
                        SETTING_SCOPE_LIVE, "GE007_FXAA",
                        "--config-override Video.Fxaa=VALUE",
                        "FXAA",
                        "Smooths jagged edges across the whole image, including sprites and the HUD. 0 = off.");
    settingsRegisterInt("Video.Smaa", &g_pcSmaa, 0, 0, 1,
                        SETTING_SCOPE_LIVE, "GE007_SMAA",
                        "--config-override Video.Smaa=VALUE",
                        "SMAA",
                        /* Single static help string (not a runtime ternary): the
                         * ENV_FLAGS generator extracts the literal, and a
                         * conditional garbled the generated row. Accurate on both
                         * backends; the backend gate below hides it on OpenGL. */
                        "Sharper edge anti-aliasing than FXAA; replaces FXAA when on. 0 = off. "
                        "Metal renderer only -- no effect on the OpenGL build.");
    /* Gate by backend: SMAA runs only on the Metal renderer. On the default
     * OpenGL build it is inert, so it must not sit in the player-facing Video
     * tab as a dead toggle -- hide it behind the "Advanced (expert)" disclosure
     * there. On a Metal build it is a real option and stays player-facing.
     * Env/CLI/ini overrides still apply either way (hidden != removed). */
    if (!gfx_backend_use_metal()) {
        settingsMarkAdvanced("Video.Smaa");
    }
    settingsRegisterFloat("Video.Sharpen", &g_pcSharpen, 0.15f, 0.0f, 1.0f,
                          SETTING_SCOPE_LIVE, "GE007_SHARPEN",
                          "--config-override Video.Sharpen=VALUE",
                          "Sharpen",
                          "Contrast-adaptive output sharpening. 0.0 = off; ~0.3 mild; higher risks ringing.");
    settingsRegisterInt("Video.GradePresets", &g_pcGradePresets, 1, 0, 1,
                        SETTING_SCOPE_LIVE, "GE007_GRADE_PRESETS",
                        "--config-override Video.GradePresets=VALUE",
                        "Per-level grade",
                        "Subtle per-level color grading for mood, layered on top of the global color settings. 0 = off.");
    settingsRegisterInt("Video.Tonemap", &g_pcTonemap, 1, 0, 1,
                        SETTING_SCOPE_LIVE, "GE007_TONEMAP",
                        "--config-override Video.Tonemap=VALUE",
                        "Filmic tonemap",
                        "Softens bright highlights for a more cinematic image. 0 = off (raw output).");
    settingsRegisterInt("Video.RemasterFX", &g_pcRemasterFX, 1, 0, 1,
                        SETTING_SCOPE_LIVE, "GE007_REMASTER_FX",
                        "--config-override Video.RemasterFX=VALUE",
                        "Remaster image effects (master)",
                        "Master switch for the cinematic image effects (color grade, tonemap, bloom, "
                        "vignette, sharpening, edge smoothing). 0 = the faithful original N64 look. "
                        "HD textures and render resolution have their own settings.");
    settingsRegisterFloat("Video.RenderScale", &g_pcRenderScale, 2.0f, 1.0f, 4.0f,
                          SETTING_SCOPE_RESTART, "GE007_RENDER_SCALE",
                          "--config-override Video.RenderScale=VALUE",
                          "Render scale",
                          "Internal rendering resolution. 1.0 matches the window; higher values render "
                          "the scene sharper (a strong form of anti-aliasing) at more GPU cost. "
                          "Automatically capped to what your GPU supports.");
    settingsRegisterEnum("Video.MSAA", &g_pcMsaaSamples, 0,
                         k_msaaOptions,
                         (s32)(sizeof(k_msaaOptions) / sizeof(k_msaaOptions[0])),
                         SETTING_SCOPE_LIVE, "GE007_MSAA",
                         "--config-override Video.MSAA=VALUE",
                         "MSAA",
                         "Scene multisample anti-aliasing samples: 0, 2, 4, or 8.");
    settingsRegisterInt("Video.AnisotropicFiltering", &g_pcTextureAnisotropy, 16, 1, 16,
                        SETTING_SCOPE_RESTART, "GE007_ANISO",
                        "--config-override Video.AnisotropicFiltering=VALUE",
                        "Anisotropic filtering",
                        "Texture anisotropy for grazing-angle surfaces (1-16). 1 = faithful N64 "
                        "filtering; higher values sharpen oblique walls/floors (e.g. Dam canyon "
                        "edges) at slight GPU cost. Remaster enhancement; takes effect on restart.");
    settingsRegisterFloat("Video.FovY", &g_pcFovY, 50.0f, 45.0f, 105.0f,
                          SETTING_SCOPE_LIVE, "GE007_FOV_Y",
                          "--config-override Video.FovY=VALUE",
                          "Vertical FOV",
                          "Gameplay vertical field of view in degrees (45-105). 60 is the original feel; higher widens peripheral view.");
    settingsRegisterFloat("Video.ViewmodelFov", &g_pcViewmodelFov, 50.0f, 0.0f, 90.0f,
                          SETTING_SCOPE_LIVE, "GE007_VIEWMODEL_FOV",
                          "--config-override Video.ViewmodelFov=VALUE",
                          "Viewmodel FOV",
                          "Vertical FOV used to project the first-person weapon. Fixed reference so the gun does not warp at wide world FOV. 0 follows world FOV (vanilla).");
    settingsRegisterFloat("Video.CutsceneFovY", &g_pcCutsceneFovY, 60.0f, 0.0f, 105.0f,
                          SETTING_SCOPE_LIVE, "GE007_CUTSCENE_FOV_Y",
                          "--config-override Video.CutsceneFovY=VALUE",
                          "Cutscene vertical FOV",
                          "Vertical FOV for authored intro/outro/death cinematics. 60 matches the N64's original cinematic framing regardless of the gameplay Video.FovY. 0 = follow Video.FovY.");
    settingsRegisterFloat("Video.FogDensity", &g_pcFogDensity, 1.0f, 0.25f, 4.0f,
                          SETTING_SCOPE_LIVE, "GE007_FOG_DENSITY",
                          "--config-override Video.FogDensity=VALUE",
                          "Fog density",
                          "Cosmetic haze thickness multiplier. 1.0 leaves fog unchanged; AI sight range is unaffected.");
    settingsRegisterInt("Video.SmoothSky", &g_pcSmoothSky, 0, 0, 1,
                        SETTING_SCOPE_LIVE, "GE007_SMOOTH_SKY",
                        "--config-override Video.SmoothSky=VALUE",
                        "Smooth sky",
                        "Skip the N64's 5-bit-per-channel sky-colour quantization so the interpolated sky "
                        "renders as a smooth gradient (output dither hides any residual banding). "
                        "0 = the original banded N64 sky (byte-identical); 1 = smooth.");
    settingsRegisterString("Video.TexturePack", g_pcTexturePack, sizeof(g_pcTexturePack), "",
                           SETTING_SCOPE_RESTART, "GE007_TEXTURE_PACK",
                           "--config-override Video.TexturePack=PATH",
                           "HD texture pack",
                           "Folder containing an HD texture pack you built from your own ROM. Empty = off (original N64 textures).");
    settingsRegisterEnum("Video.RetroFilter", &g_pcRetroFilterMode, PLATFORM_RETRO_FILTER_AUTO,
                         k_retroFilterOptions,
                         (s32)(sizeof(k_retroFilterOptions) / sizeof(k_retroFilterOptions[0])),
                         SETTING_SCOPE_LIVE, "GE007_RETRO_FILTER",
                         "--config-override Video.RetroFilter=VALUE",
                         "Retro filter",
                         "N64-style output smoothing filter that softens hard pixel edges: auto, off, or on.");
    settingsRegisterFloat("Input.MouseSensitivity", &g_pcMouseSensitivity, 0.15f, 0.01f, 2.0f,
                          SETTING_SCOPE_LIVE, "GE007_MOUSE_SENSITIVITY",
                          "--config-override Input.MouseSensitivity=VALUE",
                          "Mouse sensitivity",
                          "Mouse-look sensitivity during normal aim.");
    settingsRegisterFloat("Input.MouseSensitivityAim", &g_pcMouseSensAim, 0.05f, 0.005f, 1.0f,
                          SETTING_SCOPE_LIVE, "GE007_MOUSE_SENSITIVITY_AIM",
                          "--config-override Input.MouseSensitivityAim=VALUE",
                          "Aim mouse sensitivity",
                          "Mouse-look sensitivity while aiming.");
    settingsRegisterInt("Input.InvertY", &g_pcInvertY, 0, 0, 1,
                        SETTING_SCOPE_LIVE, "GE007_INVERT_Y",
                        "--config-override Input.InvertY=VALUE",
                        "Invert Y axis",
                        "Invert mouse-look Y input.");
    settingsRegisterFloat("Input.GamepadLookSpeed", &g_pcGamepadLookSpeed, 8.0f, 1.0f, 30.0f,
                          SETTING_SCOPE_LIVE, "GE007_GAMEPAD_LOOK_SPEED",
                          "--config-override Input.GamepadLookSpeed=VALUE",
                          "Gamepad look speed",
                          "Right-stick look speed multiplier.");
    settingsRegisterFloat("Input.GamepadLookCurve", &g_pcGamepadLookCurve, 1.5f, 1.0f, 4.0f,
                          SETTING_SCOPE_LIVE, "GE007_GAMEPAD_LOOK_CURVE",
                          "--config-override Input.GamepadLookCurve=VALUE",
                          "Gamepad look curve",
                          "Response exponent: 1.0 linear (vanilla), >1 finer near center.");
    settingsRegisterFloat("Input.GamepadDeadzone", &g_pcGamepadDeadzone, 0.15f, 0.0f, 0.9f,
                          SETTING_SCOPE_LIVE, "GE007_GAMEPAD_DEADZONE",
                          "--config-override Input.GamepadDeadzone=VALUE",
                          "Gamepad deadzone",
                          "Right-stick inner deadzone as a fraction of full deflection (radial mode).");
    settingsRegisterInt("Input.GamepadRadialDeadzone", &g_pcGamepadRadialDeadzone, 1, 0, 1,
                        SETTING_SCOPE_LIVE, "GE007_GAMEPAD_RADIAL_DEADZONE",
                        "--config-override Input.GamepadRadialDeadzone=VALUE",
                        "Radial deadzone",
                        "1 = circular deadzone with rescale-from-edge; 0 = legacy square.");
    settingsRegisterInt("Input.GamepadFpsScale", &g_pcGamepadFpsScale, 1, 0, 1,
                        SETTING_SCOPE_LIVE, "GE007_GAMEPAD_FPS_SCALE",
                        "--config-override Input.GamepadFpsScale=VALUE",
                        "FPS-independent gamepad",
                        "Scale right-stick look by frame delta so speed is fps-independent.");
    settingsRegisterInt("Input.SteadyView", &g_pcSteadyView, 1, 0, 1,
                        SETTING_SCOPE_LIVE, "GE007_STEADY_VIEW",
                        "--config-override Input.SteadyView=VALUE",
                        "Steady view",
                        "Keep the world camera upright during movement; head motion still drives position and weapon sway.");
    settingsRegisterInt("Input.Rumble", &g_pcRumble, 1, 0, 1,
                        SETTING_SCOPE_LIVE, "GE007_RUMBLE",
                        "--config-override Input.Rumble=VALUE",
                        "Rumble",
                        "Controller vibration on the game's faithful Rumble Pak events (weapon fire, taking damage). Needs a physical pad; no effect on keyboard/mouse.");
    settingsRegisterInt("Input.RumbleIntensity", &g_pcRumbleIntensity, 100, 0, 100,
                        SETTING_SCOPE_LIVE, "GE007_RUMBLE_INTENSITY",
                        "--config-override Input.RumbleIntensity=VALUE",
                        "Rumble intensity",
                        "Vibration strength as a percentage (0-100). 0 is the same as Rumble off.");

    /* Rebindable in-game menu / FPS-overlay toggles (host-side UI keys, read by
     * the app-shell overlay; never sim input, so determinism is unaffected).
     * Keys are SDL keycodes; the button is an SDL_GameController button index. */
    settingsRegisterInt("Input.MenuToggleKey", &g_cfgMenuToggleKey, SDLK_F1, 0, 0x40000FFF,
                        SETTING_SCOPE_LIVE, "GE007_MENU_TOGGLE_KEY",
                        "--config-override Input.MenuToggleKey=VALUE",
                        "Menu key",
                        "SDL keycode that opens the in-game menu overlay (default F1 = 1073741882).");
    settingsRegisterInt("Input.MenuToggleButton", &g_cfgMenuToggleButton, SDL_CONTROLLER_BUTTON_BACK, 0, 20,
                        SETTING_SCOPE_LIVE, "GE007_MENU_TOGGLE_BUTTON",
                        "--config-override Input.MenuToggleButton=VALUE",
                        "Menu button",
                        "SDL_GameController button index that opens the in-game menu overlay (default Back = 4).");
    settingsRegisterInt("Input.FpsToggleKey", &g_cfgFpsToggleKey, SDLK_F10, 0, 0x40000FFF,
                        SETTING_SCOPE_LIVE, "GE007_FPS_TOGGLE_KEY",
                        "--config-override Input.FpsToggleKey=VALUE",
                        "FPS key",
                        "SDL keycode that toggles the FPS overlay without opening the menu (default F10 = 1073741891).");

    /* Full-auto fire-rate authenticity (FID-0056). ON (default, owner decision
     * 2026-07-10) = automatics fire at the faithful N64 per-frame cadence
     * (~1/FrameCost as fast). 0 = opt out, restoring the legacy locked-60Hz fast
     * fire (the ~3x-overspeed port behavior). Default is the 3rd positional arg
     * below (the `1` immediately after &g_pcFireRateAuthentic); the trailing
     * `0, 1` are min/max. */
    settingsRegisterInt("Input.FireRateAuthentic", &g_pcFireRateAuthentic, 1, 0, 1,
                        SETTING_SCOPE_LIVE, "GE007_FIRE_RATE_AUTHENTIC",
                        "--config-override Input.FireRateAuthentic=VALUE",
                        "Authentic full-auto fire rate",
                        "Scale full-auto cadence to the faithful N64 per-frame rate (default ON). 0 = legacy vanilla 60Hz (~3x faster) cadence.");
    settingsRegisterInt("Input.FireRateN64FrameCost", &g_pcFireRateN64FrameCost,
                        FIRE_RATE_N64_FRAME_COST_DEFAULT,
                        FIRE_RATE_N64_FRAME_COST_MIN, FIRE_RATE_N64_FRAME_COST_MAX,
                        SETTING_SCOPE_LIVE, "GE007_FIRE_RATE_N64_FRAME_COST",
                        "--config-override Input.FireRateN64FrameCost=VALUE",
                        "N64 frame cost (fire rate)",
                        "Assumed N64 sim-ticks per rendered frame for authentic fire cadence (2=30fps, 3=20fps combat, 4=15fps). Only used when Input.FireRateAuthentic=1.");

    /* ADS (aim-down-sights) — opt-in modern aiming. Master flag ships OFF;
     * when 0 every ADS branch is bypassed and behavior is byte-identical. */
    settingsRegisterInt("Input.AdsEnabled", &g_pcAdsEnabled, 0, 0, 1,
                        SETTING_SCOPE_LIVE, "GE007_ADS_ENABLED",
                        "--config-override Input.AdsEnabled=VALUE",
                        "Enable ADS",
                        "Modern aim-down-sights (opt-in). 0 = vanilla aiming.");
    settingsRegisterFloat("Input.AdsSensitivity", &g_pcAdsSensitivity, 1.0f, 0.1f, 2.0f,
                          SETTING_SCOPE_LIVE, "GE007_ADS_SENSITIVITY",
                          "--config-override Input.AdsSensitivity=VALUE",
                          "ADS sensitivity",
                          "Flat look-speed multiplier while aiming.");
    settingsRegisterInt("Input.AdsFovCoupleSens", &g_pcAdsFovCoupleSens, 1, 0, 1,
                        SETTING_SCOPE_LIVE, "GE007_ADS_FOV_COUPLE_SENS",
                        "--config-override Input.AdsFovCoupleSens=VALUE",
                        "FOV-coupled ADS sens",
                        "Slow aimed look proportionally to the narrowed FOV.");
    settingsRegisterInt("Input.AdsCenterCrosshair", &g_pcAdsCenterCrosshair, 1, 0, 1,
                        SETTING_SCOPE_LIVE, "GE007_ADS_CENTER_CROSSHAIR",
                        "--config-override Input.AdsCenterCrosshair=VALUE",
                        "ADS center crosshair",
                        "Ramp the stick aim accumulators toward center while aiming.");
    settingsRegisterInt("Input.AdsSpreadEnabled", &g_pcAdsSpreadEnabled, 1, 0, 1,
                        SETTING_SCOPE_LIVE, "GE007_ADS_SPREAD_ENABLED",
                        "--config-override Input.AdsSpreadEnabled=VALUE",
                        "ADS spread tighten",
                        "Apply the per-weapon spread multiplier while aiming.");
    settingsRegisterInt("Input.AdsMovePenalty", &g_pcAdsMovePenalty, 1, 0, 1,
                        SETTING_SCOPE_LIVE, "GE007_ADS_MOVE_PENALTY",
                        "--config-override Input.AdsMovePenalty=VALUE",
                        "ADS movement penalty",
                        "Slow forward/strafe movement while aiming. 0 = vanilla speed.");
    settingsRegisterFloat("Input.AdsMoveScale", &g_pcAdsMoveScale, 1.0f, 0.1f, 2.0f,
                          SETTING_SCOPE_LIVE, "GE007_ADS_MOVE_SCALE",
                          "--config-override Input.AdsMoveScale=VALUE",
                          "ADS move scale",
                          "Trim on the per-weapon aimed forward-speed multiplier.");
    settingsRegisterFloat("Input.AdsStrafeScale", &g_pcAdsStrafeScale, 1.0f, 0.1f, 2.0f,
                          SETTING_SCOPE_LIVE, "GE007_ADS_STRAFE_SCALE",
                          "--config-override Input.AdsStrafeScale=VALUE",
                          "ADS strafe scale",
                          "Trim on the per-weapon aimed strafe-speed multiplier.");
    settingsRegisterInt("Input.AdsFaithfulZoom", &g_pcAdsFaithfulZoom, 0, 0, 1,
                        SETTING_SCOPE_LIVE, "GE007_ADS_FAITHFUL_ZOOM",
                        "--config-override Input.AdsFaithfulZoom=VALUE",
                        "Faithful ADS zoom",
                        "Use each weapon's true Zoom (no mild-iron clamp) when aiming.");
    settingsRegisterInt("Input.AdsModelPose", &g_pcAdsModelPose, 1, 0, 1,
                        SETTING_SCOPE_LIVE, "GE007_ADS_MODEL_POSE",
                        "--config-override Input.AdsModelPose=VALUE",
                        "ADS model pose",
                        "Blend the weapon model toward the sighted pose while aiming.");
    settingsRegisterFloat("Input.AdsRecoilReduce", &g_pcAdsRecoilReduce, 0.0f, 0.0f, 1.0f,
                          SETTING_SCOPE_LIVE, "GE007_ADS_RECOIL_REDUCE",
                          "--config-override Input.AdsRecoilReduce=VALUE",
                          "ADS recoil reduce",
                          "Cosmetic aimed recoil reduction (0 = off).");
    settingsRegisterInt("Input.AdsModernReticle", &g_pcAdsModernReticle, 1, 0, 1,
                        SETTING_SCOPE_LIVE, "GE007_ADS_MODERN_RETICLE",
                        "--config-override Input.AdsModernReticle=VALUE",
                        "ADS modern reticle",
                        "Clean dot+ticks aiming reticle while aiming (vs the classic crosshair).");
    settingsRegisterInt("Input.AdsSteadyView", &g_pcAdsSteadyView, 1, 0, 1,
                        SETTING_SCOPE_LIVE, "GE007_ADS_STEADY_VIEW",
                        "--config-override Input.AdsSteadyView=VALUE",
                        "ADS steady view",
                        "Damp walk/strafe head-bob out of the aimed view (1 = steadier).");
    settingsRegisterFloat("Input.AdsBobFloor", &g_pcAdsBobFloor, 0.15f, 0.0f, 1.0f,
                          SETTING_SCOPE_LIVE, "GE007_ADS_BOB_FLOOR",
                          "--config-override Input.AdsBobFloor=VALUE",
                          "ADS bob floor",
                          "Residual weapon bob kept while aiming (0 = rigid, ~0.15 = subtle).");
    settingsRegisterFloat("Input.ViewmodelSway", &g_pcViewmodelSway, 1.0f, 0.0f, 3.0f,
                          SETTING_SCOPE_LIVE, "GE007_VIEWMODEL_SWAY",
                          "--config-override Input.ViewmodelSway=VALUE",
                          "Viewmodel sway",
                          "Additive breathing sway on the first-person weapon (0 = off, 1 = subtle).");
    settingsRegisterInt("Input.ModernCrosshair", &g_pcModernCrosshair, 1, 0, 1,
                        SETTING_SCOPE_LIVE, "GE007_MODERN_CROSSHAIR",
                        "--config-override Input.ModernCrosshair=VALUE",
                        "Modern crosshair",
                        "Always-on crisp dot+ticks hip-fire reticle instead of the textured crosshair (0 = off).");
    settingsRegisterInt("Input.HitMarkers", &g_pcHitMarkers, 1, 0, 1,
                        SETTING_SCOPE_LIVE, "GE007_HIT_MARKERS",
                        "--config-override Input.HitMarkers=VALUE",
                        "Hit markers",
                        "Flash a marker on the crosshair when your shot registers (white hit, yellow head, red kill; 0 = off).");
    settingsRegisterInt("Input.ReticleTargetFeedback", &g_pcReticleTargetFeedback, 1, 0, 1,
                        SETTING_SCOPE_LIVE, "GE007_RETICLE_TARGET_FEEDBACK",
                        "--config-override Input.ReticleTargetFeedback=VALUE",
                        "Reticle target feedback",
                        "Tint the modern reticle green and thicken the dot when your crosshair is over an enemy (0 = off).");
    settingsRegisterInt("Input.MinimapEnabled", &g_pcMinimapEnabled, 1, 0, 1,
                        SETTING_SCOPE_LIVE, "GE007_MINIMAP_ENABLED",
                        "--config-override Input.MinimapEnabled=VALUE",
                        "Minimap",
                        "Draw the modern tactical minimap during single-player gameplay (0 = off).");
    settingsRegisterInt("Input.MinimapMode", &g_pcMinimapMode, 0, 0, 2,
                        SETTING_SCOPE_LIVE, "GE007_MINIMAP_MODE",
                        "--config-override Input.MinimapMode=VALUE",
                        "Minimap mode",
                        "0 = local tactical north-up; 1 = floor overview north-up; 2 = local rotating player-up.");
    settingsRegisterInt("Input.MinimapObjectives", &g_pcMinimapObjectives, 1, 0, 1,
                        SETTING_SCOPE_LIVE, "GE007_MINIMAP_OBJECTIVES",
                        "--config-override Input.MinimapObjectives=VALUE",
                        "Minimap objectives",
                        "Show objective location pins on the tactical minimap so you can see where "
                        "your current mission objectives are. On by default.");
    settingsRegisterInt("Input.MinimapEnemyFireReveal", &g_pcMinimapEnemyFireReveal, 1, 0, 1,
                        SETTING_SCOPE_LIVE, "GE007_MINIMAP_ENEMY_FIRE_REVEAL",
                        "--config-override Input.MinimapEnemyFireReveal=VALUE",
                        "Minimap enemy fire",
                        "Briefly reveal guards on the minimap when they fire audible unsuppressed weapons (0 = off).");
    settingsRegisterInt("Input.MinimapShowAllEnemies", &g_pcMinimapShowAllEnemies, 0, 0, 1,
                        SETTING_SCOPE_LIVE, "GE007_MINIMAP_SHOW_ALL_ENEMIES",
                        "--config-override Input.MinimapShowAllEnemies=VALUE",
                        "Minimap all enemies",
                        "Debug/accessibility assist that reveals all recorded enemy fire events, including suppressed shots.");
    settingsRegisterFloat("Input.MinimapOpacity", &g_pcMinimapOpacity, 0.85f, 0.0f, 1.0f,
                          SETTING_SCOPE_LIVE, "GE007_MINIMAP_OPACITY",
                          "--config-override Input.MinimapOpacity=VALUE",
                          "Minimap opacity",
                          "Alpha for the modern minimap overlay.");
    settingsRegisterFloat("Input.MinimapSize", &g_pcMinimapSize, 1.0f, 0.5f, 2.0f,
                          SETTING_SCOPE_LIVE, "GE007_MINIMAP_SIZE",
                          "--config-override Input.MinimapSize=VALUE",
                          "Minimap size",
                          "Scale for the modern minimap overlay.");
    settingsRegisterInt("Input.MinimapSharpOverlay", &g_pcMinimapSharpOverlay, 1, 0, 1,
                        SETTING_SCOPE_LIVE, "GE007_MINIMAP_SHARP_OVERLAY",
                        "--config-override Input.MinimapSharpOverlay=VALUE",
                        "Minimap sharp overlay",
                        "Draw the minimap after the retro output filter for a crisp modern overlay (0 = disable first-pass renderer).");

    settingsRegisterInt("Game.IntroSkipStyle", &g_pcIntroSkipStyle, 0, 0, 1,
                        SETTING_SCOPE_LIVE, "GE007_INTRO_SKIP_STYLE",
                        "--config-override Game.IntroSkipStyle=VALUE",
                        "Intro skip style",
                        "0 = stock staged skip (a new press of A/B/Z/Start/L/R advances the level-intro "
                        "camera one stage at a time; the stick never skips). 1 = the native any-button/"
                        "any-stick instant full skip to first-person.");
    settingsRegisterInt("Game.CheckForUpdates", &g_pcCheckForUpdates, 1, 0, 1,
                        SETTING_SCOPE_LIVE, "GE007_UPDATE_CHECK",
                        "--config-override Game.CheckForUpdates=VALUE",
                        "Check for updates",
                        "At startup, quietly check for a newer version of MGB64 and show a dismissible "
                        "banner if one is available. No personal data is sent. 0 = never check.");
    settingsRegisterEnum("UI.LauncherFullscreen", &g_uiLauncherFullscreen, PLATFORM_LAUNCHER_FS_AUTO,
                         k_launcherFullscreenOptions,
                         (s32)(sizeof(k_launcherFullscreenOptions) / sizeof(k_launcherFullscreenOptions[0])),
                         SETTING_SCOPE_LIVE, "GE007_LAUNCHER_FULLSCREEN",
                         "--config-override UI.LauncherFullscreen=VALUE",
                         "Launcher fullscreen",
                         "How the launcher window is sized. auto fills the screen on small/high-DPI "
                         "handheld panels and floats in a resizable window on desktops; on always "
                         "fills the display; off always uses a resizable window (desktop dev). "
                         "This affects only the pre-game launcher \xe2\x80\x94 the in-game window "
                         "follows Video.WindowMode.");
    settingsRegisterFloat("UI.Scale", &g_uiScale, 1.0f, 0.75f, 2.0f,
                          SETTING_SCOPE_LIVE, "GE007_UI_SCALE",
                          "--config-override UI.Scale=VALUE",
                          "UI scale",
                          "Scales the launcher and in-game overlay text, padding and buttons. "
                          "1.0 = default; handhelds usually want ~1.25-1.5 for a readable 7-inch "
                          "panel. Applies live.");

    /* RX.1 settings curation: tag dev/diagnostic knobs as advanced so the
     * launcher hides them behind the per-tab "Advanced (expert)" disclosure.
     * These stay fully overridable via env/CLI/ge007.ini (hidden != removed);
     * only the player-facing UI list is trimmed. Kept player-facing on purpose:
     * the master toggles (Ssao/SunShadow/Bloom/Fxaa/Smaa/Tonemap/GradePresets),
     * every color/FOV/fog dial, RenderScale/MSAA, and the ADS master +
     * AdsSensitivity + AdsModernReticle. Advanced = the deep tuning under them. */
    /* SSAO tuning (11) -- the Ssao master toggle stays player-facing. */
    settingsMarkAdvanced("Video.SsaoMode");
    settingsMarkAdvanced("Video.SsaoRadius");
    settingsMarkAdvanced("Video.SsaoIntensity");
    settingsMarkAdvanced("Video.SsaoBias");
    settingsMarkAdvanced("Video.SsaoPower");
    settingsMarkAdvanced("Video.SsaoFarCutoff");
    settingsMarkAdvanced("Video.SsaoNearCut");
    settingsMarkAdvanced("Video.SsaoSkyCut");
    settingsMarkAdvanced("Video.SsaoHalfRes");
    settingsMarkAdvanced("Video.SsaoBlur");
    settingsMarkAdvanced("Video.SsaoBlurDepthSharp");
    /* Sun-shadow tuning (4) -- the SunShadow master toggle stays player-facing. */
    settingsMarkAdvanced("Video.SunShadowRes");
    settingsMarkAdvanced("Video.SunShadowRadius");
    settingsMarkAdvanced("Video.SunShadowBias");
    settingsMarkAdvanced("Video.SunShadowUmbra");
    /* Directional-relight internals. */
    settingsMarkAdvanced("Video.PerPixelLight");
    settingsMarkAdvanced("Video.EnvSmoothNormals");
    settingsMarkAdvanced("Video.EnvRelightBlend");
    /* Bloom tuning -- the Bloom master toggle stays player-facing. */
    settingsMarkAdvanced("Video.BloomThreshold");
    settingsMarkAdvanced("Video.BloomIntensity");
    /* Window placement (managed automatically; expert-only). */
    settingsMarkAdvanced("Video.WindowX");
    settingsMarkAdvanced("Video.WindowY");
    /* Minimap dev/accessibility + renderer internals. */
    settingsMarkAdvanced("Input.MinimapShowAllEnemies");
    settingsMarkAdvanced("Input.MinimapSharpOverlay");
    /* ADS deep tuning (11). Player-facing: Input.AdsEnabled (master),
     * Input.AdsSensitivity, Input.AdsModernReticle. */
    settingsMarkAdvanced("Input.AdsFovCoupleSens");
    settingsMarkAdvanced("Input.AdsCenterCrosshair");
    settingsMarkAdvanced("Input.AdsSpreadEnabled");
    settingsMarkAdvanced("Input.AdsMovePenalty");
    settingsMarkAdvanced("Input.AdsMoveScale");
    settingsMarkAdvanced("Input.AdsStrafeScale");
    settingsMarkAdvanced("Input.AdsFaithfulZoom");
    settingsMarkAdvanced("Input.AdsModelPose");
    settingsMarkAdvanced("Input.AdsRecoilReduce");
    settingsMarkAdvanced("Input.AdsSteadyView");
    settingsMarkAdvanced("Input.AdsBobFloor");
}

/* `--faithful` preset: the documented "Faithful original" mode (VISUAL_MODES.md
 * §1) as one switch. These pin the remaster departures back to their pre-remaster
 * (N64-faithful) values for this launch only; everything is still independently
 * settable, and the values are applied transiently (never written to ge007.ini).
 *
 * Video.RemasterFX=0 is the master post-FX bypass, so the individual grade /
 * tonemap / bloom / vignette / sharpen / dither / FXAA / GradePresets /
 * Saturation / Contrast / Brightness settings do NOT need listing here. Only the
 * departures RemasterFX does not cover are pinned below.
 *
 * Deliberately NOT touched: Input.SteadyView (a PC mouse/stick-look correctness
 * default, not a cinematic effect) and the compile-time EXPLOSION_BUFFER_LEN
 * 6->16 effect-slot bump (not a runtime setting). */
static const struct {
    const char *key;
    const char *value;
} s_faithfulPreset[] = {
    { "Video.RemasterFX",            "0" },      /* bypass the whole post-FX stack */
    { "Video.FpsOverlay",            "0" },      /* [AUDIT-0010] no non-original HUD */
    { "Video.RenderScale",           "1" },      /* native res (no supersampling)  */
    { "Video.MSAA",                  "0" },
    { "Video.AnisotropicFiltering",  "1" },      /* faithful N64 filtering (no aniso) */
    { "Video.TexturePack",           "" },       /* stock textures (no HD pack)    */
    { "Video.FovY",                  "60" },     /* classic 4:3 vertical FOV       */
    { "Video.ViewmodelFov",          "60" },
    { "Video.CutsceneFovY",          "60" },     /* N64 cinematic framing          */
    { "Input.ModernCrosshair",       "0" },
    { "Input.HitMarkers",            "0" },
    { "Input.ReticleTargetFeedback", "0" },
    { "Input.ViewmodelSway",         "0" },      /* additive sway off (purely added) */
    { "Input.GamepadLookCurve",      "1.0" },    /* linear (vanilla)               */
    { "Input.GamepadDeadzone",       "0.2441" }, /* legacy 8000/32767 corner       */
    { "Input.GamepadRadialDeadzone", "0" },      /* per-axis square (vanilla)      */
    { "Input.GamepadFpsScale",       "0" },      /* per-frame look (vanilla)       */
    { "Input.MinimapEnabled",        "0" },      /* tactical minimap/radar off     */
    { "Audio.OutputFilter",          "0" },      /* faithful raw 22050 synth output */
    /* Beyond-1:1 sky/fog wins are remaster departures NOT covered by RemasterFX
     * (they act pre-combiner / on the environment, not in the post-FX stack), so
     * pin them back to their faithful/default values here. Both equal the
     * registered defaults -> byte-identical. */
    { "Video.FogDensity",            "1.0" },    /* R1: original N64 fog thickness  */
    { "Video.SmoothSky",             "0" },      /* R5: original N64 5-bit sky band */
};

int platformApplyFaithfulPreset(void)
{
    int applied = 0;
    size_t i;

    for (i = 0; i < sizeof(s_faithfulPreset) / sizeof(s_faithfulPreset[0]); i++) {
        if (settingsApplyFaithfulValue(s_faithfulPreset[i].key, s_faithfulPreset[i].value)) {
            applied++;
        }
    }

    return applied;
}

/* `--remaster` preset: the full "immaculate" remaster in one switch — every
 * post-FX enabled, INCLUDING SSAO, which is off by default because it op-hangs
 * Apple's GL-over-Metal translator (the whole reason for the native Metal
 * backend; --remaster selects GE007_RENDERER=metal in main_pc.c so SSAO runs).
 * These mirror the registered defaults (already remaster) plus Video.Ssao=1, and
 * are pinned explicitly so the mode holds even if the user's ge007.ini turned
 * something off. Applied transiently before env/CLI overrides (which still win)
 * via the same generic preset-override mechanism as --faithful. */
static const struct {
    const char *key;
    const char *value;
} s_remasterPreset[] = {
    { "Video.RemasterFX",    "1" },   /* master post-FX switch on               */
    { "Video.Ssao",          "1" },   /* the key enable — works via Metal now   */
    /* W3.E2 SSAO v2 (hemisphere) adoption into --remaster is a ◆ tuning-look /
     * M2-review decision (doc 03 §9.1: "flip after the demo review; keys ship
     * either way"). Held by the orchestrator pending user sign-off — --remaster
     * stays on planar v1 for now. Hemisphere is available via
     * Video.SsaoMode=hemisphere (+SsaoBlur/SsaoHalfRes/Bias=0.15/Power=3.0/
     * FarCutoff=800). Re-add these 6 lines on approval. */
    { "Video.Bloom",         "1" },
    { "Video.Fxaa",          "1" },
    { "Video.Tonemap",       "1" },
    { "Video.GradePresets",  "1" },
    { "Video.RenderScale",   "2" },   /* 2x SSAA (fidelity)                     */
    { "Video.MSAA",          "0" },   /* SSAA instead of MSAA                   */
    { "Video.Saturation",    "1.15" },
    { "Video.Contrast",      "1.08" },
    { "Video.Brightness",    "0.04" },
    { "Video.Vignette",      "0.15" },
    { "Video.Sharpen",       "0.15" },
    { "Video.OutputDither",  "1" },   /* hide RGBA8 banding in skies/fades      */
    { "Video.Gamma",         "1" },
    { "Video.SsaoRadius",    "0.5" },
    { "Video.SsaoIntensity", "1.0" },
    /* Beyond-1:1 remaster wins (DAM_RENDER_DEEP_DIVE §F, ship-first trio). Both are
     * cosmetic/draw-only and identity at their faithful default, so they live only in
     * the remaster preset — the bare defaults and --faithful/--faithful-hd stay
     * byte-identical. */
    { "Video.FogDensity",    "0.6" },  /* R1: recede the outdoor fog wall so the reservoir reads
                                        * as real distance (AI sight range is unaffected).       */
    { "Video.SmoothSky",     "1" },    /* R5: smooth sky gradient (skip the N64 5-bit quantize). */
};

int platformApplyRemasterPreset(void)
{
    int applied = 0;
    size_t i;

    for (i = 0; i < sizeof(s_remasterPreset) / sizeof(s_remasterPreset[0]); i++) {
        if (settingsApplyRemasterValue(s_remasterPreset[i].key, s_remasterPreset[i].value)) {
            applied++;
        }
    }

    return applied;
}

/* `--faithful-hd` preset (VISUAL_MODES.md mode 2, "faithful upscale"): the
 * byte-faithful original LOOK — cinematic post-FX off, classic FOV/crosshair/aim —
 * but rendered at 2x SSAA and ready for an HD texture pack. This is the one-flag
 * form of the previously-two-override middle mode. Video.TexturePack is
 * deliberately NOT pinned, so a pack supplied via GE007_TEXTURE_PACK /
 * --config-override / ge007.ini is honored. No SSAO / no post-FX, so it needs no
 * Metal backend and runs on the default GL path. Applied transiently (read-only
 * session) like --faithful/--remaster. */
static const struct {
    const char *key;
    const char *value;
} s_faithfulHdPreset[] = {
    { "Video.RemasterFX",            "0" },      /* faithful look: post-FX bypass  */
    { "Video.FpsOverlay",            "0" },      /* [AUDIT-0010] no non-original HUD */
    { "Video.RenderScale",           "2" },      /* 2x SSAA supersampling          */
    { "Video.MSAA",                  "0" },
    /* Video.TexturePack intentionally NOT pinned — supply your own HD pack. */
    { "Video.FovY",                  "60" },     /* classic 4:3 vertical FOV       */
    { "Video.ViewmodelFov",          "60" },
    { "Video.CutsceneFovY",          "60" },     /* N64 cinematic framing          */
    { "Input.ModernCrosshair",       "0" },
    { "Input.HitMarkers",            "0" },
    { "Input.ReticleTargetFeedback", "0" },
    { "Input.ViewmodelSway",         "0" },
    { "Input.GamepadLookCurve",      "1.0" },
    { "Input.GamepadDeadzone",       "0.2441" },
    { "Input.GamepadRadialDeadzone", "0" },
    { "Input.GamepadFpsScale",       "0" },
    { "Input.MinimapEnabled",        "0" },
    { "Video.FogDensity",            "1.0" },    /* R1: original N64 fog thickness (see --faithful note) */
    { "Video.SmoothSky",             "0" },      /* R5: original N64 5-bit sky band                      */
    { "Video.AnisotropicFiltering",  "1" },      /* FID-0142: keep the N64 3-point filter (aniso>1 swaps
                                                    3-point materials to hardware bilinear game-wide,
                                                    which contradicts this preset's byte-faithful look) */
};

int platformApplyFaithfulHdPreset(void)
{
    int applied = 0;
    size_t i;

    for (i = 0; i < sizeof(s_faithfulHdPreset) / sizeof(s_faithfulHdPreset[0]); i++) {
        if (settingsApplyFaithfulValue(s_faithfulHdPreset[i].key, s_faithfulHdPreset[i].value)) {
            applied++;
        }
    }

    return applied;
}

void platformGetMouseDelta(int *dx, int *dy) {
    extern int g_freezeInput;

#ifdef __EMSCRIPTEN__
    /* WEB-016: undo SDL emscripten's window/CSS relative-motion rescale so
     * mouse-look feel is window-size-independent and matches native. SDL scales
     * the two axes INDEPENDENTLY (xrel *= window_w/css_w, yrel *= window_h/css_h),
     * so each axis is undone by its OWN factor — X: (window_w/css_w)·(css_w/window_w)
     * = 1; Y: (window_h/css_h)·(css_h/window_h) = 1 — a width-only unscale would
     * leave Y aspect-dependent. Applied to the HARDWARE-accumulated delta only
     * (g_mouseDelta*), never the scripted injection below (g_pcScriptedMouseDelta*),
     * which is already in game units. The factors are refreshed once per frame in
     * platformPollEvents.
     *
     * Fractional carry: the factors are generally non-integer, so multiplying an
     * int delta and rounding drops the remainder every frame — sustained slow aim
     * would undercount whenever a factor > 1. Carry the truncated fraction per axis
     * into the next frame (residX/residY hold the rounding remainder, in [-0.5,0.5]);
     * reset the residuals whenever ungrabbed so stale carry cannot fire on a
     * recapture. */
    {
        static float residX = 0.0f, residY = 0.0f;
        if (!g_mouseGrabbed) {
            residX = 0.0f;
            residY = 0.0f;
        }
        if (g_pcWebMouseUnscaleX != 1.0f || residX != 0.0f) {
            float scaled = (float)g_mouseDeltaX * g_pcWebMouseUnscaleX + residX;
            long out = lroundf(scaled);
            residX = scaled - (float)out;
            g_mouseDeltaX = (int)out;
        }
        if (g_pcWebMouseUnscaleY != 1.0f || residY != 0.0f) {
            float scaled = (float)g_mouseDeltaY * g_pcWebMouseUnscaleY + residY;
            long out = lroundf(scaled);
            residY = scaled - (float)out;
            g_mouseDeltaY = (int)out;
        }
    }
#endif

    if (g_freezeInput) {
        *dx = g_pcScriptedMouseDeltaX;
        *dy = g_pcScriptedMouseDeltaY;
    } else {
        *dx = g_mouseDeltaX + g_pcScriptedMouseDeltaX;
        *dy = g_mouseDeltaY + g_pcScriptedMouseDeltaY;
    }
    g_mouseDeltaX = 0;
    g_mouseDeltaY = 0;
    g_pcScriptedMouseDeltaX = 0;
    g_pcScriptedMouseDeltaY = 0;
}

int platformGetMouseWheel(void) {
    int v = g_mouseWheelY;
    g_mouseWheelY = 0;
    return v;
}

/* Right stick raw values for direct aim injection (bypasses C-button acceleration) */
void platformGetRightStick(int *rx_out, int *ry_out) {
    int rx = 0, ry = 0;
    if (g_gameController) {
        rx = SDL_GameControllerGetAxis(g_gameController, SDL_CONTROLLER_AXIS_RIGHTX);
        ry = SDL_GameControllerGetAxis(g_gameController, SDL_CONTROLLER_AXIS_RIGHTY);
        /* Apply deadzone */
        if (rx > -8000 && rx < 8000) rx = 0;
        if (ry > -8000 && ry < 8000) ry = 0;
    }
#ifdef MACOS_APP_BUNDLE
    rx += g_pcBridgeRightStickX;
    ry += g_pcBridgeRightStickY;
    if (rx > 32767) rx = 32767;
    if (rx < -32767) rx = -32767;
    if (ry > 32767) ry = 32767;
    if (ry < -32767) ry = -32767;
#endif
    *rx_out = rx;
    *ry_out = ry;
}

/* ===== Per-pad accessors (multi-controller / split-screen) =====
 * Slot index k is the player number (0..PLATFORM_MAX_PADS-1). Every accessor
 * bounds-checks k and returns neutral values for an absent or out-of-range pad,
 * so callers never touch a NULL handle on hot-unplug. */

int platformGetPadCount(void) {
    int count = 0;
    int k;
    for (k = 0; k < PLATFORM_MAX_PADS; k++) {
        if (g_pads[k].handle) {
            count++;
        }
    }
    return count;
}

/* Joystick instance id of the P1 pad (slot 0), or -1 when absent. Used by the
 * app overlay to filter its gamepad toggle to player 1: in split-screen, P2-4
 * Start/Back presses must reach their own N64 pad, not open the shared overlay
 * (whose input gate would freeze every player). */
int platformGetPad0InstanceId(void) {
    return g_pads[0].handle ? (int)g_pads[0].instance_id : -1;
}

/* MC.7: active player count, for the app overlay to choose its per-mode footer
 * (single-player = "Paused", multiplayer = "game keeps running"). Returns 1 when
 * the engine has no live players yet (frontend/boot) so the overlay text stays
 * in the single-player wording there. */
int platformGetPlayerCount(void) {
    extern s32 getPlayerCount(void);
    s32 n = getPlayerCount();
    return (n >= 1) ? (int)n : 1;
}

/* Raw SDL button state for pad k (0 if absent). Mapping to N64 buttons is done
 * by the caller (stubs.c) so all players share one mapping. */
unsigned int platformGetPadButtons(int k) {
    SDL_GameController *gc;
    unsigned int mask = 0;
    int b;

    if (k < 0 || k >= PLATFORM_MAX_PADS) {
        return 0;
    }
    gc = g_pads[k].handle;
    if (!gc) {
        return 0;
    }
    for (b = 0; b < SDL_CONTROLLER_BUTTON_MAX; b++) {
        if (SDL_GameControllerGetButton(gc, (SDL_GameControllerButton)b)) {
            mask |= (1u << b);
        }
    }
    return mask;
}

/* platformApplyRadialDeadzone() now lives in the pure src/platform/radial_deadzone.c
 * TU so the aim stick, movement stick, and the ROM-free unit test share one
 * implementation (FID-0015 / M2.1). Declared in platform_os.h + radial_deadzone.h. */

/* Raw left-stick axes for pad k (range -32768..32767, 0 if absent). */
void platformGetPadLeftStick(int k, int *lx_out, int *ly_out) {
    SDL_GameController *gc;
    int lx = 0, ly = 0;

    if (k >= 0 && k < PLATFORM_MAX_PADS && (gc = g_pads[k].handle) != NULL) {
        lx = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTX);
        ly = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTY);
    }
    if (lx_out) *lx_out = lx;
    if (ly_out) *ly_out = ly;
}

/* Raw right-stick axes for pad k with deadzone applied (0 if absent).
 * Matches the deadzone used by the legacy platformGetRightStick(). */
void platformGetPadRightStick(int k, int *rx_out, int *ry_out) {
    SDL_GameController *gc;
    int rx = 0, ry = 0;

    if (k >= 0 && k < PLATFORM_MAX_PADS && (gc = g_pads[k].handle) != NULL) {
        /* Default: legacy per-axis square deadzone (|axis|<8000 -> 0). When the
         * opt-in radial deadzone is on, relax this pre-clip to a small noise
         * floor so lvl.c can apply a true radial magnitude deadzone + rescale
         * (otherwise the square clip would zero diagonals before lvl.c sees
         * them). Default (flag 0) keeps the exact vanilla 8000 clip. */
        int dz_axis = g_pcGamepadRadialDeadzone ? 1638 : 8000;
        rx = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_RIGHTX);
        ry = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_RIGHTY);
        if (rx > -dz_axis && rx < dz_axis) rx = 0;
        if (ry > -dz_axis && ry < dz_axis) ry = 0;
    }
#ifdef MACOS_APP_BUNDLE
    /* The Swift bridge only feeds player 1. */
    if (k == 0) {
        rx += g_pcBridgeRightStickX;
        ry += g_pcBridgeRightStickY;
        if (rx > 32767) rx = 32767;
        if (rx < -32767) rx = -32767;
        if (ry > 32767) ry = 32767;
        if (ry < -32767) ry = -32767;
    }
#endif
    if (rx_out) *rx_out = rx;
    if (ry_out) *ry_out = ry;
}

/* Trigger axes for pad k (range 0..32767, 0 if absent). leftTrig/rightTrig may
 * be NULL. */
void platformGetPadTriggers(int k, int *leftTrig, int *rightTrig) {
    SDL_GameController *gc;
    int lt = 0, rt = 0;

    if (k >= 0 && k < PLATFORM_MAX_PADS && (gc = g_pads[k].handle) != NULL) {
        lt = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_TRIGGERLEFT);
        rt = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
    }
    if (leftTrig) *leftTrig = lt;
    if (rightTrig) *rightTrig = rt;
}

/* ===== Scheduler references ===== */
/* We need access to the scheduler's client list to send retrace messages */
extern OSSched os_scheduler;

/**
 * Initialize SDL2 and create a window with OpenGL context.
 * Returns 0 on success, -1 on failure.
 */
int platformInitSDL(void) {
    if (getenv("GE007_FLYCAM") != NULL) {
        g_pcDebugFlyCamera = 1;
    }
    if (getenv("GE007_NO_VSYNC") != NULL) {
        g_forceNoVsync = 1;
    }
    g_backgroundWindow = platformEnvFlagEnabled("GE007_BACKGROUND");
    g_disableInputGrab = g_backgroundWindow || platformEnvFlagEnabled("GE007_NO_INPUT_GRAB");
    platformApplyWindowSizeEnv();
    if (platformEnvFlagEnabled("MGB64_PORTMASTER")) {
        g_cfgWindowW   = 640;
        g_cfgWindowH   = 480;
        g_windowMode   = PLATFORM_WINDOW_MODE_BORDERLESS; /* fullscreen-desktop, no compositor needed */
        g_disableInputGrab = 1;
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER) < 0) {
        fprintf(stderr, "[SDL] Init failed: %s\n", SDL_GetError());
        return -1;
    }

    /* Request appropriate GL context for the target platform */
#ifdef MGB64_PORTMASTER_GLES
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
#elif defined(__APPLE__)
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
#else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
#endif
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    if (platformHasHostWindow()) {
        /* MGB64_APP shell owns the SDL window; adopt it so the launcher and the
         * game render into one window. */
        g_sdlWindow = (SDL_Window *)platformHostWindow();
        if (!g_sdlWindow) {
            fprintf(stderr, "[SDL] Host window adoption failed\n");
            return -1;
        }
#ifdef MGB64_WEBGPU_BACKEND
        if (platformHasHostWebGpu()) {
            /* WebGPU shell: the launcher already stood up the wgpu device +
             * surface for THIS window; the game's wgpu_init adopts them (host
             * handoff). No GL context and no metal view here — the launcher owns
             * the surface it created from its own Metal view. WebGPU stays the
             * backend (no force_opengl). */
            g_glContext = NULL;
            printf("[SDL] Adopted app-shell window + WebGPU device/surface (%p)\n",
                   (void *)g_sdlWindow);
        } else
#endif
        {
            /* GL shell (GE007_RENDERER=gl): adopt the GL context and force the GL
             * backend — this window has no CAMetalLayer, so WebGPU/Metal cannot
             * render into it (without this the WebGPU default goes "backend
             * inert" -> a black frame while audio/sim run: the launcher->Play
             * regression). WebGPU stays the default for engine-owned boots. */
            gfx_backend_force_opengl();
            g_glContext = (SDL_GLContext)platformHostGLContext();
            if (!g_glContext) {
                fprintf(stderr, "[SDL] Host GL context adoption failed\n");
                return -1;
            }
            SDL_GL_MakeCurrent(g_sdlWindow, g_glContext);
            printf("[SDL] Adopted app-shell window/context (%p / %p)\n",
                   (void *)g_sdlWindow, (void *)g_glContext);
        }
    } else {
    {
        int diag_disable_highdpi = platformEnvFlagEnabled("GE007_DIAG_DISABLE_HIGHDPI");
        int enable_highdpi = g_cfgHiDpi != 0 && !diag_disable_highdpi;
        int window_x;
        int window_y;
        Uint32 window_flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE;
#ifdef __APPLE__
        /* Both the native Metal backend and the WebGPU backend (which drives
         * Metal through wgpu-native) render into a CAMetalLayer, so the window
         * must be a Metal window, not a GL one. */
        if (gfx_backend_use_metal() || gfx_backend_use_webgpu()) {
            window_flags = SDL_WINDOW_METAL | SDL_WINDOW_RESIZABLE;
        }
#endif

        if (enable_highdpi) {
            window_flags |= SDL_WINDOW_ALLOW_HIGHDPI;
        }

        if (g_backgroundWindow) {
            window_flags |= SDL_WINDOW_HIDDEN;
        } else {
            window_flags |= SDL_WINDOW_SHOWN;
        }

        platformGetConfiguredWindowPosition(&window_x, &window_y);
        g_sdlWindow = SDL_CreateWindow(
        "MGB64",
        window_x, window_y,
        g_cfgWindowW, g_cfgWindowH,
        window_flags
        );
    }

    if (!g_sdlWindow) {
        fprintf(stderr, "[SDL] Window creation failed: %s\n", SDL_GetError());
        SDL_Quit();
        return -1;
    }

#ifdef __APPLE__
    /* WebGPU shares the Metal-view path: platformGetMetalLayer() hands the
     * created view's CAMetalLayer to wgpu-native for the WGPUSurface. Neither
     * the Metal nor the WebGPU backend wants a GL context. */
    if (gfx_backend_use_metal() || gfx_backend_use_webgpu()) {
        g_metalView = SDL_Metal_CreateView(g_sdlWindow);
        if (!g_metalView) {
            fprintf(stderr, "[SDL] Metal view creation failed: %s\n", SDL_GetError());
            SDL_DestroyWindow(g_sdlWindow);
            SDL_Quit();
            return -1;
        }
    } else
#endif
#ifdef MGB64_WEBGPU_BACKEND
    /* On non-macOS, WebGPU creates its WGPUSurface directly from the native
     * window (HWND/X11/Wayland) and needs no GL context. (On macOS the Metal
     * branch above already handled it.) */
    if (gfx_backend_use_webgpu()) {
        /* no GL context */
    } else
#endif
    {
        g_glContext = SDL_GL_CreateContext(g_sdlWindow);
        if (!g_glContext) {
            fprintf(stderr, "[SDL] GL context creation failed: %s\n", SDL_GetError());
            SDL_DestroyWindow(g_sdlWindow);
            SDL_Quit();
            return -1;
        }
    }
    }

    /* RX.3 Fix A: apply the configured Video.WindowMode on BOTH the adopted
     * app-shell window (the Windows/macOS release path, which owns g_sdlWindow +
     * g_glContext) AND the engine-owned window, now that the window and its GL
     * context / Metal view are valid. Previously this ran only inside the
     * engine-owned branch, so borderless/exclusive was loaded into g_windowMode
     * but never pushed to SDL on the shell path — the window stayed windowed and
     * "true fullscreen" was a no-op on Windows.
     *
     * Guard: skip the hidden/background window (GE007_BACKGROUND drives the
     * SDL_WINDOW_HIDDEN flag above) so CI / screenshot harnesses are never forced
     * fullscreen. */
    if (!g_backgroundWindow) {
        /* Inherit the launcher's fullscreen so the in-game window matches it. The shell/launcher
         * may have put the shared SDL window into (borderless) fullscreen via UI.LauncherFullscreen;
         * Video.WindowMode defaults to WINDOWED, and platformApplyWindowMode() below would revert the
         * window to a title-barred windowed state — the "game has a title bar while the launcher was
         * fullscreen" bug (worst on Windows). When the adopted window is ALREADY fullscreen and the
         * user hasn't chosen a non-windowed Video.WindowMode, keep it fullscreen. A game launched
         * standalone (no fullscreen launcher) leaves the window windowed, so this only triggers on the
         * launcher hand-off; an explicit Video.WindowMode (borderless/exclusive) still wins. */
        if (g_sdlWindow && g_windowMode == PLATFORM_WINDOW_MODE_WINDOWED) {
            Uint32 wf = SDL_GetWindowFlags(g_sdlWindow);
            if ((wf & SDL_WINDOW_FULLSCREEN_DESKTOP) == SDL_WINDOW_FULLSCREEN_DESKTOP) {
                g_windowMode = PLATFORM_WINDOW_MODE_BORDERLESS;
            } else if (wf & SDL_WINDOW_FULLSCREEN) {
                g_windowMode = PLATFORM_WINDOW_MODE_EXCLUSIVE;
            }
        }
        platformApplyWindowMode();
    }

    /* Load OpenGL function pointers via glad (desktop only; GLES resolves via SDL;
     * skipped for WebGPU, which has no GL context). */
#if !defined(__APPLE__) && !defined(MGB64_PORTMASTER_GLES)
    if (gfx_backend_use_webgpu()) {
        /* no GL functions to load */
    } else if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
        fprintf(stderr, "[SDL] Failed to load OpenGL functions via glad\n");
        SDL_GL_DeleteContext(g_glContext);
        SDL_DestroyWindow(g_sdlWindow);
        SDL_Quit();
        return -1;
    }
#endif

    /* macOS can block indefinitely in SwapWindow when the test window never
     * receives focus. Allow explicit no-vsync runs for automated capture. */
    platformApplyVSync();
    if (g_forceNoVsync) {
        printf("[SDL] VSync disabled (GE007_NO_VSYNC)\n");
    }

    g_lastFrameTime = SDL_GetTicks();
#ifdef MGB64_WEBGPU_BACKEND
    if (gfx_backend_use_webgpu()) {
        /* No GL context exists — glGetString() would crash. */
        printf("[SDL] Window created (WebGPU / wgpu-native)\n");
    } else
#endif
#ifdef __APPLE__
    if (gfx_backend_use_metal()) {
        printf("[SDL] Window created (native Metal)\n");
    } else
#endif
    printf("[SDL] Window created (OpenGL %s, GLSL %s)\n",
           glGetString(GL_VERSION), glGetString(GL_SHADING_LANGUAGE_VERSION));
    if (platformEnvFlagEnabled("GE007_DIAG_DISABLE_HIGHDPI")) {
        printf("[SDL] HiDPI window mode disabled (GE007_DIAG_DISABLE_HIGHDPI)\n");
    } else if (!g_cfgHiDpi) {
        printf("[SDL] HiDPI window mode disabled (Video.HiDPI=0)\n");
    }
    if (g_backgroundWindow) {
        printf("[SDL] Background window mode enabled (GE007_BACKGROUND)\n");
    }
    if (g_disableInputGrab) {
        printf("[SDL] Input grab disabled (GE007_NO_INPUT_GRAB)\n");
    }

    /* Apply the community controller-mapping database before opening any pad so
     * exotic/hybrid devices map correctly (MC.2). Missing file is non-fatal. */
    platformLoadControllerMappings();

    /* Open every available game controller into its own player slot. The first
     * opened pad lands in slot 0 (player 1) and shares that slot with the
     * keyboard/mouse merge in stubs.c. */
    for (int i = 0; i < PLATFORM_MAX_PADS; i++) {
        g_pads[i].handle      = NULL;
        g_pads[i].instance_id = -1;
        g_pads[i].slot        = i;
    }
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        if (platformOpenPad(i) < 0 && platformGetPadCount() >= PLATFORM_MAX_PADS) {
            break; /* table full */
        }
    }
    platformSyncPad0Alias();

#ifdef __EMSCRIPTEN__
    /* PERF-037: install the per-frame browser-state mirrors (visibility,
     * pointer-lock, canvas resize) once — the canvas exists by now. */
    platformWebInstallStateMirrors();
#endif

    return 0;
}

#ifdef __APPLE__
/* Returns the CAMetalLayer* (as void*) backing the SDL Metal view, for the
 * native Metal backend to render into. NULL when not on the Metal path. */
void *platformGetMetalLayer(void) {
    return g_metalView ? SDL_Metal_GetLayer(g_metalView) : NULL;
}
#endif

/* The SDL_Window the engine renders into (as void*), for backends that need the
 * native window handle. The app shell passes its own window explicitly instead;
 * this is the standalone-engine default. NULL before window creation. */
void *platformGetSdlWindow(void) {
    return (void *)g_sdlWindow;
}

#ifdef MGB64_WEBGPU_BACKEND
#include <SDL_syswm.h>
/* Native window handles for gfx_webgpu.c's cross-platform WGPUSurface creation.
 * `sdl_window` (a SDL_Window*, as void*) is the window to resolve; NULL falls
 * back to the engine's g_sdlWindow. macOS goes through platformGetMetalLayer
 * instead. Return value tags the windowing system: 2 = Win32 (out1=HWND,
 * out2=HINSTANCE), 3 = X11 (out1=Display*, out_win=Window), 4 = Wayland
 * (out1=wl_display*, out2=wl_surface*); 0 = unknown/unsupported. */
int platformWebGpuWindowInfo(void *sdl_window, void **out1, void **out2,
                             unsigned long long *out_win) {
    if (out1) *out1 = NULL;
    if (out2) *out2 = NULL;
    if (out_win) *out_win = 0;
    SDL_Window *win = sdl_window ? (SDL_Window *)sdl_window : g_sdlWindow;
    if (win == NULL) {
        return 0;
    }
    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    if (!SDL_GetWindowWMInfo(win, &info)) {
        return 0;
    }
    switch (info.subsystem) {
#if defined(SDL_VIDEO_DRIVER_WINDOWS)
        case SDL_SYSWM_WINDOWS:
            if (out1) *out1 = (void *)info.info.win.window;      /* HWND */
            if (out2) *out2 = (void *)info.info.win.hinstance;   /* HINSTANCE */
            return 2;
#endif
#if defined(SDL_VIDEO_DRIVER_X11)
        case SDL_SYSWM_X11:
            if (out1) *out1 = (void *)info.info.x11.display;
            if (out_win) *out_win = (unsigned long long)info.info.x11.window;
            return 3;
#endif
#if defined(SDL_VIDEO_DRIVER_WAYLAND)
        case SDL_SYSWM_WAYLAND:
            if (out1) *out1 = (void *)info.info.wl.display;
            if (out2) *out2 = (void *)info.info.wl.surface;
            return 4;
#endif
        default:
            return 0;
    }
}
#endif /* MGB64_WEBGPU_BACKEND */

/* WEB-001: the F1 fly-camera, F2 screenshot, and F3–F12 level-warp keys are
 * developer tools, not player controls. They are inert unless GE007_DEV_HOTKEYS
 * is set (default OFF on every platform). Rationale for gating on native too:
 *   - On native the settings overlay (ui_overlay.cpp) swallows F1 (its menu
 *     toggle opens → platformOverlayWantsInput() → event skipped here), but its
 *     F10 FPS-toggle branch returns WITHOUT setting g_open — the event was NOT
 *     swallowed, so pre-gate native F10 DUAL-FIRED: it toggled the FPS overlay
 *     AND warped to Surface 2 via the F3–F12 block below. This gate therefore
 *     also fixes a real pre-existing native bug, not just the web exposure
 *     (web additionally had NO overlay at all: MGB64_APP OFF).
 *   - No test or tool drives them: screenshot ctests use the --screenshot-frame
 *     CLI (not F2), and test_sys_hotkey.c only checks the F1/F10 *keycode*
 *     predicates for the overlay's menu/FPS toggles, not warp/fly-cam behavior.
 * Leaving them live on web let index.html's own instructions warp a real player
 * mid-mission (F10→Surface 2) or freeze all input (F1 fly-cam). Cached once. */
static int pcDevHotkeysEnabled(void) {
    static int cached = -1;
    if (cached < 0) {
        const char *v = getenv("GE007_DEV_HOTKEYS");
        cached = (v && v[0] && v[0] != '0') ? 1 : 0;
    }
    return cached;
}

/**
 * Process SDL events and check for quit.
 */
void platformPollEvents(void) {
    SDL_Event event;
#ifdef __EMSCRIPTEN__
    /* WEB-007: reconcile our grab state with the browser's pointer-lock state
     * once per frame. While locked, the browser consumes Esc as the exit-lock
     * gesture and delivers NO SDL keydown, so the Esc handler below never runs:
     * g_mouseGrabbed would stay 1, the watch wouldn't open (the page says it
     * will), and the camera would keep turning with the freed cursor
     * ("possessed camera") until a second Esc. Detect the confirmed-locked →
     * unlocked transition and treat it exactly like the Esc-in-gameplay press.
     * The confirm latch keeps this from firing during the async lock-acquisition
     * window after a regrab-click, so it integrates with (doesn't fight) the
     * b1525bf MOUSEBUTTONDOWN relock path below. */
    if (g_mouseGrabbed) {
        int locked = platformPointerLocked();   /* PERF-037: mirrored, no EM_ASM crossing */
        if (locked) {
            g_pcWebLockConfirmed = 1;
        } else if (g_pcWebLockConfirmed) {
            /* Genuine in-gameplay lock loss (user's Esc, alt-tab, focus steal). */
            g_pcEscapePressed = 1;          /* 1 = was in gameplay → START (watch) */
            g_mouseGrabbed = 0;
            g_pcWebLockConfirmed = 0;
            SDL_SetRelativeMouseMode(SDL_FALSE);
            g_mouseDeltaX = 0;              /* stop residual motion this frame */
            g_mouseDeltaY = 0;
        }
    } else {
        g_pcWebLockConfirmed = 0;
    }
    /* WEB-016: refresh the per-axis relative-motion unscale ONCE per frame here
     * (this reconcile block already runs once per platformPollEvents), so the
     * CSS-size query never happens per motion event. Applied to accumulated deltas
     * in platformGetMouseDelta. Guarded per axis against a zero/absent window or
     * CSS box: fall back to identity (1.0) rather than zeroing mouse-look. Do NOT
     * touch the SDL window size — gfx sizing reads the CSS box independently
     * (gfx_pc.c) and must not be disturbed. */
    {
        /* PERF-037 (a): the canvas CSS box only changes on a resize, so re-query
         * it (getComputedStyle, via emscripten_get_element_css_size) ONLY on frames
         * the ResizeObserver flagged, and reuse the cached value otherwise. The
         * query — hence the fed VALUE — is byte-for-byte the one the old per-frame
         * code used; WEB-016's unscale math below is unchanged. A ResizeObserver
         * delivers before the next paint, so during an active resize the cache is at
         * most ONE frame stale — and the old code already sampled once per frame (not
         * per motion event), so it carried the same up-to-one-frame latency; a
         * 1-frame-late unscale ratio during a drag is imperceptible for mouse-look.
         * s_cssValid stays 0 until the first successful query (seeded frame 0) and is
         * re-tried every frame while invalid, matching the old fall-through to 1.0. */
        static double s_cssW = 0.0, s_cssH = 0.0;
        static int    s_cssValid = 0;
        int winw = 0, winh = 0;
        if (g_sdlWindow != NULL) {
            SDL_GetWindowSize(g_sdlWindow, &winw, &winh);
        }
        if (!s_cssValid || platformCanvasSizeDirty()) {
            double qw = 0.0, qh = 0.0;
            if (emscripten_get_element_css_size("#canvas", &qw, &qh) == 0) {
                s_cssW = qw; s_cssH = qh; s_cssValid = 1;
            } else {
                s_cssValid = 0;
            }
        }
        if (s_cssValid) {
            g_pcWebMouseUnscaleX = (s_cssW >= 1.0 && winw >= 1)
                ? (float)(s_cssW / (double)winw) : 1.0f;
            g_pcWebMouseUnscaleY = (s_cssH >= 1.0 && winh >= 1)
                ? (float)(s_cssH / (double)winh) : 1.0f;
        } else {
            g_pcWebMouseUnscaleX = 1.0f;
            g_pcWebMouseUnscaleY = 1.0f;
        }
    }
#endif
    while (SDL_PollEvent(&event)) {
        /* Let the app overlay see every event; when it is capturing input,
         * swallow input events so they don't also drive the game. Capture the
         * overlay-active state BEFORE dispatch and swallow if it was active
         * EITHER before or after: the toggle key (F1) flips the state inside
         * process_event, and we must swallow that same keypress on BOTH the
         * open and the close transition so it never leaks to the game (e.g.
         * the engine's own F1 fly-camera toggle). */
        int overlayActive = platformOverlayWantsInput();
        platformOverlayProcessEvent(&event);
        if (overlayActive || platformOverlayWantsInput()) {
            switch (event.type) {
                case SDL_KEYDOWN:
                case SDL_KEYUP:
                case SDL_MOUSEMOTION:
                case SDL_MOUSEBUTTONDOWN:
                case SDL_MOUSEBUTTONUP:
                case SDL_MOUSEWHEEL:
                case SDL_TEXTINPUT:
                case SDL_CONTROLLERBUTTONDOWN:
                case SDL_CONTROLLERBUTTONUP:
                case SDL_CONTROLLERAXISMOTION:
                case SDL_JOYBUTTONDOWN:
                case SDL_JOYBUTTONUP:
                case SDL_JOYAXISMOTION:
                case SDL_JOYHATMOTION:
                    continue;
                default:
                    break;
            }
        }
        switch (event.type) {
            case SDL_QUIT:
                g_sdlQuit = 1;
                break;
            case SDL_WINDOWEVENT:
                if (event.window.event == SDL_WINDOWEVENT_FOCUS_GAINED) {
                    /* Restore the configured swap interval when focus returns. */
                    platformApplyVSync();
                } else if (event.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
                    /* Disable vsync when unfocused to prevent macOS SwapWindow hang */
                    SDL_GL_SetSwapInterval(0);
                } else if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                    gfx_set_window_size(event.window.data1, event.window.data2);
                    platformRememberWindowGeometry();
                } else if (event.window.event == SDL_WINDOWEVENT_MOVED) {
                    platformRememberWindowGeometry();
                }
                break;
            case SDL_KEYDOWN:
                if (event.key.keysym.sym == SDLK_RETURN &&
                    (event.key.keysym.mod & KMOD_ALT)) {
                    /* RX.3 Fix B: toggle windowed <-> the user's configured
                     * non-windowed mode (borderless or exclusive) rather than a
                     * hardcoded borderless, so exclusive users return to exclusive. */
                    g_windowMode = (g_windowMode == PLATFORM_WINDOW_MODE_WINDOWED)
                        ? g_preferredFullscreenMode
                        : PLATFORM_WINDOW_MODE_WINDOWED;
                    platformApplyWindowMode();
                } else if ((event.key.keysym.sym == SDLK_ESCAPE ||
                            event.key.keysym.sym == SDLK_TAB) && !event.key.repeat) {
                    /* Esc and Tab both open the GoldenEye watch (N64 Start) in
                     * gameplay, or back out (B) in menus. Tab was advertised in the
                     * H help but previously had no handler — now it matches. */
                    if (g_mouseGrabbed) {
                        /* In gameplay: pause (START_BUTTON) and ungrab mouse */
                        g_pcEscapePressed = 1;  /* 1 = was in gameplay → START */
                        SDL_SetRelativeMouseMode(SDL_FALSE);
                        g_mouseGrabbed = 0;
                    } else {
                        /* Ungrabbed. WEB-042: don't assume "ungrabbed == menus"
                         * (keyboard-only players are ungrabbed IN gameplay too).
                         * stubs.c resolves 2 from real game state: B in the
                         * title/frontend menus, START (pause) during a mission. */
                        g_pcEscapePressed = 2;  /* 2 = ungrabbed → decided in stubs.c */
                    }
                } else if (event.key.keysym.sym == SDLK_c ||
                           event.key.keysym.sym == SDLK_LCTRL ||
                           event.key.keysym.sym == SDLK_RCTRL) {
                    /* WEB-041: accept RCtrl for crouch too (keep C and LCtrl). A
                     * full scancode-registry migration for crouch is out of scope. */
                    if (!event.key.repeat) {
                        g_pcCrouchToggle = 1;
                    }
                } else if (event.key.keysym.sym == SDLK_F1 && pcDevHotkeysEnabled()) {
                    g_pcDebugFlyCamera = !g_pcDebugFlyCamera;
                    printf("[SDL] Fly camera %s (F1 toggle, GE007_DEV_HOTKEYS)\n",
                           g_pcDebugFlyCamera ? "ON" : "OFF — gameplay input active");
                } else if (event.key.keysym.sym == SDLK_F2 && pcDevHotkeysEnabled()) {
                    g_screenshotRequested = 1;
                } else if (event.key.keysym.sym == SDLK_h && !event.key.repeat) {
#ifdef __EMSCRIPTEN__
                    /* WEB-001: web binary has no settings overlay (MGB64_APP OFF)
                     * and no dev hotkeys — describe only what actually works. */
                    printf("\n"
                        "=== CONTROLS ===\n"
                        "WASD        Move\n"
                        "Mouse       Look (click canvas to capture)\n"
                        "L Click     Fire\n"
                        "R Click     Aim (hold)\n"
                        "Scroll      Cycle weapon\n"
                        "R           Reload\n"
                        "F           Interact\n"
                        "C / LCtrl   Crouch toggle\n"
                        "Q / E       Lean L/R (aim mode)\n"
                        "Esc / Tab   Watch menu (objectives, options, pause)\n"
                        "M           Mute audio\n"
                        "H           Show this help\n"
                        "(FPS overlay is on by default in the top-right.)\n"
                        "\n"
                        "GAMEPAD: LT=Aim RT=Fire Y=Next weapon R3=Prev weapon L3=Crouch\n"
                        "         Start=Watch\n"
                        "================\n");
#else
                    printf("\n"
                        "=== CONTROLS ===\n"
                        "WASD        Move\n"
                        "Mouse       Look\n"
                        "L Click     Fire\n"
                        "R Click     Aim (hold)\n"
                        "Scroll      Cycle weapon\n"
                        "R           Reload\n"
                        "F           Interact\n"
                        "C / LCtrl   Crouch toggle\n"
                        "Q / E       Lean L/R (aim mode)\n"
                        "Esc / Tab   Watch menu (objectives, options, pause)\n"
                        "F1          Settings overlay (MGB64)\n"
                        "F10         FPS overlay toggle\n"
                        "M           Mute audio\n"
                        "H           Show this help\n"
                        "\n"
                        "GAMEPAD: LT=Aim RT=Fire Y=Next weapon R3=Prev weapon L3=Crouch\n"
                        "         Back/View=Overlay (settings)  Start=Watch\n"
                        "================\n");
#endif
                } else if (event.key.keysym.sym == SDLK_m && !event.key.repeat) {
                    /* M key: toggle audio mute on the unified queue device. */
                    platformToggleAudioMute("M key toggle");
                } else if (event.key.keysym.sym == SDLK_BACKQUOTE) {
                    extern void debugDumpRequest(void);
                    debugDumpRequest();
                } else if (event.key.keysym.sym >= SDLK_F3 && event.key.keysym.sym <= SDLK_F12
                           && pcDevHotkeysEnabled()) {
                    /* F3-F12: Quick level switch (dev tool, GE007_DEV_HOTKEYS).
                     * F3=Dam(33), F4=Facility(34), F5=Runway(35), F6=Surface(36),
                     * F7=Bunker1(9), F8=Silo(20), F9=Frigate(26), F10=Surface2(43),
                     * F11=Jungle(37), F12=Cradle(41) */
                    static const int levelMap[10] = {33,34,35,36,9,20,26,43,37,41};
                    int idx = event.key.keysym.sym - SDLK_F3;
                    extern int g_pcStartLevel;
                    extern void bossSetLoadedStage(int);
                    extern void set_solo_and_ptr_briefing(int);
                    extern int selected_stage;
                    g_pcStartLevel = levelMap[idx];
                    selected_stage = levelMap[idx];
                    set_solo_and_ptr_briefing(selected_stage);
                    bossSetLoadedStage(selected_stage);
                    pcPrimePostStageMenuForDirectBoot((LEVELID)selected_stage, FALSE);
                    printf("[SDL] Level switch requested: LEVELID %d (F%d)\n",
                           levelMap[idx], idx + 3);
                }
                break;
            case SDL_MOUSEBUTTONDOWN:
                /* WEB-044: SDL 2.32's emscripten backend already re-requests
                 * pointer lock on the click that follows a lock loss, so the
                 * grab-when-ungrabbed line below is largely redundant with SDL's
                 * own regrab in the ordinary case. It is kept because SDL's
                 * regrab does NOT cover the Chrome post-exit cooldown desync
                 * (the __EMSCRIPTEN__ branch below) or the non-web/native grab —
                 * so this stays the single game-level entry point that also arms
                 * g_pcMouseRegrabFrame (WEB-017) to swallow the recapture click.
                 * No behavior change here beyond what WEB-007 already landed. */
                if (!g_disableInputGrab && !g_mouseGrabbed) {
                    SDL_SetRelativeMouseMode(SDL_TRUE);
                    g_mouseGrabbed = 1;
                    g_pcMouseRegrabFrame = 1; /* suppress fire on regrab click */
#ifdef __EMSCRIPTEN__
                    /* Final-review F2: a regrab starts a fresh async lock
                     * acquisition — a stale confirm from the PREVIOUS lock
                     * must not combine with the not-yet-granted window to
                     * fire a spurious synthetic Esc next frame. */
                    g_pcWebLockConfirmed = 0;
#endif
                }
#ifdef __EMSCRIPTEN__
                /* Browser pointer-lock resilience: Chrome enforces a ~1.25 s
                 * cooldown after exiting pointer lock (its own Esc, or our Esc
                 * handler) before a new request may succeed. A click inside the
                 * cooldown makes SDL's requestPointerLock REJECT while SDL's
                 * relative-mode state stays TRUE — so without this, every later
                 * click is a no-op (g_mouseGrabbed==1, SDL thinks it's locked)
                 * and the mouse is dead until another Esc round-trip. If the
                 * document actually holds no pointer lock on a click while we
                 * believe we're grabbed, force a fresh request by toggling
                 * relative mode off->on (SDL early-outs on a same-state set). */
                else if (!g_disableInputGrab && g_mouseGrabbed &&
                         !platformPointerLocked()) {   /* PERF-037: mirrored state */
                    SDL_SetRelativeMouseMode(SDL_FALSE);
                    SDL_SetRelativeMouseMode(SDL_TRUE);
                    g_pcMouseRegrabFrame = 1; /* suppress fire on regrab click */
                    g_pcWebLockConfirmed = 0; /* F2: stale confirm must not span relocks */
                }
#endif
                break;
            case SDL_MOUSEWHEEL: {
                /* Accumulate the FLOAT wheel delta and emit whole steps.
                 * The integer wheel.y field truncates per event: browsers
                 * (SDL emscripten scales DOM pixel deltas by 1/100) and macOS
                 * trackpads deliver many small fractional deltas that all
                 * truncate to 0 — the wheel felt dead/rough — while momentum
                 * flicks burst. Carrying the fraction across events makes one
                 * physical notch/step equal exactly one weapon-cycle step on
                 * every input stack. (Menu/UI input only — tapes record
                 * controller-level state, so replay hashes are unaffected.) */
                static float s_wheelAccumY = 0.0f;
#if SDL_VERSION_ATLEAST(2, 0, 18)
                float wyf = event.wheel.preciseY;
#else
                float wyf = (float)event.wheel.y;
#endif
                if (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) wyf = -wyf;
                s_wheelAccumY += wyf;
                {
                    int steps = (int)s_wheelAccumY; /* trunc toward zero */
                    if (steps != 0) {
                        g_mouseWheelY += steps;
                        s_wheelAccumY -= (float)steps;
                    }
                }
                break;
            }
            case SDL_CONTROLLERDEVICEADDED:
                /* event.cdevice.which is a device index for ADDED. */
                platformOpenPad(event.cdevice.which);
                break;
            case SDL_CONTROLLERDEVICEREMOVED:
                /* event.cdevice.which is an instance id for REMOVED. Free only
                 * that slot; other players keep their stable mapping. */
                platformClosePadByInstance(event.cdevice.which);
                break;
            case SDL_AUDIODEVICEREMOVED: {
                /* AUDIT-0068: if our opened output device is unplugged, stop
                 * queueing to the dead handle. event.adevice.which is the
                 * SDL_AudioDeviceID for an opened device. */
                extern void osAiNotifyDeviceRemoved(unsigned int which);
                if (!event.adevice.iscapture) {
                    osAiNotifyDeviceRemoved(event.adevice.which);
                }
                break;
            }
            case SDL_MOUSEMOTION:
                if (g_mouseGrabbed) {
                    if (g_pcDebugFlyCamera) {
                        /* Mouse look for fly camera */
                        g_pcCamYaw   += event.motion.xrel * MOUSE_SENSITIVITY;
                        g_pcCamPitch -= event.motion.yrel * MOUSE_SENSITIVITY;
                        if (g_pcCamPitch > 1.5f)  g_pcCamPitch = 1.5f;
                        if (g_pcCamPitch < -1.5f) g_pcCamPitch = -1.5f;
                    } else {
                        /* Accumulate for N64 controller emulation only */
                        g_mouseDeltaX += event.motion.xrel;
                        g_mouseDeltaY += event.motion.yrel;
                    }
                }
                break;
        }
    }

    /* WASD fly camera movement (only when fly camera is active) */
    if (g_pcDebugFlyCamera) {
        const Uint8 *keys = SDL_GetKeyboardState(NULL);
        float sx = sinf(g_pcCamYaw), cx = cosf(g_pcCamYaw);
        /* Forward/backward along yaw direction */
        if (keys[SDL_SCANCODE_W]) { g_pcCamX -= sx * FLY_SPEED; g_pcCamZ -= cx * FLY_SPEED; }
        if (keys[SDL_SCANCODE_S]) { g_pcCamX += sx * FLY_SPEED; g_pcCamZ += cx * FLY_SPEED; }
        /* Strafe left/right */
        if (keys[SDL_SCANCODE_A]) { g_pcCamX -= cx * FLY_SPEED; g_pcCamZ += sx * FLY_SPEED; }
        if (keys[SDL_SCANCODE_D]) { g_pcCamX += cx * FLY_SPEED; g_pcCamZ -= sx * FLY_SPEED; }
        /* Up/down */
        if (keys[SDL_SCANCODE_SPACE])  g_pcCamY -= FLY_SPEED;
        if (keys[SDL_SCANCODE_LSHIFT]) g_pcCamY += FLY_SPEED;
    }
}

/**
 * Drain any stale SDL events (e.g. macOS sends SDL_QUIT during long init).
 * Call right before entering the game loop.
 */
void platformDrainEvents(void) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        /* discard all queued events */
    }
    g_sdlQuit = 0;
}

/**
 * Check if user requested quit.
 */
int platformShouldQuit(void) {
    return g_sdlQuit;
}

/**
 * Wait for next frame boundary and send retrace messages to scheduler clients.
 * This replaces the N64 VI retrace interrupt.
 */
/* Defined in stubs.c — check and fire expired OS timers */
extern void platformCheckTimers(void);

void platformSetWindowTitle(const char *title) {
    if (g_sdlWindow) {
        SDL_SetWindowTitle(g_sdlWindow, title);
    }
}

/* ===== T11: FPS-overlay frame-stats ring (platform layer, sim-pure) =====
 *
 * See frame_stats.h for the contract. Fixed-capacity ring buffer of recent
 * frame durations (timestamped in SDL performance-counter ticks) backs both
 * aggregation windows; no per-frame allocation. */
#define FRAME_STATS_RING_CAP  1024   /* >= 2s at 512fps; plenty for any real cap */
#define FRAME_STATS_PUBLISH_MS  250.0   /* ~4x/sec headline refresh              */
#define FRAME_STATS_LOW_WINDOW_MS 2000.0 /* rolling window for the 1%-low metric */

static f32    s_frameStatsRingMs[FRAME_STATS_RING_CAP];
static Uint64 s_frameStatsRingTime[FRAME_STATS_RING_CAP];
static int    s_frameStatsRingHead  = 0;
static int    s_frameStatsRingCount = 0;

static Uint64 s_frameStatsLastTick    = 0;
static Uint64 s_frameStatsWindowStart = 0;
static f32    s_frameStatsAccumMs     = 0.0f;
static int    s_frameStatsAccumCount  = 0;

static PlatformFrameStats s_frameStatsPublished = { 0.0f, 0.0f, 0.0f, 0 };

static int platformFrameStatsCompareDescending(const void *a, const void *b) {
    f32 fa = *(const f32 *)a;
    f32 fb = *(const f32 *)b;
    if (fa < fb) return 1;
    if (fa > fb) return -1;
    return 0;
}

/* 1%-low FPS: average FPS of the slowest 1% of frames observed within the
 * trailing FRAME_STATS_LOW_WINDOW_MS. Stack-only (no heap), so this is safe
 * to call from the publish path a few times a second. */
static f32 platformFrameStatsComputeLow1(Uint64 now, Uint64 freq) {
    f32 window[FRAME_STATS_RING_CAP];
    int windowCount = 0;
    int i;
    int onePctCount;
    f64 sumMs = 0.0;

    for (i = 0; i < s_frameStatsRingCount; i++) {
        int idx = (s_frameStatsRingHead - 1 - i + FRAME_STATS_RING_CAP) % FRAME_STATS_RING_CAP;
        f64 age_ms = (double)(now - s_frameStatsRingTime[idx]) * 1000.0 / (double)freq;
        if (age_ms > FRAME_STATS_LOW_WINDOW_MS) {
            break;   /* ring is time-ordered; older entries are all out of window too */
        }
        window[windowCount++] = s_frameStatsRingMs[idx];
    }

    if (windowCount == 0) {
        return 0.0f;
    }

    qsort(window, (size_t)windowCount, sizeof(window[0]), platformFrameStatsCompareDescending);

    onePctCount = windowCount / 100;
    if (onePctCount < 1) {
        onePctCount = 1;
    }
    for (i = 0; i < onePctCount; i++) {
        sumMs += window[i];
    }
    {
        f64 avgSlowMs = sumMs / (f64)onePctCount;
        return avgSlowMs > 0.0 ? (f32)(1000.0 / avgSlowMs) : 0.0f;
    }
}

void platformFrameStatsTick(void) {
    Uint64 freq = SDL_GetPerformanceFrequency();
    Uint64 now = SDL_GetPerformanceCounter();
    f64 dt_ms;

    if (s_frameStatsLastTick == 0) {
        s_frameStatsLastTick = now;
        s_frameStatsWindowStart = now;
        return;
    }

    dt_ms = (double)(now - s_frameStatsLastTick) * 1000.0 / (double)freq;
    s_frameStatsLastTick = now;
    if (dt_ms < 0.0) {
        dt_ms = 0.0;   /* guard against a non-monotonic clock oddity */
    }

    s_frameStatsRingMs[s_frameStatsRingHead] = (f32)dt_ms;
    s_frameStatsRingTime[s_frameStatsRingHead] = now;
    s_frameStatsRingHead = (s_frameStatsRingHead + 1) % FRAME_STATS_RING_CAP;
    if (s_frameStatsRingCount < FRAME_STATS_RING_CAP) {
        s_frameStatsRingCount++;
    }

    s_frameStatsAccumMs += (f32)dt_ms;
    s_frameStatsAccumCount++;

    {
        f64 sinceWindowStartMs = (double)(now - s_frameStatsWindowStart) * 1000.0 / (double)freq;
        if (sinceWindowStartMs >= FRAME_STATS_PUBLISH_MS && s_frameStatsAccumCount > 0) {
            f32 avgFrameMs = s_frameStatsAccumMs / (f32)s_frameStatsAccumCount;

            s_frameStatsPublished.frame_ms = avgFrameMs;
            s_frameStatsPublished.fps = avgFrameMs > 0.0f ? (1000.0f / avgFrameMs) : 0.0f;
            s_frameStatsPublished.low1_fps = platformFrameStatsComputeLow1(now, freq);
            s_frameStatsPublished.generation++;

            s_frameStatsAccumMs = 0.0f;
            s_frameStatsAccumCount = 0;
            s_frameStatsWindowStart = now;
        }
    }
}

const PlatformFrameStats *platformFrameStatsGet(void) {
    return &s_frameStatsPublished;
}

/* FID-0089 (FID-0033 rail defect; found by the full uncap purity gate, exposed by
 * the FID-0014 merge; derivation docs/fidelity/derivations/FID-0089-uncap-audio-pump.md).
 * A render-only (0-tick) fuzz frame must leave every hashed sim byte untouched.
 * portAudioFrame() advances the SFX voice mixer once per call, and a voice whose
 * samples are exhausted is disposed by the next sndGetPlayingState(), which writes
 * NULL back into the owning ChrRecord.ptr_SEbuffer* slot (src/snd.c
 * sndDisposeSound -> *ownerSlot = NULL). Those slots live in the hashed 8 MB game
 * pool, so pumping audio on a 0-tick frame leaks the render frame cadence into
 * hashed sim state: under GE007_UNCAP_FUZZ ~75% of loop iterations are render-only,
 * so a guard's gunshot voice finishes at a different SIM tick than the all-tick
 * vanilla run and its handle is NULLed at a different g_GlobalTimer. On the N64 the
 * audio task runs once per video frame == once per 60 Hz sim tick; the port's
 * per-loop-iteration pump is the divergence. Skip the pump on the 0-tick frame,
 * mirroring boss.c's render-only skip of sim + present + maintenance, so audio
 * advances exactly once per sim tick. Never taken in normal play (the flag is 0
 * unless the fuzz harness armed it). GE007_NO_UNCAP_AUDIO_FIX=1 restores the legacy
 * per-frame pump (negative control). */
static int portUncapAudioSkipEnabled(void) {
    /* port_env_bool is read-once/cached; default 0 => fix active.
     * GE007_NO_UNCAP_AUDIO_FIX=1 restores the legacy per-frame pump. */
    return !port_env_bool("GE007_NO_UNCAP_AUDIO_FIX", 0,
        "FID-0089 negative control: restore the legacy per-loop-iteration audio pump on render-only (0-tick) fuzz frames (leaks render cadence into hashed ChrRecord.ptr_SEbuffer* SFX handles).");
}

void platformFrameSync(void) {
    OSScClient *client;
    int perf_trace = platformPerfTraceEnabled();
    Uint64 perf_start = perf_trace ? SDL_GetPerformanceCounter() : 0;
    double perf_interval_ms = 0.0;
    u32 perf_work_ms;
    u32 perf_delay_ms = 0;

    g_frameSyncCallCount++;
    platformFrameStatsTick();
    if (perf_trace) {
        if (g_perfLastFrameStart != 0) {
            perf_interval_ms = platformPerfCounterMs(g_perfLastFrameStart, perf_start);
        }
        g_perfLastFrameStart = perf_start;
    }
    {
        extern void pcAdvanceDeterministicCountForFrame(void);
        pcAdvanceDeterministicCountForFrame();
    }

    /* Fire any expired OS timers */
    platformCheckTimers();

#ifdef __EMSCRIPTEN__
    /* WEB-059: the web build never runs platformShutdownSDL (no SDL_QUIT,
     * EXIT_RUNTIME=0), so configSave() at shutdown never fires and
     * /save/ge007.ini would freeze after first boot. Flush opportunistically:
     * ~every 10 s (600 frames @ 60 Hz) write the ini IFF a setting actually
     * changed (configWebSaveIfDirty is a no-op otherwise). The shell's periodic
     * FS.syncfs then persists it to IndexedDB. Native is unchanged — it still
     * saves once at shutdown. */
    if ((g_frameSyncCallCount % 600) == 0) {
        extern void configWebSaveIfDirty(void);
        configWebSaveIfDirty();
    }
#endif

    /* Frame pacing — hold the loop to the configured frame period.
     *
     * Deadline-based with sub-millisecond precision. The previous integer-ms
     * cap (SDL_Delay(16 - elapsed)) plus macOS's ~1ms sleep overshoot paced the
     * loop at ~17.2ms — a ~58fps production rate that beats against the 60/120Hz
     * display and repeats/skips a vblank every few dozen frames (visible,
     * periodic stutter). A vsynced GL swap partially masked it by blocking
     * inside `elapsed`; Metal's present never blocks, so the coarse cap became
     * fully load-bearing there. Sleeping coarsely to ~2ms short of an ABSOLUTE
     * deadline and finishing on the performance counter holds the average
     * period at exactly the target (16.667ms) with no cumulative drift. */
    u32 now = SDL_GetTicks();
    u32 elapsed = now - g_lastFrameTime;
    perf_work_ms = elapsed;
    double period_ms = platformFrameCapPeriodMs();
    u32 frame_delay_ms = (u32)(period_ms + 0.5);   /* perf-trace cap_ms only */
    if (period_ms > 0.0) {
        Uint64 freq = SDL_GetPerformanceFrequency();
        Uint64 period = (Uint64)(period_ms * (double)freq * 0.001);
        Uint64 pace_now = SDL_GetPerformanceCounter();
        if (g_paceDeadline == 0) {
            g_paceDeadline = pace_now;   /* arm on the first paced frame */
        }
        g_paceDeadline += period;        /* this frame's absolute target */
        if (pace_now > g_paceDeadline && (pace_now - g_paceDeadline) > period) {
            /* Fell more than a full period behind (level load, window drag,
             * debugger stop, vsync-paced GL): resync instead of accumulating
             * catch-up debt that would fast-forward the next frames. */
            g_paceDeadline = pace_now;
        }
        Uint64 wait_begin = pace_now;
#ifdef __EMSCRIPTEN__
        /* Browser pacing (1%-low fix): wait on requestAnimationFrame, not
         * setTimeout. The previous emscripten_sleep() loop unwound through
         * Asyncify into setTimeout, whose 1-4ms clamp on short/nested timers
         * overshot the deadline by a different amount every frame — measured
         * 1% lows of ~40-45 fps against a 60 fps average (visible hitching,
         * rough input). A rAF wait is display-synced (16.7ms at 60Hz, 8.3ms on
         * 120Hz ProMotion), so the loop lands on vsync edges with no timer
         * clamp; the absolute deadline above still holds the 60Hz sim average
         * on any refresh rate. ALWAYS yield at least one rAF per paced frame:
         * this Asyncify unwind is the main loop's only yield point (it also
         * services the stubs.c osRecvMesg spin via platformFrameSync), so an
         * unconditional-yield keeps the browser responsive even when a frame
         * overruns its budget. Stop once within 3ms of the deadline — another
         * whole-vsync wait would overshoot; finishing slightly early is phase
         * lead, not drift, because the deadline is absolute.
         *
         * Hidden-tab fallback: requestAnimationFrame never fires while the tab
         * is backgrounded, so unconditionally waiting on it here would block the
         * whole loop until refocus. Fall back to emscripten_sleep, which stays
         * ALIVE while hidden. But note the real clamp is NOT 1-4ms: browsers
         * throttle background-tab timers to >=1s (Chrome ~1/min after ~5 min for
         * silent tabs), so each loop iteration takes ~1s of wall clock and the
         * 60Hz sim advances at ~1 Hz while hidden — a de-facto pause. That is
         * DELIBERATE: hidden == effectively paused. The sleep exists only to keep
         * the loop responsive enough to notice refocus (via platformTabHidden,
         * now a mirrored visibilitychange flag — PERF-037), not to keep audio
         * meaningful — PERF-038 skips portAudioFrame entirely while hidden (frame
         * tail below), since synthesizing at ~1 Hz produces only gaps-and-blips.
         * At least one wait call always runs per paced frame (the "ALWAYS yield"
         * contract above), whichever primitive is picked on the first iteration.
         * WEB-005's no-catch-up deadline resync (the >period rewind above) is what
         * keeps refocus from fast-forwarding the backlog of missed frames. */
        bool waited_once = false;
        while (!waited_once ||
               (pace_now < g_paceDeadline &&
                ((double)(g_paceDeadline - pace_now) * 1000.0 / (double)freq) > 3.0)) {
            if (platformTabHidden()) {
                double rem_ms = (double)(g_paceDeadline - pace_now) * 1000.0 / (double)freq;
                emscripten_sleep(rem_ms > 0.0 ? (unsigned)rem_ms : 0);
            } else {
                platformWaitAnimationFrame();
            }
            pace_now = SDL_GetPerformanceCounter();
            waited_once = true;
        }
#else
        /* Absolute deadline held: recompute rem_ms every iteration, never round
         * the whole remainder to integer ms (that beat-hitches — the bug this
         * pacer was built to avoid). Sleep for real down to the last ~0.5ms
         * instead of burning a core with SDL_Delay(0) the whole tail. */
        while (pace_now < g_paceDeadline) {
            double rem_ms = (double)(g_paceDeadline - pace_now) * 1000.0 / (double)freq;
            if (rem_ms > 2.5) {
                SDL_Delay((u32)(rem_ms - 2.0));  /* coarse sleep, ~2ms precise tail */
#if defined(__APPLE__) || defined(__linux__)
            } else if (rem_ms > 0.5) {
                nanosleep(&(struct timespec){0, 500000L}, NULL);  /* 0.5ms real sleep, not a core burn */
            } else {
                SDL_Delay(0);                    /* final <0.5ms: yield-spin for deadline precision */
            }
#else
            } else {
                SDL_Delay(0);                    /* Windows: keep the original yield-spin (nanosleep res unreliable) */
            }
#endif
            pace_now = SDL_GetPerformanceCounter();
        }
#endif
        perf_delay_ms = (u32)((double)(pace_now - wait_begin) * 1000.0 / (double)freq);
    }
    g_lastFrameTime = SDL_GetTicks();

    /* Process SDL events AFTER the pacing wait, so the simulation that runs on the
     * retrace below consumes the freshest possible input. Polling before the wait
     * left mouse/stick deltas up to one frame-cap stale on high-refresh displays;
     * this removes that latency. Identity at 60 Hz / vsync-locked (delay ~= 0).
     * Deterministic mode freezes input, so trace/screenshot gates are unaffected. */
    platformPollEvents();
    platformApplyAutoMuteToggles();

    if (g_sdlQuit) {
        printf("[GE007-PC] Quit requested, shutting down.\n");
        platformShutdownSDL();
        exit(0);
    }

#ifdef MACOS_APP_BUNDLE
    /* Check if the Swift app shell requested shutdown via game_request_shutdown() */
    {
        extern int gameBridgeCheckShutdown(void);
        if (gameBridgeCheckShutdown()) {
            printf("[GameBridge] Shutdown requested by app shell.\n");
            platformShutdownSDL();
            exit(0);
        }
    }
#endif

    /* Swap the GL framebuffer (shows whatever was rendered) */
    if (g_sdlWindow) {
        if (platformDiagDisplayCastScreenshotDue()) {
            platformSaveScreenshot();
            platformFinishAutoScreenshotIfRequested();
        }
        if (platformDiagMenuScreenshotDue()) {
            platformSaveScreenshot();
            platformFinishAutoScreenshotIfRequested();
        }
        /* Auto-screenshot at specified frame */
        if (g_autoScreenshotFrame >= 0 && g_frameSyncCallCount == g_autoScreenshotFrame) {
            platformSaveScreenshot();
            g_autoScreenshotFrame = -1;
            platformFinishAutoScreenshotIfRequested();
        }
        /* Auto-screenshot at specified gameplay timer. This captures matched
         * simulation states even when startup/loading consumes render frames. */
        if (g_autoScreenshotGameTimer >= 0) {
            extern s32 g_GlobalTimer;
            if (g_GlobalTimer >= g_autoScreenshotGameTimer) {
                platformSaveScreenshot();
                g_autoScreenshotGameTimer = -1;
                platformFinishAutoScreenshotIfRequested();
            }
        }
        /* Manual screenshot (F2) */
        if (g_screenshotRequested) {
            platformSaveScreenshot();
            g_screenshotRequested = 0;
        }
        /* Swap is now handled by gfx_end_frame() — don't double-swap */
    }

    {
        extern void portAudioFrame(void);
        extern s32 g_pcUncapRenderOnlyFrame;
        int skip_audio = (g_pcUncapRenderOnlyFrame && portUncapAudioSkipEnabled());
#ifdef __EMSCRIPTEN__
        /* PERF-038: a backgrounded tab clamps timers to >=1s, so the loop above is
         * running the sim at ~1 Hz — effectively paused. Synthesizing audio at that
         * cadence produces only gaps-and-blips that then pile into the AI queue, so
         * skip portAudioFrame entirely while hidden: no broken audio is produced or
         * queued. The SDL device keeps draining, so the queue empties to silence and
         * refocus resumes from an empty queue (no stale burst — WEB-045's mute ramp
         * governs the mute edge, not this one). Deterministic runs never branch here
         * (and this whole path is web-only), so no baseline is affected. */
        if (!g_deterministic && platformTabHidden()) {
            skip_audio = 1;
        }
#endif
        /* Sim-pure 0-tick frame: do not advance audio playback (see the
         * portUncapAudioSkipEnabled comment above). No-op in normal play. */
        if (!skip_audio) {
            portAudioFrame();
        }
    }
    /* Only call trace if tracing was requested. Dump-only trace modes do not
     * set g_traceStatePath, so include their env gates here too. */
    if (platformTraceRequested()) {
        extern void portTraceFrame(void);
        portTraceFrame();
    }
    /* Per-frame sim-hash trace (GE007_SIM_HASH_EVERY_FRAME): behavior-neutral
     * frame-lock diagnostic for aspect/cull A/B (FID-0058). No-op otherwise. */
    {
        extern void simStateHashPerFrameTrace(int global_timer);
        extern s32 g_GlobalTimer;
        simStateHashPerFrameTrace((int)g_GlobalTimer);
    }

    /* Send retrace message to all registered scheduler clients */
    os_scheduler.frameCount++;
    for (client = os_scheduler.clientList; client != NULL; client = client->next) {
        osSendMesg(client->msgQ, (OSMesg)&os_scheduler.retraceMsg, OS_MESG_NOBLOCK);
    }

    if (perf_trace &&
        g_frameSyncCallCount >= g_perfTraceAfterFrame &&
        g_perfTraceBudget > 0) {
        extern s32 g_GlobalTimer;
        Uint64 perf_end = SDL_GetPerformanceCounter();
        printf("[PERF] frame=%d global=%d interval_ms=%.3f work_ms=%u "
               "delay_ms=%u sync_ms=%.3f cap_ms=%u\n",
               g_frameSyncCallCount,
               (int)g_GlobalTimer,
               perf_interval_ms,
               perf_work_ms,
               perf_delay_ms,
               platformPerfCounterMs(perf_start, perf_end),
               frame_delay_ms);
        g_perfTraceBudget--;
    }
}

/**
 * Shutdown SDL2.
 */
void platformShutdownSDL(void) {
    /* Save config on clean shutdown */
    platformRememberWindowGeometry();
    configSave();
    /* NOTE: portTraceShutdown() is NOT called here. DL buffer overruns
     * can corrupt static FILE pointers in port_trace.c, causing fclose()
     * to SIGSEGV. The OS reclaims all file handles on process exit.
     * The trace file is flushed every 60 frames, so data loss is minimal. */
    for (int i = 0; i < PLATFORM_MAX_PADS; i++) {
        if (g_pads[i].handle) {
            SDL_GameControllerClose(g_pads[i].handle);
            g_pads[i].handle      = NULL;
            g_pads[i].instance_id = -1;
        }
    }
    g_gameController   = NULL;
    g_gameControllerID = -1;
    /* After SIGSEGV recovery via siglongjmp, the GL context may be corrupt.
     * Skip GL cleanup and just destroy the window + quit SDL. The OS will
     * reclaim the GL resources on process exit anyway. */
    extern volatile int g_gfxRecoveryActive;
    extern int g_crashRecoveryCount;
    /* When the MGB64_APP shell owns the window/context, it is responsible for
     * tearing them down (and for SDL_Quit) after control returns to it. Leave
     * them intact here so the launcher survives an engine session. */
    if (platformHasHostWindow()) {
        return;
    }
    if (g_crashRecoveryCount == 0 && g_glContext) {
        SDL_GL_DeleteContext(g_glContext);
    }
    if (g_sdlWindow) SDL_DestroyWindow(g_sdlWindow);
    SDL_Quit();
}
