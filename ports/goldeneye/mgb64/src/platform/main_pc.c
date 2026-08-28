/**
 * main_pc.c — PC entry point for the GoldenEye native port.
 *
 * Replaces the N64 boot chain (boot.s → init() → mainproc() → bossEntry()).
 * On PC we initialize SDL2 + ROM, then set up the scheduler and call
 * bossEntry() which runs the infinite bossMainloop() loop.
 */

#include <ultra64.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <signal.h>
#include <errno.h>
/* SDL_MAIN_HANDLED (set project-wide, see CMakeLists.txt) keeps SDL_main.h
 * from renaming our main() to SDL_main and requiring SDL2main -- we call
 * SDL_SetMainReady() ourselves below, first thing in main(). */
#include <SDL.h>
/* backtrace() is glibc/macOS-only — absent on MinGW-w64 and musl. */
#if defined(__GLIBC__) || defined(__APPLE__)
#define PORT_HAVE_BACKTRACE 1
#include <execinfo.h>
#endif
#include <unistd.h>
#include <dirent.h>
#ifdef _WIN32
/* SetUnhandledExceptionFilter for the SEH crash path (see crashSehFilter).
 * Undo the min/max/near/far macro pollution windows.h drags in. */
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <io.h> /* _write for the crash-write mirror in pc_diag_write_stderr */
#undef near
#undef far
#endif
#include "rom_io.h"
#include "savedir.h"
#include "config_pc.h"
#include "settings.h"
#include "bondconstants.h"
#include "game/ramromreplay.h"
#include "input_tape.h"           /* --record-tape / --play-tape (FID-0034) */
#include "sim_state_hash.h"        /* simHashRegistryBuild for --print-sim-hash-regions */
#include "../app/input_actions.h"  /* rebindable keyboard registry (not app-gated) */
#include "../app/cli_stage_tables.h" /* single-sourced stage/difficulty/MP tables + parse helpers (AUDIT-0060) */
#ifdef MGB64_APP
#include "../app/engine_entry.h"  /* MgbBootConfig + mgb64_engine_boot decl */
#endif

/* POSIX setenv() is absent on Windows/MinGW (which provides only _putenv_s()).
 * Every setenv() call in this file seeds a GE007_* variable with overwrite=1,
 * so map it to _putenv_s() there. No effect on macOS/Linux (real setenv). */
#if defined(_WIN32)
static int port_setenv(const char *name, const char *value, int overwrite) {
    if (!overwrite && getenv(name) != NULL) {
        return 0;
    }
    return _putenv_s(name, value);
}
#define setenv(name, value, overwrite) port_setenv((name), (value), (overwrite))
#endif

#ifndef __has_feature
#define __has_feature(x) 0
#endif

uintptr_t g_lastMtxAddr = 0;
int g_mtxCallCount = 0;

/* Level selection from command line: -1 = default (menu), else raw LEVELID */
int g_pcStartLevel = -1;
int g_pcStartDifficulty = DIFFICULTY_AGENT;
const char *g_pcStartRamrom = NULL;
/* --sim-state-hash-out <path>: at deterministic screenshot-exit, hash the mutable
 * simulation state and write it here (remaster P0.2 invariance gate). NULL = off. */
const char *g_simStateHashOut = NULL;
int g_pcDirectBootLevelActive = 0;
static int g_pcStartLevelForcedRaw = 0;

/* §4.1 master faithful-sim switch: set for the lifetime of a `--faithful`
 * launch only. A handful of port-added visibility/physics "wideners" (portal
 * edge-rescue, portal-project frustum fallback, visibility supplement, door-
 * floor collision, floor-reattach lowering, render-camera clearance push --
 * plus the §4.4 sim one-offs they share a class with) are individually
 * env-gated default-ON; each one's env-read helper consults this flag for its
 * *default* only, so an explicit GE007_* env value still overrides in either
 * direction (see e.g. bg.c:bgPortalEdgeRescueEnabled, bondview.c:
 * bondviewClosedDoorFloorCollisionEnabled). Never set outside of the
 * `--faithful` preset below -- default (non-faithful) behavior is untouched. */
int g_pcFaithfulSim = 0;

/* Multiplayer direct-boot selection from command line.
 * g_pcStartMultiplayer gates the GAMEMODE_MULTI direct-boot path in
 * init_menus_or_reset(); the rest mirror the frontend MP option state.
 * MP_stage value is a MP_STAGE_* index into multi_stage_setups[];
 * scenario value is a SCENARIO_* (MPSCENARIOS) index. */
int g_pcStartMultiplayer = 0;
int g_pcStartMpPlayers = 2;
int g_pcStartMpStage = MP_STAGE_TEMPLE;
int g_pcStartMpScenario = SCENARIO_NORMAL;
int g_pcStartMpTimeLimitSecs = 0;

#define PC_MAX_CONFIG_SET_ARGS 32

/* Fatal-crash write mirror: filled in by DiagLog_install() when the app
 * shell's stdout/stderr tee is active (see ../app/engine_entry.h). Defined
 * unconditionally so headless builds (no app shell) still link. */
int g_diagLogRealErrFd = -1;
int g_diagLogFileFd = -1;

static inline void pc_diag_write_stderr(const char *msg, int len)
{
    if (len <= 0) {
        return;
    }
#ifdef _WIN32
    /* Every Windows caller of this function is on a fatal path (the recovery
     * path is compiled out there). Under the app-shell tee, fd 2 is a pipe
     * whose reader thread dies with the process: a crash-time write there is
     * a scheduler race at best and blocks forever if the pipe is full. Write
     * the saved console fd and mgb64.log's raw fd directly instead. */
    if (g_diagLogRealErrFd >= 0 || g_diagLogFileFd >= 0) {
        if (g_diagLogRealErrFd >= 0) {
            int w = _write(g_diagLogRealErrFd, msg, (unsigned)len);
            (void)w;
        }
        if (g_diagLogFileFd >= 0) {
            int w = _write(g_diagLogFileFd, msg, (unsigned)len);
            (void)w;
        }
        return;
    }
#endif
    {
        ssize_t written = write(STDERR_FILENO, msg, (size_t)len);
        (void)written;
    }
}

/* The solo/MP stage + difficulty tables and their pure parse helpers
 * (PcStartStage/kPcStartStages, kPcMpStages, kPcMpScenarios, pcFindStageBy*,
 * pcParseIntArg, pcParseDifficultyArg, pcParseMpStageArg, pcParseMpScenarioArg,
 * pcStageSlugForLevelId) were moved VERBATIM to src/app/cli_stage_tables.{h,c}
 * so the launcher's launch_intent parser resolves levels/difficulties with the
 * exact same semantics as this headless CLI (AUDIT-0060). Included above. */

static int pcApplyConfigArg(const char *arg, int mark_cli_override) {
    const char *eq;
    size_t key_len;
    char key[CONFIG_MAX_KEYNAME + 1];

    if (arg == NULL) {
        return 0;
    }

    eq = strchr(arg, '=');
    if (eq == NULL || eq == arg) {
        fprintf(stderr, "[CONFIG] Invalid --config-set '%s'. Expected Section.Key=value.\n", arg);
        return 0;
    }

    key_len = (size_t)(eq - arg);
    if (key_len > CONFIG_MAX_KEYNAME) {
        fprintf(stderr, "[CONFIG] --config-set key is too long: %s\n", arg);
        return 0;
    }

    memcpy(key, arg, key_len);
    key[key_len] = '\0';

    if (!configSetValue(key, eq + 1)) {
        fprintf(stderr, "[CONFIG] Unknown config key: %s\n", key);
        return 0;
    }

    if (mark_cli_override) {
        settingsMarkCliOverride(key);
    }

    return 1;
}

