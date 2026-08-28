# ROM Comparison Instrumentation

This repo can compare native gameplay against a stock-ROM oracle without
committing emulator source, ROMs, saves, screenshots, or generated traces.

The public lane is intentionally narrow:

- route specs are authored JSON and safe to commit;
- native captures use `ge007 --trace-state`;
- stock captures use a locally patched ares checkout in ignored build space;
- generated `.jsonl`, `.bmp`, `.ppm`, save, and emulator outputs stay local.

## Tools

| Tool | Purpose |
|------|---------|
| `tools/rom_oracle_route.py` | validates route specs and emits native/ares input scripts |
| `tools/route_contract_smoke.sh` | validates all built-in route specs/adapters and can run native-only route captures |
| `tools/movement_oracle_capture.sh` | runs native capture, optional stock ares capture, then compares |
| `tools/audit_oracle_trace.py` | audits trace input gates, movement records, watch state, and optional displacement thresholds before delta comparison |
| `tools/audit_intro_trace.py` | audits native intro actor/readiness/render/animation fields |
| `tools/compare_movement_trace.py` | compares aligned movement fields and reports first divergence |
| `tools/compare_intro_trace.py` | compares active level-intro camera path fields |
| `tools/compare_combat_health_trace.py` | compares stock/native health and damage-HUD phase at visual checkpoints |
| `tools/compare_glass_projection_trace.py` | compares stock/native falling-glass shard projection and screen coverage summaries |
| `tools/audit_screenshot_health.py` | rejects missing, wrong-size, blank, or nearly monochrome native route screenshots |
| `tools/intro_trace_summary.py` | emits stable intro setup/camera/Bond-animation digests and timer-keyed stock/native comparisons |
| `tools/summarize_intro_census.py` | summarizes native intro setup/render/animation coverage across traces |
| `tools/prepare_ares_movement_oracle_build.sh` | clones/patches/builds a local instrumented ares binary |

Built-in route specs live in `tools/rom_oracle_routes/`.

## Route Contract Smoke

Validate every committed route spec and generated adapter without a ROM:

```sh
tools/route_contract_smoke.sh
```

With a ROM and native binary, also run the routes through the native capture
side of `movement_oracle_capture.sh`:

```sh
tools/route_contract_smoke.sh \
  --native-smoke \
  --no-build \
  --binary build/ge007 \
  --rom /path/to/GoldenEye.z64
```

This does not require a stock emulator build. It proves that route JSON,
native-env generation, ares-input generation, route-level screenshot checks,
native render audits, movement audits, and intro actor/animation audits still
compose cleanly.

## Quick Native Probe

This validates the public route/compiler/native trace path without an emulator:

```sh
tools/movement_oracle_capture.sh \
  --route dam_forward_stop \
  --rom /path/to/GoldenEye.z64 \
  --native-only
```

The output directory contains a native JSONL trace, screenshot, and log. These
are local ROM-derived artifacts and should not be committed.

## ROM-Vs-Native Capture

Prepare the local ares build:

```sh
tools/prepare_ares_movement_oracle_build.sh
```

In this workspace, the macOS instrumented ares binary is expected at:

```sh
build/ares-movement-oracle/ares/build-movement-oracle/desktop-ui/ares.app/Contents/MacOS/ares
```

Check that path first before searching `PATH`, `/tmp`, or `/Applications`. On
Linux builds the equivalent executable may be
`build/ares-movement-oracle/ares/build-movement-oracle/desktop-ui/ares`. This
must be the locally instrumented movement-oracle build; if a route or trace field
such as `glass_projection` is missing, rerun
`tools/prepare_ares_movement_oracle_build.sh` instead of using a system ares
binary.

Quick sanity check:

```sh
test -x build/ares-movement-oracle/ares/build-movement-oracle/desktop-ui/ares.app/Contents/MacOS/ares && echo "ares OK"
```

Then run the comparison:

```sh
tools/movement_oracle_capture.sh \
  --route dam_forward_stop \
  --ares-bin build/ares-movement-oracle/ares/build-movement-oracle/desktop-ui/ares.app/Contents/MacOS/ares \
  --rom /path/to/GoldenEye.z64
```

To compare a native capture against an already-produced stock trace:

```sh
tools/movement_oracle_capture.sh \
  --route dam_forward_stop \
  --stock-trace /tmp/stock_dam_forward_stop.jsonl \
  --rom /path/to/GoldenEye.z64
```

## Route Spec

Route files use schema `mgb64.rom_oracle.route.v1`:

