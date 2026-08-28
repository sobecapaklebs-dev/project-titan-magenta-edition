// engine_entry.h — C entry points the app shell delegates to.
//
// mgb64_headless_main() is the legacy main() body from main_pc.c (compiled
// under -DMGB64_APP). The shell calls it verbatim for automation/CLI
// invocations so that path stays byte-identical to the pre-app binary.
#ifndef MGB64_ENGINE_ENTRY_H
#define MGB64_ENGINE_ENTRY_H

#ifdef __cplusplus
extern "C" {
#endif

int mgb64_headless_main(int argc, char **argv);

// Register the shell's SDL window + GL context so the engine adopts them
// instead of creating its own (see src/platform/host_window.h). Call before
// booting the engine.
void platformSetHostWindow(void *sdl_window, void *gl_context);

// Register the shell's WebGPU objects (instance/adapter/device/queue/surface +
// the surface's WGPUTextureFormat, all opaque as void*/int) so the engine
// adopts them instead of creating its own. Call after platformSetHostWindow,
// before booting the engine. See src/platform/host_window.h.
void platformSetHostWebGpu(void *instance, void *adapter, void *device,
                           void *queue, void *surface, int surface_format);
int  platformHasHostWebGpu(void);

// Boot configuration for the launcher -> game handoff. Optional fields use
// -1 / 0 / NULL to mean "default".
typedef struct {
    const char *rom_path;   // NULL/empty => engine auto-detects a ROM
    const char *save_dir;   // NULL => default save directory
    int level_id;           // -1 => normal boot to the frontend
    int difficulty;         // -1 => default (0=Agent..3=00Agent)
    int multiplayer;        // 0/1
    int players;            // 2..4 when multiplayer
    int mp_stage_id;        // -1 => default MP stage
    const char *level_slug; // preferred over level_id when non-NULL (e.g. "dam")
    int preset;             // 0=default, 1=faithful, 2=faithful-hd, 3=remaster
} MgbBootConfig;

// Boot the engine with the given config. Blocks until the game exits; returns
// the engine's exit code. The caller must platformSetHostWindow() first if it
// wants the engine to render into its window. Implemented by synthesizing a
// CLI invocation and delegating to mgb64_headless_main(), so it shares the
// exact engine boot path.
int mgb64_engine_boot(const MgbBootConfig *cfg);

// Fatal-crash write mirror (Windows): DiagLog_install() saves the pre-tee
// console fd and mgb64.log's raw fd here, so the engine's crash writers can
// bypass the tee pipe — its reader thread dies with the process, making a
// crash-time pipe write a scheduler race (or a hang if the pipe is full).
// -1 = tee not installed. Defined in src/platform/main_pc.c.
extern int g_diagLogRealErrFd;
extern int g_diagLogFileFd;

// In-game overlay hooks (mirrors src/platform/app_overlay_hooks.h). The app
// registers these before boot; the engine's event pump + frame-end call them.
typedef struct {
    void (*process_event)(const void *sdl_event);
    int  (*wants_input)(void);
    void (*render)(void);
} AppOverlayHooks;
void platformSetOverlayHooks(const AppOverlayHooks *hooks);

// Joystick instance id of the player-1 pad (engine slot 0), or -1 when absent.
// The overlay filters its gamepad toggle to P1 so P2-4 presses in split-screen
// reach their own N64 pad instead of freezing everyone into the overlay.
int platformGetPad0InstanceId(void);

// Active player count (1 in the frontend/boot). The overlay uses this to show
// the honest per-mode footer: single-player pauses the sim when open, MP does
// not (you cannot freeze the other players' clocks). Defined in platform_sdl.c.
int platformGetPlayerCount(void);

#ifdef __cplusplus
}
#endif

#endif  // MGB64_ENGINE_ENTRY_H
