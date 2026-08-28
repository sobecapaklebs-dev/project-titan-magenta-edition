/**
 * GameBridge.c — Implementation of the macOS app bridge API.
 *
 * Wraps the existing platform layer (main_pc.c, platform_sdl.c, config_pc.c,
 * rom_io.c) behind the GameBridge.h interface for the Swift app shell.
 *
 * Only compiled when MACOS_APP_BUNDLE is defined. The existing main() in
 * main_pc.c is excluded in that case.
 */

#ifdef MACOS_APP_BUNDLE

#include "GameBridge.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <setjmp.h>
#include <unistd.h>
#include <execinfo.h>
#include <sys/stat.h>
#include <os/lock.h>
#include <os/log.h>
#include <CommonCrypto/CommonDigest.h>

/* Game engine headers */
#include <ultra64.h>
#include "rom_io.h"
#include "savedir.h"
#include "config_pc.h"
#include "bondconstants.h"

/* ========================================================================
 * External declarations from the platform layer
 * ======================================================================== */

/* main_pc.c */
extern int g_pcStartLevel;

/* main_pc.c — GFX crash recovery state (non-static globals) */
extern sigjmp_buf g_gfxRecoveryJmp;
extern volatile int g_gfxRecoveryActive;
extern int g_crashRecoveryCount;

/* boss.c — game entry point */
extern void bossEntry(void);

/* Scheduler init (called from mainproc on N64, explicitly on PC) */
extern void schedulerInitThread(void);

/* platform_sdl.c */
extern int  platformInitSDL(void);
extern void platformShutdownSDL(void);

/* gfx_pc.c */
extern void gfx_init(void);

/* audio_pc.c */
extern void portAudioInit(void);
extern void portAudioRegisterConfig(void);

/* config_pc.c */
extern void platformRegisterConfig(void);

/* Frame counter (platform_sdl.c or stubs.c) */
extern int g_frame_count_diag;

/* ========================================================================
 * os_log subsystem for Console.app integration
 * ======================================================================== */

static os_log_t s_log = NULL;

static void bridge_log_init(void) {
    if (!s_log) {
        s_log = os_log_create("com.mgb64.app", "bridge");
    }
}

/* ========================================================================
 * Internal state
 * ======================================================================== */

/** Atomic shutdown flag. Written by main thread, read by game thread. */
static _Atomic int s_shutdown_requested = 0;

/** Initialization and running state. */
static _Atomic int s_initialized = 0;
static _Atomic int s_running = 0;

/** Thread-safe input state. Protected by os_unfair_lock. */
static os_unfair_lock s_input_lock = OS_UNFAIR_LOCK_INIT;
static GameInputState s_input_current;
static GameInputState s_input_pending;
static _Atomic int s_input_has_pending = 0;

/** Render surface info (set before init, consumed during init). */
static void *s_render_layer = NULL;
static int s_render_width = 960;
static int s_render_height = 540;
static float s_render_scale = 2.0f;

/** Pending resize (thread-safe). */
static os_unfair_lock s_resize_lock = OS_UNFAIR_LOCK_INIT;
static _Atomic int s_resize_pending = 0;
static int s_resize_width = 0;
static int s_resize_height = 0;
static float s_resize_scale = 0.0f;

/** Serialized config bridge access. */
static os_unfair_lock s_config_lock = OS_UNFAIR_LOCK_INIT;

/* ========================================================================
 * Crash handler & callback for Swift layer
 * ======================================================================== */

/** Crash callback — set via game_set_crash_callback(), read from signal handler.
 *  volatile because it's written from normal code and read from a signal handler. */
static volatile GameCrashCallback s_crash_callback = NULL;

/**
 * Notify the Swift layer of a crash (async-signal-safe path).
 * Called from the signal handler; must not use locks or malloc.
 */
static void bridgeNotifyCrash(int sig, const char *msg) {
    GameCrashCallback cb = s_crash_callback;
    if (cb) {
        cb(sig, msg);
    }
}

/* Recovery limit: generous in debug, tight in release. */
#ifdef NDEBUG
#define BRIDGE_MAX_CRASH_RECOVERY 10
#else
#define BRIDGE_MAX_CRASH_RECOVERY 10000
#endif