```json
{
  "schema": "mgb64.rom_oracle.route.v1",
  "name": "dam_forward_stop",
  "level": "33",
  "frames": 220,
  "native_frames": 220,
  "stock_frames": 6500,
  "stock_screenshot_frame": 2950,
  "native_speedframes": 3,
  "stock_speedframes": 3,
  "stock_gameplay_start_global": 1146,
  "stock_require_first_gameplay_global": 1146,
  "stock_menu_close_on_player": true,
  "native_render_audit": true,
  "native_min_moving_records": 60,
  "stock_min_moving_records": 60,
  "stock_min_gameplay_input_records": 60,
  "stock_max_suppressed_menu_records": 0,
  "stock_min_menu_to_gameplay_gap": 30,
  "compare_align": "move",
  "compare_profile": "scalar-speed",
  "compare_min_aligned": 60,
  "compare_max_aligned": 60,
  "compare_normalize_position": true,
  "events": [
    { "start": 80, "len": 90, "forward": true }
  ],
  "stock_events": [
    {
      "start": 100,
      "len": 3,
      "buttons": ["start"],
      "phase": "menu",
      "repeat": { "every": 240, "until": 2260 }
    },
    { "start": 80, "len": 90, "forward": true, "phase": "gameplay" }
  ]
}
```

Native events are deterministic gameplay frames. Stock `phase: "gameplay"`
events use a gameplay timeline derived from the ROM's global timer after the
target stage reaches the configured control-ready point. Stock `phase: "menu"`
events use absolute boot/controller-poll frames so they can navigate the
frontend before gameplay starts.

Movement events support `forward`, `back`, `left`, `right`, `stick_x`,
`stick_y`, and `buttons`. Native captures currently support full-scale
cardinal/diagonal stick values, matching the existing `GE007_AUTO_*` lane. The
ares script can carry the same route as raw N64 controller state.

Provider-specific fields are optional:

- `native_frames` and `stock_frames` override `frames` for direct-native and
  booted-stock-ROM captures.
- `stock_screenshot_frame` optionally overrides the stock framebuffer dump frame.
  It defaults to `stock_frames`. Use it when a route needs extra stock frontend
  frames for traces/comparison but the meaningful visual checkpoint is an earlier
  gameplay frame.
- `native_screenshot_game_timer` and `stock_screenshot_game_timer` optionally
  switch screenshots from process-frame triggers to `g_GlobalTimer` triggers.
  Use these for visual parity checkpoints that need the same mission tick across
  native direct boot and stock frontend boot. The stock ares hook also requires
  the configured `stock_level` before the timer can fire.
- `native_level` and `stock_level` override `level` for the native direct-boot
  target and stock-oracle target-stage gate respectively. `stock_level` does not
  by itself select a mission in the stock frontend; it tells the ares hook which
  raw `LEVELID` to wait for, audit, and use when closing menu automation.
- `native_events` overrides `events` for native only; `stock_events` overrides
  `events` for ares only.
- `native_env` is an object of extra `KEY=value` pairs for native capture.
  Intro routes use this to enable authored level intros during deterministic
  direct boot and, when needed, pin the native selected intro camera to the
  stock camera selected by the scripted boot path. For focused visual routes,
  `GE007_AUTO_FACE_COORD_SCRIPT` and `GE007_AUTO_AIM_DIR_SCRIPT` accept compact
  inclusive ranges such as `45-158:x:y:z`, keeping camera and shot direction
  fixed through a screenshot without expanding the route into many one-frame
  entries. When the stock side uses `MGB64_ARES_FORCE_PLAYER_SCRIPT`, prefer the
  native counterpart
  `GE007_AUTO_FORCE_PLAYER_SCRIPT=START-END:X:Y:Z:YAW_DEG:PITCH_DEG[:EYE_OFFSET[:PAD]]`
  so native pose, floor/height, tile basis, and current background room are
  pinned to the same checkpoint before interpreting visual deltas.
- `stock_env` is an object of extra `KEY=value` pairs for the instrumented ares
  stock capture. The visual oracle can use
  `MGB64_ARES_FORCE_PLAYER_SCRIPT=START:LEN:X:Y:Z:YAW_DEG:PITCH_DEG:EYE_OFFSET[:PAD]`
  to hold stock Bond at a target checkpoint on the stock gameplay timeline.
  `X:Y:Z` is the viewer/camera position; `EYE_OFFSET` seeds the floor/height
  state as `Y - EYE_OFFSET`. The optional `PAD` resolves
  `g_CurrentSetup.pads[PAD].stan` or bound pad `PAD - 10000`, then writes the
  player tile, portal tile, prop stan, and current background room. The hook
  deliberately leaves `player->current_model_pos` under ROM control because it
  is the room render basis, not the viewer position. Add
  `stock_min_force_player_applies` when a route depends on this hook, and
  `stock_min_force_player_stan_applies` when it must prove pad-to-stan
  resolution worked. Use
  `MGB64_ARES_RNG_SEED_SCRIPT=GAMEPLAY_FRAME:SEED` when a visual route must
  stabilize stock firing or shard generation across frontend-timing jitter.
  Add `MGB64_ARES_RNG_SEED_REPEAT=1` when the target gameplay frame can be
  sampled by multiple VI frames and the route needs every duplicate sample pinned.
  Use `MGB64_ARES_CROSSHAIR_SCRIPT=START:LEN:X:Y` or `START-END:X:Y` to force
  stock `crosshair_angle` and `field_FFC` before scripted fire input while still
  letting the ROM run its normal shot path.
  Some visual routes remain sensitive to stock ares frontend/update cadence even
  with forced pose and crosshair. For those, keep active-glass/actor guards in
  front of pixel comparison, archive clean and dirty traces in `/tmp`, and
  classify stock misses such as local prop impacts with `glass.active=0` as
  route-origin evidence unless a native focused guard reproduces the same
  renderer/material failure.
