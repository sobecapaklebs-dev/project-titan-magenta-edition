/**
 * GameBridge.h — C interface between the macOS Swift app shell and the game engine.
 *
 * This header defines the complete API surface that the Swift AppDelegate and
 * UI layer use to control the game. It wraps the existing platform layer
 * (main_pc.c, platform_sdl.c, config_pc.c, rom_io.c) behind a clean,
 * thread-safe interface designed for the macOS app bundle architecture.
 *
 * Threading model:
 *   - MAIN THREAD: AppKit event loop, UI, window management, input capture.
 *   - GAME THREAD: game_init() + game_run() — blocking game loop.
 *   - AUDIO THREAD: managed internally by the audio backend.
 *
 * Functions are annotated with their expected calling thread. Functions marked
 * THREAD-SAFE may be called from any thread. Functions marked GAME THREAD ONLY
 * must only be called from the game thread (the thread that calls game_run).
 *
 * Build: compile with -DMACOS_APP_BUNDLE. The existing main() in main_pc.c
 * is excluded, and these functions become the entry points instead.
 */

#ifndef GAME_BRIDGE_H
#define GAME_BRIDGE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================
 * ROM Validation
 * ======================================================================== */

/**
 * Result of ROM file validation. Populated by game_validate_rom().
 */
typedef struct {
    /** Whether the ROM passed all checks and is compatible. */
    bool valid;

    /** SHA1 hash of the ROM as a 40-character hex string (null-terminated). */
    char sha1_hex[41];

    /** Detected region: "US", "EU", "JP", or "??" if unrecognized. */
    char region[4];

    /** Detected byte order: "z64" (big-endian), "v64" (byte-swapped), "n64" (little-endian). */
    char byte_order[4];

    /** File size in bytes. */
    uint32_t size_bytes;

    /** Human-readable status message (e.g., "Valid US ROM" or "Unknown SHA1"). */
    char message[128];
} GameROMInfo;

/**
 * Validate a ROM file without loading the full game engine.
 *
 * Checks: file existence, size range, byte order detection, SHA1 against
 * known good hashes. Does NOT load the ROM into memory permanently.
 *
 * Thread safety: THREAD-SAFE. No global state modified.
 *
 * @param rom_path  Absolute path to the ROM file (UTF-8).
 * @return          Populated GameROMInfo struct.
 */
GameROMInfo game_validate_rom(const char *rom_path);

/* ========================================================================
 * Lifecycle — called from the game thread
 * ======================================================================== */

/**
 * Initialize the game engine. Must be called exactly once, from the game thread,
 * before game_run().
 *
 * Performs: save directory init, config load, ROM load + file table patch,
 * SDL/rendering init, audio init, GBI translator init, scheduler init.
 *
 * Thread safety: GAME THREAD ONLY. Must not be called from the main thread.
 *
 * @param rom_path   Absolute path to the ROM file (UTF-8).
 * @param save_dir   Absolute path to the save/config directory (UTF-8).
 *                   Created if it doesn't exist. Pass NULL for default.
 * @return           0 on success, nonzero on failure.
 */
int game_init(const char *rom_path, const char *save_dir);

/**
 * Enter the game's main loop. Blocks until game_request_shutdown() is called
 * or the game exits naturally.
 *
 * Internally calls bossEntry() which runs the infinite bossMainloop() loop.
 * Frame pacing is handled by platformFrameSync() at ~60fps.
 *
 * Thread safety: GAME THREAD ONLY. Call from the same thread as game_init().
 */
void game_run(void);

/**
 * Request a clean shutdown of the game loop.
 *
 * Sets an internal flag that platformFrameSync() checks each frame. The game
 * will finish the current frame, clean up, and game_run() will return.
 *
 * Thread safety: THREAD-SAFE. Designed to be called from the main thread
 * while game_run() is blocking on the game thread.
 */
void game_request_shutdown(void);

