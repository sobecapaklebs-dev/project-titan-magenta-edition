# MGB64 -- macOS App Shell

Native macOS wrapper for the N64 game engine port. This directory contains the
Swift/AppKit UI layer that hosts the C game engine in an app bundle, providing
ROM selection, input handling, preferences, and distribution tooling.

The app ships **zero game data**. Users provide their own ROM file (BYOR).


## Architecture

The macOS app splits work across threads. A C bridge layer (`GameBridge.h` /
`GameBridge.c`) sits between the Swift UI and the C game engine. Cross-thread
state is protected by `os_unfair_lock`; simple flags use `_Atomic`.

```
Main Thread (AppKit)               Game Thread
+---------------------------+      +---------------------------+
| NSApplication.run()       |      | game_init(rom, save_dir)  |
| Window / Menu / Toolbar   |      | game_run()       [blocks] |
| Input capture (keyboard,  | ---> | platformFrameSync()       |
|   mouse, gamepad)         |      | gfx_run_dl()              |
|   via GameInputState      |      +---------------------------+
| Preferences / About       |
| OnboardingFlow            |                Audio Thread
| game_request_shutdown() --|----> +---------------------------+
+---------------------------+      | Managed by audio backend  |
                                   +---------------------------+
```

**Input flow:** `InputManager.swift` accumulates keyboard, mouse, and gamepad
events on the main thread into a `GameInputState` struct, then calls
`game_set_input()`. The game thread reads this state atomically at the start
of each frame.

**Render surface:** Before `game_init()`, the main thread calls
`game_set_render_surface()` with a `CAMetalLayer` (or NULL for OpenGL).
Resize events are forwarded via `game_notify_resize()`.

**Shutdown:** The main thread calls `game_request_shutdown()`, which sets an
atomic flag checked by `platformFrameSync()`. The game finishes its current
frame, cleans up, and `game_run()` returns.


## Directory Structure

```
macos/
  README.md                  This file
  BRANDING.md                Brand guide: colors, icon, voice, typography

  Sources/
    AppDelegate.swift        NSApplicationDelegate; creates window, spawns game thread
    GameBridge.h             C API between Swift and the game engine (thread annotations)
    GameBridge.c             C implementation of the bridge layer
    MGB64-Bridging-Header.h  Exposes GameBridge.h to Swift
    BrandConfig.swift        Single source of truth for all user-facing brand strings
    InputManager.swift       Keyboard/mouse/gamepad -> GameInputState accumulation
    GameRenderView.swift     NSView subclass hosting the Metal/GL render surface
    MenuBarManager.swift     Main menu bar construction and actions
    DockMenuProvider.swift   Right-click Dock menu items
    WindowToolbar.swift      NSToolbar configuration
    PreferencesView.swift    Settings panel (video, audio, input, etc.)
    OnboardingFlow.swift     First-launch ROM selection and setup wizard
    ROMPickerView.swift      ROM file selection and validation UI
    SplashView.swift         Launch splash screen
    AboutView.swift          About panel (brand name, version, disclaimer)
    ErrorPresenter.swift     Centralized error/alert presentation
    PerformanceOverlay.swift FPS counter and debug stats overlay
    ScreenshotManager.swift  In-game screenshot capture and save

  Resources/
    Info.plist               App bundle metadata (CFBundleName, version, etc.)
    Entitlements.plist       Sandbox and hardened runtime entitlements
    Localizable.xcstrings    Localized string catalog
    PrivacyInfo.xcprivacy    Apple privacy manifest

  Assets.xcassets/
    Contents.json            Asset catalog root
    AccentColor.colorset/    Brand accent color for system controls
    AppIcon.appiconset/      Icon slot manifest; icons are generated at build time

  Scripts/
    build_app_bundle.sh     Local unsigned .app bundle build
    build_universal.sh       Universal C library build (arm64 + x86_64)
    sign_and_notarize.sh     Code signing + Apple notarization
    create_dmg.sh            DMG installer creation
    verify_asset_free.sh     Asset-free binary/app verification
```


## Building

### Prerequisites