- `native_config` is an object of `Section.Key: value` pairs passed to native as
  repeated `--config-override Section.Key=value` arguments. Use it for visual
  routes that must not inherit local play settings from `ge007.ini`; for stock
  parity captures, pin values such as `Video.FovY=60`, `Video.RenderScale=1`,
  `Video.WindowWidth=640`, `Video.WindowHeight=480`, `Video.WindowMode=windowed`,
  `Video.RetroFilter=on`, and post-FX identity settings directly in the route.
  The native capture wrapper runs with an isolated `--savedir` under the output
  directory, so route overrides are recorded in local artifacts instead of
  mutating the repo-root/user `ge007.ini`. After capture, the wrapper audits the
  generated native config and fails if any route override is absent or persisted
  with the wrong value.
- `native_render_audit: true` runs `tools/audit_render_trace.py` on the native
  trace. Built-in routes keep this strict: zero crash recoveries, unhandled GBI
  commands, display-list resolve failures, room-render fallback records, and
  non-finite trace values. This turns rendering defects into route failures
  instead of relying on manual JSON inspection. The wrapper writes the render
  result to `native_<route>.render.json`.
- `tools/movement_oracle_capture.sh` always requires the native route
  screenshot and runs `tools/audit_screenshot_health.py` on it. This proves the
  visual artifact exists, is 640x480, and is not blank before the trace is used
  as route evidence. The wrapper writes the screenshot result to
  `native_<route>.screenshot.json`.
- `native_intro_audit: true` runs `tools/audit_intro_trace.py` on the native
  JSONL trace before any stock comparison. Optional fields such as
  `native_intro_require_bond_rendered`, `native_intro_min_rendered_frames`, and
  `native_intro_max_first_render_frame` turn actor/readiness/render state into
  route-level pass/fail checks. Intro audits can also assert animation presence
  and progress with `native_intro_require_bond_anim` /
  `native_intro_min_anim_advance`, animation identity with
  `native_intro_require_bond_anim_hash`, plus attached held-item parity with
  `native_intro_require_bond_right_item`. The wrapper writes the audit result to
  `native_<route>.intro-audit.json` and the lightweight intro fingerprint to
  `native_<route>.intro-summary.json`.
- `native_speedframes` sets `GE007_DETERMINISTIC_SPEEDFRAMES` for native
  capture. Use this when comparing against stock ROM traces that advance game
  logic by multiple N64 frames per update.
- `stock_speedframes` sets the stock gameplay-frame divisor used by the ares
  hook. It defaults to `native_speedframes` when omitted. Set it explicitly for
  stock routes with gameplay-phase controller input; deriving it from the live
  stock timer delta can change between video frames and make route input windows
  non-monotonic.
- `stock_gameplay_start_global` is the earliest stock `g_GlobalTimer` value
  that can start the gameplay event timeline. When set, the instrumented ares
  oracle also uses this configured tick as the fixed gameplay-frame origin, so
  small differences in the first observed target-stage frame do not shift all
  gameplay-phase events. This prevents player structs that exist during
  intro/setup from consuming movement events before controls are live.
- `stock_require_first_gameplay_global` is a stricter audit precondition for
  cadence-sensitive visual/state routes. It does not change stock timing; it
  fails the stock trace if the first observed gameplay record does not have the
  expected `move.global` value. Ares-backed captures retry this precondition up
  to `MGB64_ARES_STOCK_ROUTE_CONTROL_RETRIES` times (default `4`) and preserve
  failed attempts with `.attemptN` suffixes in the artifact directory.
- `stock_require_native_selected_camera` (bool, default `false`) is the intro
  analogue of the above for level-intro camera-path routes. The native capture
  pins the intro camera (`GE007_INTRO_CAMERA_INDEX`), but the stock ROM RNG-picks
  it at boot (faithful N64 behaviour), so an unpinned stock capture can land on a
  different authored camera and diverge the whole path — the source of BUG-3's
  run-to-run `0 vs 4767` variance. When set, the harness reads the native trace's
  resolved `intro.selected_camera.pad` and retries the stock capture (same
  `MGB64_ARES_STOCK_ROUTE_CONTROL_RETRIES` budget) until stock lands the same pad,
  so both sides compare the same camera. Self-configuring from the route's own
  pinned native camera; no per-route pad literal.