/**
 * Signal handler for SIGSEGV, SIGBUS, SIGABRT on the game thread.
 *
 * Mirrors the crashHandler in main_pc.c: attempts GFX display-list recovery
 * via siglongjmp when possible, otherwise logs diagnostics and exits.
 *
 * All output uses write() (async-signal-safe) instead of printf/fprintf.
 */
static void bridgeCrashHandler(int sig) {
    /* Diagnostic externs from gfx_pc.c */
    extern volatile uintptr_t g_lastDlCmd;
    extern volatile uint32_t g_lastDlOpcode, g_lastDlW0;
    extern volatile uintptr_t g_lastDlW1;
    extern volatile uintptr_t g_diag_tex_addr;
    extern volatile uint32_t g_diag_tex_size_bytes, g_diag_tex_needed;
    extern volatile uint8_t g_diag_tex_fmt, g_diag_tex_siz, g_diag_tex_slot, g_diag_tex_tile;

    /* If we're in DL processing and recovery is available, skip this frame. */
    if (g_gfxRecoveryActive && g_crashRecoveryCount < BRIDGE_MAX_CRASH_RECOVERY) {
        g_crashRecoveryCount++;
        char msg[512];
        int n = snprintf(msg, sizeof(msg),
            "[GFX-RECOVER] sig=%d #%d op=0x%02X w0=0x%08X w1=0x%lX cmd=%p"
            " tex_addr=%p tex_sz=%u tex_need=%u tex_fmt=%u tex_siz=%u tex_slot=%u tex_tile=%u\n",
            sig, g_crashRecoveryCount,
            g_lastDlOpcode, g_lastDlW0, (unsigned long)g_lastDlW1,
            (void *)g_lastDlCmd,
            (void *)g_diag_tex_addr, g_diag_tex_size_bytes, g_diag_tex_needed,
            g_diag_tex_fmt, g_diag_tex_siz, g_diag_tex_slot, g_diag_tex_tile);
        if (n > 0) write(STDERR_FILENO, msg, (size_t)n);
        g_gfxRecoveryActive = 0;
        siglongjmp(g_gfxRecoveryJmp, 1);
    }

    /* Unrecoverable crash — log diagnostics */
    {
        extern volatile uintptr_t g_diag_current_cmd_addr;
        char diag[768];
        int dlen = snprintf(diag, sizeof(diag),
            "\n[CRASH] Signal %d (unrecoverable)\n"
            "[CRASH-DL] frame=%d op=0x%02X w0=0x%08X w1=0x%lX cmd=%p diag_cmd=%p\n"
            "[CRASH-TEX] addr=%p size_bytes=%u needed=%u fmt=%u siz=%u slot=%u tile=%u\n",
            sig, g_frame_count_diag,
            g_lastDlOpcode, g_lastDlW0, (unsigned long)g_lastDlW1,
            (void *)g_lastDlCmd, (void *)g_diag_current_cmd_addr,
            (void *)g_diag_tex_addr, g_diag_tex_size_bytes, g_diag_tex_needed,
            g_diag_tex_fmt, g_diag_tex_siz, g_diag_tex_slot, g_diag_tex_tile);
        if (dlen > 0) write(STDERR_FILENO, diag, (size_t)dlen);

        /* Notify Swift layer before we die */
        bridgeNotifyCrash(sig, diag);
    }

    /* Backtrace */
    void *bt[32];
    int n = backtrace(bt, 32);
    backtrace_symbols_fd(bt, n, STDERR_FILENO);

    _exit(1);
}

void game_set_crash_callback(GameCrashCallback callback) {
    s_crash_callback = callback;
}

/* ========================================================================
 * Known ROM hashes for validation
 * ======================================================================== */

typedef struct {
    const char *sha1;
    const char *region;
} KnownROM;

static const KnownROM s_known_roms[] = {
    { "abe01e4aeb033b6c0836819f549c791b26cfde83", "US" },
    { "167c3c433dec1f1eb921736f7d53fac8cb45ee31", "EU" },
    { "2a5dade32f7fad6c73c659d2026994632c1b3174", "JP" },
};

#define NUM_KNOWN_ROMS (sizeof(s_known_roms) / sizeof(s_known_roms[0]))

/* ========================================================================
 * Level table (mirrors main_pc.c kPcStartStages)
 * ======================================================================== */

