# Project Titan TargetFix8 — Artwork / Live-Area Rebase

## Scope closed in this rebase

- Application live-area background primary contract is **640x480 RGB565** (614400 bytes).
- **512x256 RGB565** remains accepted only as a legacy/fallback application background.
- Lower application-background sizes are rejected by the TPKG validator/installer contract.
- 640x480 backgrounds are streamed without reserving a second full GS framebuffer: 480i/480p uses the existing 512x256 cached-layer allocation in bounded tiles; compact/native-720p uses the existing 256x64 scratch path.
- A declared application background owns the Home live-area while it is loading. Waves/ribbons are suppressed and the shell's existing loading-symbol animation is drawn on a neutral backing until the image is published.
- When a selected application background is ready it replaces the shell waves/ribbons in the live-area.
- Titan-native application launch now enters through the same shell launch choreography used by ISO/UL/Disc content rather than a separate visual path.
- The live-area application background remains visible through that choreography and is released at runtime handoff.
- `Titan Native` / `Titan Native Game` launch badges use the same badge component family as ISO/UL launch badges (panel, outline, glyph family, `Powered by:` line and accent-word treatment).
- Startup Cross affordance was replaced with a centered DS2-style circular face button and a geometrically centered Cross, with the prompt pulled into the same visual group.
- Shell/runtime/package/application legacy literals are routed through the active localization compatibility catalog. EN/PT-BR/ES entries were extended for the touched release surfaces, including profile/VMC/storage/network/accessory/package/runtime and dynamic-format messages.
- App package metadata now carries a dedicated `tile.rgb565` resource; installer/catalog fallback still resolves `tile.rgb565` when metadata is absent.

## Prepared app artwork

Each app/port below now has repository-ready RGB565 resources:

| Path | background.rgb565 | tile.rgb565 | icon.rgb565 |
|---|---:|---:|---:|
| `apps/project-nes/resources/` | 640x480 / 614400 B | 256x256 / 131072 B | 64x64 / 8192 B |
| `apps/wlaunchelf/resources/` | 640x480 / 614400 B | 256x256 / 131072 B | 64x64 / 8192 B |
| `ports/goldeneye/resources/` | 640x480 / 614400 B | 256x256 / 131072 B | 64x64 / 8192 B |

## Validation performed in the handoff environment

- `python3 -m py_compile kernel/tools/titan_pkg.py ports/goldeneye/tools/build_private_package.py`: **PASS**.
- TPKG v2 primary 640x480 application-background package verify: **PASS**.
- TPKG v2 legacy 512x256 fallback package verify: **PASS**.
- Invalid smaller application background (320x240): **REJECTED as expected**.
- Preprocessor pass for modified/relevant C translation units: **PASS** for `titan_video.c`, `titan_app_assets.c`, `titan_app_runtime.c`, `titan_applications.c`, `titan_localization.c`, `titan_package_installer.c`, `titan_shell_scene.c` using private header stubs only to compensate for the partial source snapshot.
- Host clang parser-shape audit: no structural parse errors in the modified region of `titan_video.c`; zero structural parse errors in the other touched C units. Host semantic errors from PS2-specific types/APIs are expected with the private stubs and are not treated as target-build validation.
- Localization literal audit of the touched shell/runtime/app/installer surfaces found no remaining likely Portuguese display literal outside the compatibility catalog (one English false positive only).

## Target-build limitation

This handoff environment does **not** contain the full Project Titan repository/PS2SDK header set or `mips64r5900el-ps2-elf-gcc`. Therefore this package does **not** claim a final R5900 link/build pass. Run the normal target build in the real TargetFix8 checkout before GitHub release.

## Apply

From the Project Titan repository root, either extract the ready tarball directly over the checkout, or apply the textual patch and then copy the three prepared `resources/` directories from this bundle.

Text patch command:

```sh
patch -p1 < TITAN_TARGETFIX8_ARTWORK_REBASE.patch
```

The textual patch intentionally does not encode RGB565 binaries; the tarball contains those files at their final repository-relative paths.