- `stock_menu_close_on_player` defaults to `true`. It closes stock `phase:
  "menu"` automation as soon as the target-stage player exists, before the later
  movement timeline starts. Keep this enabled for movement parity routes: a
  route may use repeated boot/frontend pulses to reach the level, but it must
  not keep pressing Start/A after the ROM has entered the target stage.
- `native_min_moving_records`, `stock_min_moving_records`, and
  `stock_min_gameplay_input_records` make movement routes prove that they
  entered controllable gameplay and applied enough real route input before
  comparison. Use them on speed/dynamics routes so a boot script that reaches a
  level but keeps toggling menus fails in the audit phase. The route validator
  requires these fields for stock-backed movement routes.
- `stock_max_suppressed_menu_records` caps how many stock `phase: "menu"`
  events the ares hook had to block after the route's menu gate closed. Set it
  to `0` for standard movement and intro routes: the generated frontend
  bootstrap should stop before target-player entry, and the hook should only be
  a safety backstop. The route validator requires this cap when a stock-backed
  movement route mixes boot/menu automation with gameplay input.
- `stock_min_menu_to_gameplay_gap` requires a minimum controller-poll gap
  between the last accepted stock `phase: "menu"` pulse and the first accepted
  stock `phase: "gameplay"` input. Built-in movement routes use this to prove
  that reaching a level is not being conflated with Start/pause-menu mashing.
  The route validator requires this field for stock-backed movement routes that
  mix boot/menu automation with gameplay input.
- `repeat` expands simple deterministic event pulses without hand-writing
  hundreds of JSON entries.
- `phase: "menu"` marks stock boot/frontend input. The ares hook suppresses
  those events once the target-stage player exists, once the route's
  `stock_gameplay_start_global` gate is reached for the target stage, once the
  gameplay event timeline starts, or, if `MGB64_ARES_CURRENT_MENU` is configured
  for the ROM layout, once the stock ROM reaches `MENU_RUN_STAGE`. This prevents
  broad menu/bootstrap routes from skipping intro/setup or opening the pause
  watch after the route should be playing.
- `compare_align: "move"` aligns traces at first detected movement, useful for
  one stable movement window when stock capture includes boot/frontend frames
  but native capture starts directly in a level.
- `compare_align: "gameplay-frame"` aligns stock `oracle.gameplay_frame` with
  native frame `f`. Use it for multi-segment routes where repeated stock video
  frames would make index/move alignment drift at segment boundaries.
- `compare_kind` selects the comparator: `movement` by default, `intro`, or
  `visual`.
- Movement `compare_profile` selects the field set: `full`, `dynamics`,
  `scalar-speed`, or `timing`. The stock Dam route uses `scalar-speed` because
  the stock ROM's timer can move from 3-frame to 2-frame updates after the
  intro; full timing/pose comparisons should be run deliberately when that is
  the target.
- Intro `compare_profile` selects `path`, `scalar`, `state`, or `full`.
  Intro routes normally use `compare_align: "active-index"` and
  `compare_require_frozen: true` so setup frames before Bond enters frozen
  intro camera mode are ignored.
- Intro `compare_align: "intro-timer"` aligns on `intro.timer`, the authored
  level-intro camera timer. This is the preferred clock for swirl/Bond-animation
  windows because stock video traces can contain several samples for one game
  tick. Duplicate key samples are collapsed to the last sample for that key so
  comparisons use the settled per-tick state.
- Visual routes compare stock/native screenshots with
  `tools/compare_screenshots.py`, writing `visual_compare_<route>.json`,
  `visual_compare_<route>.txt`, and a heatmap PNG. Visual `compare_align` can
  be `global`, `frame`, or `index`; visual `compare_profile` can be `full`,
  `screenshot`, `active-normalized`, or `logical-viewport`. The comparison JSON
  always includes presentation metrics for both images: active bbox, active
  coverage, margins, active aspect, center offset, and a coarse border class. Use
  those metrics to decide whether a visual delta is caused by viewport/crop
  policy before treating it as renderer material drift. `active-normalized`
  crops each provider to its nonblack bbox and resizes the test crop to the
  baseline crop size before comparing active viewport content.
  Routes may also define `visual_regions` entries as
  `{ "name": "...", "roi": [x, y, width, height] }`. The wrapper passes those
  regions to `tools/compare_screenshots.py --region NAME:X,Y,W,H`, and the
  visual comparison JSON records per-region changed-pixel, mean-color,
  per-channel, color-count, and feature metrics under `regions`. Feature metrics
  include bright/near-white counts, bright connected components, and warm
  red/orange counts for effect-vs-HUD triage. Region coordinates are final
  comparison coordinates after any active-normalized or logical-viewport
  crop/resize, so keep them tied to the route's declared presentation profile.
  The stock ares oracle arms screenshots from the N64 VI hook, then writes the
  next post-`Screen::refresh()` presented viewport to `stock_<route>.ppm`,
  scaling that source viewport to the route screenshot dimensions. Do not
  threshold stock/native visual routes until the route declares the accepted
  presentation normalization policy, such as full presented frame, active crop,
  active-normalized viewport, or a route-specific transform. A valid stock trace
  can prove camera, room, and force-hook state while the remaining pixel delta
  still reflects provider crop/guard margins or real material drift.