static const GameLevelInfo s_levels[] = {
    {  LEVELID_DAM,      1, "dam",       "Dam" },
    {  LEVELID_FACILITY, 2, "facility",  "Facility" },
    {  LEVELID_RUNWAY,   3, "runway",    "Runway" },
    {  LEVELID_SURFACE,  4, "surface1",  "Surface 1" },
    {  LEVELID_BUNKER1,  5, "bunker1",   "Bunker 1" },
    {  LEVELID_SILO,     6, "silo",      "Silo" },
    {  LEVELID_FRIGATE,  7, "frigate",   "Frigate" },
    {  LEVELID_SURFACE2, 8, "surface2",  "Surface 2" },
    {  LEVELID_BUNKER2,  9, "bunker2",   "Bunker 2" },
    {  LEVELID_STATUE,  10, "statue",    "Statue" },
    {  LEVELID_ARCHIVES,11, "archives",  "Archives" },
    {  LEVELID_STREETS, 12, "streets",   "Streets" },
    {  LEVELID_DEPOT,   13, "depot",     "Depot" },
    {  LEVELID_TRAIN,   14, "train",     "Train" },
    {  LEVELID_JUNGLE,  15, "jungle",    "Jungle" },
    {  LEVELID_CONTROL, 16, "control",   "Control" },
    {  LEVELID_CAVERNS, 17, "caverns",   "Caverns" },
    {  LEVELID_CRADLE,  18, "cradle",    "Cradle" },
    {  LEVELID_AZTEC,   19, "aztec",     "Aztec" },
    {  LEVELID_EGYPT,   20, "egypt",     "Egyptian" },
};

#define NUM_LEVELS (sizeof(s_levels) / sizeof(s_levels[0]))

/* ========================================================================
 * ROM Validation
 * ======================================================================== */

static void compute_sha1_hex(const uint8_t *data, size_t len, char out[41]) {
    unsigned char digest[CC_SHA1_DIGEST_LENGTH];
    /* CC_SHA1 is deprecated in favor of CryptoKit, but CryptoKit requires
     * Swift or Objective-C++. For a pure C file, CommonCrypto is the only
     * option. Suppress the deprecation warning for this single call. */
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    CC_SHA1(data, (CC_LONG)len, digest);
#pragma clang diagnostic pop
    for (int i = 0; i < CC_SHA1_DIGEST_LENGTH; i++) {
        snprintf(out + i * 2, 3, "%02x", digest[i]);
    }
    out[40] = '\0';
}

static const char *detect_byte_order(const uint8_t *header) {
    if (header[0] == 0x80 && header[1] == 0x37) return "z64";
    if (header[0] == 0x37 && header[1] == 0x80) return "v64";
    if (header[0] == 0x40 && header[1] == 0x12) return "n64";
    return "???";
}