/* ========================================================================
 * Crash Callback — optional notification for the Swift layer
 * ======================================================================== */

/**
 * Callback type for crash notifications.
 *
 * Called from a signal handler context — the implementation MUST be
 * async-signal-safe (no malloc, no Objective-C messaging, no locks).
 * Suitable for setting a flag or writing to a pipe/fd.
 *
 * @param signal   The signal number (SIGSEGV, SIGBUS, SIGABRT).
 * @param details  Human-readable crash details (stack-allocated buffer,
 *                 valid only for the duration of the call).
 */
typedef void (*GameCrashCallback)(int signal, const char *details);

/**
 * Register a callback to be invoked when the game thread receives a fatal
 * signal (SIGSEGV, SIGBUS, SIGABRT) that cannot be recovered.
 *
 * Only the most recently registered callback is used. Pass NULL to clear.
 *
 * Thread safety: THREAD-SAFE. Must be called before game_run().
 *
 * @param callback  The callback function, or NULL to clear.
 */
void game_set_crash_callback(GameCrashCallback callback);

/**
 * Check whether the game engine has been initialized successfully.
 *
 * Thread safety: THREAD-SAFE.
 *
 * @return true if game_init() completed successfully.
 */
bool game_is_initialized(void);

/**
 * Check whether the game loop is currently running.
 *
 * Thread safety: THREAD-SAFE.
 *
 * @return true if game_run() is active and hasn't been shut down.
 */
bool game_is_running(void);

/* ========================================================================
 * Input — called from the main thread, consumed by the game thread
 * ======================================================================== */

/**
 * N64 controller button masks (matching ultra64 CONT_* defines).
 * These are the canonical button IDs used by the game engine.
 */
enum {
    GAME_BTN_A      = 0x8000,
    GAME_BTN_B      = 0x4000,
    GAME_BTN_Z      = 0x2000,
    GAME_BTN_START  = 0x1000,
    GAME_BTN_DU     = 0x0800,
    GAME_BTN_DD     = 0x0400,
    GAME_BTN_DL     = 0x0200,
    GAME_BTN_DR     = 0x0100,
    GAME_BTN_L      = 0x0020,
    GAME_BTN_R      = 0x0010,
    GAME_BTN_CU     = 0x0008,
    GAME_BTN_CD     = 0x0004,
    GAME_BTN_CL     = 0x0002,
    GAME_BTN_CR     = 0x0001,
};

/**
 * Complete input state for one frame. Written atomically by the main thread,
 * read by the game thread at the start of each frame.
 */
typedef struct {
    /** Bitmask of GAME_BTN_* values for pressed buttons. */
    uint16_t buttons;

    /** Analog stick X axis: -80 (full left) to +80 (full right). */
    int8_t stick_x;

    /** Analog stick Y axis: -80 (full down) to +80 (full up). */
    int8_t stick_y;

    /** Accumulated mouse X delta since last frame (pixels, positive = right). */
    float mouse_dx;

    /** Accumulated mouse Y delta since last frame (pixels, positive = down). */
    float mouse_dy;

    /** Mouse scroll wheel delta since last frame (positive = up/forward). */
    int32_t mouse_wheel;

    /** Right analog stick X (for dual-stick controllers), -1.0 to +1.0. */
    float right_stick_x;

    /** Right analog stick Y (for dual-stick controllers), -1.0 to +1.0. */
    float right_stick_y;
} GameInputState;

/**
 * Set the input state for controller pad 0.
 *
 * The provided state is copied atomically. The game thread reads it at the
 * start of the next frame. Mouse deltas are accumulated (added to any
 * existing unread delta), not replaced.
 *
 * Thread safety: THREAD-SAFE. Lock-free on arm64 (uses os_unfair_lock fallback
 * on x86_64 if atomic 128-bit ops unavailable).
 *
 * @param state  Pointer to the new input state. Must not be NULL.
 */