- Visual routes also compare stock/native `combat.health` at the screenshot
  checkpoint with `tools/compare_combat_health_trace.py`, writing
  `combat_health_compare_<route>.json`. This is a warning-class artifact by
  default, but routes can set `compare_require_health_match`,
  `compare_health_tolerance`, and `compare_damage_show_tolerance` to make health
  and damage/health HUD phase a pre-pixel gate. The report records Bond health,
  damage/health HUD timers, first health drop, and first-glass-active offsets so
  HUD damage does not get misclassified as a renderer material bug.
- Stock visual traces include `combat.crosshair` and `combat.screen` for the
  player aim fields used by GoldenEye's shot path. When
  `MGB64_ARES_CROSSHAIR_SCRIPT` is active, `oracle.crosshair_force` records
  event count, apply count, last gameplay frame, and last forced `[x,y]`.
- Visual or glass routes can declare actor-contamination guards with
  `compare_actor_chrnums`, optional `compare_actor_fields`,
  `compare_actor_frame`, and `compare_actor_position_tolerance`. The wrapper
  converts those fields into `tools/compare_glass_trace.py --require-actor-match`
  arguments and writes `actor_compare_<route>.json`. Use this when a visual route
  should fail until a sampled guard/HUD/content state matches before its pixel
  delta is promoted to a renderer gate.
- Before changing visual screenshot frames, run
  `tools/score_visual_checkpoints.py --require-active stock.jsonl native.jsonl`.
  It scores candidate frame pairs across shard timer, health/HUD state, actor
  fields/position, visible actor count, and active-shard count, and reports the
  best strict match if one exists. This keeps route-timing experiments from
  promoting a checkpoint that only fixes one visible symptom.
- Stock trace `rooms.dl` entries summarize the current rendered-room sample's
  primary and secondary room buffers from `g_BgRoomInfo`. Each summary includes
  room flags, point/primary/secondary pointers and sizes, command counts,
  texture/combine/env-alpha counts, render-mode values observed directly in the
  scanned buffer, raw hashes, and combiner hashes. This is read-only RDRAM
  evidence for room-buffer contents. It is not a full RSP/RDP draw-state hook:
  inherited othermode, fog, geometry mode, texture state, and sort/depth policy
  can still differ even when the scanned room buffers match.
- `compare_start_intro_timer` and `compare_end_intro_timer` restrict intro
  comparisons to a timer window. Dam's swirl route uses this to compare the
  deterministic `ACT_BONDINTRO` segment before the later idle handoff, whose
  mirror flag depends on random-stream parity.
- `compare_selected_camera: true` adds the selected authored intro camera
  fingerprint to the intro comparator: presence, camera position, yaw, pitch,
  and target pad. Use it when a route pins native `GE007_INTRO_CAMERA_INDEX`
  from a stock boot path so a wrong camera cannot pass merely because a later
  camera path segment happens to line up.
- `compare_intro_setup: true` adds decoded intro setup parity: the selected
  intro animation index, contiguous swirl record count, a hash of the converted
  swirl records, and the current swirl segment fields used by playback. Use it
  on level-intro routes so camera/animation playback is backed by evidence that
  native decoded the same authored setup data as stock.
- `compare_bond_anim: true` adds intro Bond actor/action/animation fields:
  presence, action, animation-valid flag, animation frame count, a stable
  logical animation-header hash, entry/bitstream offsets, frame, end frame,
  speed, absolute speed, looping flag, and gunhand/mirror flag.
- `compare_exclude_fields` is a comma-separated list of comparator field names
  to omit for a route. Use it sparingly for derived fields that are not part of
  the route's parity claim; Dam's swirl route excludes `cam_delta` because it is
  relative to player/head state, while the route's authored camera path is
  covered by absolute camera vectors.
- `compare_min_aligned` fails the route if fewer than N aligned movement or
  intro records are found. Use it with semantic-key routes so a bad address,
  missing timer, or shortened gameplay overlap cannot silently pass with only a
  tiny comparison window.
- `compare_max_aligned` limits comparison to the first N aligned records. Use
  this for stable timing windows; omit it for full-route comparisons.