#include <setjmp.h>
sigjmp_buf g_gfxRecoveryJmp;
volatile int g_gfxRecoveryActive = 0;
int g_crashRecoveryCount = 0;  /* non-static: used by platformShutdownSDL */

/* The fatal [CRASH-DL]/[CRASH-TEX] state dump, shared between the POSIX
 * signal handler and the Windows SEH filter. Avoids fprintf/printf (stdio
 * locks can deadlock if the fault interrupted stdio); snprintf() to a stack
 * buffer + write() is not strictly async-signal-safe per POSIX but is safe
 * in practice on macOS/glibc (no heap allocation or locks). */
static void crashWriteDlTexDiag(void) {
    extern volatile uintptr_t g_lastDlCmd;
    extern volatile uint32_t g_lastDlOpcode, g_lastDlW0;
    extern volatile uintptr_t g_lastDlW1;
    extern volatile uintptr_t g_diag_current_cmd_addr;
    extern int g_frame_count_diag;
    extern volatile uintptr_t g_diag_tex_addr;
    extern volatile uint32_t g_diag_tex_size_bytes, g_diag_tex_needed;
    extern volatile uint8_t g_diag_tex_fmt, g_diag_tex_siz, g_diag_tex_slot, g_diag_tex_tile;
    char diag[768];
    int dlen = snprintf(diag, sizeof(diag),
        "[CRASH-DL] frame=%d op=0x%02X w0=0x%08X w1=0x%llX cmd=%p diag_cmd=%p\n"
        "[CRASH-TEX] addr=%p size_bytes=%u needed=%u fmt=%u siz=%u slot=%u tile=%u\n",
        g_frame_count_diag,
        g_lastDlOpcode, g_lastDlW0, (unsigned long long)g_lastDlW1,
        (void*)g_lastDlCmd, (void*)g_diag_current_cmd_addr,
        (void*)g_diag_tex_addr, g_diag_tex_size_bytes, g_diag_tex_needed,
        g_diag_tex_fmt, g_diag_tex_siz, g_diag_tex_slot, g_diag_tex_tile);
    pc_diag_write_stderr(diag, dlen);
}

static void crashHandler(int sig) {
#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
    /* If we're in DL processing and recovery is available, skip this frame.
     * POSIX-only: longjmp'ing out of a Windows signal handler unwinds across
     * the CRT's SEH dispatch frame, which is undefined behavior there (the
     * recovery gate in gfx_run_dl() is likewise forced off on _WIN32). */
    /* In debug/investigation builds, allow many recoveries to gather data.
     * In release, cap low — a build that needs recovery is not green.
     * CMake defines NDEBUG in Release builds; its absence means debug. */
#ifdef NDEBUG
#define MAX_CRASH_RECOVERY 10
#else
#define MAX_CRASH_RECOVERY 10000
#endif
    if (g_gfxRecoveryActive && g_crashRecoveryCount < MAX_CRASH_RECOVERY) {
        g_crashRecoveryCount++;
        extern volatile uintptr_t g_lastDlCmd;
        extern volatile uint32_t g_lastDlOpcode, g_lastDlW0;
        extern volatile uintptr_t g_lastDlW1;
        extern volatile uintptr_t g_diag_tex_addr;
        extern volatile uint32_t g_diag_tex_size_bytes, g_diag_tex_needed;
        extern volatile uint8_t g_diag_tex_fmt, g_diag_tex_siz, g_diag_tex_slot, g_diag_tex_tile;
        char msg[512];
        int n = snprintf(msg, sizeof(msg),
            "[GFX-RECOVER] sig=%d #%d op=0x%02X w0=0x%08X w1=0x%llX cmd=%p"
            " tex_addr=%p tex_sz=%u tex_need=%u tex_fmt=%u tex_siz=%u tex_slot=%u tex_tile=%u\n",
            sig, g_crashRecoveryCount,
            g_lastDlOpcode, g_lastDlW0, (unsigned long long)g_lastDlW1,
            (void*)g_lastDlCmd,
            (void*)g_diag_tex_addr, g_diag_tex_size_bytes, g_diag_tex_needed,
            g_diag_tex_fmt, g_diag_tex_siz, g_diag_tex_slot, g_diag_tex_tile);
        pc_diag_write_stderr(msg, n);
        g_gfxRecoveryActive = 0;
        siglongjmp(g_gfxRecoveryJmp, 1);
    }
#endif /* !_WIN32 */
    {
        char head[64];
        int hlen = snprintf(head, sizeof(head),
            "\n[CRASH] Signal %d (unrecoverable)\n", sig);
        pc_diag_write_stderr(head, hlen);
        crashWriteDlTexDiag();
    }
#ifdef PORT_HAVE_BACKTRACE
    void *bt[32];
    int n = backtrace(bt, 32);
    backtrace_symbols_fd(bt, n, STDERR_FILENO);
#endif
    _exit(1);
}

#ifdef _WIN32
/* Hardware faults (access violation, illegal instruction, ...) on Windows
 * arrive as SEH exceptions, not signals; msvcrt/UCRT signal(SIGSEGV) is a
 * thin translation over the same mechanism with sharp UB edges (longjmp out
 * of it unwinds across the SEH dispatch frame). Take them at the SEH layer
 * instead: write the same [CRASH] diagnostics and terminate. */
static LONG WINAPI crashSehFilter(EXCEPTION_POINTERS *info) {
    char head[128];
    int hlen = snprintf(head, sizeof(head),
        "\n[CRASH] Unhandled exception 0x%08lX at %p (unrecoverable)\n",
        (unsigned long)info->ExceptionRecord->ExceptionCode,
        (void *)info->ExceptionRecord->ExceptionAddress);
    pc_diag_write_stderr(head, hlen);
    crashWriteDlTexDiag();
    /* Not _exit(): the CRT routes that through ExitProcess, which takes the
     * loader lock and runs DLL_PROCESS_DETACH callbacks — machinery that may
     * be exactly what's corrupt/held in a crashed process. TerminateProcess
     * skips all of it. */
    TerminateProcess(GetCurrentProcess(), 1);
    return EXCEPTION_EXECUTE_HANDLER; /* unreachable */
}
#endif /* _WIN32 */

#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
/* Alternate signal stack: without sigaltstack()+SA_ONSTACK, a SIGSEGV caused
 * by a stack overflow re-faults immediately on handler entry (no stack space
 * left to push a frame) and the process dies silently with none of
 * crashHandler()'s [CRASH] diagnostics. 64KB comfortably covers
 * crashHandler()'s largest locals (a couple of ~512-768 byte stack buffers).
 * POSIX-only (sigaltstack/sigaction/siginfo_t don't exist on native
 * Windows) -- Windows uses the SEH filter above plus signal(SIGABRT), and
 * has no altstack analog: a stack-overflow fault there dies without
 * diagnostics (recorded in docs/WINDOWS_CONFIDENCE.md). */
#define PORT_ALTSTACK_SIZE (64 * 1024)
static unsigned char s_crashAltStack[PORT_ALTSTACK_SIZE];