GameROMInfo game_validate_rom(const char *rom_path) {
    GameROMInfo info;
    memset(&info, 0, sizeof(info));

    if (!rom_path || !rom_path[0]) {
        snprintf(info.message, sizeof(info.message), "No ROM path provided");
        return info;
    }

    FILE *f = fopen(rom_path, "rb");
    if (!f) {
        snprintf(info.message, sizeof(info.message), "Cannot open file");
        return info;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size < 1024 || size > 64 * 1024 * 1024) {
        fclose(f);
        snprintf(info.message, sizeof(info.message),
                 "Invalid file size: %ld bytes (expected 8-16 MB)", size);
        return info;
    }

    info.size_bytes = (uint32_t)size;

    /* Read header for byte order detection */
    uint8_t header[4];
    if (fread(header, 1, 4, f) != 4) {
        fclose(f);
        snprintf(info.message, sizeof(info.message), "Cannot read ROM header");
        return info;
    }

    const char *order = detect_byte_order(header);
    strncpy(info.byte_order, order, sizeof(info.byte_order) - 1);

    /* Read full ROM for SHA1 */
    fseek(f, 0, SEEK_SET);
    uint8_t *data = malloc(size);
    if (!data) {
        fclose(f);
        snprintf(info.message, sizeof(info.message), "Out of memory");
        return info;
    }

    if ((long)fread(data, 1, size, f) != size) {
        free(data);
        fclose(f);
        snprintf(info.message, sizeof(info.message), "Read error");
        return info;
    }
    fclose(f);

    /* Byte-swap to big-endian (.z64) for consistent SHA1 */
    if (strcmp(info.byte_order, "v64") == 0) {
        for (long i = 0; i < size - 1; i += 2) {
            uint8_t tmp = data[i];
            data[i] = data[i + 1];
            data[i + 1] = tmp;
        }
    } else if (strcmp(info.byte_order, "n64") == 0) {
        for (long i = 0; i < size - 3; i += 4) {
            uint8_t tmp0 = data[i], tmp1 = data[i + 1];
            data[i] = data[i + 3];
            data[i + 1] = data[i + 2];
            data[i + 2] = tmp1;
            data[i + 3] = tmp0;
        }
    }

    compute_sha1_hex(data, size, info.sha1_hex);
    free(data);

    /* Match against known ROMs */
    strncpy(info.region, "??", sizeof(info.region) - 1);
    for (size_t i = 0; i < NUM_KNOWN_ROMS; i++) {
        if (strcmp(info.sha1_hex, s_known_roms[i].sha1) == 0) {
            strncpy(info.region, s_known_roms[i].region, sizeof(info.region) - 1);
            info.valid = true;
            snprintf(info.message, sizeof(info.message),
                     "Valid %s ROM (%s, %.1f MB)",
                     info.region, info.byte_order, size / (1024.0 * 1024.0));
            return info;
        }
    }

    /* Unknown hash — might still work, but warn */
    snprintf(info.message, sizeof(info.message),
             "Unrecognized ROM (SHA1: %.8s...). May not be compatible.",
             info.sha1_hex);
    return info;
}

/* ========================================================================
 * Lifecycle
 * ======================================================================== */

/**
 * Shutdown hook called from platformFrameSync() each frame.
 * Returns nonzero if the game should exit.
 */
int gameBridgeCheckShutdown(void) {
    return atomic_load_explicit(&s_shutdown_requested, memory_order_acquire);
}

/**
 * Input consumption hook called from platformPollEvents() each frame.
 * Copies durable button/stick state plus one-frame pointer deltas into the
 * platform's controller state.
 */
void gameBridgeConsumeInput(GameInputState *out) {
    if (!out) return;

    os_unfair_lock_lock(&s_input_lock);

    if (atomic_load_explicit(&s_input_has_pending, memory_order_acquire)) {
        s_input_current = s_input_pending;
        atomic_store_explicit(&s_input_has_pending, 0, memory_order_release);

        /* Pending deltas have been moved into current for this frame. */
        s_input_pending.mouse_dx = 0.0f;
        s_input_pending.mouse_dy = 0.0f;
        s_input_pending.mouse_wheel = 0;
    }

    *out = s_input_current;

    /* Buttons and sticks are stateful. Mouse motion and wheel are deltas. */
    s_input_current.mouse_dx = 0.0f;
    s_input_current.mouse_dy = 0.0f;
    s_input_current.mouse_wheel = 0;

    os_unfair_lock_unlock(&s_input_lock);
}

/**
 * Resize consumption hook called from renderer each frame.
 * Returns true if a resize is pending, populates out parameters.
 */
int gameBridgeConsumeResize(int *w, int *h, float *scale) {
    if (!atomic_load_explicit(&s_resize_pending, memory_order_acquire)) return 0;

    os_unfair_lock_lock(&s_resize_lock);
    *w = s_resize_width;
    *h = s_resize_height;
    *scale = s_resize_scale;
    atomic_store_explicit(&s_resize_pending, 0, memory_order_release);
    os_unfair_lock_unlock(&s_resize_lock);
    return 1;
}

