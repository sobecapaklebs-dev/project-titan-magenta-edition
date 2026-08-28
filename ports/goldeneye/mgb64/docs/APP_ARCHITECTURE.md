# MGB64 App Architecture (portable in-process ImGui shell)

The MGB64 app is **one C/C++ codebase compiled into the `ge007` binary**. There
is no separate launcher process and no per-OS UI toolkit: a
[Dear ImGui](https://github.com/ocornut/imgui) layer renders inside the same
SDL2 + OpenGL window the game uses. This document describes the seams so that
Windows and Linux are a straightforward follow-through from the macOS-validated
implementation.

> Status: **macOS (Apple Silicon) is fully implemented and validated.** Windows
> (MSYS2/MinGW) and Linux are wired in the build and use only portable seams
> (below); their build-and-launch is verified in CI (Part 3), not yet by the
> maintainers. See "What's validated where".

---

## 1. Big picture

```
                    ge007 (single binary, per OS)
   ┌───────────────────────────────────────────────────────────┐
   │  App shell  (src/app/, C++17 + Dear ImGui)                 │
   │   AppHost: owns SDL2 window + GL context + ImGui           │
   │                                                            │
   │   State A: LAUNCHER  (no ROM)   — ui_launcher/rom/settings │
   │     · insert ROM (nfd)  · settings  · launch options       │
   │                         │  Play →                          │
   │   State B: IN-GAME   mgb64_engine_boot() [blocks]          │
   │     + overlay (F1): ui_overlay — live settings/relaunch    │
   └───────────────────────────────┬───────────────────────────┘
                                    │  C seams (extern "C")
   ┌────────────────────────────────▼──────────────────────────┐
   │  Engine (unchanged sim/render) + config + rom_io           │
   └───────────────────────────────────────────────────────────┘
```

**Entry & threading.** `src/app/main_app.cpp` owns `main()`. It triages args:
automation/diagnostic invocations (`arg_triage.cpp`) delegate to the unchanged
engine entry `mgb64_headless_main()`; everything else opens the launcher. The
launcher runs its own ImGui loop on the main thread. On **Play**, the shell
hands its window to the engine and calls `mgb64_engine_boot()`, which **blocks**
running the game on the same thread. The in-game overlay is not a second loop —
the engine calls back into ImGui via hooks at the right moments (below).

**Renderer.** v1 is **OpenGL on all platforms** (one `imgui_impl_opengl3`
integration). `AppHost` requests the exact GL attributes the engine uses
(`platform_sdl.c`: macOS 4.1-core forward-compatible, others 3.3-core) so the
engine's fast3d renderer works when it adopts the context. Native Metal +
ImGui-over-Metal is a Part-2 enhancement.

---

## 2. The seams (and how each OS binds them)

Every OS-specific concern is isolated behind one of these seams. Porting = making
each row work on the target OS; the UI/logic above them is identical everywhere.

| Seam | Contract | macOS | Windows (MinGW) | Linux |
|---|---|---|---|---|
| **Window + GL context** | `AppHost` creates it; `platformSetHostWindow()` hands it to the engine, which **adopts** it in `platformInitSDL()` instead of creating its own (`src/platform/host_window.{h,c}`) | SDL2 (Homebrew/framework) | SDL2 (MSYS2) | SDL2 (distro) |
| **GL symbol loading** | ImGui GL3 backend loads its own; the shell calls `glClear/glViewport` | `<OpenGL/gl3.h>` (framework) | `glad` (`lib/glad`) | `glad` |
| **Native file dialog** | nfd (`lib/nfd`), `NFD_OpenDialogU8` | `nfd_cocoa.m` (+AppKit, UniformTypeIdentifiers) | `nfd_win.cpp` (COM) | `nfd_portal.cpp` (+`dbus-1`) |
| **Prefs directory** | `SDL_GetPrefPath("MGB64","MGB64")` → `mgb64_app.ini` (`app_config.cpp`) | `~/Library/Application Support/…` | `%APPDATA%\…` | `~/.local/share/…` |
| **Embedded UI font** | compiled-in Roboto Medium (`app_font.h`) | — same everywhere — | | |
| **Overlay hooks** | `src/platform/app_overlay_hooks.{h,c}`: engine calls `platformOverlayRender()` (before swap in `gfx_end_frame`) + `platformOverlayProcessEvent()`/`platformOverlayWantsInput()` (in `platformPollEvents`); the app sets function pointers. Engine stays C — zero ImGui symbols. | — same everywhere — | | |
| **Config schema** | `src/app/config_schema.h` (plain types) implemented by `src/platform/config_schema.c` over the `settings.c` registry — the settings UI is fully auto-generated | — same everywhere — | | |
| **Return to launcher** | re-exec the app binary for clean engine state | `execv` | `spawn+exit` (Part 3) | `execv` |
| **Automation bypass** | any automation flag → `mgb64_headless_main()` verbatim → byte-identical | — same everywhere — | | |

**Why the app lib is a separate CMake target (`mgb64_app`).** It keeps the
C-only decomp flags (`-fms-extensions`, `-Wno-implicit-function-declaration`, …)
off the C++/ObjC code, and — critically — keeps the engine's `src/`, `include/`
dirs *off* the app include path. Those dirs contain headers (`sched.h`,
`byteswap.h`, a `math.h` shim) that shadow the system angle-includes SDL/pthread/
ImGui pull in. App↔engine seam headers therefore live in the collision-free
`src/app/` dir; the engine-side implementations live in `src/platform/`.

---

## 3. File map

**App shell — `src/app/` (C++17):**
- `main_app.cpp` — entry, arg triage, lifecycle, Play/overlay wiring.
- `app_host.{h,cpp}` — window/context/ImGui ownership; frame begin/end; BMP capture.
- `app_theme.{h,cpp}` + `app_font.h` — palette (from `macos/BRANDING.md`), metrics, embedded font.
- `ui_launcher.{h,cpp}` — nav rail + panel router; ROM panel; launch options.
- `ui_settings.{h,cpp}` — auto-generated settings tabs.
- `ui_overlay.{h,cpp}` — in-game F1 overlay.
- `rom_validate.{h,cpp}` — portable ROM validation (header-only inspection).
- `app_config.{h,cpp}` — app prefs (last ROM).
- `arg_triage.{h,cpp}` — automation-flag allow-list.
- `config_schema.h` / `engine_entry.h` — clean C interfaces to the engine.

**Engine-side seams — `src/platform/` (C, compiled into `ge007`):**
- `host_window.{h,c}` — window/context handoff.
- `app_overlay_hooks.{h,c}` — overlay hook registry.
- `config_schema.c` — schema translation over `settings.c`.
- `main_pc.c` — `mgb64_headless_main` + `mgb64_engine_boot` (under `-DMGB64_APP`).
- `platform_sdl.c` — window adoption + overlay event feed (edited).
- `fast3d/gfx_pc.c` — overlay render call before the GL swap (edited).

---

## 4. Build

All platforms: one CMake tree, `MGB64_APP=ON` by default.

**macOS (validated):**
```sh
brew install sdl2 cmake pkg-config
cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
./build/ge007                    # launcher
./macos/Scripts/build_gl_app.sh  # → build-macos-app/MGB64.app (asset-free, ad-hoc signed)
```

**Windows (MSYS2 MinGW 64-bit):**
```sh
pacman -S mingw-w64-x86_64-{gcc,cmake,SDL2,pkg-config,ninja}
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
./scripts/package_windows_zip.sh --binary build/ge007.exe --version vX.Y.Z
# → dist/mgb64-windows-vX.Y.Z.zip  (ge007.exe + SDL2.dll + docs)
```

**Linux:**
```sh
sudo apt-get install cmake pkg-config libsdl2-dev libgl1-mesa-dev libdbus-1-dev file
cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
./scripts/package_linux_appimage.sh --binary build/ge007 --version vX.Y.Z
# → dist/mgb64-linux-vX.Y.Z.{AppImage,tar.gz}  (bundles SDL2)
```

`-DMGB64_APP=OFF` builds the bare engine CLI (no ImGui/nfd), useful for minimal
or headless-only builds. Release automation (CI for Windows/Linux + a local
macOS build) is documented in **[RELEASING.md](RELEASING.md)**.

---

## 5. What's validated where

- **macOS (Apple Silicon):** fully validated — launcher, ROM picker + validation,
  auto-generated settings (97), launch-to-level, in-game overlay, `.app` bundle.
  The overlay renders over live gameplay with no fast3d GL-state corruption.
- **The ROM-free launcher is CI-smokeable on all three OSes** (it needs no game
  data): `MGB64_APP_SMOKE_FRAMES=N [MGB64_APP_SMOKE_SHOT=path]` renders N frames
  and exits, and `MGB64_APP_DUMP_SCHEMA=1` self-checks the config schema. This is
  how Part 3 CI verifies Windows/Linux build-and-launch without a ROM.
- **Gameplay validation stays local (macOS) + community reports** on Windows/
  Linux, and the docs stay honest about that boundary.

---

## 6. Invariants (do not break)

1. **Automation is byte-identical.** Any automation/diagnostic flag (see
   `arg_triage.cpp`) must run `mgb64_headless_main()` unchanged. The overlay/host
   hooks are all no-ops when the app never registers them (the automation path).
2. **The engine stays C.** It references overlay/host/config seams only through
   the `extern "C"` headers in `src/platform/`; no ImGui or C++ symbols leak in.
3. **Asset-free always.** Every artifact passes `macos/Scripts/verify_asset_free.sh`.

---

## 7. Controller support (handheld / gamepad)

The shell is fully operable with a standard XInput pad (SDL2 GameController) and
no keyboard/mouse — the target being gaming handhelds (XInput-class controls, no keyboard). Only the
generic GameController path is used; there is no device-specific code.

**ImGui navigation (MC.1).** `ImGuiConfigFlags_NavEnableGamepad` is set in
`AppHost::init`; the bundled ImGui SDL2 backend (`AutoFirst` mode) opens the
first controller and feeds nav inputs. The launcher's nav rail and every panel
(ROM, Settings tabs, Launch, Controls, Modes, Diagnostics) are standard
focusable widgets, so they navigate out of the box; the active tab gets initial
focus. In-game the F1 overlay reuses the same ImGui context, so it navigates
identically. Overlay controls: **Back/View** toggles it (F1 still works), **A**
selects, **B** backs out one level (cancel → hide settings → close), yielding to
ImGui's own combo/popup cancel first. Back was chosen over Start (the MC.1 brief
predated weighing the displacement cost): Start = watch is GoldenEye's core
system interaction, while Back's only prior duty was the port-invented
weapon-prev binding — so Start stays the N64 Start. The toggle button is a named
constant (`Overlay_gamepadToggleButton()`, `ui_overlay.h`) and is excluded from
the rebind capture so no game action can collide with it. The overlay engine
also has no controller knowledge — it drives ImGui through the same
`app_overlay_hooks` seam.