/* Set once in main() after sigaltstack() is attempted; gates whether
 * installCrashSignalHandler() asks for SA_ONSTACK. If sigaltstack() itself
 * failed, requesting SA_ONSTACK anyway would be requesting a stack swap to
 * an alternate stack the kernel never registered -- better to fall back to
 * handling the signal on the normal stack (still gets [CRASH] diagnostics
 * for every crash except an actual stack-overflow SIGSEGV) than to leave
 * that undefined. */
static int s_altstackReady = 0;

/* sigaction() with SA_SIGINFO requires a 3-arg handler (it changes which
 * union member the kernel populates); crashHandler() only needs `sig`, so
 * just forward. */
static void crashHandlerSigaction(int sig, siginfo_t *info, void *ucontext) {
    (void)info;
    (void)ucontext;
    crashHandler(sig);
}

static void installCrashSignalHandler(int sig) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = crashHandlerSigaction;
    sa.sa_flags = SA_SIGINFO | (s_altstackReady ? SA_ONSTACK : 0);
    sigemptyset(&sa.sa_mask);
    sigaction(sig, &sa, NULL);
}
#elif defined(__EMSCRIPTEN__)
/* No POSIX signals in wasm; crash recovery is the browser tab itself. The
 * altstack/sigaction machinery above is a native crash-diagnostic layer with
 * no analog (and no runtime backing) under single-threaded Emscripten. */
static void installCrashSignalHandler(int sig) { (void)sig; }
#endif /* !_WIN32 && !__EMSCRIPTEN__ */

/* Game entry — defined in boss.c */
extern void bossEntry(void);

/* Scheduler init — called by mainproc() on N64, we call it explicitly on PC.
 * Creates gfxFrameMsgQ, initializes the scheduler, registers the gfx client. */
extern void schedulerInitThread(void);

/* SDL platform — defined in platform_sdl.c */
extern int  platformInitSDL(void);
extern int  platformPrintDisplays(FILE *f);
extern void platformShutdownSDL(void);
extern void platformSetScreenshotLabel(const char *label);
extern void pcSetTraceStatePath(const char *path);

/* GBI translator — defined in gfx_pc.c */
extern void gfx_init(void);

/* Audio backend — defined in audio_pc.c */
extern void portAudioInit(void);
extern void portAudioRegisterConfig(void);

/* Config system — defined in config_pc.c */
extern void configInit(void);

/* Platform config — defined in platform_sdl.c */
extern void platformRegisterConfig(void);
extern int platformApplyFaithfulPreset(void);
extern int platformApplyRemasterPreset(void);
extern int platformApplyFaithfulHdPreset(void);

/* Default ROM path — can be overridden via command line */
static const char *DEFAULT_ROM_PATHS[] = {
    "baserom.u.z64",
    "GoldenEye 007 (USA).z64",
    "ge007.z64",
    "goldeneye.z64",
    "../GoldenEye 007 (USA).z64",
    "../baserom.u.z64",
    "../../GoldenEye 007 (USA).z64",
    NULL
};

/* N64 ROM size for all GoldenEye regions (0xC00000). */
#define GE_ROM_SIZE 12582912L

/* Heuristic: does `path` look like a GoldenEye .z64 ROM? Checks size, the
 * big-endian N64 bootstrap magic, and the "GOLDENEYE" internal cartridge name.
 * No SHA-1 needed — this is just for zero-config auto-detection; an explicit
 * --rom always bypasses it. Avoids auto-loading some unrelated N64 ROM. */
static int looksLikeGoldeneyeRom(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return 0; }
    long sz = ftell(f);
    if (sz != GE_ROM_SIZE) { fclose(f); return 0; }
    unsigned char hdr[0x40];
    rewind(f);
    size_t n = fread(hdr, 1, sizeof(hdr), f);
    fclose(f);
    if (n != sizeof(hdr)) return 0;
    if (!(hdr[0] == 0x80 && hdr[1] == 0x37 && hdr[2] == 0x12 && hdr[3] == 0x40))
        return 0; /* not a big-endian (.z64) N64 ROM */
    for (int i = 0x20; i + 9 <= 0x34; i++) {
        if (memcmp(&hdr[i], "GOLDENEYE", 9) == 0) return 1;
    }
    return 0;
}

/* Scan one directory for a GoldenEye ROM; on first match writes the full path
 * to `out` and returns 1. */
static int scanDirForRom(const char *dir, char *out, size_t outsz) {
    if (!dir || !*dir) return 0;
    DIR *d = opendir(dir);
    if (!d) return 0;
    struct dirent *e;
    int found = 0;
    while ((e = readdir(d)) != NULL) {
        size_t len = strlen(e->d_name);
        if (len < 5) continue;
        const char *ext = e->d_name + len - 4;
        if (strcasecmp(ext, ".z64") != 0 && strcasecmp(ext, ".n64") != 0 &&
            strcasecmp(ext, ".v64") != 0)
            continue;
        char cand[1100];
        snprintf(cand, sizeof(cand), "%s/%s", dir, e->d_name);
        if (looksLikeGoldeneyeRom(cand)) {
            snprintf(out, outsz, "%s", cand);
            found = 1;
            break;
        }
    }
    closedir(d);
    return found;
}

static const char *findRomFile(void) {
    /* 1) Known names next to the executable / in parent dirs (fast path). */
    for (int i = 0; DEFAULT_ROM_PATHS[i]; i++) {
        FILE *f = fopen(DEFAULT_ROM_PATHS[i], "rb");
        if (f) {
            fclose(f);
            return DEFAULT_ROM_PATHS[i];
        }
    }
    /* 2) Zero-config auto-detect: scan common locations for a GoldenEye ROM. */
    static char s_found[1100];
    if (scanDirForRom(".", s_found, sizeof(s_found))) {
        printf("[ROM] Auto-detected ROM: %s\n", s_found);
        return s_found;
    }
    const char *home = getenv("HOME");
    if (home && *home) {
        const char *rel[] = { "Downloads", "Documents", "Desktop", NULL };
        char dir[1100];
        for (int i = 0; rel[i]; i++) {
            snprintf(dir, sizeof(dir), "%s/%s", home, rel[i]);
            if (scanDirForRom(dir, s_found, sizeof(s_found))) {
                printf("[ROM] Auto-detected ROM: %s\n", s_found);
                return s_found;
            }
        }
        if (scanDirForRom(home, s_found, sizeof(s_found))) {
            printf("[ROM] Auto-detected ROM: %s\n", s_found);
            return s_found;
        }
    }
    return NULL;
}

/* When building as a macOS app bundle, the Swift AppDelegate owns the entry
 * point and calls game_init()/game_run() via GameBridge.h instead.
 *
 * When building the portable ImGui app shell (MGB64_APP), the shell's main()
 * (src/app/main_app.cpp) owns the entry point; this body becomes the reusable
 * mgb64_headless_main() that the shell delegates to for automation/CLI
 * invocations, and that mgb64_engine_boot() will be extracted from (Task 5).
 * The body is unchanged either way, so the automation path stays byte-identical. */
