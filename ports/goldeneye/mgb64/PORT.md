# What "port" means for MGB64

MGB64 is a **native source port built on a decompilation** of a 1997
Nintendo 64 first-person shooter. This document defines — precisely and honestly —
what that does and does not mean, so the project's claims are credible. If you
find anything here that doesn't match reality, please open an issue.

## What this IS

- **A faithful decompilation** of the original game's logic: the C and MIPS
  assembly here reproduce the game's actual functions and code-coupled data
  (AI scripts, collision, level/room tables, display lists), in the tradition of
  the [SM64](https://github.com/n64decomp/sm64) and
  [OoT](https://github.com/zeldaret/oot) projects.
- **A native platform layer** (`src/platform/`) that runs that decompiled game
  code on PC/macOS: it loads *your* ROM at runtime, translates N64 RDP/RSP
  display lists to OpenGL, synthesizes the N64 audio, and maps keyboard/mouse +
  gamepad to the N64 controller.
- **Genuinely playable.** All 20 single-player levels boot, play, and reach
  mission-complete; combat, guard AI, weapons, pickups, objectives, menus, and
  audio function. See [Validation coverage](#validation-coverage).

## What this IS NOT

- **Not an emulator** and **not cycle-exact.** It runs the game's *code*, not the
  N64 hardware; timing-sensitive and edge-case behavior may differ.
- **Not guaranteed 1:1 visual/behavioral parity** with original hardware. There
  are documented rendering/audio approximations (see [Divergences](#known-divergences-from-n64-behavior)).
- **Not a forced remaster and not a gameplay reimagining.** The faithful path
  (`--faithful`) stays byte-faithful to the 1997 original, and no visual option
  changes the gameplay simulation. An **opt-in** visual remaster (`--remaster` —
  ambient occlusion, bloom, filmic tonemap, colour grade, and optional user-built
  HD texture packs) is available behind flags and is off in faithful mode. See
  [docs/VISUAL_MODES.md](docs/VISUAL_MODES.md).
- **Not a way to obtain the game.** It ships no ROM and no assets; you supply your
  own legally-dumped copy (see [DISCLAIMER.md](DISCLAIMER.md)).

## The gameplay loop (important clarification)

The native port build (`NATIVE_PORT` / `PORT_FIXME_STUBS`) compiles the
**converged** N64 gameplay coordinator (`lvlManageMpGame`, `src/game/lvl.c`) with
a PC-side timing cap — **not** a minimal stub. Gameplay is driven by the real
decompiled sequencing. Full byte-for-byte matching of this function against the
original is still in progress; that work does **not** affect playability.

## Known divergences from N64 behavior

These are deliberate, documented approximations or in-progress areas. None
prevent normal play.

| Area | Divergence | Status |
| --- | --- | --- |
| Audio | Synthesis-based and asset-free; SFX mapping/owner-slot playback is validated, and the native ABI1 mixer exposes reference-backed diagnostics for envmixer, resampler, ADPCM, and custom-FX pole-filter stages. Startup/gunbarrel music still differs from hardware/reference output in program-34-heavy windows, most likely in the custom-FX pole-filter HLE. | Playable; fidelity work ongoing. |
| Bullet-impact decals | Prop-attached impacts render shade-only by default — the original textured path can corrupt world texture state on the PC renderer. | Compat default; opt-in via `GE007_TEXTURED_PROP_BULLET_IMPACTS=1`. |
| Room visibility | Portal-BFS room admission with a frustum-all fallback for debugging. | Default on; fallback via `GE007_PORTAL_BFS=0`. |
| Room scissoring | Per-room N64 scissor boxes disabled by default (they can under-cover interior seams on the PC renderer). | Compat default; exact via `GE007_EXACT_ROOM_SCISSOR=1`. |
| Sky | Falls back to fog color in some cases. | Known; minor. |
| Level intro cameras | Bond is absent during Dam's initial authored establishing camera and only appears once the later swirl phase begins. | Known parity issue; the swirl renders Bond, but the earlier camera is not fixed. |
| Frontend menus | A briefing/menu material shows a small (~5–6%) brightness residual vs stock. | Cosmetic. The experimental color-scale path remains **default-off** behind `GE007_DIAG_SETTEX_CC_COLOR_SCALE`; pending external visual A/B sign-off before any promotion to default. |
| Save / mission flow | Cross-session EEPROM persistence has a deterministic multi-folder, multi-difficulty smoke check; broader organic menu/mission flows still need expansion. | Smoke validated via `tools/save_persistence_check.sh`. |
| Timing / AI cadence | Some frame-budget/cadence details still converging toward exact N64 timing. | In progress; not visible in normal play. |

A diagnostic env-var reference for these toggles is in
[docs/INSTRUMENTATION.md](docs/INSTRUMENTATION.md).

## Validation coverage

What has been validated (and how) — see [docs/INSTRUMENTATION.md](docs/INSTRUMENTATION.md)
for the tooling:

- **All 20 single-player levels**: boot → render → combat → progression →
  mission-complete on deterministic routes, with **zero crashes** on multi-hundred-frame soak runs.
- **Combat / AI**: guards spawn, see/hear, take cover, return fire, and die;
  weapon and item pickup; hit reactions.
- **Rendering**: blend patterns classified across 5 levels (1.2M+ triangles, zero
  unclassified); per-level rendering audited.
- **Audio**: SFX + music banks decode and play from the user's ROM; SFX
  submission/chain mapping is instrumented, and final PCM peak/rail metrics are
  available through `GE007_AUDIO_TRACE`.
- **Save persistence**: deterministic folder 0 Dam/Agent completion and folder
  1 Dam-through-Runway Secret Agent completion are written across separate
  processes to an isolated `--savedir`, then both reload from
  `ge007_eeprom.bin` in a final process (`tools/save_persistence_check.sh`).

Not yet validated end-to-end: long-session stability, exhaustive organic
save/menu mission flows, exhaustive per-level visual parity, Linux ROM-backed
runtime play smoke, and Windows/MSYS2 setup. Linux/GCC configure/build plus the
ROM-free CTest suite is wired into the local preflight and source-archive smoke
lanes and must pass for the exact public launch commit.

## Feature maturity

| Feature | Status |
| --- | --- |
| Decompiled game systems (AI, combat, levels, menus) | ✅ Working |
| Native renderer (display-list → OpenGL) | ✅ Working, with documented compat paths |
| Audio (SFX + music synthesis) | ✅ Working/playable; music fidelity still being matched |
| Input (keyboard/mouse + gamepad) | ✅ Working |
| Runtime ROM asset loading (asset-free binary) | ✅ Working |
| Gameplay coordinator parity | 🟡 Converged + playable; byte-match in progress |
| Save / mission-flow persistence | 🟢 Cross-session multi-folder smoke validated; broader organic flow coverage in progress |
| N64 ROM (byte-matching) rebuild | 🟡 Needs data-table extraction wired into the N64 link |
| Prebuilt distributables | 🟢 macOS `.app` (zip), Windows portable `.zip`, and Linux AppImage/`.tar.gz` ship on tagged releases (see docs/RELEASING.md); Developer ID signing / notarization still deferred (macOS `.app` is unsigned) |

## Reporting a divergence

Open an issue with: platform + ROM region, the level/scene, what the original
hardware does vs. what MGB64 does, and steps to reproduce (a save state or a
short clip helps — but **no ROM or copyrighted imagery**, please). If you can,
note whether toggling a relevant `GE007_*` env var changes the behavior.
