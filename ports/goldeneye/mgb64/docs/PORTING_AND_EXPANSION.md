# Porting & Expansion Guide

This guide is for native-port work that goes beyond parity: settings, input,
direct-boot modes, renderer toggles, and validation lanes.

## Targets

MGB64 has two practical targets:

- Native port: CMake build with `NATIVE_PORT`, used for normal play.
- N64 matching build: keep original behavior byte-stable.

Native-only work must be invisible to the matching target. Keep port code behind
`#ifdef NATIVE_PORT` or an existing port abstraction, and keep defaults stock.
For example, the original gameplay FOV (`Video.FovY = 60`) is what `--faithful`
restores; any modern default a port feature adds must stay reversible to that
stock behavior.

Never add ROM bytes, ROM-derived data, SDK payloads, screenshots, or generated
captures. The contamination guard and release checks must stay green.

## User Settings

User-facing options live in `ge007.ini` through the settings/config system.
Register settings before `configInit()` and prefer explicit min/max bounds.

Current native settings include:

- `Video.WindowWidth`, `Video.WindowHeight`, `Video.WindowX`, `Video.WindowY`
- `Video.Display`
- `Video.WindowMode`
- `Video.FullscreenWidth`, `Video.FullscreenHeight`, `Video.FullscreenRefresh`
- `Video.VSync`, `Video.FrameCap`
- `Video.Gamma`, `Video.RenderScale`, `Video.MSAA`, `Video.FovY`,
  `Video.RetroFilter`
- `Input.MouseSensitivity`, `Input.MouseSensitivityAim`, `Input.InvertY`,
  `Input.GamepadLookSpeed`
- `Audio.MasterVolume`, `Audio.DeviceSamples`

Users can inspect settings with:

```sh
./build/ge007 --list-settings
./build/ge007 --config-override Video.FovY=70 --level dam
```

Use settings for persistent user preferences. Use `GE007_*` environment
variables for diagnostics, deterministic automation, and temporary experiments.

## Direct-Boot Modes

Direct-boot paths should reuse engine setup functions rather than hand-rolling
state. The solo `--level` path and multiplayer `--multiplayer --players
--mp-stage --scenario` path are the templates.

For multiplayer automation, `--mp-timelimit SECS` forces a short deterministic
match length for smoke testing. It is applied after MP scenario setup so normal
menu-driven matches are not left with a stale override.

## Input Model

Keyboard and mouse are player 1. SDL gamepads map to N64 controller slots and
are consumed per player by the existing game input path. New input work should
preserve that split:

- Platform code opens and samples devices.
- `osContGetReadData()` fills per-slot controller data.
- Game code reads player-specific state through the existing controller/player
  plumbing.

Avoid global input state for anything that must work in split-screen.

## Validation Lanes

Every feature should have a reproducible lane. Existing lanes include:

- `tools/playability_smoke.sh` for solo boot/playability.
- `tools/mp_smoke.sh` for split-screen render separation.
- `tools/mp_smoke.sh --timelimit SECS` for MP match-timer boundary coverage.
- `tools/soak_stability.sh` for longer per-stage stability.
- `tools/asan_smoke.sh` for sanitizer runs.
- `scripts/ci/check_no_rom_data.sh` and the release-readiness checks for provenance.

Captured traces, screenshots, and logs are generated from the user's ROM and
must remain local.

## Change Checklist

1. Keep native work `NATIVE_PORT`-scoped or isolated in platform code.
2. Preserve stock behavior at the default setting.
3. Add a setting only for persistent user preference; otherwise use `GE007_*`.
4. Add or extend a validation lane with a concrete assertion.
5. Update the relevant docs: this guide, `INSTRUMENTATION.md`, and any feature
   plan or README section users will rely on.
6. Run the build and contamination checks before committing.

## Common Pitfalls

- Enum signedness can turn `-1` sentinels into unsigned values. Cast explicitly
  where decomp enums are used as sentinels.
- Raw ROM offsets are not native pointers. Convert through the proper offset
  helper before dereferencing.
- Direct-boot paths may skip frontend setup. If a direct-boot-only crash or
  state gap appears, compare against the normal menu path and port the missing
  setup step deliberately.
- Renderer changes are high blast-radius. Keep diagnostics env-gated and prove
  both solo and split-screen paths still render.

## Related Docs

- [INSTRUMENTATION.md](INSTRUMENTATION.md)
- [ROM_COMPARISON.md](ROM_COMPARISON.md)
- [CODING_STYLE.md](CODING_STYLE.md)