- `compare_gameplay_windows` is a list of `{ "start": N, "len": M }` windows.
  When present, the wrapper passes them to the comparator so multi-segment
  routes compare stable gameplay-frame ranges and skip controller-transition
  edges.
- `compare_normalize_position: true` compares positions relative to the first
  aligned movement frame, which keeps stock intro/camera settle offsets from
  obscuring movement-speed dynamics.

Inspect generated adapters with:

```sh
python3 tools/rom_oracle_route.py native-env dam_forward_stop
python3 tools/rom_oracle_route.py native-config dam_glass_visual_probe
python3 tools/rom_oracle_route.py ares-input dam_forward_stop
```

## Trace Contract

The movement comparator consumes JSONL records with:

- `f`: provider frame index;
- `p`: current player number, 1-based (`0` = no current player, `1` in
  single-player; `2`/`3`/`4` distinguish per-player viewpoints in split-screen).
  Any `p >= 1` means a player is present, so existing presence checks still hold;
- `pos`: player prop position;
- `col`: player collision position, when available;
- `move.speed`: `[speedforwards, speedsideways]`;
- `move.raw`: `[speedgo, speedstrafe]`;
- `move.boost`, `move.turn`, `move.pitch`, `move.max_t`;
- `move.head`, `move.prev`;
- `move.clock`, `move.dt`, `move.global`.
- `oracle.gameplay_frame`, `oracle.gameplay_origin_global`, and
  `oracle.gameplay_origin_input` for stock traces, when available.
- `oracle.input` for stock traces: last injected buttons/stick, active menu
  and gameplay events, suppressed events, and the menu-closed gate.
- `oracle.crosshair_force` for stock traces, when a route forces the stock
  crosshair/gunsight fields.
- `combat.crosshair` and `combat.screen` for stock visual traces, mirroring the
  stock player aim fields consumed by shot generation.
- `combat.health` for stock and native traces: Bond health, armor, actual
  health/armor, damage and health HUD timers, damage types, and screen-fade RGBA.
  Visual routes use this to tell HUD phase mismatches apart from material
  rendering defects.
- `track.firecount`, `track.shotbondsum`, and `track.accuracyrating` when
  `MGB64_ARES_TRACE_CHRNUM=N` is set, matching the native `GE007_TRACE_CHRNUM=N`
  actor root-cause fields for guard fire/damage cadence work.
- `watch.state` for stock traces, when the player layout is known.

The stock hook writes to `MGB64_ARES_ORACLE_TRACE`. The legacy
`MGB64_ARES_MOVEMENT_TRACE` name is still accepted for existing movement-only
workflows.

Native traces already emit this shape through `--trace-state`. The ares hook
reads the same fields directly from RDRAM. Intro comparison additionally uses
`cam_pos`, `cam_target`, `cam_up`, `cam_floor`, `cam_delta`, `facing`,
`cam`, `cam_after`, `icam`, `p_unk`, `intro.frozen`, `intro.timer`, and, when
enabled, `intro.setup.*`, `intro.selected_camera.{present,pos,yaw,pitch,pad}` plus
`intro.bond_*` / `intro.bond_anim.*` fields.

The default symbol layout is USA:

| Symbol/field | Address or player offset |
|--------------|--------------------------|
| `g_CurrentPlayer` | `0x8008a0b0` |
| `g_playerPointers` | `0x80079ee0` |
| `g_CurrentStageToLoad` | `0x80048364` |
| `g_ClockTimer` | `0x80048374` |
| `g_GlobalTimerDelta` | `0x80048378` |
| `g_GlobalTimer` | `0x8004837c` |
| `g_CurrentSetup` | `0x80075d00` |
| `g_BgCurrentRoom` | `0x80044838` |
| `g_BgNumberOfRoomsDrawn` | `0x8004483c` |
| `D_8004489C` portal depth limit | `0x8004489c` |
| `list_visible_rooms_in_cur_global_vis_packet` | `0x8007bfa0` |
| `num_visible_rooms_in_cur_global_vis_packet` | `0x8007c038` |
| `dword_CODE_bss_8007FFA0` rendered-room list | `0x8007ffa0` |
| `D_800442FC` portal depth bytes | `0x800442fc` |
| `table_for_portals` projection cache | `0x80081618` |
| `g_BgPortals` pointer | `0x8007ff80` |
| `g_BgRoomInfo` | `0x80041414` |
| `g_CameraMode` | `0x80036494` |
| `g_CameraAfterCinema` | `0x80036498` |
| `camera_transition_timer` | `0x800364a4` |
| `intro_camera_index` | `0x800364a8` |
| `g_IntroSwirl` | `0x800364ac` |
| `selected intro camera` | `0x800364c0` |
| `g_IntroAnimationIndex` | `0x80036514` |
| `ptr_animation_table` pointer | `0x80069538` |
| `player->prop` | `0x00a8` |
| `player view mode / intro camera vectors` | `0x0000..0x0028` |
| `field_488.current_tile_ptr` | `0x0488` |
| `field_488.collision_position` | `0x048c` |
| `field_488.theta_transform` | `0x0498` |
| `field_488.pos3 / floor` | `0x04a4` |
| `field_488.collision_radius` | `0x04b0` |
| `field_488.pos / camera` | `0x04b4` |
| `field_488.applied_view` | `0x04c0` |
| `field_488.applied_up` | `0x04cc` |
| `field_488.portal_tile_ptr` | `0x04d8` |
| `speedsideways/speedstrafe/speedforwards/speedboost/max_t` | `0x016c..0x017c` |
| `speedtheta/speedverta` | `0x014c`, `0x0160` |
| `bondprevpos` | `0x0408` |
| `headpos` | `0x04fc` |
| `speedgo` | `0x2a4c` |
| `prop->pos` | `0x0008` |