#ifndef MACOS_APP_BUNDLE
#ifdef MGB64_APP
int mgb64_headless_main(int argc, char **argv)
#else
int main(int argc, char **argv)
#endif
{
    const char *romPath = NULL;
    const char *saveDirOverride = NULL;
    const char *configOverrideArgs[PC_MAX_CONFIG_SET_ARGS];
    const char *configSetArgs[PC_MAX_CONFIG_SET_ARGS];
    int configOverrideCount = 0;
    int configSetCount = 0;
    int resetConfig = 0;
    int faithful = 0;
    int remaster = 0;
    int faithfulHd = 0;
    int listSettings = 0;
    int listDisplays = 0;
    int dumpConfig = 0;
    extern int g_autoScreenshotExit;
    extern const char *g_traceStatePath;

    /* First thing in main(): tell SDL we're handling the platform entry
     * point ourselves (paired with -DSDL_MAIN_HANDLED, see CMakeLists.txt).
     * Required by SDL's docs whenever SDL_MAIN_HANDLED is defined. */
    SDL_SetMainReady();

    /* Force line-buffered stdout so crash diagnostics appear */
    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(stderr, NULL, _IOLBF, 0);
    if (!getenv("GE007_NO_CRASH_HANDLER")) {  /* leave signals raw for debuggers */
#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
        stack_t ss;
        ss.ss_sp = s_crashAltStack;
        ss.ss_size = PORT_ALTSTACK_SIZE;
        ss.ss_flags = 0;
        if (sigaltstack(&ss, NULL) == 0) {
            s_altstackReady = 1;
        } else {
            fprintf(stderr,
                    "[CRASH] sigaltstack() failed (errno=%d: %s); installing crash "
                    "handlers without SA_ONSTACK. A stack-overflow SIGSEGV may still "
                    "re-fault silently without a [CRASH] diagnostic; all other crashes "
                    "are unaffected.\n",
                    errno, strerror(errno));
        }
#ifdef __GLIBC__
        /* glibc's backtrace()/backtrace_symbols_fd() lazily resolve symbols
         * (dlopen-ish work) on their first call; doing that for the first
         * time from inside a signal handler that fired because of e.g. a
         * stack overflow risks a second fault. Warm it up once here, outside
         * any signal context, so the crashHandler() call at the bottom of
         * this file is safe. */
        { void *warm[1]; backtrace(warm, 1); }
#endif
        installCrashSignalHandler(SIGSEGV);
#if defined(SIGBUS) && !defined(__SANITIZE_ADDRESS__) && !__has_feature(address_sanitizer)
        /* ASAN uses SIGBUS internally for shadow memory probing.
         * Don't intercept it in sanitizer builds. SIGBUS doesn't exist on
         * Windows. */
        installCrashSignalHandler(SIGBUS);
#endif
        installCrashSignalHandler(SIGABRT);
#elif defined(_WIN32)
        /* Hardware faults go through the SEH filter (see crashSehFilter);
         * abort() never raises an SEH exception, so keep a signal handler
         * for SIGABRT (assert failures, CRT aborts). */
        SetUnhandledExceptionFilter(crashSehFilter);
        signal(SIGABRT, crashHandler);
#else
        /* __EMSCRIPTEN__: no POSIX signal delivery and no SEH; a wasm trap
         * unwinds to the JS host, which surfaces it in the devtools console.
         * Nothing to install — the browser tab is the crash boundary. */
#endif
    }

    /* Parse command line */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--rom") == 0 && i + 1 < argc) {
            romPath = argv[++i];
        } else if (strcmp(argv[i], "--list-settings") == 0) {
            listSettings = 1;
        } else if (strcmp(argv[i], "--list-displays") == 0) {
            listDisplays = 1;
        } else if (strcmp(argv[i], "--dump-config") == 0) {
            dumpConfig = 1;
        } else if (strcmp(argv[i], "--reset-config") == 0) {
            resetConfig = 1;
        } else if (strcmp(argv[i], "--faithful") == 0) {
            faithful = 1;
        } else if (strcmp(argv[i], "--faithful-hd") == 0) {
            faithfulHd = 1;
        } else if (strcmp(argv[i], "--remaster") == 0) {
            remaster = 1;
            /* Historically this pinned GE007_RENDERER=metal on Apple: the full
             * remaster enables SSAO, which op-hangs Apple's GL-over-Metal
             * translator, and Metal was the only backend with working SSAO.
             * WebGPU (the default backend everywhere, including macOS) now
             * carries the full remaster output post-FX chain and SSAO at
             * verified GL parity (487a4b3/81344c4/935e4bc), so --remaster no
             * longer needs to force a backend — it runs on whatever backend
             * is already selected (WebGPU by default). Metal remains a valid
             * explicit choice via GE007_RENDERER=metal until Phase M
             * (docs/BACKEND_DEPRECATION_PLAN.md) removes it. */
        } else if (strcmp(argv[i], "--config-override") == 0 && i + 1 < argc) {
            if (configOverrideCount >= PC_MAX_CONFIG_SET_ARGS) {
                fprintf(stderr, "[CONFIG] Too many --config-override values; max is %d.\n", PC_MAX_CONFIG_SET_ARGS);
                return 2;
            }
            configOverrideArgs[configOverrideCount++] = argv[++i];
        } else if (strcmp(argv[i], "--config-set") == 0 && i + 1 < argc) {
            if (configSetCount >= PC_MAX_CONFIG_SET_ARGS) {
                fprintf(stderr, "[CONFIG] Too many --config-set values; max is %d.\n", PC_MAX_CONFIG_SET_ARGS);
                return 2;
            }
            configSetArgs[configSetCount++] = argv[++i];
        } else if (strcmp(argv[i], "--background") == 0) {
            setenv("GE007_BACKGROUND", "1", 1);
            setenv("GE007_NO_INPUT_GRAB", "1", 1);
        } else if (strcmp(argv[i], "--no-input-grab") == 0) {
            setenv("GE007_NO_INPUT_GRAB", "1", 1);
        } else if (strcmp(argv[i], "--unlock-all-levels") == 0) {
            /* Demo/showcase: mission-select shows every solo stage (file2.c
             * fileUnlockAllLevelsHatch). Menu availability only — the save
             * file and sim are untouched. Exposed by the web shell toggle. */
            setenv("GE007_UNLOCK_ALL_LEVELS", "1", 1);
        } else if (strcmp(argv[i], "--screenshot-frame") == 0 && i + 1 < argc) {
            extern int g_autoScreenshotFrame;
            extern int g_screenshotFrameSessionActive;
            g_autoScreenshotFrame = atoi(argv[++i]);
            g_screenshotFrameSessionActive = 1;
        } else if (strcmp(argv[i], "--screenshot-game-timer") == 0 && i + 1 < argc) {
            extern int g_autoScreenshotGameTimer;
            g_autoScreenshotGameTimer = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--screenshot-exit") == 0) {
            g_autoScreenshotExit = 1;
        } else if (strcmp(argv[i], "--screenshot-label") == 0 && i + 1 < argc) {
            platformSetScreenshotLabel(argv[++i]);
        } else if (strcmp(argv[i], "--freeze-input") == 0) {
            extern int g_freezeInput;
            g_freezeInput = 1;
        } else if (strcmp(argv[i], "--deterministic") == 0) {
            extern int g_deterministic;
            extern int g_freezeInput;
            g_deterministic = 1;
            g_freezeInput = 1; /* deterministic implies frozen input */
        } else if (strcmp(argv[i], "--trace-state") == 0 && i + 1 < argc) {
            pcSetTraceStatePath(argv[++i]);
        } else if (strcmp(argv[i], "--sim-state-hash-out") == 0 && i + 1 < argc) {
            g_simStateHashOut = argv[++i];
        } else if (strcmp(argv[i], "--record-tape") == 0 && i + 1 < argc) {
            /* Byte-exact input capture (FID-0034). Forces the determinism
             * envelope so the recorded seed/clock are the fixed ones a replay
             * will reproduce. Requires --level and refuses --ramrom (validated
             * post-parse). */
            extern int g_deterministic;
            extern int g_freezeInput;
            inputTapeConfigureRecord(argv[++i]);
            g_deterministic = 1;
            g_freezeInput = 1;
        } else if (strcmp(argv[i], "--play-tape") == 0 && i + 1 < argc) {
            extern int g_deterministic;
            extern int g_freezeInput;
            inputTapeConfigurePlayback(argv[++i]);
            g_deterministic = 1;
            g_freezeInput = 1;
        } else if (strcmp(argv[i], "--print-sim-hash-regions") == 0) {
            /* Emit the sim-state-hash region table (`name base size`, one per
             * line) and exit. ROM-free: simHashRegistryBuild() only reads the
             * static region bases (g_ClockTimer/g_GlobalTimer/pos_data_entry)
             * plus the not-yet-allocated pool base (0x0/0 pre-init). Consumed by
             * tools/fidelity/hash_coverage_audit.py to cross-reference every
             * writable decomp symbol against the hashed regions (FID-0030). */
            SimHashRegion regs[SIM_HASH_MAX_REGIONS];
            int nregs = 0;
            simHashRegistryBuild(regs, &nregs);
            for (int r = 0; r < nregs; r++) {
                printf("%s %p %zu\n", regs[r].name, regs[r].base, regs[r].size);
            }
            return 0;
        } else if (strcmp(argv[i], "--mission") == 0 && i + 1 < argc) {
            int mission = 0;
            const PcStartStage *stage;
            if (!pcParseIntArg(argv[++i], &mission) || !(stage = pcFindStageByMission(mission))) {
                fprintf(stderr, "[GE007-PC] Invalid --mission value. Use 1-20 for the solo campaign.\n");
                return 2;
            }
            g_pcStartLevel = stage->level_id;
        } else if (strcmp(argv[i], "--level") == 0 && i + 1 < argc) {
            const char *arg = argv[++i];
            const PcStartStage *stage = pcFindStageByName(arg);
            int raw_level_id = 0;

            if (stage != NULL) {
                g_pcStartLevel = stage->level_id;
            } else if (pcParseIntArg(arg, &raw_level_id)) {
                stage = pcFindStageByLevelId(raw_level_id);
                if (!stage) {
                    const PcStartStage *mission = pcFindStageByMission(raw_level_id);
                    if (mission) {
                        fprintf(stderr,
                                "[GE007-PC] --level %d is a raw LEVELID, not solo mission %d.\n"
                                "[GE007-PC] If you meant %s, use --mission %d or --level %s (raw LEVELID %d).\n",
                                raw_level_id, raw_level_id,
                                mission->name, mission->mission_num, mission->slug, mission->level_id);
                    } else {
                        fprintf(stderr,
                                "[GE007-PC] Raw LEVELID %d is not a supported direct-boot solo mission.\n"
                                "[GE007-PC] Use --stage-id %d to force an internal stage id.\n",
                                raw_level_id, raw_level_id);
                    }
                    return 2;
                }
                g_pcStartLevel = raw_level_id;
            } else {
                fprintf(stderr, "[GE007-PC] Unknown level '%s'. Use a solo stage name like 'facility' or a raw LEVELID like 34.\n", arg);
                return 2;
            }
        } else if (strcmp(argv[i], "--stage-id") == 0 && i + 1 < argc) {
            g_pcStartLevel = atoi(argv[++i]);
            g_pcStartLevelForcedRaw = 1;
        } else if (strcmp(argv[i], "--difficulty") == 0 && i + 1 < argc) {
            if (!pcParseDifficultyArg(argv[++i], &g_pcStartDifficulty)) {
                fprintf(stderr, "[GE007-PC] Invalid --difficulty value. Use agent, secret, 00, 007, or 0-3.\n");
                return 2;
            }
        } else if (strcmp(argv[i], "--ramrom") == 0 && i + 1 < argc) {
            g_pcStartRamrom = argv[++i];
            if (!pcRamromReplayNameIsValid(g_pcStartRamrom)) {
                fprintf(stderr,
                        "[GE007-PC] Invalid --ramrom value '%s'. Use a built-in symbol like ramrom_Facility_3 or an alias like facility3.\n",
                        g_pcStartRamrom);
                return 2;
            }
            g_pcStartLevel = -1;
        } else if (strcmp(argv[i], "--savedir") == 0 && i + 1 < argc) {
            saveDirOverride = argv[++i];
        } else if (strcmp(argv[i], "--multiplayer") == 0) {
            g_pcStartMultiplayer = 1;
        } else if (strcmp(argv[i], "--players") == 0 && i + 1 < argc) {
            int players = 0;
            if (!pcParseIntArg(argv[++i], &players) || players < 2 || players > 4) {
                fprintf(stderr, "[GE007-PC] Invalid --players value. Use 2, 3, or 4.\n");
                return 2;
            }
            g_pcStartMpPlayers = players;
            g_pcStartMultiplayer = 1;
        } else if (strcmp(argv[i], "--mp-stage") == 0 && i + 1 < argc) {
            if (!pcParseMpStageArg(argv[++i], &g_pcStartMpStage)) {
                fprintf(stderr,
                        "[GE007-PC] Unknown --mp-stage. Use a stage name like 'temple' or 'complex',\n"
                        "[GE007-PC] or a raw MP_STAGE index (1-%d).\n",
                        MP_STAGE_SELECTED_MAX - 1);
                return 2;
            }
            g_pcStartMultiplayer = 1;
        } else if (strcmp(argv[i], "--scenario") == 0 && i + 1 < argc) {
            if (!pcParseMpScenarioArg(argv[++i], &g_pcStartMpScenario)) {
                fprintf(stderr,
                        "[GE007-PC] Unknown --scenario. Use 'normal' (combat deathmatch), 'yolt',\n"
                        "[GE007-PC] 'flagtag', 'goldengun', 'ltk', '2v2', '3v1', or '2v1'.\n");
                return 2;
            }
            g_pcStartMultiplayer = 1;
        } else if (strcmp(argv[i], "--mp-timelimit") == 0 && i + 1 < argc) {
            int secs = 0;
            if (!pcParseIntArg(argv[++i], &secs) || secs < 1 || secs > 3600) {
                fprintf(stderr, "[GE007-PC] Invalid --mp-timelimit value. Use seconds, 1-3600.\n");
                return 2;
            }
            g_pcStartMpTimeLimitSecs = secs;
            g_pcStartMultiplayer = 1;
        } else if (argv[i][0] != '-') {
            romPath = argv[i];
        }
    }

    /* Input-tape semantics (FID-0034): record and playback are mutually
     * exclusive, require a direct --level boot, and refuse --ramrom (the tape
     * IS the input stream — a demo would double-drive the seam). */
    if (inputTapeIsRecordingRequested() && inputTapeIsPlaybackRequested()) {
        fprintf(stderr, "[GE007-PC] --record-tape and --play-tape are mutually exclusive.\n");
        return 2;
    }
    if (inputTapeIsRecordingRequested() || inputTapeIsPlaybackRequested()) {
        if (g_pcStartRamrom != NULL) {
            fprintf(stderr, "[GE007-PC] input tape and --ramrom cannot be combined.\n");
            return 2;
        }
        if (g_pcStartLevel < 0) {
            fprintf(stderr, "[GE007-PC] --record-tape/--play-tape require --level (direct boot).\n");
            return 2;
        }
        inputTapeSetSessionParams(g_pcStartLevel, g_pcStartDifficulty);
    }

    if ((g_autoScreenshotExit || g_traceStatePath != NULL)
        && getenv("GE007_BACKGROUND") == NULL
        && getenv("GE007_NO_INPUT_GRAB") == NULL
        && getenv("GE007_SHOW_AUTOMATION_WINDOW") == NULL) {
        setenv("GE007_BACKGROUND", "1", 1);
        setenv("GE007_NO_INPUT_GRAB", "1", 1);
    }

    if (!romPath && !listSettings && !listDisplays && !dumpConfig && !resetConfig && configSetCount == 0) {
        romPath = findRomFile();
    }

    /* Phase 1: Initialize save directory (before anything that reads/writes files).
     * Fall back to MGB64_APP_SAVEDIR when no explicit --savedir was given, so the
     * bare engine honors the same save-dir contract the app shell / PortMaster
     * launcher export (AUDIT-0033) — an explicit --savedir still wins. */
    if (!saveDirOverride) {
        const char *envSaveDir = getenv("MGB64_APP_SAVEDIR");
        if (envSaveDir && envSaveDir[0]) {
            saveDirOverride = envSaveDir;
        }
    }
    /* An explicit/env --savedir that can't be created or written is a fatal user
     * error, not something to accept and then fail every save against
     * (AUDIT-0054). Exit nonzero before touching config/eeprom. */
    if (savedirInit(saveDirOverride) != 0) {
        fprintf(stderr, "[GE007-PC] Save directory '%s' is unusable; aborting.\n",
                saveDirOverride ? saveDirOverride : "(auto)");
        return 1;
    }

    /* Phase 1b: Config system — register settings, then load ge007.ini */
    platformRegisterConfig();
    portAudioRegisterConfig();
    configInit();

    /* Load user key bindings; automation/deterministic runs force the canonical
     * defaults so scripted input stays byte-identical regardless of any file. */
    inputBindingLoad();
    gamepadBindingLoad();
    {
        extern int g_deterministic;
        extern int g_freezeInput;
        if (g_deterministic || g_freezeInput) {
            inputBindingForceDefaults(1);
            gamepadBindingForceDefaults(1);
        }
    }

    /* --faithful: apply the "Faithful original" preset (VISUAL_MODES.md section 1)
     * as a transient baseline BEFORE env/CLI overrides, so any explicit env var or
     * --config-override still wins and `--faithful --dump-config` reflects the
     * faithful values. Marked SETTING_OVERRIDE_FAITHFUL so configSave never
     * persists it -- the user's saved remaster ge007.ini is left untouched. */
    if (faithful + remaster + faithfulHd > 1) {
        fprintf(stderr, "[CONFIG] --faithful, --faithful-hd, and --remaster are mutually exclusive.\n");
        return 2;
    }
    if (faithful) {
        int n = platformApplyFaithfulPreset();
        /* A faithful session is read-only for config: suppress every save path
         * (clean shutdown, in-game menu, --config-set/--reset-config) so the
         * user's saved remaster ge007.ini is left byte-for-byte untouched. */
        configSetSaveSuppressed(1);
        printf("[CONFIG] --faithful: pinned %d setting(s) to the pre-remaster baseline "
               "(RemasterFX off, native res, stock textures, classic FOV, no modern "
               "crosshair/hitmarkers, vanilla pad aim, minimap off). Read-only session: ge007.ini is not modified.\n",
               n);

        /* §4.1: this is the ONE place g_pcFaithfulSim is set. It flips the
         * default (not override -- an explicit GE007_* still wins in either
         * direction) of the port-added visibility/physics wideners and the
         * §4.4 sim one-offs that share their class, so --faithful is faithful
         * in *sim behavior*, not just in look. One boot-log line reports the
         * resolved state of each so a `--faithful` run's log is directly
         * verifiable. */
        {
            extern void bgGetFaithfulWidenerStates(s32 *edgeRescue, s32 *frustumFallback, s32 *visSupplement);
            extern void bondviewGetFaithfulWidenerStates(s32 *doorFloorCollision, s32 *floorReattach,
                                                          s32 *renderCameraClearance, s32 *portalSeedRefresh);
            extern s32 chrHatHitListEnabled(void);
            extern s32 bheadGaitClampEnabled(void);
            extern s32 chrPropNeighborVisibilityEnabled(void);
            s32 edgeRescue, frustumFallback, visSupplement;
            s32 doorFloorCollision, floorReattach, renderCameraClearance, portalSeedRefresh;

            g_pcFaithfulSim = 1;

            bgGetFaithfulWidenerStates(&edgeRescue, &frustumFallback, &visSupplement);
            bondviewGetFaithfulWidenerStates(&doorFloorCollision, &floorReattach,
                                              &renderCameraClearance, &portalSeedRefresh);

            printf("[CONFIG] --faithful: widener states -- "
                   "PortalEdgeRescue=%d PortalProjectFrustumFallback=%d VisSupplement=%d "
                   "DoorFloorCollision=%d FloorReattach=%d RenderCameraClearance=%d "
                   "PortalSeedRefresh=%d HatHitList=%d BondheadGaitClamp=%d "
                   "PropNeighborVisibility=%d\n",
                   edgeRescue, frustumFallback, visSupplement,
                   doorFloorCollision, floorReattach, renderCameraClearance,
                   portalSeedRefresh, chrHatHitListEnabled(), bheadGaitClampEnabled(),
                   chrPropNeighborVisibilityEnabled());
        }
    }
    if (remaster) {
        /* --remaster: the full "immaculate" remaster in one switch — all post-FX
         * on INCLUDING SSAO. Runs on whatever backend is already selected
         * (WebGPU by default, everywhere including macOS — SSAO/post-FX reach
         * verified GL parity there now; no backend pin needed, see
         * docs/BACKEND_DEPRECATION_PLAN.md). Applied transiently before env/CLI
         * overrides so an explicit --config-override still wins; not persisted
         * to ge007.ini. */
        int n = platformApplyRemasterPreset();
        /* Read-only session (like --faithful): suppress every save path so the
         * launch-only preset is NEVER persisted. Critical — without this,
         * `--remaster --config-set ...` would write Video.Ssao=1 into ge007.ini,
         * and a later plain GL launch would reload it and re-trigger the
         * GL-over-Metal SSAO op-hang the WebGPU/Metal path avoids. */
        configSetSaveSuppressed(1);
        printf("[CONFIG] --remaster: enabled %d remaster setting(s) (RemasterFX + SSAO + "
               "bloom/FXAA/tonemap/grade/vignette/sharpen/dither, 2x SSAA). "
               "Launch-only preset; env/--config-override still win. ge007.ini not modified.\n",
               n);
    }
    if (faithfulHd) {
        /* --faithful-hd: the faithful original look at 2x SSAA, HD-pack-ready
         * (VISUAL_MODES.md mode 2). No post-FX / no SSAO, so no Metal backend
         * needed. Applied transiently before env/CLI overrides; not persisted. */
        int n = platformApplyFaithfulHdPreset();
        configSetSaveSuppressed(1);

        /* §4.1 follow-up: --faithful-hd pins the same faithful SIM baseline as
         * --faithful (classic FOV/aim/crosshair, no modern wideners) -- it only
         * adds 2x SSAA and leaves the texture pack open for an HD replacement.
         * It must therefore also flip g_pcFaithfulSim, or a --faithful-hd run
         * would silently keep the port-added visibility/physics wideners on
         * while claiming a faithful preset. Same boot-log shape as --faithful. */
        {
            extern void bgGetFaithfulWidenerStates(s32 *edgeRescue, s32 *frustumFallback, s32 *visSupplement);
            extern void bondviewGetFaithfulWidenerStates(s32 *doorFloorCollision, s32 *floorReattach,
                                                          s32 *renderCameraClearance, s32 *portalSeedRefresh);
            extern s32 chrHatHitListEnabled(void);
            extern s32 bheadGaitClampEnabled(void);
            extern s32 chrPropNeighborVisibilityEnabled(void);
            s32 edgeRescue, frustumFallback, visSupplement;
            s32 doorFloorCollision, floorReattach, renderCameraClearance, portalSeedRefresh;

            g_pcFaithfulSim = 1;

            bgGetFaithfulWidenerStates(&edgeRescue, &frustumFallback, &visSupplement);
            bondviewGetFaithfulWidenerStates(&doorFloorCollision, &floorReattach,
                                              &renderCameraClearance, &portalSeedRefresh);

            printf("[CONFIG] --faithful-hd: pinned %d setting(s) to the faithful look at 2x SSAA "
                   "(post-FX off, classic FOV/crosshair/aim). Supply Video.TexturePack "
                   "(GE007_TEXTURE_PACK=... or --config-override Video.TexturePack=...) for HD "
                   "textures. Read-only session: ge007.ini is not modified.\n", n);
            printf("[CONFIG] --faithful-hd: widener states -- "
                   "PortalEdgeRescue=%d PortalProjectFrustumFallback=%d VisSupplement=%d "
                   "DoorFloorCollision=%d FloorReattach=%d RenderCameraClearance=%d "
                   "PortalSeedRefresh=%d HatHitList=%d BondheadGaitClamp=%d "
                   "PropNeighborVisibility=%d\n",
                   edgeRescue, frustumFallback, visSupplement,
                   doorFloorCollision, floorReattach, renderCameraClearance,
                   portalSeedRefresh, chrHatHitListEnabled(), bheadGaitClampEnabled(),
                   chrPropNeighborVisibilityEnabled());
        }
    }

    settingsApplyEnvOverrides();
    for (int i = 0; i < configOverrideCount; i++) {
        if (!pcApplyConfigArg(configOverrideArgs[i], 1)) {
            return 2;
        }
    }
    if (resetConfig) {
        settingsResetAllToDefaults();
    }
    for (int i = 0; i < configSetCount; i++) {
        if (!pcApplyConfigArg(configSetArgs[i], 0)) {
            return 2;
        }
    }
    if (resetConfig || configSetCount > 0) {
        if (faithful || remaster || faithfulHd) {
            printf("[CONFIG] --%s is a read-only session; --config-set/--reset-config "
                   "was applied to this run only and NOT written to ge007.ini.\n",
                   faithful ? "faithful" : (remaster ? "remaster" : "faithful-hd"));
        } else if (!configSave()) {
            return 1;
        }
    }

    /* §4.2: pin Video.FovY to its registered default under --deterministic.
     * FovY writes hashed sim state (bondviewGetNativeBaseFovY ->
     * g_CurrentPlayer->fovy) and gates frustum-visibility RNG (monitor
     * microcode only rolls for frustum-visible monitors), so an un-pinned
     * FovY silently breaks sim-hash reproducibility across installs/configs
     * with different ge007.ini values -- exactly the class of divergence
     * --deterministic exists to rule out. Runs after config load/persist
     * above, so a `--config-set Video.FovY=...` value is still written to
     * ge007.ini untouched; only the in-memory value used by *this*
     * deterministic run is forced back to default.
     *
     * An explicit per-launch request (--config-override / GE007_FOVY env,
     * override_source CLI/ENV) still wins -- same "explicit override always
     * wins" rule as the §4.1 wideners. This matters for tooling that
     * deliberately pairs --deterministic with a specific --config-override
     * FovY (e.g. tools/movement_oracle_capture.sh's route-oracle captures,
     * which must apply exactly the config they ask for). A --faithful /
     * --faithful-hd preset's FovY=60 (override_source FAITHFUL) is likewise
     * exempt: the whole point of that preset is a fixed, cross-install
     * constant (not per-install ini drift), so it is already exactly as
     * reproducible as the registered default -- pinning over it would
     * silently discard the faithful FOV under --faithful --deterministic.
     * Only a value that arrived un-requested for *this* run -- a plain
     * ge007.ini value or the built-in default -- gets forced back to the
     * registered default (50) here, closing the reproducibility gap without
     * breaking deliberate per-run overrides or the faithful preset. */
    {
        extern int g_deterministic;
        if (g_deterministic) {
            /* Video.CutsceneFovY (T16) feeds the player FOV during cinematic
             * camera modes, same hashed-sim-state path as Video.FovY, so it is
             * pinned by the identical rule. */
            const char *pinFovKeys[] = { "Video.FovY", "Video.CutsceneFovY" };
            for (size_t fk = 0; fk < sizeof(pinFovKeys) / sizeof(pinFovKeys[0]); fk++) {
                const Setting *fovySetting = settingsFind(pinFovKeys[fk]);
                if (fovySetting != NULL && fovySetting->ptr != NULL && fovySetting->type == SETTING_TYPE_FLOAT
                    && fovySetting->override_source != SETTING_OVERRIDE_CLI
                    && fovySetting->override_source != SETTING_OVERRIDE_ENV
                    && fovySetting->override_source != SETTING_OVERRIDE_FAITHFUL) {
                    f32 *fovyPtr = (f32 *)fovySetting->ptr;
                    f32 def = fovySetting->def.f32_value;
                    if (*fovyPtr != def) {
                        fprintf(stderr,
                                "[CONFIG] --deterministic: %s=%.3f differs from the default %.3f; "
                                "pinning to %.3f for this run so the sim-invariance hash is reproducible.\n",
                                pinFovKeys[fk], (double)*fovyPtr, (double)def, (double)def);
                        *fovyPtr = def;
                    }
                }
            }
        }
    }

    if (listSettings || listDisplays || dumpConfig || resetConfig || configSetCount > 0) {
        if (listSettings) {
            settingsPrintList(stdout);
        }
        if (listSettings && (dumpConfig || listDisplays)) {
            printf("\n");
        }
        if (dumpConfig) {
            settingsPrintDump(stdout);
        }
        if (dumpConfig && listDisplays) {
            printf("\n");
        }
        if (listDisplays && !platformPrintDisplays(stdout)) {
            return 1;
        }
        return 0;
    }

    /* ADS reticle sanity note: surface a misconfigured reticle loudly rather than
     * silently falling back to the classic crosshair. Fires only when the user
     * opted into ADS (Input.AdsEnabled=1) yet the modern reticle is off
     * (Input.AdsModernReticle=0) -- exactly the state a stray --config-override or
     * a hand-edited ge007.ini can leave behind. ADS ships off, so this is never
     * noisy in traditional play. */
    {
        const Setting *adsEnabled = settingsFind("Input.AdsEnabled");
        const Setting *adsReticle = settingsFind("Input.AdsModernReticle");
        if (adsEnabled && adsEnabled->ptr && *(const s32 *)adsEnabled->ptr != 0 &&
            adsReticle && adsReticle->ptr && *(const s32 *)adsReticle->ptr == 0) {
            printf("[GE007-PC] Note: Input.AdsEnabled=1 but Input.AdsModernReticle=0 -- "
                   "the classic crosshair will show while aiming. Set "
                   "Input.AdsModernReticle=1 in ge007.ini for the modern ADS reticle.\n");
        }
    }

    printf("[GE007-PC] Starting...\n");
    if (g_pcStartMultiplayer) {
        const char *stage_slug = "?";
        const char *scenario_slug = "?";
        size_t i;
        /* kPcMpStages/kPcMpScenarios are now extern (single-sourced in
         * cli_stage_tables.c); sizeof() an incomplete array type is unavailable
         * across the TU boundary, so iterate against the exported counts. */
        for (i = 0; i < (size_t)kPcMpStagesCount; i++) {
            if (kPcMpStages[i].mp_stage == g_pcStartMpStage) {
                stage_slug = kPcMpStages[i].slug;
                break;
            }
        }
        for (i = 0; i < (size_t)kPcMpScenariosCount; i++) {
            if (kPcMpScenarios[i].scenario == g_pcStartMpScenario) {
                scenario_slug = kPcMpScenarios[i].slug;
                break;
            }
        }
        printf("[GE007-PC] Start multiplayer: %d players, stage %s (MP_STAGE %d), scenario %s (SCENARIO %d)\n",
               g_pcStartMpPlayers, stage_slug, g_pcStartMpStage,
               scenario_slug, g_pcStartMpScenario);
    } else if (g_pcStartLevel >= 0) {
        const PcStartStage *stage = pcFindStageByLevelId(g_pcStartLevel);
        if (stage) {
            printf("[GE007-PC] Start stage: %s (mission %d, LEVELID %d)\n",
                   stage->name, stage->mission_num, stage->level_id);
        } else if (g_pcStartLevelForcedRaw) {
            printf("[GE007-PC] Start stage: raw internal LEVELID %d\n", g_pcStartLevel);
        }
    } else if (g_pcStartRamrom != NULL) {
        printf("[GE007-PC] Start RAMROM demo: %s\n", g_pcStartRamrom);
    }

    /* Phase 2: Load ROM file (fail-fast — game cannot function without ROM) */
    if (!romPath) {
        fprintf(stderr,
            "[GE007-PC] No GoldenEye ROM found.\n"
            "  * Place your ROM next to the executable (e.g. baserom.u.z64), or\n"
            "  * pass it explicitly:  ge007 --rom /path/to/your_rom.z64\n"
            "  Auto-detect also scans: ./, ~/Downloads, ~/Documents, ~/Desktop, ~\n"
            "  You must supply your own legally-dumped ROM (see DISCLAIMER.md).\n");
        return 1;
    }
    if (platformInitRom(romPath) != 0) {
        fprintf(stderr, "[GE007-PC] Failed to load ROM: %s\n", romPath);
        return 1;
    }
    platformPatchFileTable(g_romData);
    printf("[ROM] File table patched with %u-byte ROM\n", g_romSize);

    /* Phase 3: SDL2 window + OpenGL context */
    if (platformInitSDL() != 0) {
        fprintf(stderr, "[GE007-PC] SDL init failed, exiting\n");
        return 1;
    }

    /* Phase 4: Initialize SDL2 audio backend */
    portAudioInit();

    /* Phase 5: Initialize GBI display list translator */
    gfx_init();

    /* Initialize the N64 scheduler infrastructure.
     * On N64 this is called from mainproc() before bossEntry().
     * It creates gfxFrameMsgQ and registers the gfx scheduler client. */
    schedulerInitThread();

    printf("[GE007-PC] Entering game loop...\n");
    printf("[GE007-PC] Controls: WASD=move Mouse=look LClick=fire RClick=aim Scroll=weapon R=reload F=interact C=crouch Q/E=lean Esc=pause  [H for full list]\n");

    /* BUG-1 stall watchdog: converts a silent freeze (sim spin or GL present
     * wedge — force-quit-only in the field) into a stall dump + all-thread
     * sample the first time it happens. GE007_NO_WATCHDOG=1 disables. */
    {
        extern void portWatchdogInit(void);
        portWatchdogInit();
    }

    /* On N64: init() → mainproc() → schedulerInitThread() → bossEntry()
     * bossEntry() calls bossInitMainthreadData() then loops on bossMainloop().
     * bossMainloop() blocks on osRecvMesg(&gfxFrameMsgQ, ..., OS_MESG_BLOCK)
     * waiting for retrace messages from the scheduler.
     *
     * On PC, the scheduler thread never runs (osStartThread is a no-op).
     * Instead, osRecvMesg's blocking path calls platformFrameSync() which
     * sends retrace messages at ~60fps via SDL timing. */
    bossEntry();

    platformShutdownSDL();
    return 0;
}
#endif /* MACOS_APP_BUNDLE */

