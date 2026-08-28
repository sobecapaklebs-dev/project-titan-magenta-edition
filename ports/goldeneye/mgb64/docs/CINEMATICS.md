# Level Intro / Outro Cinematics

Reference for the animated mission-intro (the camera fly-in / swirl) and the
outro / death cameras: what plays, the settings that shape it, the A/B escape
hatches, and how to validate faithfulness against a stock ROM.

Companion documents: `docs/ROM_COMPARISON.md` (the stock-ROM oracle
workflow) and `docs/VISUAL_MODES.md` (the post-FX / remaster look, which is
separate from these cutscene cameras).

## What plays

A mission intro is a sequence of authored camera modes:

1. **INTRO** — one randomly-selected static "establishing" shot with the
   mission-title text. The camera is chosen per attempt from the stage's
   authored set (Dam has 6), rolling the RNG like the original game.
2. **FADESWIRL** → **SWIRL** — the descend-and-spin fly-around while Bond
   performs an intro animation (draw weapon, look around), ending by handing
   control to first-person gameplay.

Outros are level-scripted: an **AI-driven pose/orbit camera** (`POSEND`) on the
stages that use one, then a fade to the mission-report screen; **death** runs
the `DEATH_CAM` replay(s). Most stages simply fade to the report screen on
completion — there is no orbit-on-completion in the original game.

## Settings (persisted, in `ge007.ini`)

| Key | Default | Effect |
|-----|---------|--------|
| `Video.CutsceneFovY` | `60` | Vertical FOV for intro/outro/death cinematics, matching the N64's original framing regardless of the gameplay `Video.FovY`. `0` = follow `Video.FovY`. |
| `Game.IntroSkipStyle` | `0` | `0` = original staged skip (a face/trigger button advances one intro stage). `1` = instant skip (any input jumps straight to gameplay). |

Both are also settable per-run via `--config-override Key=VALUE` and are pinned
to their defaults under `--deterministic` for reproducible captures. The
`--faithful` / `--faithful-hd` presets pin `Video.CutsceneFovY=60` and
`Game.IntroSkipStyle=0`.

Whether the animated intro plays at all follows the launch path: it plays on a
normal menu-started mission, and is skipped (short first-person handoff) on a
direct `--level` / `--mission` / `--deterministic` boot. Force it either way
with the env hatches below.

## A/B escape hatches (environment)

Every faithfulness fix ships with a hatch that restores the pre-fix behavior,
for bisecting a regression or comparing looks. Set the variable to `1`.

| Env var | Restores |
|---------|----------|
| `GE007_ENABLE_LEVEL_INTRO` / `GE007_DISABLE_LEVEL_INTRO` | Force the authored intro on / off regardless of launch path. |
| `GE007_INTRO_CAMERA_INDEX=N` | Pin the establishing camera to index N (else per-attempt RNG). |
| `GE007_NO_CAMERA_SEED_FIX` | Pre-fix room admission — the establishing/outro shot renders only the player's spawn room (near-blank when the camera is elsewhere). |
| `GE007_NO_CINEMA_INTRO_FIX` | Pre-fix gate that skipped a stage's intro when it had cinema cameras but no swirl data. |
| `GE007_NO_INTRO_CHR_TIMING_FIX` | Pre-fix Bond intro-chr spawn timing at swirl entry (one record early vs stock). |
| `GE007_NO_INTRO_PHASE3` | Pre-fix static Bond: skip the scripted phase-3 intro animation (Bond holds the phase-2 idle instead of drawing/aiming his weapon). See the phase-3 note below. |
| `GE007_NO_INTRO_ROOTMOTION` | Pre-fix pinned Bond: keep the intro viewer body pinned to its static spawn anchor instead of letting the phase-3 animation's root motion move it (stock's Bond shifts/settles as he draws). |
| `GE007_INTRO_PHASE3_ANIM=N` / `GE007_INTRO_PHASE3_SEGMENT=N` / `GE007_INTRO_PHASE3_ONSET=F` / `GE007_INTRO_PHASE3_START=N` / `GE007_INTRO_PHASE3_END=N` | Tuning overrides for the phase-3 anim index (default 99), swirl segment (4), per-segment timer onset (40.0), start frame (1), and end frame (96). Defaults are oracle-measured against stock on the Dam intro. |
| `GE007_NO_BOND_BODY_FIX` | Pre-fix aliased viewer body (invisible / spiky Bond in the swirl and outro pose). |
| `GE007_BOND_BODY_ALLOC_FAIL=header\|buffer\|both` | Fault-injection: force the dedicated viewer-body header/buffer allocation to "fail" so the fail-closed path can be exercised without real OOM. With the body fix enabled, `solo_char_load` then **skips** the 1P viewer body (Bond absent, one `[BONDVIEW][RENDER-HEALTH]` log) instead of reverting to the shard-producing aliased `GUNRIGHT` slot. |
| `GE007_INTRO_ANIM_LEGACY_SEED` | The pre-T12 hand-tuned intro anim seed constant (value-identical; documentation A/B). |
| `Video.CutsceneFovY=0` (via `--config-override`) | Cinematics follow the gameplay FOV instead of the faithful 60. |

Diagnostic traces (no behavior change): `GE007_TRACE_INTRO_PARSE`,
`GE007_SETUP_DIAG`, `GE007_VERBOSE` (spawn/room/camera dumps),
`GE007_TRACE_FOV`, `GE007_TRACE_CAMERA`, `GE007_TRACE_BOND_BUF` (1P viewer
body/head/weapon offsets inside the shared load buffer, with an OVERLAP flag),
`GE007_TRACE_INTRO_AUTHORITY` (per-tick current-player viewer-body transform
authority: prop pos before/after `chrTickBeams`, the collision anchor, the
animation root-motion delta, and the anchor-snap delta — see the note below).