If a different ROM revision is used, set `MGB64_ARES_SYMBOL_LAYOUT=jp` for the
known JP layout or override individual addresses with:

- `MGB64_ARES_CURRENT_PLAYER`;
- `MGB64_ARES_PLAYER_POINTERS`;
- `MGB64_ARES_CURRENT_SETUP`;
- `MGB64_ARES_CURRENT_STAGE`;
- `MGB64_ARES_BG_CURRENT_ROOM`;
- `MGB64_ARES_BG_ROOMS_DRAWN_COUNT`;
- `MGB64_ARES_BG_PORTAL_DEPTH_LIMIT`;
- `MGB64_ARES_BG_VISIBLE_ROOMS`;
- `MGB64_ARES_BG_VISIBLE_ROOMS_COUNT`;
- `MGB64_ARES_BG_RENDERED_ROOMS`;
- `MGB64_ARES_BG_PORTAL_DEPTH_BYTES`;
- `MGB64_ARES_BG_PORTAL_PROJECTION_CACHE`;
- `MGB64_ARES_BG_PORTALS_POINTER`;
- `MGB64_ARES_BG_ROOM_INFO`;
- `MGB64_ARES_BG_ROOM_INFO_STRIDE`;
- `MGB64_ARES_BG_ROOM_INFO_PORTAL_COUNT_OFFSET`;
- `MGB64_ARES_BG_ROOM_FILEPOSITION_LIST`;
- `MGB64_ARES_BG_ROOM_POS_LIST`;
- `MGB64_ARES_ROOM_DATA_FLOAT2`;
- `MGB64_ARES_BG_PORTAL_TRACE_INDEX`;
- `MGB64_ARES_CLOCK_TIMER`;
- `MGB64_ARES_GLOBAL_TIMER_DELTA`;
- `MGB64_ARES_GLOBAL_TIMER`.
- `MGB64_ARES_CAMERA_MODE`;
- `MGB64_ARES_CAMERA_AFTER_CINEMA`;
- `MGB64_ARES_CAMERA_TRANSITION_TIMER`;
- `MGB64_ARES_INTRO_CAMERA_INDEX`;
- `MGB64_ARES_ANIMATION_TABLE_PTR`;

`MGB64_ARES_CURRENT_MENU` can also be set for a verified ROM layout. It is not
enabled by default because menu globals are not needed for movement comparison;
the route's global gameplay gate is the authoritative stop for frontend input.
`MGB64_ARES_CLOSE_MENU_ON_PLAYER` defaults to `1` and is set from
`stock_menu_close_on_player` by the capture wrapper.

Treat comparison output as authoritative only after the stock trace reports
`p: 1` and a plausible `oracle.player_source` such as `current` or
`players[0]`. `tools/movement_oracle_capture.sh` sets
`MGB64_ARES_TARGET_STAGE` from the route level so title/frontend structures are
not treated as gameplay.

Before comparing deltas, `tools/movement_oracle_capture.sh` runs
`tools/audit_oracle_trace.py` on stock traces. Movement audits require a
target-stage player and applied gameplay input, and reject menu events after
target-player entry or gameplay-timeline start, `START` during gameplay input,
non-zero `watch.state`, and any route-configured excess of suppressed menu
attempts. Moving-record counts are scoped to the target-stage player, and stock
routes can require a menu-to-gameplay gap, so a capture that reaches a level but
keeps opening menus cannot pass as a parity run.
The movement comparator enforces the same pause/menu guard and, when invoked by
the route wrapper, filters both traces to the route's stock/native raw `LEVELID`
before alignment. Use the comparator's `--allow-gameplay-start`,
`--allow-menu-after-player`, or `--allow-watch` only for routes that
intentionally exercise pause/watch logic.

## Comparator Behavior

