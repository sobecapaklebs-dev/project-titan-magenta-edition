# Project Titan: Magenta Edition — BuildFix28 Final Source Status

Date: 2026-08-20
Baseline: BuildFix27 final target-ready source with real frozen FCEUmm

## Frozen source-side state

- `BUILDFIX28_SOURCE_IMPLEMENTATION_COMPLETE=1`
- `GLOBAL_HOST_REGRESSION_PASS=1`
- `REHYDRATABLE_SOURCE_GATE=PASS`
- `FCEUMM_REAL_INTEGRATED=1` (inherited unchanged from BF27 freeze)
- `PROJECT_NES_SOURCE_COMPLETE=1`
- `SMART_IGR_SOURCE_COMPLETE=1`
- `INGAME_OVERLAY_SOURCE_COMPLETE=1`
- `UNIFIED_INPUT_SOURCE_COMPLETE=1`
- `THEME_ENGINE_V4_SOURCE_COMPLETE=1`

## External validation state

- `TARGET_BUILD_VALIDATED=0` — external PS2DEV/PS2SDK validation pending.
- `PCSX2_VALIDATED=0` — external validation pending.
- `HARDWARE_VALIDATED=0` — physical PS2/controllers validation pending.

No target, emulator or hardware PASS is inferred from host validation.

## Material coverage

The source contains Session Service; adaptive Smart IGR; Overlay Lite paths for Native/Project NES/UL/Neutrino; Unified Input; DS2 baseline; DS3/DS4 PADEMU integration; DualSense USB/Bluetooth; Xbox One/Series USB GIP and Bluetooth/BLE; asynchronous Early Input; keyboard/mouse; platform overhead/fast paths; pikaVR/VlombisCore pre-freeze work; Theme API v4/scene-v4 multilayer runtime/renderer/profiler/toolchain; and the historical BF27 platform baseline including real FCEUmm.

## Library preservation policy
Final preservation uses `/Project Titan/BuildFix28/FINAL/`. Checkpoint history is organized under `INTERMEDIATE/` and is not a build dependency once the full final archive passes the independence gate.