Transform authority (ledger R2/M1.3): in the frozen intro swirl the viewer
body's single transform authority is the animation advanced inside
`chrTickBeams`; the post-tick block in `playerTickBeams` only re-syncs the
outward collision/camera anchor and never applies an inward snap on animated
frames (the D31/D43 root-motion branch runs and returns first). On the few
non-animated frozen-intro frames the inward snap does run, but it is a measured
no-op (the prop already sits on its anchor). A permanent `[BONDVIEW]
[RENDER-HEALTH]` warning fires once if that snap ever moves the already-rendered
body by more than 0.01 units, i.e. if a second authority re-emerges;
`GE007_TRACE_INTRO_AUTHORITY` dumps the per-tick deltas that warning is built
on. FP / FP_NOINPUT legitimately snap the body every tick and are excluded.

Red-shard note: the intro Bond body, head, and right-hand weapon are packed into
one shared load buffer in `solo_char_load`. The native path previously failed to
advance the weapon offset past the head mesh, so the weapon model overwrote most
of Bond's head and the head rendered as dark-red degenerate shards during the
swirl/outro (only visible once the body de-alias keeps the render count at ~21;
with `GE007_NO_BOND_BODY_FIX` the body collapses and hides it). Fixed by running
retail's `totalsize` advancement in the native path; verify with
`GE007_TRACE_BOND_BUF=1` (expect `OVERLAP=0`).

Phase-3 animation + root motion (ledger D43): the intro Bond animation is a
3-phase sequence. Phases 1 (`extending_left_hand`) and 2 (idle loop) are driven
procedurally; phase 3 is a scripted `PlayAnimation` that stock issues from the
level's Bond-cinema AI-list (`CHR_BOND_CINEMA`, which resolves to the intro chr
itself). The native port never ran that script in frozen-camera modes, so Bond
held the phase-2 idle and stood **static**, and — because the current-player
intro prop is pinned to its static spawn anchor every tick — did not move.
Fixed by (a) transitioning the intro chr to the measured phase-3 animation
(`ANIM_aim_one_handed_weapon_left_right`, index 99, hash `0x79F92FB064997857`,
end 96, speed 0.5) at swirl segment 4, and (b) letting the intro animation's root
motion drive `prop->pos` during the frozen swirl instead of snapping it to the
static spawn anchor. Against the ares oracle on the Dam intro this makes every
`bond_anim` field except `frame` (a <=1-frame seed offset, D41) match stock,
collapses the D35 camera-anchor drift (`cam_target[0]` 39.4 -> 1.0), and drops
total intro divergences from 2546 to 1116.

Vertical settle (ledger D31): the root-motion unpin above must apply to ALL intro
animation phases (phase 1 `ACT_BONDINTRO`, phase 2 `ACT_STAND` idle, phase 3
`ACT_ANIM`), not just phase 3. The intro Bond's phase-1 animation settles him from
the spawn height down onto the floor (~-42 -> 2.5 on Dam); the earlier
phase-3-only unpin left him pinned at the raw spawn anchor (~60) through phases
1-2, so he **hovered ~57u above the ground and then visibly dropped** when phase 3
took over. Unpinning every phase makes Bond follow the animation and settle like
stock across the whole swirl (measured Y delta vs stock 0.00 in all phases); the
only residual is a one-frame spawn transient that is hidden by the swirl-entry
fade. Validated no-regression (no crash, sane position, deterministic) across all
20 intro stages. The parameters are oracle-validated on the Dam intro and applied
as a faithful default to all stages (the Bond-cinema intro is shared game
behavior); per-stage phase-3 scripts are not yet individually oracle-verified.
Disable with `GE007_NO_INTRO_PHASE3` / `GE007_NO_INTRO_ROOTMOTION`.

## Validating against a stock ROM

The intro/outro camera paths are validated frame-for-frame against a patched
`ares` emulator running the stock ROM. See `docs/ROM_COMPARISON.md` for the
oracle build. Quick commands (all outputs are ROM-derived — keep them local):

```bash
# Headless establishing-shot screenshot (never use --ramrom for visuals):
SDL_AUDIODRIVER=dummy GE007_MUTE=1 GE007_BACKGROUND=1 GE007_NO_INPUT_GRAB=1 \
GE007_ENABLE_LEVEL_INTRO=1 GE007_INTRO_CAMERA_INDEX=5 \
./build/ge007 --rom baserom.u.z64 --level 33 --deterministic \
  --screenshot-frame 120 --screenshot-label dam_intro --screenshot-exit

# Camera-path oracle comparison (native vs stock ares):
tools/movement_oracle_capture.sh --route dam_intro_camera_path --no-build \
  --ares-bin build/ares-movement-oracle/ares/build-movement-oracle/desktop-ui/ares.app/Contents/MacOS/ares \
  --out-dir <local-dir>

# Emulator-free parse-determinism gate + full-stage census:
tools/intro_parse_digest_gate.sh
tools/intro_census_capture.sh --all
```

The comparator (`tools/compare_intro_trace.py`) aligns per camera mode and
reports divergences by field, with a ledger-tagged waiver mechanism for
characterized, not-yet-fixed differences (see
`docs/design/INTRO_OUTRO_LEDGER.md` for the defect ledger).

## Regression gates (`ctest`, ROM-gated — skip cleanly without a ROM)

- `intro_tools_unittests` — the comparator/digest/route tooling unit tests.
- `intro_parse_digest_smoke` — parsed intro-stream determinism.
- `intro_oracle_dam_route` — the full Dam camera-path oracle comparison.
- `intro_census_dam_smoke` — intro render-health counters.