#if defined(MGB64_APP) && !defined(MACOS_APP_BUNDLE)
/* Boot the engine from the app shell. Rather than duplicate main()'s delicate
 * preamble (signal handlers, config, ROM resolve, boot), synthesize a CLI
 * invocation from the config and delegate to mgb64_headless_main() — the exact
 * same engine boot path. The app must platformSetHostWindow() first so the
 * engine adopts the shell's window instead of creating its own. */
int mgb64_engine_boot(const MgbBootConfig *cfg)
{
    const char *argv[40];
    char levelbuf[16], diffbuf[16], playersbuf[16], stagebuf[16];
    int argc = 0;

    argv[argc++] = "ge007";
    if (cfg && cfg->rom_path && cfg->rom_path[0]) {
        argv[argc++] = "--rom";
        argv[argc++] = cfg->rom_path;
    }
    if (cfg && cfg->save_dir && cfg->save_dir[0]) {
        argv[argc++] = "--savedir";
        argv[argc++] = cfg->save_dir;
    }
    if (cfg && cfg->level_slug && cfg->level_slug[0]) {
        argv[argc++] = "--level";
        argv[argc++] = cfg->level_slug;
    } else if (cfg && cfg->level_id >= 0) {
        snprintf(levelbuf, sizeof levelbuf, "%d", cfg->level_id);
        argv[argc++] = "--level";
        argv[argc++] = levelbuf;
    }
    if (cfg && cfg->difficulty >= 0) {
        snprintf(diffbuf, sizeof diffbuf, "%d", cfg->difficulty);
        argv[argc++] = "--difficulty";
        argv[argc++] = diffbuf;
    }
    if (cfg) {
        if (cfg->preset == 1) argv[argc++] = "--faithful";
        else if (cfg->preset == 2) argv[argc++] = "--faithful-hd";
        else if (cfg->preset == 3) argv[argc++] = "--remaster";
    }
    if (cfg && cfg->multiplayer) {
        argv[argc++] = "--multiplayer";
        if (cfg->players >= 2) {
            snprintf(playersbuf, sizeof playersbuf, "%d", cfg->players);
            argv[argc++] = "--players";
            argv[argc++] = playersbuf;
        }
        if (cfg->mp_stage_id >= 0) {
            snprintf(stagebuf, sizeof stagebuf, "%d", cfg->mp_stage_id);
            argv[argc++] = "--mp-stage";
            argv[argc++] = stagebuf;
        }
    }
    /* Validation/CI seam: boot, render N frames, screenshot, and exit cleanly
     * (used to verify the launcher->engine handoff non-interactively). */
    {
        const char *shot = getenv("MGB64_BOOT_SCREENSHOT_FRAME");
        if (shot && shot[0]) {
            argv[argc++] = "--screenshot-frame";
            argv[argc++] = shot;
            argv[argc++] = "--screenshot-exit";
        }
    }

    return mgb64_headless_main(argc, (char **)argv);
}
#endif /* MGB64_APP && !MACOS_APP_BUNDLE */
