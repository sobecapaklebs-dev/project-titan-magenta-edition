# Project Titan Magenta Edition RC14 — BuildFix17
## Theme Engine + Scarlet Flow + Settings transition fix

### Delivered

- New kernel-resident Theme Engine (`titan_theme.c/.h`).
- Persistent active theme selection in `mass:/TITAN/settings/active_theme.cfg` using temp/backup atomic replacement.
- Built-in `Default` fallback theme that cannot be removed and preserves the existing Project Titan look when no installed theme is selected.
- New `Settings > Themes` category.
- Theme list uses the same Settings depth transition as every normal category.
- `★Debug Settings` now enters through the same shared transition helper; its prior instant/teleport-style entry is removed.
- Package Installer recognizes `.tthm` in addition to `.pkg`.
- TPKG v2 app type `theme` / reserved entrypoint `@theme`.
- Installed themes live in `mass:/TITAN/themes/<THEME-ID>/`.
- Theme packages are declarative only: `.elf`, `.tmod`, `.irx`, `.so` and `.vsm` payloads are rejected.
- Optional theme preview contract: raw RGB565 256x128.
- Shell styling API v1: accent color, selected HOME scale, tile spacing, title Y offset, header/help/chrome alpha, horizontal/vertical motion response.
- Theme settings never change button mappings, navigation state machines, application launching or package behavior.

### Scarlet Flow 1.0.0

ID: `SCARLETFLOW`

Visual contract:

- pure black top and bottom background;
- no magenta in the theme palette;
- subtle translucent scarlet/red ribbons;
- brighter red/pale-scarlet fold highlights;
- low-alpha red flow particles;
- atmosphere blobs disabled so the black canvas remains clean.

The Flow3D renderer is a PS2-native perceptual adaptation rather than a claim to reproduce Sony's original shader implementation. It keeps the known-good VU1 PATH1/XGKICK ribbon path and adds:

- multiple Gerstner-like carriers with horizontal and vertical displacement;
- temporal decorrelated gradient/value-noise modulation;
- a depth-Z term projected back into the existing XY VU1 rails, giving safe 2.5D parallax without adding a second GS perspective pipeline;
- per-strip depth/tint/alpha hierarchy and an additive central highlight;
- existing VU1 Fresnel-style edge weighting;
- flow particles driven by the same time/motion field;
- right-to-left idle flow;
- horizontal navigation speed impulse;
- vertical top-menu crest/offset response;
- day/night frequency and speed multipliers from the system clock.

### Settings animation repair

`TitanShellScene_OpenSettingsCategoryAnimated()` is now the only interactive root->category entrance path. It always resets `settings_depth_transition` to zero before opening the subpage. Both `Themes` and `★Debug Settings` therefore use the same standard slide/depth/fade progression as Users, Saved Data, Video Output, System and About.

### Validation

Host validation passed:

- package installer transactions
- TPKG builder/verifier, including `.tthm` and executable-payload rejection
- theme service scan/validation/apply/persistence
- theme engine source contract
- Settings/Home contracts
- XMB spline runtime + VU1 geometry contracts
- VU1 internal-edge protection
- 480i/720p renderer contracts
- audio/runtime/source hardening
- boot path / installer transaction / curated assets
- `project-titan-source-validation: OK`

Asset integrity versus BuildFix16:

- `kernel/assets`: 241 files, 0 differences
- `bootloader/assets`: 3 files, 0 differences

The PS2DEV/R5900 toolchain is not installed in the artifact environment, so the final MIPS link was not executed here. Physical PS2 rendering remains the final authority for performance and exact visual tuning.