int game_init(const char *rom_path, const char *save_dir) {
    if (atomic_load_explicit(&s_initialized, memory_order_acquire)) {
        fprintf(stderr, "[GameBridge] game_init() called twice\n");
        return -1;
    }

    if (!rom_path || !rom_path[0]) {
        fprintf(stderr, "[GameBridge] No ROM path provided\n");
        return -1;
    }

    /* Line-buffered output for diagnostics */
    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(stderr, NULL, _IOLBF, 0);

    /* os_log subsystem for Console.app */
    bridge_log_init();

    /* Install signal handlers for crash recovery on the game thread.
     * Mirrors the signal setup in main_pc.c's main(). */
    signal(SIGSEGV, bridgeCrashHandler);
#if !defined(__SANITIZE_ADDRESS__) && !__has_feature(address_sanitizer)
    /* ASAN uses SIGBUS internally for shadow memory probing.
     * Don't intercept it in sanitizer builds. */
    signal(SIGBUS, bridgeCrashHandler);
#endif
    signal(SIGABRT, bridgeCrashHandler);

    /* Phase 1: Save directory. An explicit override that can't be created or
     * written is a fatal error, not something to accept and then lose every save
     * against (AUDIT-0054) — mirror main_pc.c/main_app.cpp and abort. */
    if (savedirInit(save_dir) != 0) {
        fprintf(stderr, "[GameBridge] Save directory '%s' is unusable; aborting.\n",
                save_dir ? save_dir : "(auto)");
        return -1;
    }

    /* Phase 2: Config system */
    platformRegisterConfig();
    portAudioRegisterConfig();
    configInit();

    os_log_info(s_log, "Initializing with ROM: %{public}s", rom_path);

    /* Phase 3: Load ROM */
    if (platformInitRom(rom_path) != 0) {
        os_log_error(s_log, "Failed to load ROM: %{public}s", rom_path);
        return -1;
    }
    platformPatchFileTable(g_romData);
    os_log_info(s_log, "ROM loaded: %u bytes, file table patched", g_romSize);

    /* Phase 4: SDL2 window + rendering context */
    if (platformInitSDL() != 0) {
        os_log_error(s_log, "SDL init failed");
        return -1;
    }

    /* Phase 5: Audio */
    portAudioInit();

    /* Phase 6: GBI display list translator */
    gfx_init();

    /* Phase 7: N64 scheduler infrastructure */
    schedulerInitThread();

    atomic_store_explicit(&s_initialized, 1, memory_order_release);
    os_log_info(s_log, "Initialization complete");
    return 0;
}

void game_run(void) {
    if (!atomic_load_explicit(&s_initialized, memory_order_acquire)) {
        os_log_error(s_log, "game_run() called before game_init()");
        return;
    }

    atomic_store_explicit(&s_running, 1, memory_order_release);
    os_log_info(s_log, "Entering game loop");

    /* bossEntry() calls bossMainloop() which blocks forever.
     * The shutdown flag is checked by platformFrameSync() via
     * gameBridgeCheckShutdown(). When set, the game loop exits. */
    bossEntry();

    atomic_store_explicit(&s_running, 0, memory_order_release);
    os_log_info(s_log, "Game loop exited");

    platformShutdownSDL();
}

void game_request_shutdown(void) {
    atomic_store_explicit(&s_shutdown_requested, 1, memory_order_release);
}

bool game_is_initialized(void) {
    return atomic_load_explicit(&s_initialized, memory_order_acquire) != 0;
}

bool game_is_running(void) {
    return atomic_load_explicit(&s_running, memory_order_acquire) != 0;
}

/* ========================================================================
 * Input
 * ======================================================================== */

void game_set_input(const GameInputState *state) {
    if (!state) return;

    os_unfair_lock_lock(&s_input_lock);

    /* Accumulate mouse deltas rather than replacing — prevents lost motion
     * when the main thread sends input faster than the game consumes it. */
    float accum_dx = s_input_pending.mouse_dx + state->mouse_dx;
    float accum_dy = s_input_pending.mouse_dy + state->mouse_dy;
    int32_t accum_wheel = s_input_pending.mouse_wheel + state->mouse_wheel;

    s_input_pending = *state;
    s_input_pending.mouse_dx = accum_dx;
    s_input_pending.mouse_dy = accum_dy;
    s_input_pending.mouse_wheel = accum_wheel;
    atomic_store_explicit(&s_input_has_pending, 1, memory_order_release);

    os_unfair_lock_unlock(&s_input_lock);
}

/* ========================================================================
 * Configuration
 *
 * The config system exposes lookup and string-set APIs for registered
 * "Section.Key" settings. The bridge accepts split section/key arguments
 * because that maps cleanly to Swift preferences code.
 * ======================================================================== */

static bool game_config_join_key(const char *section, const char *key, char *out, size_t out_size) {
    if (!section || !key || !*section || !*key || out_size == 0) {
        return false;
    }

    return snprintf(out, out_size, "%s.%s", section, key) > 0;
}