- macOS 13.0+ (Ventura)
- Xcode 15+ or clang with Swift support
- SDL2 (`brew install sdl2`)
- CMake 3.16+
- Python 3 and `iconutil` (provided by Apple's command line tools)

### Status (be aware before you start)

- ✅ **C static library build works** and is verified **asset-free**.
- ✅ **Local Swift → `.app` packaging works** via
  `macos/Scripts/build_app_bundle.sh`. The bundle is unsigned, links the Swift
  shell against `build-macos/libge007_lib.a`, and still ships zero game data.
- 🚧 **Signing/notarization is deferred** (requires an Apple Developer account).
  Unsigned builds run locally; macOS Gatekeeper will warn on first launch.

### Step 1: Build the local unsigned app

```bash
./macos/Scripts/build_app_bundle.sh --release
```

This produces `build-macos/MGB64.app` and, as an intermediate artifact,
`build-macos/libge007_lib.a`. The `-DMACOS_APP_BUNDLE=ON` CMake flag excludes
`main()` from `main_pc.c` so the Swift `AppDelegate` can serve as the entry
point.
The script also generates a project-owned `AppIcon.icns` from auditable Python
drawing code, so no binary icon asset needs to be tracked in the public tree.

The local app bundle is not self-contained for redistribution: it links against
the SDL2 dylib reported by `pkg-config`, so local builders should install SDL2
with Homebrew (`brew install sdl2`). The effective minimum macOS version of that
local bundle also depends on how the local SDL2 dylib was built; release-quality
distribution should use a controlled SDL2 build before signing/notarization.
If the local SDL2 dylib was built with a newer minimum macOS version than the
script's requested deployment target, `build_app_bundle.sh` raises the bundle's
effective deployment target to match SDL2 and reports both values. This keeps
the unsigned source-build bundle honest about the host dependency it links
against, but it is still not a redistributable release asset.

For a distributable app candidate, use a controlled SDL2 build with the intended
minimum macOS version and make the packaging check fail closed:

```bash
PKG_CONFIG_PATH=/path/to/controlled-sdl2/lib/pkgconfig \
  ./macos/Scripts/build_app_bundle.sh --release \
    --deployment-target 13.0 \
    --strict-deployment-target \
    --bundle-sdl2
```

`--strict-deployment-target` fails if SDL2 would force the app above the
requested minimum macOS version. `--bundle-sdl2` copies the linked SDL2 dylib
into `Contents/Frameworks` and rewrites the app executable to load that bundled
copy, which is the shape expected before signing and notarization.

Verify the packaged app carries no ROM media:

```bash
./macos/Scripts/verify_asset_free.sh build-macos/MGB64.app   # -> PASSED
```

For app bundles, the verifier checks both the Mach-O executable and the
`Contents/Resources` allowlist, including the generated `AppIcon.icns`.

To exercise the tag-workflow library lane locally, build the universal engine
library in a separate directory and verify it remains asset-free:

```bash
./macos/Scripts/build_universal.sh --release --build-dir build-macos-universal
./macos/Scripts/verify_asset_free.sh build-macos-universal/libge007_lib.a
```

### Step 2: Sign + notarize (needs an Apple Developer account — deferred)

```bash
./macos/Scripts/sign_and_notarize.sh build-macos/MGB64.app   # requires Developer ID + notarytool creds
./macos/Scripts/create_dmg.sh build-macos/MGB64.app
```

Until then, distribute source builds only; users can build and run the unsigned
app locally.


## Branding

The brand name is centralized in `Sources/BrandConfig.swift`. To rename the
app, change that file and `Resources/Info.plist` -- nothing else. Internal
code (GameBridge, the port, build scripts) uses "ge007" or generic
identifiers. The brand name is a UI-layer concern only.

- **Full name:** The Man with the Golden Build
- **Short name:** MGB64
- **Bundle ID:** `com.mgb64.app`
- **Tagline:** "An N64 game engine ported natively to macOS, built on a faithful decompilation."

See `BRANDING.md` for the full design guide (colors, icon concept,
typography, voice and tone).


## ROM Handling (BYOR)

The app ships zero game data. Users supply their own ROM file through the
onboarding flow or ROM picker.

- `game_validate_rom()` checks file size, detects byte order, and computes a
  SHA1 hash against known good checksums.
- Supported formats: `.z64` (big-endian), `.v64` (byte-swapped), `.n64`
  (little-endian). Byte order is auto-detected from the first four bytes.
- Validation runs on any thread and does not modify global state.
- The ROM path is stored in user defaults for subsequent launches.


## Testing

```bash
./macos/Tests/run_tests.sh
./macos/Scripts/verify_asset_free.sh build-macos/MGB64.app
```

The test harness exercises ROM-file validation and the app-bundle asset-free
verifier's positive/negative resource cases. The standalone verifier confirms
that the packaged app does not contain ROM media. The broader repository
contamination guard runs through `scripts/ci/check_release_ready.sh` on every
push.


## Distribution

Scripts in `Scripts/` cover the local unsigned build and the deferred signing /
packaging helpers. The only verified distribution-adjacent path today is the
asset-free local source-built `.app`. The scripts now support the stricter
app-candidate shape (`--strict-deployment-target --bundle-sdl2`), but signed /
notarized DMG distribution still needs maintainer credentials, a controlled SDL2
runtime, and release-policy validation.

| Script | Purpose |
|--------|---------|
| `build_app_bundle.sh` | Local unsigned `.app` bundle |
| `build_universal.sh` | Universal C library build (arm64 + x86_64) |
| `sign_and_notarize.sh` | Optional code signing + Apple notarization |
| `create_dmg.sh` | Optional DMG installer creation |
| `verify_asset_free.sh` | Asset-free binary/app verification |


## Contributing

- All user-facing strings must reference `Brand` constants from
  `BrandConfig.swift`. Never hardcode the app name.
- Zero force unwraps (`!`) in Swift code.
- All new UI must support Dark Mode and VoiceOver.
- Run `verify_asset_free.sh` before committing changes that touch asset
  files or resource directories.
- Thread safety: use `os_unfair_lock` for cross-thread shared state,
  `_Atomic` for simple boolean/integer flags.
- The game engine is a C library. All Swift-to-C calls go through
  `GameBridge.h`. Do not call engine internals directly.