`tools/compare_movement_trace.py` aligns by `move.global` by default and
collapses consecutive duplicate state records. Routes that include stock
boot/frontend frames can use `compare_align: "move"` for one stable segment, or
`compare_align: "gameplay-frame"` for multi-segment input scripts.
`--gameplay-window START:LEN` can be repeated to limit comparison to stable
per-segment gameplay frames. `--baseline-stage LEVELID` and `--test-stage
LEVELID` restrict comparison to the intended stage; the route wrapper supplies
these automatically.

Profiles:

- `full`: timing, scalar speed, boost/turn/pitch, head, previous position, and
  player position;
- `dynamics`: scalar speed plus boost/turn/pitch;
- `scalar-speed`: `move.speed[]` and `move.raw[]`;
- `timing`: `move.clock`, `move.dt`, and `move.max_t`.

Default tolerances:

- speed/raw/boost/turn/pitch: `0.005`;
- player/previous position: `0.05`;
- head position: `0.005`;
- dt: `0.001`;
- clock and `max_t`: exact.

Top-level `col` comparison is opt-in with `--compare-collision`; the default
movement lane compares player position, previous position, head displacement,
and speed fields because native flow-only traces omit collision position.

The report includes first divergent fields plus aggregate horizontal distance
and maximum per-frame step for both traces. When invoked through
`tools/movement_oracle_capture.sh`, movement comparisons also write
`compare_<route>.json` with record counts, filters, aligned keys, summaries,
max deltas, and any divergence details. The route wrapper also writes
`summary_<route>.json`, a top-level manifest that links the native/stock traces,
screenshot, audit JSON, comparison JSON, optional actor-comparison JSON, and any
timer-summary comparison JSON.

`tools/compare_intro_trace.py` filters active intro records by camera mode
(`intro,fadeswirl,swirl` by default) and compares camera path vectors. By
default it fails if the instrumented input script injects skip-capable buttons
while an intro camera is active. Use `compare_require_frozen: true` for level
intros so one-frame setup records before `player->unknown` becomes frozen intro
camera mode are excluded. When invoked by `tools/movement_oracle_capture.sh`,
strict intro comparisons also write `compare_<route>.json` with active-record
counts, aligned keys, max deltas, and any divergences.

`tools/intro_trace_summary.py` is the lightweight inventory/diff companion for
expanding intro coverage. It summarizes any native or stock JSONL trace into
stable setup, selected-camera, camera-path, Bond-animation, and full-active
digests. In compare mode it aligns stock/native samples by `intro.timer` and can
check only the fields relevant to the next route, for example
`--compare-profile setup,selected-camera,bond-anim` while validating authored
animation identity before promoting a new strict camera-path route. The
`bond-anim` profile includes the animation header hash, so a route can no longer
pass with a different animation that happens to advance over a similar frame
range. Rich summaries may include native-only readiness fields such as render
counts or held-item state; compare mode uses the cross-provider subset that both
stock and native traces emit. For `compare_align: "intro-timer"` routes, the
capture wrapper asks the summary comparison to use integer timer keys so it
matches the strict comparator's settled per-tick alignment and ignores duplicate
fractional video samples. For `active-index`, `frame`, and `global` intro
routes, the wrapper treats `tools/compare_intro_trace.py` as the authoritative
comparison and skips the timer-summary compare because timer keys are not the
route's alignment contract. Timer-summary comparisons write
`summary_compare_<route>.json` when run through the capture wrapper.

The built-in `dam_intro_camera_path` route documents a representative
deterministic intro comparison. Stock boot/menu navigation selects Dam intro
camera index 5 for that path, so the route pins native `GE007_INTRO_CAMERA_INDEX`
to 5, verifies the selected-camera fingerprint, and compares the first 180
frozen intro camera records. It also enables the native intro actor audit over a
longer capture, requiring the stock-timed swirl actor to appear later in the
intro, render, animate, and carry the stage-authored right-hand silenced PP7
(`ITEM_WPPKSIL`, item id 5).

The built-in `dam_intro_swirl_bond_anim` route compares the authored swirl
window using `compare_align: "intro-timer"`. It checks the moving swirl camera
vectors, decoded intro setup fingerprint, selected-camera fingerprint, Bond
actor state, deterministic `ACT_BONDINTRO` animation identity hash, and
animation frames over timer 11..97. The route deliberately ends
before the idle handoff because that later mirror flag depends on random-stream
parity, which is useful future evidence but not part of this route's pass/fail
claim.

## Public-Repo Rules

Commit:

- route JSON;
- tool scripts;
- docs;
- small source patches that enable native tracing.

Do not commit:

- ROMs;
- generated `.jsonl`, screenshots, saves, raw audio/video, or emulator logs;
- cloned ares source or local build products.

The `.gitignore` already blocks the generated artifact families and `build/`.