float game_config_get_float(const char *section, const char *key, float fallback) {
    char full_key[CONFIG_MAX_KEYNAME + 1];
    int type = -1;
    void *ptr = NULL;
    float value = fallback;

    if (!game_config_join_key(section, key, full_key, sizeof(full_key))) {
        return fallback;
    }

    os_unfair_lock_lock(&s_config_lock);
    ptr = configFindEntry(full_key, &type);
    if (ptr) {
        if (type == 1) {
            value = *(f32 *)ptr;
        } else if (type == 0) {
            value = (float)*(s32 *)ptr;
        } else if (type == 2) {
            value = (float)*(u32 *)ptr;
        }
    }
    os_unfair_lock_unlock(&s_config_lock);

    return value;
}

void game_config_set_float(const char *section, const char *key, float value) {
    char full_key[CONFIG_MAX_KEYNAME + 1];
    char value_text[64];

    if (!game_config_join_key(section, key, full_key, sizeof(full_key))) {
        return;
    }

    snprintf(value_text, sizeof(value_text), "%g", (double)value);
    os_unfair_lock_lock(&s_config_lock);
    configSetValue(full_key, value_text);
    os_unfair_lock_unlock(&s_config_lock);
}

int32_t game_config_get_int(const char *section, const char *key, int32_t fallback) {
    char full_key[CONFIG_MAX_KEYNAME + 1];
    int type = -1;
    void *ptr = NULL;
    int32_t value = fallback;

    if (!game_config_join_key(section, key, full_key, sizeof(full_key))) {
        return fallback;
    }

    os_unfair_lock_lock(&s_config_lock);
    ptr = configFindEntry(full_key, &type);
    if (ptr) {
        if (type == 0) {
            value = *(s32 *)ptr;
        } else if (type == 1) {
            value = (int32_t)*(f32 *)ptr;
        } else if (type == 2) {
            value = (int32_t)*(u32 *)ptr;
        }
    }
    os_unfair_lock_unlock(&s_config_lock);

    return value;
}

void game_config_set_int(const char *section, const char *key, int32_t value) {
    char full_key[CONFIG_MAX_KEYNAME + 1];
    char value_text[64];

    if (!game_config_join_key(section, key, full_key, sizeof(full_key))) {
        return;
    }

    snprintf(value_text, sizeof(value_text), "%d", value);
    os_unfair_lock_lock(&s_config_lock);
    configSetValue(full_key, value_text);
    os_unfair_lock_unlock(&s_config_lock);
}

bool game_config_save(void) {
    bool saved;

    os_unfair_lock_lock(&s_config_lock);
    saved = configSave() != 0;
    os_unfair_lock_unlock(&s_config_lock);

    return saved;
}

/* ========================================================================
 * Render Surface
 * ======================================================================== */

void game_set_render_surface(void *native_layer, int width, int height, float scale) {
    s_render_layer = native_layer;
    s_render_width = width;
    s_render_height = height;
    s_render_scale = scale;
}

void game_notify_resize(int width, int height, float scale) {
    os_unfair_lock_lock(&s_resize_lock);
    s_resize_width = width;
    s_resize_height = height;
    s_resize_scale = scale;
    atomic_store_explicit(&s_resize_pending, 1, memory_order_release);
    os_unfair_lock_unlock(&s_resize_lock);
}

/* ========================================================================
 * Game State Queries
 * ======================================================================== */

uint64_t game_get_frame_count(void) {
    return (uint64_t)g_frame_count_diag;
}

const char *game_get_current_level_name(void) {
    /* NOTE: This currently returns the start level from CLI/config, not the
     * runtime active level. To reflect in-game level changes, this needs to
     * read the game's actual current stage variable (e.g., g_StageNum or
     * equivalent). For now this is sufficient for the app shell's UI. */
    for (size_t i = 0; i < NUM_LEVELS; i++) {
        if (s_levels[i].level_id == g_pcStartLevel) {
            return s_levels[i].name;
        }
    }
    return NULL;
}

const GameLevelInfo *game_get_level_list(int *out_count) {
    if (out_count) *out_count = (int)NUM_LEVELS;
    return s_levels;
}

#endif /* MACOS_APP_BUNDLE */