void game_set_input(const GameInputState *state);

/* ========================================================================
 * Configuration — reads/writes ge007.ini settings
 * ======================================================================== */

/**
 * Get a float config value by section and key.
 *
 * Thread safety: THREAD-SAFE after game_init(). The config system is
 * read-only after initialization; live changes go through the set functions
 * which are serialized internally.
 *
 * @param section  INI section name (e.g., "Video", "Audio", "Input").
 * @param key      Setting key name (e.g., "MouseSensitivity").
 * @param fallback Value returned if the key doesn't exist.
 * @return         The config value, or fallback if not found.
 */
float game_config_get_float(const char *section, const char *key, float fallback);

/**
 * Set a float config value. Takes effect on the next frame.
 *
 * Thread safety: THREAD-SAFE. Internally serialized.
 *
 * @param section  INI section name.
 * @param key      Setting key name.
 * @param value    New value. Clamped to the registered min/max range.
 */
void game_config_set_float(const char *section, const char *key, float value);

/**
 * Get an integer config value by section and key.
 *
 * Thread safety: THREAD-SAFE after game_init().
 */
int32_t game_config_get_int(const char *section, const char *key, int32_t fallback);

/**
 * Set an integer config value. Takes effect on the next frame.
 *
 * Thread safety: THREAD-SAFE. Internally serialized.
 */
void game_config_set_int(const char *section, const char *key, int32_t value);

/**
 * Save all config values to ge007.ini immediately.
 *
 * Thread safety: THREAD-SAFE. Performs an atomic file write.
 *
 * @return true on success, false on I/O error.
 */
bool game_config_save(void);

/* ========================================================================
 * Render Surface — set before game_init, updated on resize
 * ======================================================================== */

/**
 * Provide the native render surface for the game to draw into.
 *
 * For Metal: pass a pointer to a CAMetalLayer.
 * For OpenGL: pass NULL (SDL creates its own GL context).
 *
 * Must be called from the main thread BEFORE game_init().
 *
 * @param native_layer  CAMetalLayer pointer (or NULL for GL path).
 * @param width         Initial drawable width in points.
 * @param height        Initial drawable height in points.
 * @param scale         Backing scale factor (e.g., 2.0 for Retina).
 */
void game_set_render_surface(void *native_layer, int width, int height, float scale);

/**
 * Notify the game engine of a window resize.
 *
 * Thread safety: THREAD-SAFE. The new dimensions are picked up by the
 * renderer at the start of the next frame.
 *
 * @param width   New drawable width in points.
 * @param height  New drawable height in points.
 * @param scale   New backing scale factor.
 */
void game_notify_resize(int width, int height, float scale);

/* ========================================================================
 * Game State Queries — read-only, thread-safe
 * ======================================================================== */

/**
 * Get the current frame number (monotonically increasing, starts at 0).
 *
 * Thread safety: THREAD-SAFE.
 */
uint64_t game_get_frame_count(void);

/**
 * Get the name of the currently loaded level, or NULL if in menus.
 *
 * The returned pointer is valid until the next level transition.
 *
 * Thread safety: THREAD-SAFE. Returns a pointer to static storage.
 */
const char *game_get_current_level_name(void);

/**
 * Level information for the ROM picker / UI.
 */
typedef struct {
    int level_id;
    int mission_num;       /* 1-based campaign order */
    const char *slug;      /* CLI-friendly name (e.g., "facility") */
    const char *name;      /* Display name (e.g., "Facility") */
} GameLevelInfo;

/**
 * Get the list of available levels.
 *
 * Thread safety: THREAD-SAFE. Returns pointer to static data.
 *
 * @param out_count  Receives the number of levels.
 * @return           Array of GameLevelInfo structs.
 */
const GameLevelInfo *game_get_level_list(int *out_count);

#ifdef __cplusplus
}
#endif

#endif /* GAME_BRIDGE_H */