The engine and ImGui each `SDL_GameControllerOpen` controller 0; this is safe
because SDL2 refcounts controller handles, so neither closing (device
remove / shutdown) dangles the other's pointer.

**Input gate.** When the overlay is open it owns input: `osContGetReadData`
returns neutral pads while `platformOverlayWantsInput()` is true — the
polled-input analogue of the event-swallow in `platformPollEvents` — and the
right-stick look injection in `lvl.c` (which bypasses `osContGetReadData`)
zeroes under the same condition, so pad nav never leaks into gameplay. Both
gates are no-ops on the automation path (no overlay hooks registered), so
byte-identity is preserved.

**Gamepad rebinding (MC.3).** 14 player-1 actions are rebindable in the Controls
panel's Gamepad tab, mirroring the keyboard registry: each binds to a
`SDL_GameControllerButton` or a trigger axis (LT/RT), persisted to
`ge007_gp_bindings.ini`, with reset-to-default. `stubs.c` reads them through
`gamepadBindingActive()`. The sticks stay fixed (movement + look, preserving the
radial-deadzone/aim mapping). Pad **Start** is the N64 Start (watch); because
**Back/View** drives the app overlay, the previous-weapon default moved to the
**Right-Stick click**. Player 2–4 pads use fixed defaults — multiplayer
rebinding is out of scope. Under `--deterministic` / freeze-input the bindings
force defaults so scripted runs stay byte-identical.
