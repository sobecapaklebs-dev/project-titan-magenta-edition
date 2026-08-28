# Instrumentation & Validation

MGB64 ships a small, dependency-clean subset of the instrumentation used to
develop the `ge007` native port. These tools let contributors check that a change
does not regress rendering, gameplay state, or boot health — using their own
build and their own ROM.

> The full internal validation matrix (emulator parity oracles, RAMROM replay,
> real-soundplayer packs, curated visual baselines — a few hundred scripts) is
> **not** shipped. It depends on dev-only artifacts and a stock ROM driven
> through a local emulator. See [Advanced / dev-only](#advanced--dev-only-not-shipped)
> for what exists and why it's omitted.

## Prerequisites

- A built native port at `build/ge007` (see [BUILDING.md](BUILDING.md)).
- Your own GoldenEye ROM at `baserom.u.z64` in the repo root.
- `python3` (standard library only) for the static check and the state/audio lanes.
- `python3` with **Pillow** (`pip install pillow`) for screenshot-health and
  pixel-comparison lanes.

Dev-only stock-ROM parity routes also need a local instrumented ares binary. In
this workspace, check this path first before searching elsewhere:

```sh
build/ares-movement-oracle/ares/build-movement-oracle/desktop-ui/ares.app/Contents/MacOS/ares
```

Linux builds may use
`build/ares-movement-oracle/ares/build-movement-oracle/desktop-ui/ares`. Rebuild
with `tools/prepare_ares_movement_oracle_build.sh` if the binary is missing or
after trace-hook changes such as `glass_projection`. Do not substitute a system
`/Applications/ares.app` for movement-oracle work unless it was built by that
script; stock ares binaries do not carry the MGB64 trace, input, screenshot,
route-control, or glass-projection hooks.

The instrumentation runs the game headless and deterministically. The shipped
scripts set `GE007_DETERMINISTIC_STABLE_COUNT=1` so synthetic frame time advances
once per rendered frame instead of once per `osGetCount()` call; use the same env
var for hand-written deterministic probes, especially movement/timing captures.
Background runs should mute host audio:

```sh
export SDL_AUDIODRIVER=dummy
export GE007_MUTE=1
export PYTHONDONTWRITEBYTECODE=1
```

## The validation lanes

The public validation surface is organized into these lanes:

| Lane | What it catches | Tool |
|------|-----------------|------|
| Static | Raw native switch-node dereferences (no ROM) | `check_native_switch_access.py` |
| Boot | Spawn invariants, asserts, crashes, render-health counters on level load | `spawn_health_check.sh` |
| Playability | Deterministic gameplay input, movement records, actual player displacement, render-health counters | `playability_smoke.sh` |
| Scripted look | Deterministic input-freeze isolation while preserving authored `GE007_AUTO_LOOK_*` probes | `scripted_look_smoke.sh` |
| Damage HUD | Deterministic Bond damage, active health/damage timers, visible health/armor rings, optional HUD-class triangle check | `damage_hud_smoke.sh` |
| Ammo HUD | Per-ammo-type icon + digit pixel assertions on GL and Metal, icon-fault negative controls, `hud_image_fault` render-health counter | `ammo_hud_smoke.sh` |
| Combat | Active guard fire, hidden-guard no-phantom-fire/no-damage gates, thrown-knife impact and flight-SFX crash regressions | `hidden_guard_contract_smoke.sh`, `knife_impact_smoke.sh`, `knife_throw_sfx_smoke.sh` |
| Soak | Long headless deterministic stability run; hard-fails on any crash/recovery/bad-cmd/NaN/DL-resolve failure | `soak_stability.sh` |
| Sanitizer | Short `-DSANITIZE=ON` ASan/UBSan pass over a few stages (report-only unless `--gate`) | `asan_smoke.sh` |
| Multiplayer | Split-screen deathmatch boot; asserts the two framebuffer halves are measurably dissimilar | `mp_smoke.sh` |
| Route contract | ROM-oracle route spec/adapters, optional native route captures | `route_contract_smoke.sh` |
| Dam visual suite | Focused Dam gates for camera tilt, tunnel visibility, effect textures, palette colors, glass material, actor-masked active shards, and impact-aligned glass | `dam_visual_regression_suite.sh` |
| Intro visual | Pixel-level Bond intro body: presence (silhouette), grounding (root vs floor), red-shard outliers, with baked-in negative controls | `intro_visual_regression.sh` |
| Surface projection | Surface 1 sky-dominance regression from unscaled `field_10E0`; includes negative-control capture | `surface_projection_regression.sh` |
| Bunker brightness | Bunker 1 faithful brightness health with a bright remaster A/B sensitivity check | `bunker_brightness_regression.sh` |
| Visual oracle | Clean stock/native Dam static-glass screenshot fixture and pre-pixel guards | `glass_visual_oracle_regression.sh` |
| Active visual isolation | First-active Dam regular-glass visual fixture with exact shard-state pre-pixel guards | `glass_active_visual_isolation_regression.sh` |
| Impact visual isolation | Impact-aligned Dam regular-glass visual fixture for pane/crack/decal work | `glass_impact_visual_isolation_regression.sh` |
| Pad10092 impact seed | Actor-light Dam pad-10092 impact/decal route seed with strict impact geometry, report-only pixels | `glass_pad10092_impact_visual_regression.sh` |
| Effect footprint visual | Stock/native pixel metrics inside and outside localized effect bboxes from a pixel-semantics summary | `compare_effect_footprint_visual.py` |
| Stock RDP command stream | Dev-only ares RDP command sidecar for actual stock draw-state evidence | `analyze_stock_rdp_command_stream.py` |
| Stock RDP pixel probe | Dev-only ares Parallel-RDP post-draw framebuffer samples for exact stock per-pixel output | `analyze_stock_rdp_pixel_probe.py` |
| Glass center handoff | Read-only join from stock center-pixel output to stock command-region and native settex evidence | `analyze_glass_center_handoff.py` |
| Glass handoff points | Batch summary of multiple stock/native handoff JSONs with source, framebuffer-input, hidden-coverage, and final-output fields | `summarize_glass_handoff_points.py` |
| Glass handoff run compare | Baseline-vs-candidate classification for handoff point summaries; reports per-point wins, regressions, and neutral deltas | `compare_glass_handoff_runs.py` |
| Native TRI pixel chain | Read-only reducer for native `[TRI-PIXEL]` owner chains at a selected target/frame | `summarize_native_tri_pixel_probe.py` |
| Bullet impact sequence | Sampled stock/native bullet-impact sequence parity at selected frames; catches later-impact drift hidden by first-impact gates | `compare_bullet_impact_sequence.py` |
| Save | Cross-process EEPROM persistence smoke | `save_persistence_check.sh` |
| Screenshot | Missing, wrong-size, blank, or nearly monochrome frame captures | `audit_screenshot_health.py` |
| Pixel | Renderer regressions (fog, texture, geometry) | `compare_screenshots.py` |
| State | Spawn pos, facing, floor height, collision, NaN | `compare_state.py` |
| Sim hash | Cross-process deterministic state, per-region/byte attribution, opaque-pool pointer liveness | `streets_determinism_regression.sh`, `sim_invariance_gate.sh` |
| Audio | Static/noise, silence, synth-chain breakage | `compare_audio.py` |

## Sim-hash divergence attribution

`--sim-state-hash-out=path` covers the 8 MiB stage pool plus curated typed
globals. `GE007_SIM_HASH_PER_REGION=1` prints an isolated hash for each region.
If region `pool` is the only mover, capture both byte views:

```sh
GE007_SIM_HASH_DUMP=/tmp/pool.raw.bin \
GE007_SIM_HASH_CANON_DUMP=/tmp/pool.canon.bin \
GE007_SIM_HASH_PER_REGION=1 \
build/ge007 ... --sim-state-hash-out=/tmp/hash.json
```

The raw dump preserves source bytes; the canonical dump is exactly what the
hash consumes after pointer normalization. For the permanent Streets fixture,
`tools/streets_determinism_regression.sh --diagnostic-dumps` assigns unique dump
paths to both fix-on and both fix-off processes automatically.

Pointer policy is region-specific. Typed regions preserve relative pointer
targets. The untyped `pool` cannot safely infer a pointer merely because a word
falls inside an ASLR-moved range, so it preserves `NULL` versus non-`NULL` but
neutralizes every non-`NULL` word in the host pointer-value window. That is an
explicit ambiguity tradeoff: such a word could also be a scalar, and raw bytes
alone cannot distinguish the two. Bytes outside that window retain full
sensitivity; pointer liveness remains covered; exact pointer targets remain
covered in the typed regions. This prevents the FID-0046 false-positive class
without pretending the opaque arena has field-type information. See
`docs/fidelity/derivations/FID-0046-opaque-pool-canonicalization.md`.

## Dev-only stock RDP command stream

Use this when room-info display-list tracing is no longer enough and you need to
know what stock ares actually handed to the RDP. The hook is injected into
`ares/n64/rdp/render.cpp` by `tools/prepare_ares_movement_oracle_build.sh`, so
always use the local binary:

```sh
ARES_BIN=build/ares-movement-oracle/ares/build-movement-oracle/desktop-ui/ares.app/Contents/MacOS/ares
test -x "$ARES_BIN" || tools/prepare_ares_movement_oracle_build.sh
```

Capture a focused command sidecar:

```sh
OUT=/tmp/mgb64_glass_pad10092_stock_rdp_command_target_only_probe
rm -rf "$OUT" && mkdir -p "$OUT"

env MGB64_ARES_TRACE_RDP_COMMANDS=1 \
  MGB64_ARES_TRACE_RDP_DRAW_OPS=1 \
  MGB64_ARES_TRACE_RDP_COMMANDS_AFTER_FRAME=2541 \
  MGB64_ARES_TRACE_RDP_COMMANDS_BEFORE_FRAME=2541 \
  MGB64_ARES_RDP_COMMAND_TRACE_PATH="$OUT/rdp_command_stream.jsonl" \
  tools/glass_pad10092_impact_visual_regression.sh --no-build \
    --out-dir "$OUT" --ares-bin "$ARES_BIN" --timeout 300

python3 tools/analyze_stock_rdp_command_stream.py \
  "$OUT/rdp_command_stream.jsonl" \
  --route tools/rom_oracle_routes/dam_regular_glass_shatter_pad10092_impact_visual_probe.json \
  --coverage-model span \
  --known-image 0x80132c80 --known-image 0x801504f8 \
  --known-image 0x8013b990 --known-image 0x8014a250
```

The tracer maintains texture/combine/other/color state whenever
`MGB64_ARES_TRACE_RDP_COMMANDS=1` is enabled and writes records only inside the
requested frame window. That matters: a target-frame-only trace can now produce
valid draw state without manually writing an earlier lead-in window. The current
Dam pad-10092 proof artifact is
`/tmp/mgb64_glass_pad10092_stock_rdp_command_target_only_probe`: frame `2541`,
`2578` command records, `690` draw ops, `46` unique draw states, zero truncation,
and real draw ops for candidate stock images including `0x80132c80`,
`0x801504f8`, `0x8013b990`, and `0x8014a250`.

Add `MGB64_ARES_TRACE_RDP_DRAW_OPS=1` when you need ROI attribution. The tracer
then decodes Parallel-RDP triangle setup fields and emits conservative logical
screen bboxes per draw op. The current draw-op proof artifact is
`/tmp/mgb64_glass_pad10092_stock_rdp_draw_ops_probe`: frame `2541`, `690`
recorded draw ops, `688` valid bboxes, zero truncation, bbox union
`[1,10,319,230]`. With the pad-10092 route JSON, the analyzer reports
`projected_impact` is covered by `55` stock draw ops across `9` draw states,
with `380/380` unique ROI pixels covered. Treat the region overlap as
attribution evidence; it is still a bbox oracle, not a final per-pixel blender
oracle.

Use `--stack-limit N` to retain first/last ordered hits for each route region.
Use `--coverage-model span` to tighten bbox attribution with analyzer-side
Parallel-RDP scanline spans. The span-aware summaries for the same artifact are
`/tmp/mgb64_glass_pad10092_stock_rdp_draw_ops_probe/rdp_command_stream_summary_span_stack.txt`
and
`/tmp/mgb64_glass_pad10092_stock_rdp_draw_ops_probe/rdp_command_stream_summary_span_stack.json`.
For `projected_impact`, the span model still covers `380/380` ROI pixels but
reduces the final-owner stack to `0x8012b150` for `216/380` pixels and late
`0x80132c80` for `134/380` pixels. The bbox-only `seq=559` hit is a false
positive under the span model; the late stock glass-like owners are `seq=552`
and `seq=553`. That is the current handoff point for a per-pixel stock blender
oracle.

When a stock screenshot is available, add `--screenshot PATH` to attach observed
stock colors to each final owner state. Screenshot sampling is optional and
requires Pillow; without it, the analyzer remains standard-library only. For the
current pad-10092 artifact, add `--compare-screenshot PATH` to sample the native
screenshot on the same stock-owner masks:

```sh
python3 tools/analyze_stock_rdp_command_stream.py \
  /tmp/mgb64_glass_pad10092_stock_rdp_draw_ops_probe/rdp_command_stream.jsonl \
  --route tools/rom_oracle_routes/dam_regular_glass_shatter_pad10092_impact_visual_probe.json \
  --coverage-model span \
  --screenshot /tmp/mgb64_glass_pad10092_stock_rdp_draw_ops_probe/pad10092_impact/stock_dam_regular_glass_shatter_pad10092_impact_visual_probe.ppm \
  --compare-screenshot /tmp/mgb64_glass_pad10092_stock_rdp_draw_ops_probe/pad10092_impact/native_dam_regular_glass_shatter_pad10092_impact_visual_probe.bmp \
  --known-image 0x80132c80 --known-image 0x801504f8 \
  --known-image 0x8013b990 --known-image 0x8014a250
```

The stock/native color-aware summary
`/tmp/mgb64_glass_pad10092_stock_rdp_draw_ops_probe/rdp_command_stream_summary_span_stack_stock_native_color.json`
uses the route presentation frames: stock is sampled through active bbox
`[8,2,625,474]`, while native is sampled through full frame `[0,0,640,480]`.
It reports `projected_impact` final owners as `0x8012b150` for `216/380`
pixels with stock mean RGB `27.171,27.072,27.174` / luma `27.113` versus
native `56.762,56.434,57.310` / luma `56.632` (`+29.519` luma), and late
`0x80132c80` for `134/380` pixels with stock mean RGB
`36.948,36.201,36.146` / luma `36.418` versus native
`57.763,55.884,55.994` / luma `56.459` (`+20.041` luma). These are sampled by
stock-owner mask, not native draw ownership; override `--screenshot-frame` or
`--compare-screenshot-frame` only when intentionally testing presentation
mapping.

For exact post-blend stock output at one logical/RDP pixel, the Parallel-RDP
pixel probe is the intended next tool in the same local ares checkout. This is
deliberately separate from the command-stream sidecar: the command-stream
analyzer owns draw-state attribution, while the pixel probe waits for each
matching draw to finish and then reads the stock color framebuffer and hidden
coverage memory from RDRAM.
The local ares binary is still:

```sh
build/ares-movement-oracle/ares/build-movement-oracle/desktop-ui/ares.app/Contents/MacOS/ares
```

Generate the route-center target and env block first:

```sh
python3 tools/analyze_stock_rdp_pixel_probe.py \
  --route tools/rom_oracle_routes/dam_regular_glass_shatter_pad10092_impact_visual_probe.json \
  --region projected_impact \
  --env-path "$OUT/rdp_pixel_probe_projected_impact.jsonl" \
  --changed-only
```

For pad `10092`, the route center is logical pixel `183,165` inside
`projected_impact`. Use that value for the current route; do not map it through
the `640x480` screenshot or the `source=640x240` presentation log. This probe
reads the active RDP color image, and the proven pad-`10092` capture reaches the
final stock color-image state `fb=0x3da800/320/0/2`.

Useful capture env:

```sh
env MGB64_ARES_TRACE_RDP_PIXEL_PROBE=1 \
  MGB64_ARES_TRACE_RDP_PIXEL_PROBE_PATH="$OUT/rdp_pixel_probe_projected_impact.jsonl" \
  MGB64_ARES_TRACE_RDP_PIXEL_PROBE_X=183 \
  MGB64_ARES_TRACE_RDP_PIXEL_PROBE_Y=165 \
  MGB64_ARES_TRACE_RDP_PIXEL_PROBE_MAX_RECORDS=65536 \
  MGB64_ARES_TRACE_RDP_PIXEL_PROBE_MAX_STATS_RECORDS=65536 \
  MGB64_ARES_TRACE_RDP_PIXEL_PROBE_CHANGED_ONLY=1 \
  MGB64_ARES_TRACE_RDP_PIXEL_PROBE_DIAGNOSTICS=1 \
  tools/glass_pad10092_impact_visual_regression.sh --no-build \
    --out-dir "$OUT" --ares-bin "$ARES_BIN" --timeout 300
```

The emitted sample rows include `frame_context`, global and frame-local
command/draw counters, `op`, bbox, color-image state, current texture image
state and `texture_serial`, raw RDP `other` and `combine` words, `env`, decoded
raster/depth/blend/combiner state, draw tile/tile state, draw words, raw
framebuffer word, hidden coverage byte/word, decoded RGBA, and `changed`.
Diagnostic `stats` rows include target, framebuffer state, bbox hit counts,
readback success count, and read-failure counters. `frame_context` is a
Parallel-RDP frame-context counter, not the route gameplay frame. Use it only to
align rows inside the pixel-probe capture; keep the command-stream sidecar as
the source of gameplay-frame draw-state ownership.

Current proven pad-`10092` artifact:
`/tmp/mgb64_rdp_pixel_probe_183.rjP13r`. It passed the stock-backed route and
recorded `6387` successful readbacks, `3059` changed sample rows, and zero
readback failures at target `183,165`. The selected authoritative sample is
`frame_context=2669`, `frame_draw_sequence=321`, `texture_image=0x12f2f0`,
`bbox=[176,154,190,172]`, `raw=0x000018c7`, `hidden=0x00000003`, decoded
RGBA `[24,24,24,224]`. The handoff analyzer selects the last changed stock
pixel sample by default, not merely the last emitted row; pass
`--stock-frame-context`, `--stock-fb-addr`, or `--stock-texture-image` only when
intentionally pinning an alternate stock color-image state. The analyzer also
reports `selected_framebuffer_input` from the previous emitted sample in the same
`frame_context`, plus `selected_framebuffer_input_vs_selected_rgb`,
`selected_raw_transition`, and `selected_hidden_transition`. If the previous
emitted row is from a different frame, the input candidate is left empty rather
than treating stale frame memory as the selected draw's framebuffer input.

The current handoff join is:

```sh
python3 tools/analyze_glass_center_handoff.py \
  --stock-probe /tmp/mgb64_rdp_pixel_probe_183.rjP13r/rdp_pixel_probe_projected_impact.jsonl \
  --stock-command-summary /tmp/mgb64_glass_pad10092_stock_rdp_draw_ops_probe/rdp_command_stream_summary_span_stack_color.json \
  --native-settex-summary /tmp/mgb64_glass_pad10092_room_glass_settex_sample_current_v2/pad10092_room_glass_settex_sample.json \
  --native-pixel-log /tmp/mgb64_native_settex_pixel_probe_mapped_1782630417/native_dam_regular_glass_shatter_pad10092_impact_visual_probe.log \
  --native-pixel-target 92,93 \
  --region projected_impact \
  --json-out /tmp/mgb64_glass_center_handoff_current/glass_center_handoff.json
```

Current proof `/tmp/mgb64_native_settex_pixel_probe_mapped_1782630417` passes
and is the preferred handoff for the next renderer pass. It confirms the stock
selected center sample above, finds five `0x8012f2f0` command-region hits
(`seq=314,315,325,327,330`), and warns that the command-stream span/bbox model
also has three later different-texture hits covering the same target. Therefore
use the pixel probe as final-pixel authority and the command-stream region model
as a shortlist, not as exact center-pixel ownership. The native settex summary
remains fragment-source evidence, not final framebuffer output:
`shaderL_frag` luma min/mean/max `0.0/11.0/23.0`, alpha counts `{102: 4}`.
The native single-pixel SETTEX framebuffer probe now records the same draw
boundary in native logical space: stock/aligned target `183,165` maps to native
logical target `92,93` for this route, because the route's aligned capture is
`640x440` while the native visual viewport is `[0,10,320,220]`. The mapped
probe records `7` target-covering changed room-glass rows for texnum `654`
across frames `120..123`. Use the complete native frame selected by the settex
summary, not the screenshot-exit tail frame: frame `122` has two target-covering
rows and the selected post pixel is `[25,25,25]`, while the stock final pixel is
`[24,24,24]`. That makes the center pixel a near-match, not proof of an
eight-luma renderer defect. The next implementation target should be driven by
multi-pixel stock/native final-output evidence across the room-glass ROI before
promoting any translucent-composition change; include the new same-frame stock
framebuffer-input fields in that comparison so source, memory color, hidden
coverage, and final output can be separated per pixel.

On WebGPU, draw-boundary probes have a stricter contract than end-of-frame
screenshots. A probe read during an open frame now ends and submits the current
scene pass, reads `s_scene_tex`, and resumes color and depth with Load/Store
semantics. Before FID-0145, WebGPU only recorded the selected draw and sampled
the previous completed presentation target, so a 2,156-row all-triangle probe
reported `pre == post` for every row. This partial-submit path is diagnostic
only: ordinary gameplay still performs one vertex upload and submission at the
end of the frame. `port_webgpu_live_readback_guard` permanently pins the split,
deferred-VBO upload, live texture selection, Load/Store resume, and dynamic-state
reset contracts.

Two additional current probes show why a multi-pixel pass is required:
`/tmp/mgb64_pixel_handoff_176_158_1782631121` samples stock/aligned `176,158`
mapped to native `88,89` and reports stock `[32,32,32]` versus native
`[22,22,22]`; `/tmp/mgb64_pixel_handoff_188_170_1782631053` samples
stock/aligned `188,170` mapped to native `94,95` and reports stock
`[32,32,32]` versus native `[11,11,11]`. The refreshed native-only source probe
`/tmp/mgb64_native_pixel_source_94_95_rgba_1782631504` adds target shader-source
fields for the second point: the selected frame-`122` row has `src_valid=1`,
texture samples populated, `shaderL_frag=[0,0,0,102]`, and framebuffer movement
`[7,7,7] -> [11,11,11]`. This points at per-pixel source/filter/raster/RDP
semantics, not a uniform alpha or brightness scalar.

`tools/summarize_glass_handoff_points.py` now reduces handoff JSONs into a
multi-point final-output table. Current source-enriched proof
`/tmp/mgb64_glass_handoff_points_source_current_1782655165` refreshes center,
`176,158`, and `188,170` with the same analyzer and current native
`[SETTEX-PIXEL]` source fields. It reports `3/3` complete points: center
stock/native mean_abs_rgb `3.0`, left `10.0`, and lower-right `24.0`. Native
room-glass source differs per point (`[32,32,32,102]`, `[0,0,0,102]`,
`[22,22,22,102]`), stock same-frame framebuffer inputs differ
(`[104,96,96,224]`, `[16,48,96,224]`, `[56,40,40,32]`), and the lower-right
hidden coverage transition is `0x1 -> 0x3`. The next renderer change should be
evaluated per pixel against source, framebuffer memory, hidden coverage, and
final post output rather than by a single global opacity or color scalar.
The same reducer now inverts the observed native `pre -> post` pixels through
the selected native source alpha. The current equation summary
`/tmp/mgb64_glass_handoff_points_source_equation_1782655314.json` shows native
would need source RGB near `10.5`, `22.0`, and `9.5` to produce the observed
post pixels, while the reconstructed sources are `32`, `0`, and `22`. It also
shows the native pre pixel differs from the stock framebuffer-input candidate by
`35.333..70.667` mean_abs_rgb. Treat those as source/raster and prior-owner
ordering leads before changing the global blend equation.
The matching forced room-glass coverage-memory diagnostic
`GE007_DIAG_XLU_RDP_CVG_MEMORY_BLEND_CC=0x00738e4f020a2d12` remains a checked
negative on this lane: `/tmp/mgb64_glass_handoff_points_rdp_cvg_1782655314`
reports center `4.0`, left `10.0`, and lower-right `25.0` mean_abs_rgb, worse
than the default source-enriched run (`3.0`, `10.0`, `24.0`).
Use `tools/compare_glass_handoff_runs.py` for future candidate runs so each
renderer change is classified against the same baseline. The current forced
coverage-memory comparison is
`/tmp/mgb64_glass_handoff_run_compare_rdp_cvg_1782655314.json`: `0` wins, `2`
regressions, and `1` neutral point, with a `+0.667` mean_abs_rgb aggregate
delta.

2026-06-28 glass tie-off: pause here unless resuming the bounded raw-state
handoff. The next useful capture is one stock Parallel-RDP pixel-probe rerun at
stock/aligned `188,170` with the current local ares binary, followed by the
native SETTEX pixel probe at mapped logical `94,95`. Compare stock `other`,
`combine`, `env`, `draw_tile`, `tile_state`, decoded blend/depth/combiner
fields, and `draw_words` against the native `[SETTEX-PIXEL]` row. Avoid broad
A/B screenshot sweeps and alpha/brightness tuning until that state comparison
has either explained the off-center delta or falsified this thesis.

The native SETTEX single-pixel probe is default-off and lives in the native
fast3d path. Use it when a stock RDP pixel target needs a matching native
draw-boundary framebuffer sample:

```sh
OUT=/tmp/mgb64_native_settex_pixel_probe_mapped
env GE007_TRACE_SETTEX_PIXEL=0x00738e4f020a2d12 \
  GE007_TRACE_SETTEX_PIXEL_AFTER_FRAME=120 \
  GE007_TRACE_SETTEX_PIXEL_BUDGET=64 \
  GE007_TRACE_SETTEX_PIXEL_TEXNUM=654 \
  GE007_TRACE_SETTEX_PIXEL_TEXSIZE=54x54 \
  GE007_TRACE_SETTEX_PIXEL_X=92 \
  GE007_TRACE_SETTEX_PIXEL_Y=93 \
  tools/movement_oracle_capture.sh \
    --route dam_regular_glass_shatter_pad10092_impact_visual_probe \
    --native-only --no-compare \
    --out-dir "$OUT" \
    --rom baserom.u.z64 \
    --binary build/ge007 \
    --no-build \
    --timeout 300
```

Rows are emitted as `[SETTEX-PIXEL]` JSON in the native log and include the
target, framebuffer/gl coordinates, triangle id, barycentrics, combiner ids,
draw class, display-list room, raw/effective othermodes, decoded depth/coverage
state, texture number/size, screen bbox, target texel samples, interpolated shade/fog,
shader-mirrored target fragment values, pre/post RGB, delta, and changed flag.
Rows with reconstructed source data also include `pred_alpha` and `pred_rdp`,
computed from `shaderL_frag` plus the pre-draw framebuffer pixel. Their
`post_delta_*` fields answer whether the observed native post pixel follows
ordinary GL alpha blending or the current RDP force-blend equation before
chasing broader stock/native ownership hypotheses.
Leave `GE007_TRACE_SETTEX_PIXEL_INSIDE_ONLY=1` at its default when you want only
target-covering rows; set it to `0` only to debug coordinate-space mistakes.
For material-level traces, the `[SETTEX-MATERIAL-CC] lodgate={...}` fields
separate source sampling from final compositing: authored room-XLU trilerp
should report `cc_lod=1`, `settex_endpoint=1`, and `allowfp=1` even if
`roommtx=0`. Use `GE007_DISABLE_SETTEX_FOOTPRINT_LOD=1` as the focused
negative control before changing color scale, alpha scale, or RDP blend policy.
`tools/analyze_glass_center_handoff.py` selects
`native_settex.frame_selection.selected` by default when a native pixel log is
provided, and compares native post pixels against `stock_pixel.selected_sample`.
Pass `--native-pixel-frame N` only when deliberately inspecting a different
frame.

For generalized final-owner work, use the native all-triangle pixel probe. It
does not reconstruct shader sources, but it brackets every selected triangle
with a flush/readback boundary and records the same ownership/depth/coverage
context plus `pre`, `post`, `delta`, and `changed`:

```sh
OUT=/tmp/mgb64_native_tri_pixel_probe
env GE007_TRACE_TRI_PIXEL='*' \
  GE007_TRACE_TRI_PIXEL_AFTER_FRAME=122 \
  GE007_TRACE_TRI_PIXEL_BUDGET=1600 \
  GE007_TRACE_TRI_PIXEL_X=94 \
  GE007_TRACE_TRI_PIXEL_Y=95 \
  GE007_TRACE_TRI_PIXEL_INSIDE_ONLY=0 \
  tools/movement_oracle_capture.sh \
    --route dam_regular_glass_shatter_pad10092_impact_visual_probe \
    --native-only --no-compare \
    --out-dir "$OUT" \
    --rom baserom.u.z64 \
    --binary build/ge007 \
    --no-build
```

Rows are emitted as `[TRI-PIXEL]` JSON. Leave
`GE007_TRACE_TRI_PIXEL_INSIDE_ONLY=1` for focused owner probes; set it to `0`
when auditing edge coverage or suspected non-triangle/rect handoffs. The
`GE007_TRACE_TRI_PIXEL` value accepts `1`, `*`, or an effective-combiner list,
and `GE007_TRACE_TRI_PIXEL_DRAWCLASS=name` limits rows to matching draw classes.
Triangle rows now include a `rect` object. For ordinary world triangles this is
`op:"none"`; for `FILLRECT`, `TEXRECT`, and `TEXRECTFLIP` converted through the
triangle path it records the raw RDP rectangle, expanded draw rectangle, tile,
ST deltas, derived UV extent, cycle type, fill/primitive colors, and which of
the two generated triangles is being probed. Use
`GE007_TRACE_TRI_PIXEL_RECT_ONLY=1` to suppress ordinary triangles and isolate
screen-space rectangle ownership.

Summarize large native triangle logs before hand-reading them:

```sh
python3 tools/summarize_native_tri_pixel_probe.py "$OUT"/native_*.log \
  --target 94,95 \
  --frame 122 \
  --changed-only
```

The current lower-right Dam glass owner-chain proof is
`/tmp/mgb64_native_tri_pixel_lower_right_all_1782656042/tri_pixel_chain_94_95_frame122_changed.json`.
At native target `94,95`, frame `122`, the changed chain is: sky texnum `2228`
changes `[16,48,96] -> [42,71,113]`; room `132` primary texnum `949` changes
`[42,71,113] -> [11,11,11]`; texnum `654` glass then changes
`[11,11,11] -> [7,7,7] -> [8,8,8]`. This explains the dark native pre-glass
input in the lower-right handoff and makes room texnum-`949` ownership/order the
next concrete comparison point before changing texnum-`654` blending.
The matching refreshed stock point is
`/tmp/mgb64_stock_pixel_188_170_diag_1782656309/handoff_188_170_refreshed.json`.
For stock/aligned `188,170`, frame context `2600`, the pre-glass owner is
texture `0x149b28`, raw `0x0000394a`, hidden `0x1`, tile masks `5x5`, combiner
`0xfc26a004/0x1f1093ff`, and output `[56,40,40,32]`; final glass texture
`0x12f2f0` then outputs `[32,32,32,224]`. The native texnum-`949` source probe
`/tmp/mgb64_native_settex_pixel_tex949_lower_right_1782656233` reconstructs an
opaque source near `[10,10,10,255]`, so the next code hypothesis is
room/background source sampling or texture-coordinate interpretation for that
pre-glass owner, not an alpha-only texnum-`654` fix.

Deferred secondary-room XLU batches are drawn after their source triangles have
been queued, so their final pixel ownership is not visible as a normal
`[TRI-PIXEL]` post-draw row. Use the default-off deferred probe when a
triangle trace shows a pre/post gap that may be caused by sorted room XLU:

```sh
OUT=/tmp/mgb64_native_room_xlu_defer_pixel_probe
env GE007_TRACE_ROOM_XLU_DEFER_PIXEL=1 \
  GE007_TRACE_ROOM_XLU_DEFER_PIXEL_AFTER_FRAME=122 \
  GE007_TRACE_ROOM_XLU_DEFER_PIXEL_BUDGET=256 \
  GE007_TRACE_ROOM_XLU_DEFER_PIXEL_X=94 \
  GE007_TRACE_ROOM_XLU_DEFER_PIXEL_Y=95 \
  GE007_TRACE_ROOM_XLU_DEFER_PIXEL_INSIDE_ONLY=0 \
  tools/movement_oracle_capture.sh \
    --route dam_regular_glass_shatter_pad10092_impact_visual_probe \
    --native-only --no-compare \
    --out-dir "$OUT" \
    --rom baserom.u.z64 \
    --binary build/ge007 \
    --no-build
```

Rows are emitted as `[ROOM-XLU-DEFER-PIXEL]` JSON and include batch index,
room, command address, draw class, combiner, raw/effective othermodes, saved
blend/depth state, batch bbox, pre/post RGB, delta, and changed flag. The Dam
pad10092 `[94,95]` proof run on 2026-06-28 produced 68 rect-only rows with zero
target changes and zero deferred-room-XLU rows, ruling out both classes for the
remaining local `[8,8,8] -> [10,10,10]` triangle-probe gap.

When the all-triangle trace still has a pre/post discontinuity next to native
sky draws, use the default-off sky preparation probe to bracket native sky queue
and marker replay:

```sh
OUT=/tmp/mgb64_native_sky_prep_pixel_probe
env GE007_TRACE_SKY_PREP_PIXEL=1 \
  GE007_TRACE_SKY_PREP_PIXEL_AFTER_FRAME=122 \
  GE007_TRACE_SKY_PREP_PIXEL_BUDGET=80 \
  GE007_TRACE_SKY_PREP_PIXEL_X=94 \
  GE007_TRACE_SKY_PREP_PIXEL_Y=95 \
  tools/movement_oracle_capture.sh \
    --route dam_regular_glass_shatter_pad10092_impact_visual_probe \
    --native-only --no-compare \
    --out-dir "$OUT" \
    --rom baserom.u.z64 \
    --binary build/ge007 \
    --no-build
```

Rows are emitted as `[SKY-PREP-PIXEL]` JSON and sample the framebuffer at
`gfx_prepare_sky_rendering()`, queue insertion, marker replay, sky triangle
submission, and sky triangle emit-state checkpoints. They include the
target/framebuffer coordinates, current RGB, previous same-frame RGB, triangle
counter, sky/settex state, raw/effective othermodes, geometry mode, renderer
depth/blend state, viewport, and scissor. In the default queued path, expect
`queue_prepare`/`queue_tri` during display-list construction and
`replay_*`/`sky_tri_*` when the interpreter reaches the PC-only marker. Use
`GE007_DISABLE_SKY_QUEUE=1` as the negative control: on the Dam pad10092
`[94,95]` proof target, default queue replay removes same-frame
`[TRI-PIXEL]` post/pre jumps, while disabling the queue reproduces the old
tri1080-to-sky `[8,8,8] -> [10,10,10]` gap with `prepare_begin` already at
`[10,10,10]`.

Summarize a capture with:

```sh
python3 tools/analyze_stock_rdp_pixel_probe.py \
  "$OUT/rdp_pixel_probe_projected_impact.jsonl" \
  --route tools/rom_oracle_routes/dam_regular_glass_shatter_pad10092_impact_visual_probe.json \
  --region projected_impact
```

The ROM-free analyzer regression guard is:

```sh
python3 tools/check_stock_rdp_command_stream_regression.py
python3 tools/check_stock_rdp_pixel_probe_regression.py
python3 tools/check_glass_center_handoff_regression.py
python3 tools/check_glass_handoff_points_regression.py
python3 tools/check_glass_handoff_run_compare_regression.py
python3 tools/check_native_tri_pixel_probe_regression.py
python3 tools/check_room_glass_source_reconstruction_regression.py
```

It constructs a synthetic sidecar where bbox attribution would incorrectly give
a late non-overlapping triangle full final ownership, then asserts that
`--coverage-model span` preserves the true split final-owner stack. The same
guard is registered with CTest as
`port_stock_rdp_command_stream_analyzer_guard`.
The pixel-probe guard constructs a synthetic post-draw JSONL capture and checks
route-center targeting, changed-sample counting, texture-state aggregation, and
the suggested probe env block. It is registered with CTest as
`port_stock_rdp_pixel_probe_analyzer_guard`.

## Quick lane (start here)

The fast path is wired up in one script:

```sh
./tools/validate_quick.sh
```

It always runs the static switch-access guard (no ROM, no build needed). If a
`build/ge007` and a `baserom.u.z64` are present, it also runs a short boot/spawn
smoke. Missing prerequisites are reported as SKIPs, never hard failures, so the
static guard can run anywhere (including CI without a ROM).

## Running the lanes individually

### Static switch-access guard (always available)

```sh
./tools/check_native_switch_access.py
```

Fails if native-visible game code dereferences `ModelFileHeader->Switches[]`
directly outside the allowlisted low-level helper in `src/game/model.c`. Pure
source scan: no ROM, no build, standard-library Python only.

### Boot / spawn health

```sh
./tools/spawn_health_check.sh                 # Dam + Cradle
./tools/spawn_health_check.sh --all           # all 20 stages
./tools/spawn_health_check.sh --level 33      # single raw LEVELID
./tools/spawn_health_check.sh --no-build      # reuse an existing build
```

For each level it boots deterministically with `GE007_DEBUG=1` and checks:
camera-handoff seed, an initialized `standheight`, zero `[GEASSERT]` failures,
presence of guard `CHR_RENDER` calls, a clean exit, and a strict
`tools/audit_render_trace.py` pass over the generated JSONL trace. The render
audit fails on crash recoveries, unhandled GBI commands, display-list resolve
failures, room-render fallback records, or non-finite trace values. Exit code =
number of failed levels.

### Gameplay playability smoke

```sh
./tools/playability_smoke.sh                 # Dam + Cradle
./tools/playability_smoke.sh --all           # all 20 stages
./tools/playability_smoke.sh --level 33      # single raw LEVELID
./tools/playability_smoke.sh --no-build      # reuse an existing build
./tools/playability_smoke.sh --all --no-build \
  --config-override Video.RenderScale=2      # scene-target probe
```

This direct-native lane disables authored intros explicitly, holds a deterministic
gameplay stick window, captures a state trace plus screenshot, and requires:
clean process exit, zero `[GEASSERT]` failures, a valid 640x480 nonblank
screenshot, strict render-health audit, target player records for the requested
raw `LEVELID`, at least the configured nonzero `move.speed` record count, no
watch/pause state, and a minimum horizontal player position delta. The default
route tries `forward`, `right`, `back`, then `left` for each level and accepts
the first pattern that satisfies all checks. Use `--pattern`, `--input-window`,
`--min-moving-records`, and `--min-horizontal-delta` when validating a narrower
route.

The lane pins `Video.WindowWidth=640`, `Video.WindowHeight=480`, and
`Video.WindowMode=windowed` with command-line config overrides by default. This
keeps generated `ge007.ini` files and high-DPI desktop window sizes from
changing screenshot composition or scene-target size between attempts. Add
repeatable `--config-override Section.Key=value` options for targeted probes,
or pass `--no-default-config-overrides` when the point of the test is the user's
actual window/config profile.

After each process run, the lane audits the generated `ge007.ini` against every
requested override whose value should persist. This catches branch-vs-reference
mistakes where an older binary ignores `--config-override` and renders with a
different window/aspect while the harness log still lists the requested
settings. `Video.WindowX=-1` and `Video.WindowY=-1` are intentionally exempt
because the runtime persists the resolved centered window position.

The output directory defaults to `/tmp/mgb64_playability_smoke_*`. It includes a
`summary.tsv` row for each level's accepted pattern, a top-level `summary.json`
with the accepted-level list and pass/fail counts, a `contact_sheet.png` visual
review sheet of accepted screenshots, plus per-attempt screenshot, render, and
movement audit JSON files. Movement audit JSON contains moving-record counts,
displacement, target-player record counts, input-event counts, and any
failures. The top-level summary also records the config overrides applied to the
run. Keep generated JSONL traces, screenshots, summaries, contact sheets, and
audit logs local.

#### Bunker 1 full-boot gait/debug-dump regressions

Bunker 1 (`LEVELID 9`) has a recent native-only regression class that direct
playability smoke alone can miss. Bad signatures include a full-game/frontend
boot into Bunker where Bond cannot move forward/backward from spawn, the
authored intro/head state is not seeded correctly, and logs spam one of:

```text
[ANIM_FRAME2_GUARD] model=... chr=0x0 frame1=-... frame2=-... speed=0.540 playspeed=1.000
[ANIM_FRAME_SET_GUARD] model=... chr=0x0 frame=-... safe=... speed=0.500 playspeed=1.000
```

Root cause: on `NATIVE_PORT`, `struct player::model` is a pointer to the
lightweight Bond gait/head model. A dirty full-frontend stage mempool could make
that pointer look non-null before the native gait model was rebuilt, causing
`bhead` animation to consume unrelated memory as a `Model`. A second native
overlay mismatch kept the bug alive after pointer validation: `field_5C0` used
to mirror the embedded N64 model's current gait frame, but the native player now
stores `model` out-of-line. `bheadAdjustAnimation()` still mapped transitions
from raw `field_5C0`, so stale huge frame values could be fed back into
`modelSetAnimation()` even when the gait model header was otherwise valid.
Native `modelSetAnimPlaySpeed(..., startframe=0)` also must keep `animrate`
synchronized with `playspeed`, or repair code can falsely reject a rebuilt gait
animation.

The datathief/equipment crash in this same area had a different root cause:
the live debug dump walked every chr `prop`, `model`, `render_pos`, root rwdata,
and bone matrix without validating that those pointers still belonged to the
current prop table or live dyn-vtx buffer. The observed Bunker datathief crash
stack was in `debugDumpExecute`; the `CRASH-TEX` line was stale renderer
diagnostic context. The dump path must be non-fatal by default: it may report
model/render state, but live bone matrices are opt-in with
`GE007_DEBUG_DUMP_BONES=1`, and raw root rwdata bytes are opt-in with
`GE007_DEBUG_DUMP_RWDATA=1`.

Keep this regression guarded by the checks below. For the no-arg/frontend route,
use `GE007_AUTO_SELECT_LEVEL=bunker1`; numeric `9` is interpreted by the current
selector as a menu index before it is treated as a level id.

```sh
./tools/playability_smoke.sh --level 9 --frames 420 \
  --input-window 120:100 --pattern forward --no-build

GE007_AUTO_SELECT_LEVEL=bunker1 \
GE007_AUTO_SELECT_DELAY=1 \
GE007_AUTO_EXIT_FRAME=850 \
GE007_AUTO_FORWARD=260:260 \
./build/ge007 --deterministic --background --no-input-grab \
  --trace-state /tmp/bunker_noarg_select.jsonl \
  > /tmp/bunker_noarg_select.log 2>&1

rg -n 'ANIM_FRAME[0-9_]*_GUARD|BHEAD_INVALID|CRASH|Signal' \
  /tmp/bunker_noarg_select.log

GE007_ENABLE_LEVEL_INTRO=1 \
GE007_AUTO_EXIT_FRAME=700 \
GE007_AUTO_START=80:82 \
GE007_AUTO_FORWARD=140:260 \
./build/ge007 --level bunker --deterministic --background --no-input-grab \
  --trace-state /tmp/bunker_intro_move.jsonl \
  > /tmp/bunker_intro_move.log 2>&1

rg -n 'ANIM_FRAME[0-9_]*_GUARD|BHEAD_INVALID|CRASH|Signal' \
  /tmp/bunker_intro_move.log

GE007_DIAG_POISON_BHEAD_FRAME=1 \
GE007_AUTO_EXIT_FRAME=460 \
GE007_AUTO_FORWARD=120:160 \
./build/ge007 --level bunker --deterministic --background --no-input-grab \
  --trace-state /tmp/bunker_poison_move.jsonl \
  > /tmp/bunker_poison_move.log 2>&1

rg -n 'BHEAD_DIAG_POISON_GAIT|BHEAD_RESET_GAIT|ANIM_FRAME[0-9_]*_GUARD|CRASH|Signal' \
  /tmp/bunker_poison_move.log

GE007_AUTO_ADD_ITEM_FRAME=80 \
GE007_AUTO_ADD_ITEM=55 \
GE007_AUTO_EQUIP_ITEM_FRAME=130 \
GE007_AUTO_EQUIP_ITEM=55 \
GE007_AUTO_DEBUG_DUMP_FRAME=220 \
GE007_AUTO_EXIT_FRAME=520 \
./build/ge007 --level bunker --deterministic --background --no-input-grab \
  --trace-state /tmp/bunker_datathief.jsonl \
  > /tmp/bunker_datathief.log 2>&1

rg -n 'DUMP|CRASH|Signal|ANIM_FRAME[0-9_]*_GUARD|BHEAD_INVALID' \
  /tmp/bunker_datathief.log ./ge007_dump_*.txt
# AUDIT-0066: dumps now land in the save directory (savedirPath(), see
# savedir.c), not a hardcoded /tmp path. Without --savedir this is CWD (if
# writable) or $HOME/.ge007/ -- adjust the glob above if yours resolves there.
```

The expected result is: `playability_smoke.sh` passes movement for level `9`;
the no-arg selector trace spends gameplay frames on stage `9` and shows real
horizontal movement after the scripted forward window; the forced-authored-intro
trace reaches first-person and moves; and the warning scans for the normal lanes
print no matches. The poisoned-frame lane must print one
`BHEAD_DIAG_POISON_GAIT` line with the huge negative frame value and one
`BHEAD_RESET_GAIT` line, then no `ANIM_FRAME*_GUARD` or crash lines; the trace
must still move. The datathief lane must equip item `55`, write one dump, show
the right hand as item `55` in the trace/dump, and not crash. If this class
returns, inspect the native player/gait path before collision or input:
`player->model` must be rebuilt/validated, `field_5C0` must be initialized from
and resynchronized to the live gait animation frame, bad gait frame state must
be repaired before `modelTickAnimQuarterSpeed()`, immediate `animrate` must stay
synced with `playspeed`, and debug dump must validate prop/model/dyn pointers
before dereferencing optional live state.

### Scripted look smoke

```sh
./tools/scripted_look_smoke.sh
./tools/scripted_look_smoke.sh --no-build --level 36
```

This lane exists because deterministic screenshot/oracle captures set
`g_freezeInput` to block live keyboard, mouse, and gamepad input. That freeze
must not suppress authored `GE007_AUTO_LOOK_*` probes; otherwise broad
look-direction screenshot comparisons silently capture a different camera angle
and mislabel composition drift as a renderer regression. The smoke direct-boots
a baseline and a `GE007_AUTO_LOOK_UP` run, then requires the baseline pitch to
stay near zero while the scripted run changes `move.pitch` by the configured
minimum. Keep the generated traces and screenshots local.

### Damage HUD smoke

```sh
./tools/damage_hud_smoke.sh                 # Dam, Surface 1, Facility
./tools/damage_hud_smoke.sh --level 33      # single raw LEVELID
./tools/damage_hud_smoke.sh --no-build      # reuse an existing build
```

This focused lane injects deterministic Bond damage, captures a state trace plus
screenshot, and requires: clean process exit, zero `[GEASSERT]` failures, valid
screenshot health, strict render-health audit, active `damage_show` and
`health_show` trace state, and visible warm health-ring plus cool armor-ring
pixels. HUD-class triangle counting is optional via `--min-hud-tris`; the
default level set includes Dam and Surface 1 because their level visibility scale
is `0.2`, plus Facility as the normal-scale control.

The output directory defaults to `/tmp/mgb64_damage_hud_smoke_*` and contains
per-level screenshots, logs, JSONL traces, render audit JSON, damage-HUD audit
JSON, and a `summary.tsv`. Keep these ROM-derived artifacts local.

### Ammo HUD smoke

```sh
./tools/ammo_hud_smoke.sh                   # GL (+ Metal on macOS), all 13 icon types
./tools/ammo_hud_smoke.sh --backends gl     # single backend
./tools/ammo_hud_smoke.sh --no-build        # reuse an existing build
```

Validates the ammo HUD for every distinct ammo-icon type (backlog M2.6 /
audit R6): 9mm, rifle, shotgun, grenade, rocket, remote/proximity/timed mine,
throwing knife, grenade round, magnum, golden gun, and tank shell
(`AMMO_9MM_2` has no icon by design). Per type it runs three deterministic
Dam captures that differ in exactly one variable and asserts pixel diffs that
isolate one HUD element each: icon presence via equipped-vs-icon-fault
(digits cancel), digit presence via two ammo values (icon cancels) — see
`tools/audit_ammo_hud_capture.py` for the isolation argument. Normal runs
must report zero `hud_image_fault` (strict `audit_render_trace.py`); the
fault runs must render the bordered-rectangle placeholder, log
`[HUD][RENDER-HEALTH]`, and move the counter. Negative controls: digits
still render under an icon fault, and `GE007_AMMO_ICON_FAULT_INVALID`
exercises the `portValidateImageEntry` poisoned-entry path. On macOS the
whole suite runs under both GL and Metal (`GE007_RENDERER=metal`).

The output directory defaults to `/tmp/mgb64_ammo_hud_smoke_*` and contains
per-run screenshots, logs, JSONL traces, audit JSON, and a `summary.tsv`
with per-type icon/digit diff pixel counts per backend. Keep these
ROM-derived artifacts local.

### Combat guard and knife smokes

```sh
./tools/hidden_guard_contract_smoke.sh --no-build
./tools/knife_impact_smoke.sh --no-build
./tools/knife_throw_sfx_smoke.sh --no-build
```

The hidden-guard lane warps a Dam guard into an active firing setup, then hides
it in two modes. The H2 mode clears `CHRFLAG_00040000` and requires the hidden
guard's AI/firecount to freeze with no further Bond damage. The H1 mode leaves
the update-action bit set, requires the AI to keep ticking, and still requires
firecount and Bond health to stay stable, isolating the hidden-fire discharge
gate. Both modes fail if the guard did not fire while visible.

The knife impact lane equips Bond with throwing knives, lands an accepted Bond
knife hit on a live guard, and proves the throwknife guard-impact branch exits
cleanly instead of crashing. The knife SFX lane keeps thrown knives in flight
long enough to require only valid whoosh SFX submissions from the object handler.

### Stability soak

```sh
./tools/soak_stability.sh                 # Dam, Cradle, Caverns
./tools/soak_stability.sh --all           # all 20 stages
./tools/soak_stability.sh --frames 10800  # longer soak per stage
./tools/soak_stability.sh --no-build      # reuse an existing build
```

This long headless deterministic lane boots each stage with `GE007_DEBUG=1`,
runs it for `--frames` frames, captures the JSONL state trace, and pipes it
through `tools/audit_render_trace.py --max-crashes 0`. Any crash, recovery,
unhandled GBI command, non-finite trace value, or display-list resolve failure
hard-fails the lane. Per-frame render cost is now part of the trace via the
`tris` and `rooms_drawn` fields (triangle count and `g_BgNumberOfRoomsDrawn`),
so soak summaries can track rendering load over time. The output directory
defaults to `/tmp/mgb64_soak_stability_*`. Exit code = number of failed stages.

### ASan/UBSan smoke

```sh
./tools/asan_smoke.sh                 # report-only, exits 0
./tools/asan_smoke.sh --gate          # fail on any sanitizer finding
./tools/asan_smoke.sh --no-build      # reuse an existing build-asan binary
```

This configures a separate `-DSANITIZE=ON` build directory (`build-asan`) and
runs a short deterministic pass over a few stages with
`ASAN_OPTIONS=halt_on_error=1:detect_leaks=0` and
`UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`. It is report-only by default
and always exits 0; pass `--gate` to make sanitizer findings fail the lane. The
output directory defaults to `/tmp/mgb64_asan_smoke_*`.

### Multiplayer split-screen smoke

```sh
./tools/mp_smoke.sh                                   # 2P temple deathmatch
./tools/mp_smoke.sh --players 4 --mp-stage complex    # 4P split-screen
./tools/mp_smoke.sh --timelimit 4                     # forced 4s match timer boundary
./tools/mp_smoke.sh --no-build                        # reuse an existing build
```

This boots a split-screen deathmatch via the native `--multiplayer`,
`--players`, `--mp-stage`, and `--scenario` flags, drives a short deterministic
P1 input window, and requires: clean process exit, zero `[GEASSERT]` failures,
a strict render-health audit (`--max-crashes 0`), and a valid 640x480
screenshot. It then crops the framebuffer at `SCREEN_HEIGHT/2` and uses
`tools/compare_screenshots.py` to assert the top (P1) and bottom (P2) halves are
measurably dissimilar; a duplicated-camera regression that renders identical
halves fails the lane. `--timelimit SECS` additionally passes
`--mp-timelimit` to the native direct-boot path and asserts the MP trace timer
reaches the requested limit crash-free. The output directory defaults to
`/tmp/mgb64_mp_smoke_*`.

Native-port feature patterns, current user-facing settings, and validation lane
expectations are summarized in [PORTING_AND_EXPANSION.md](PORTING_AND_EXPANSION.md).

### ROM-oracle route contract smoke

```sh
./tools/route_contract_smoke.sh
./tools/route_contract_smoke.sh --native-smoke --no-build --rom baserom.u.z64 --binary build/ge007
```

The ROM-free mode validates every built-in route spec plus generated native-env
and ares-input adapters. `--native-smoke` runs each route through the native side
of `tools/movement_oracle_capture.sh`, so route-level screenshots, render
audits, movement audits, and intro actor/animation audits execute without a
stock ares oracle build. The output directory defaults to
`/tmp/mgb64_route_contract_smoke_*`; generated traces, screenshots, logs, and
summaries are ROM-derived local artifacts and must not be committed.

The movement-oracle wrapper applies the same FID-0135 capture contract to every
route, including trace-oriented routes that still emit a terminal screenshot:
native overrides end with `640x480`, windowed, and HiDPI off; the saved config is
audited for those effective values; and `GE007_FIDELITY_CAPTURE=1` rejects a
runtime aspect mismatch. Route-local and ad-hoc overrides cannot silently move
the native half away from retail's 4:3 presentation boundary.

With a local instrumented ares binary from
`tools/prepare_ares_movement_oracle_build.sh`,
`tools/movement_oracle_capture.sh --ares-bin ...` captures the same route from
stock ares. The stock lane now writes a local `stock_<route>.ppm` screenshot from
the post-`Screen::refresh()` presented ares viewport, audits it with
`audit_screenshot_health.py`, and records it in `summary_<route>.json`. The
oracle arms the dump from the N64 VI hook, then writes the next presented
viewport and scales that source viewport to the route screenshot dimensions. By
default the dump uses `stock_frames`, but a route may set
`stock_screenshot_frame` when the emulator needs extra menu/frontend frames and
the useful visual checkpoint happens earlier than process exit. Routes can also
set `native_screenshot_game_timer` and `stock_screenshot_game_timer` to capture
once `g_GlobalTimer` reaches the requested mission tick; the stock hook also
waits for the configured `stock_level` so frontend frames cannot satisfy the
timer early. Stock routes can pass stock-only oracle settings through
`stock_env`; for visual checkpoints that cannot yet be reached by controller
input, `MGB64_ARES_FORCE_PLAYER_SCRIPT` accepts
`START:LEN:X:Y:Z:YAW_DEG:PITCH_DEG:EYE_OFFSET[:PAD]` entries on the stock
gameplay timeline. In this workspace, check the macOS ares binary at
`build/ares-movement-oracle/ares/build-movement-oracle/desktop-ui/ares.app/Contents/MacOS/ares`
before searching elsewhere; Linux builds may use
`build/ares-movement-oracle/ares/build-movement-oracle/desktop-ui/ares`.
`X:Y:Z` is the viewer/camera position; `EYE_OFFSET` seeds
floor/height as `Y - EYE_OFFSET`; optional `PAD` resolves the stock setup stan
and writes player tile, portal tile, prop stan, and current background room.
Pair that with `stock_min_force_player_applies` and, when pad resolution matters,
`stock_min_force_player_stan_applies` so the stock trace audit fails if the
direct-position hook did not run or did not resolve a real stan. The hook leaves
the ROM-owned room render basis alone; do not force `current_model_pos` as a
viewer position. Native deterministic captures have the matching
`GE007_AUTO_FORCE_PLAYER_SCRIPT` hook for stock-backed visual probes:
`START-END:X:Y:Z:YAW_DEG:PITCH_DEG[:EYE_OFFSET[:PAD]]`. `X:Y:Z` is the
viewer/camera position, `EYE_OFFSET` seeds floor/height as `Y - EYE_OFFSET`,
and optional `PAD` resolves through the same setup/bound-pad path as the warp
helpers so `current_tile_ptr`, `current_tile_ptr_for_portals`, `prop->stan`, and
`g_BgCurrentRoom` stay coherent. The hook only runs in deterministic mode. It
also zeros movement, turn/pitch speed, vertical bounce, and shot-speed fields,
then rebuilds `vv_theta`/`vv_verta`, `theta_transform`, and the applied view
basis. Use it when the stock route uses `MGB64_ARES_FORCE_PLAYER_SCRIPT` and a
native direct warp would otherwise drift from damage/recoil/movement before the
screenshot. Use the dump as a candidate visual checkpoint next to the
movement/intro/visual comparator, but do not add pixel thresholds until the
route has an accepted stock/native presentation normalization policy. It is
still a ROM-derived artifact and must stay out of git.

For renderer/config A/B probes on stock-backed routes, use
`tools/movement_oracle_capture.sh --native-config-override Section.Key=value`.
The wrapper appends these native overrides after the route's own
`native_config_overrides`, audits the effective `ge007.ini`, and accepts the
same option through `tools/glass_active_visual_isolation_regression.sh`. This
matters for probes such as `--native-config-override Video.MSAA=4`: route files
often pin `Video.MSAA=0`, and plain `GE007_MSAA=4` will not override a later
route-level config write.

Visual routes can define named comparison regions with `visual_regions` entries
in the route file:

```json
{ "name": "center_glass", "roi": [260, 120, 140, 180] }
```

`tools/movement_oracle_capture.sh` passes each entry to
`tools/compare_screenshots.py --region NAME:X,Y,W,H`, and the resulting
`visual_compare_<route>.json` plus `summary_<route>.json` record per-region
changed pixels, mean RGB, unique colors, per-channel differences, and feature
metrics under `regions`. Feature metrics include bright/near-white pixel counts,
connected bright components, and warm red/orange pixel counts, which are useful
for separating effect bursts from damage-HUD arcs. These coordinates are final
comparison coordinates after any active-normalized crop/resize and after any
direct comparator ROI, so keep them tied to the route's comparison profile. Use
them for stable glass, PP7/HUD, room surface, and guard-content ROIs while
whole-frame thresholds are still being normalized.

When a visual route needs an aggregate that excludes known overlay/noise regions,
set `visual_mask_exclude_regions` to a list of names from `visual_regions`.
`movement_oracle_capture.sh` passes those regions to
`compare_screenshots.py --exclude-region`, and the JSON report writes a
`masked` aggregate with changed-pixel and feature metrics over the remaining
pixels. The excluded regions are still reported individually under `regions`, so
this should be used to separate glass/material evidence from HUD/viewmodel
evidence, not to hide defects.

Visual routes run their declared health, actor, glass/prop, and impact guards
before screenshot pixel comparison. Set `compare_require_health_match` when Bond
health and damage/health HUD timers must match at the screenshot checkpoint;
optional `compare_health_tolerance` and `compare_damage_show_tolerance` override
the health comparator defaults. Routes that declare actor guards with
`compare_actor_chrnums` run that guard in the same pre-pixel phase. Visual
routes can also expose the normal glass comparator options, including
`compare_require_active`, `compare_max_active_tolerance`,
`compare_first_active_tolerance`, `compare_first_position_tolerance`,
`compare_first_sample_tolerance`, `compare_require_prop_destroyed`,
`compare_prop_position_tolerance`, `compare_require_impact_active`,
`compare_require_impact_match`, and `compare_impact_position_tolerance`. When a
visual route declares any of those, `movement_oracle_capture.sh` runs
`compare_glass_trace.py` before pixels and records
`compare_<route>.json`. A dirty stock checkpoint, such as an active-shard route
that missed its pane and never created stock shards, fails at
`compare_glass_trace.py` and does not write `visual_compare_*` artifacts. The
wrapper still writes
`summary_<route>.json` with `status: "fail"` plus any health, actor, and impact
JSON artifacts that were produced. This keeps route-control failures from being
misread as renderer deltas. When a route sets
`compare_actor_frame: "screenshot"`, the wrapper compares actor state at the
actual stock/native screenshot checkpoints. Frame-triggered native captures write
the trace record immediately before the requested screenshot frame, so
`--screenshot-frame N` maps to actor trace frame `N-1`. For timer-triggered
routes, the wrapper reads the `combat_health_compare_<route>.json` checkpoint
frames and feeds those concrete frames to the actor comparator, so nominal
timer values that do not directly appear as trace `move.global` records do not
make a clean visual route fail before pixels for the wrong reason.

Use `tools/score_visual_checkpoints.py --require-active stock.jsonl native.jsonl`
to scout alternative visual checkpoints before editing a route. It scores all
stock/native trace-frame pairs by shard timer, Bond health/HUD state, sampled
actor position/fields, visible actor count, and active-shard count, then reports
the best overall, health-matched, actor-matched, timer-matched, and strict
matches. This is a read-only planning tool; use it to reject tempting checkpoint
shifts that align one precondition while breaking another.

Visual routes that compare an in-game viewport through different provider
presentation paths can set `compare_profile` to `logical-viewport`. The route
must provide `visual_logical_size` and `visual_logical_viewport`, for example:

```json
"compare_profile": "logical-viewport",
"visual_logical_size": [320, 240],
"visual_logical_viewport": [0, 10, 320, 220],
"visual_baseline_logical_frame": "active",
"visual_test_logical_frame": "full"
```

`visual_baseline_logical_frame` and `visual_test_logical_frame` choose whether
logical coordinates map through the full image or the detected non-black active
bbox for that provider. Dam uses stock `active` and native `full`: stock ares
screenshots include emulator VI/overscan presentation in their active bbox,
while native 640x480 captures map the original 320x240 VI directly and preserve
the 320x220 gameplay viewport as black top/bottom guard bands.

Native ROM-oracle captures run with `--savedir <out-dir>/native_savedir`, so
route-level `native_config` overrides are saved only in the local artifact bundle
and cannot persist into the repo-root/user `ge007.ini`. The wrapper audits the
generated native config after capture and fails if a route override is missing
or saved with a different value. Visual stock-parity routes should still pin
every display setting that changes composition or filtering, including
`Video.RetroFilter`, and native-only cosmetic settings that move rendered
geometry, including `Input.ViewmodelSway`.

Camera-basis parity work can use the top-level `render_cam_pos`,
`render_cam_target`, `render_cam_delta`, `cam_up`, and `view_basis` fields.
Native and stock both emit the camera position actually intended for render
comparison, the derived render target, and a compact basis summary with
`vv_verta`, `vv_verta360`, `vv_cosverta`, `vv_sinverta`, `headlook`, and
`headup`. Native `cam_up` is emitted with enough precision to diagnose
movement-coupled roll. Use these fields to separate a visual-route camera setup
mismatch from room material, projection, or blender defects. In forced visual
checkpoints, verify both `cam_up` and `view_basis` before classifying a broad
screen shift as a renderer issue.

Native traces expose the rendered-room set under `rooms.vis.sample`; this list
is sorted by room id and is useful for set equality, not draw order. For room
order and translucent stacking checks, use native `rooms.vis.draw_sample`, which
is read from the same background draw array that stock oracle traces expose as
`rooms.vis.rendered_sample`.

Stock ROM-oracle traces also include `rooms.dl` summaries for the current
rendered-room sample. These are read-only scans of `g_BgRoomInfo` point,
primary, and secondary room buffers: pointers, sizes, command counts,
texture/combine/env-alpha counts, directly observed render-mode commands, raw
hashes, and combiner hashes. Use them to prove whether stock loaded the expected
secondary room material data. Do not treat them as complete RSP/RDP draw-state
captures; inherited othermode, fog, geometry mode, texture state, depth policy,
and translucent sort order may come from surrounding display-list or emulator
state instead of the scanned room buffer itself.

First-person weapon parity routes can use the stock `watch.hands.<right|left>.vm`
block. This is a read-only snapshot of the N64 player hand/model layout:
hand-root, world, muzzle, and depth fields, embedded model header/render_pos
pointers, render_pos slot 0 decoded from N64 `Mtx` format, sampled render-pos
matrix translations under `model.mtx`, and a `pose` block. The sampled matrix
block includes the active logical viewport/projection plus `screen` and
`screen50` projections for each sampled view-space anchor. Native traces expose
matching `wr_pose` / `wl_pose` fields and `wr_mtx` / `wl_mtx` sampled matrix
blocks. Use `tools/compare_viewmodel_projection.py --stock stock.jsonl --native
native.jsonl --hand right` to decide whether stock/native foreground differences
begin in game-authored viewmodel state, projection/screen mapping, or later
weapon material/lighting/rendering.

Stock and native route traces also emit a compact top-level `actors` summary.
It is read-only and bounded: `slots`, `live`, `alive`, `hidden`, `onscreen`, and
`rendered` counts plus the nearest six live chr props by distance to the player.
Each sample records slot, chrnum, action, alert, sleep, hidden bits, alive,
onscreen flag, rendered-room membership, first room, distance, and position.
The stock oracle reads US `g_ChrSlots`/`g_NumChrSlots` by default
(`0x8002CC64`/`0x8002CC68`) and can be overridden with
`MGB64_ARES_CHR_SLOTS` / `MGB64_ARES_NUM_CHR_SLOTS`; if those addresses are
invalid it emits zero counts rather than scanning arbitrary memory. Use this
field to separate material/glass mismatches from actor simulation or visibility
divergence at a visual checkpoint.

For one-actor root-cause work, set `GE007_TRACE_CHRNUM=N` on native or
`MGB64_ARES_TRACE_CHRNUM=N` on the stock oracle. The trace then emits a
top-level `track` object with slot, hidden/action flags, damage, prop
position/rooms/render flags, `firecount`, `shotbondsum`, `accuracyrating`, AI
list/offset/command, and patrol waydata
(`mode`, `age`, `lastvisible`, `nextstep`, `forward`, `speed`,
`segdistdone`, and `segdisttotal`). This is the preferred probe for
background-AI/offscreen-patrol parity, especially `WAYMODE_MAGIC` movement
where stock can advance segment progress without moving the render prop.
When the branch decision itself matters, add `GE007_TRACE_MAGIC_TRAVEL=1`
with the same `GE007_TRACE_CHRNUM=N` filter. Native then logs each patrol/gopos
magic-travel decision to stderr with `PROPFLAG_ONSCREEN`, the rendered-path
`chrlvStanRoomRelated*` result, hidden bits, chr flags, and whether the prop's
registered rooms are currently rendered. Use `GE007_TRACE_MAGIC_TRAVEL_BUDGET=N`
to raise or lower the default line budget.

When a guard/object collision needs ownership proof, set
`GE007_TRACE_GUARD_OBJECT_SHOTS=1`. Native logs bounded
`[GUARD_OBJECT_SHOT]` rows from the guard weapon path before object damage is
applied, including frame/global timer, chr/action/hidden flags, held item,
firecount, target prop/object/pad, damage amount, shot origin/direction, hit
point, and player position. Filter with
`GE007_TRACE_GUARD_OBJECT_SHOTS_CHRNUM=N` and
`GE007_TRACE_GUARD_OBJECT_SHOTS_PAD=N`, and cap output with
`GE007_TRACE_GUARD_OBJECT_SHOTS_BUDGET=N`. This is the preferred native-side
probe when separating player-fired glass hits from offscreen guard fire, such as
Dam pane pad `10004` receiving stock-compatible AK47 (`item=8`) hits from chr
`11`.

When guard-to-Bond damage cadence needs source proof, set
`GE007_TRACE_GUARD_BOND_SHOTS=1`. Native logs bounded
`[GUARD_BOND_SHOT]` rows from `chrlvUpdateShotbondsum`, including frame/global
timer, `g_ClockTimer`, `g_GlobalTimerDelta`, chr/action/hidden flags, item,
firecount, aim-cone result, damage-show gate, `shotbondsum`
before/add/after, damage amount, Bond health/armor before/after, AI modifiers,
guard/player positions, and whether `bondviewCallRecordDamageKills` was called.
Filter with `GE007_TRACE_GUARD_BOND_SHOTS_CHRNUM=N` and cap output with
`GE007_TRACE_GUARD_BOND_SHOTS_BUDGET=N`. Use this before classifying combat HUD
or health differences in stock-backed visual routes as renderer defects.

Stock and native route traces emit a top-level `glass` shard-state summary.
This is read-only and bounded to the window-piece ring buffer: buffer length,
next shard index, active shard count, first active shard sample, a `sample`
array for the first four active pieces, and an FNV-style hash over active shard
records. Each sample records piece index/state, position, three-axis rotation,
velocity, angular velocity, and the first triangle's local vertices. Stock US
addresses default to
`SHATTERED_WINDOW_PIECES_BUFFER_LEN=0x8007A160`,
`ptr_shattered_window_pieces=0x8007A164`, and `g_NextShardNum=0x80040940`;
override them with `MGB64_ARES_SHATTERED_WINDOW_LEN`,
`MGB64_ARES_SHATTERED_WINDOW_PTR`, and `MGB64_ARES_NEXT_SHARD_NUM` if a symbol
layout changes. Route traces also emit top-level `rng` state. Native records the
current `g_randomSeed` plus `randomGetNext()` call count; the stock oracle reads
US `g_randomSeed` from `0x80024460`, overrideable with
`MGB64_ARES_RANDOM_SEED`. Use
`tools/compare_glass_trace.py --require-active stock.jsonl native.jsonl` for
deterministic regular-glass shatter parity. Add `--require-hash-match` only
after the route is proven to use identical stock/native firing timing and random
state; the current hash is a raw active-buffer diagnostic and is not a substitute
for semantic shard samples across stock/native memory layouts. The richer
`glass.sample` field is meant to prove or disprove that precondition before a
screenshot delta is treated as a renderer bug. Use `--require-sample-match` or
`--first-sample-tolerance N` when the first active sampled shards must match.
Route JSON exposes the tolerance as `compare_first_sample_tolerance`. The first
active frame delta is always reported, but it is only a failure when
`--first-active-tolerance N` is passed; stock routes usually include
frontend/menu frames, so route-level shatter-state checks should start with
`compare_require_active` plus `compare_max_active_tolerance` before gating exact
timing. Use `--first-position-tolerance N` when the route should also prove the
same pane/location shattered; route JSON exposes this as
`compare_first_position_tolerance`. When both sides include `rng`, the comparator
reports the pre-active seed, first-active seed, and inferred RNG draw count across
the transition.

The top-level `glass.first` object keeps the older `age` and `rot` keys for
compatibility, but new work should use the explicit names: `timer` is the shard
lifetime counter (`piece`), `rot_y` is the Y rotation component formerly printed
as `age`, and `rot_z` is the Z rotation component formerly printed as `rot`.

Stock `stock_events` normally use `phase: "gameplay"`, where `start`/`len` are
relative to the ares oracle's gameplay-frame counter. For route probes that must
schedule a button against the ROM mission timer despite frontend/menu jitter,
use `phase: "global"`; the emitted ares input event compares `start`/`len`
against `g_GlobalTimer` after the target stage and player are live. Keep this
for oracle-route control only, and still require active glass/actor/health
evidence before treating a visual route as renderer evidence.

Stock and native traces also emit top-level `impact_state` for bullet-hole and
glass-crack placement checks. This read-only summary walks the bullet-impact
ring buffer, records occupied count, current write slot, first occupied impact,
up to four sampled impacts, room-local vertex positions, world-space positions
when the impact is attached to a room, texture coordinates, vertex colors, prop
attachment identity (`prop_type`, `prop_chrnum`, `prop_obj_type`, `prop_obj`,
and `prop_pad`), and a hash over semantic impact fields. Use those identity
fields before blaming materials: a local prop crack on `PROPDEF_GLASS` is not
the same evidence as a world-glass shatter impact. The stock oracle defaults to US
`g_BulletImpactBuffer=0x8007A154`, `g_NumImpactEntries=0x80040808`,
`ptr_bgdata_room_fileposition_list=0x8007FF8C`, and
`room_data_float2=0x800413F8`; override them with
`MGB64_ARES_BULLET_IMPACT_BUFFER`, `MGB64_ARES_NUM_IMPACT_ENTRIES`,
`MGB64_ARES_BG_ROOM_FILEPOSITION_LIST` / `MGB64_ARES_BG_ROOM_POS_LIST`, and
`MGB64_ARES_ROOM_DATA_FLOAT2` if a symbol layout changes. The comparator uses
the first world-converted background impact when both traces expose one, so prop
impact slots do not mask glass/background placement. Use
`compare_glass_trace.py --require-impact-active --require-impact-match
--impact-position-tolerance N` only after the route's firing timing is stable
enough that selected impact state is expected to match. Route JSON exposes those
as `compare_require_impact_active`, `compare_require_impact_match`, and
`compare_impact_position_tolerance`. By default the position check covers the
impact center plus all four impact-quad vertices. Routes that only need to prove
shot origin before a screenshot comparison can set
`compare_impact_position_points` or pass `--impact-position-points center` to
gate the center while leaving decal size/orientation differences for the pixel
report.

Route-only RNG controls are available for glass and other deterministic oracle
work. Native frame-level events use `GE007_AUTO_RNG_SEED_SCRIPT=FRAME:SEED`
with comma/space/semicolon-separated entries, or the single-event pair
`GE007_AUTO_RNG_SEED_FRAME` and `GE007_AUTO_RNG_SEED`. These write the exact
64-bit `g_randomSeed` value; they do not call `randomSetSeed()` and therefore do
not add one. Native call-level events use
`GE007_AUTO_RNG_CALL_SEED_SCRIPT=CALL:SEED`, or
`GE007_AUTO_RNG_CALL_SEED_CALL` plus `GE007_AUTO_RNG_CALL_SEED`, to seed
immediately before a selected `randomGetNext()` call. `GE007_TRACE_RNG_CALLS=1`
logs native RNG callers during ramrom replay; add
`GE007_TRACE_RNG_CALLS_FORCE=1` for non-ramrom auto routes,
`GE007_TRACE_RNG_CALLS_AFTER=N` to start at a call count, and
`GE007_TRACE_RNG_CALL_BUDGET=N` to cap lines. Stock ares route-level seed events
use `MGB64_ARES_RNG_SEED_SCRIPT=GAMEPLAY_FRAME:SEED`, or the single-event pair
`MGB64_ARES_RNG_SEED_FRAME` and `MGB64_ARES_RNG_SEED`; stock traces report
`oracle.rng_seed.events`, `applies`, and `last_seed`.

The stock-side analogue of `GE007_TRACE_RNG_CALLS` is
`MGB64_ARES_RNG_PC_TRACE=PATH` (FID-0063): the instrumented ares build arms a
CPU-dispatch PC hook at the retail `randomGetNext` entry (VRAM `0x7000A450`,
the `GLOBAL_ASM` block in `src/random.c`; override with
`MGB64_ARES_RNG_PC_HOOK=ADDR` for non-US layouts) and writes one JSONL line per
call: call index `i`, guest `$ra` (caller PC = `ra - 8` for a `jal`),
`g_randomSeed` BEFORE the call's step, `g_GlobalTimer`, and the VI frame.
Validation is NOT a per-line "exactly one PRNG step apart" invariant: the
RDRAM-sampled seed lags the guest CPU's dcache writeback by a few draws and
catches up in bursts, so most consecutive lines sample the SAME seed (0 steps
apart) and a single-pair gap can span into the hundreds. The log is instead
self-validating as a whole, against the LOCK chain established by a seed
write (scripted lock or `randomSetSeed`): every sampled seed after the write
must lie on that chain in monotone order, the lag (line offset minus chain
position) must be negative NOWHERE, and the lag must return to exactly 0 at
every frame-boundary writeback catch-up point — a missed or duplicated call
would force a negative lag at one of those catch-up points. See derivation
`docs/fidelity/derivations/FID-0063-rng-phase.md` §9.1 for the measured lag
distribution this is based on. Default off; the hook only reads guest state
and never adds RNG calls. Entry-PC counting assumes the hooked address is
never block-interior code (true for `randomGetNext`, empirically confirmed by
this same chain/lag analysis); re-run the chain/lag validation whenever
`MGB64_ARES_RNG_PC_HOOK` retargets the hook to a different address, since
nothing else checks that assumption for an arbitrary target.

`tools/rom_oracle_routes/dam_regular_glass_shatter_rng_isolation_probe.json`
uses the call-level native seed control as an explicit renderer-isolation route:
it keeps the Dam pad-103 same-pose stock/native shot from the visual probe,
disables route-only native autoaim before the shot, then requires exact first
active `glass.sample` parity via
`compare_first_sample_tolerance=0`. Treat this route as proof that later
presentation deltas are renderer/HUD/material candidates under controlled shard
state. It pins `stock_speedframes=2` because the shot window is scheduled on the
stock gameplay timeline; the ares oracle uses the configured
`stock_gameplay_start_global` as the fixed origin for frame numbering. This keeps
route frame math reproducible, but stock VI/global-timer parity can still change
the exact sampled global tick for visual routes. It is not proof that unseeded
stock/native gameplay RNG timing is aligned.

`tools/rom_oracle_routes/dam_regular_glass_shatter_rng_visual_probe.json` is the
visual companion to that state route. It keeps the same call-level native RNG
seed, pins the same force-player view on stock and native, and requires exact
first active `glass.sample` parity before writing screenshot metrics. Use it
when you need active-shard renderer-isolation evidence; keep using the unseeded
`dam_regular_glass_shatter_visual_probe` when investigating the real route's
health, actor, impact, and cadence drift.

The same traces also emit `glass_props`, a bounded read-only summary of authored
regular/tinted glass props in the current setup table. It reports aggregate
counts plus the nearest, first removed, and first destroyed pane records using
stable object fields (setup index, object id, pad, state, remove flag, damage,
maxdamage, and runtime position), not host pointers. New traces also include a
`sample` array of up to 32 glass/tinted-glass setup records plus
`sample_truncated`; Dam currently fits all 25 panes in that sample. Use it to
choose stock/native visual candidates by pad, setup index, position, and guard
proximity instead of guessing from screenshots. Use
`--require-prop-destroyed` to require a pane to reach both destroyed and
remove-pending state, and `--prop-position-tolerance N` to prove stock/native
broke the same authored pane object. Route JSON exposes these as
`compare_require_prop_destroyed` and `compare_prop_position_tolerance`.

Stock and native route traces also emit top-level `glass_projection` records for
active shard coverage. Native computes these from the actual shard render inputs:
the current player projection (`field_10E0`), the emitted per-piece model matrix,
the `piece+0x38` shard vertices, and the active player viewport. The stock ares
hook computes the same compact summary from RDRAM using the current player
projection, shattered-window buffer, current model position, and viewport fields.
Use `tools/compare_glass_projection_trace.py` to compare counts and screen-space
coverage without relying on screenshots:

```sh
python3 tools/compare_glass_projection_trace.py \
  --baseline-label stock \
  --test-label native \
  --baseline-frame 2541 \
  --test-frame 124 \
  --require-present \
  --max-active-delta 0 \
  --max-projected-delta 0 \
  --max-behind-delta 0 \
  --max-test-max-area-pct 1.0 \
  --max-test-union-area-pct 20.0 \
  --json-out /tmp/glass_projection_compare.json \
  stock_trace.jsonl native_trace.jsonl
```

Current pad-`10092` proof:
`/tmp/mgb64_dam_visual_regression_suite_current/glass_actor_masked/pad10092_actor_masked/projection_dam_regular_glass_shatter_pad10092_actor_masked_visual_probe.json`.
It shows the default native path is bounded and stock-close under
`scale=inv_vis_full`: `88` active/projected/onscreen, `0` behind, max area
`0.206% -> 0.244%`, and union `6.860% -> 6.119%`.

For the current pad-`103` projection-parity proof, use
`/tmp/mgb64_invvis_active_gate2/active_rng_visual/projection_dam_regular_glass_shatter_rng_visual_probe.json`.
It matches active/projected/onscreen/behind counts exactly (`90/90/82/0`) with
max area `5.421% -> 5.392%` and union `65.245% -> 65.003%`. The per-piece
diagnostic tool `tools/compare_glass_projection_pieces.py` can compare all active
shards when native uses `GE007_TRACE_GLASS_PROJECTION_ALL=1` and the local ares
oracle uses `MGB64_ARES_TRACE_GLASS_PROJECTION_ALL=1`; the proof artifact
`/tmp/mgb64_glass_projection_invvis_selected_1782580888/native_probe/projection_pieces_selected_vs_stock.json`
compares all `90` pieces with screen-center mean error `0.44` pixels and median
clip-w ratio `1.0006`.

For stock-backed glass presentation evidence, use the visual route
`tools/rom_oracle_routes/dam_regular_glass_shatter_visual_probe.json`. It keeps
the same deterministic Dam pad-103 regular-glass shot, pins stock and native to
the same +Z force-player view (`MGB64_ARES_FORCE_PLAYER_SCRIPT` on stock and
`GE007_AUTO_FORCE_PLAYER_SCRIPT` on native), captures a stock frame while the
shard buffer is still active, and compares the original logical VI gameplay
viewport with named
`glass_burst`, `damage_arc`, and `hud_viewmodel` ROIs. The route also excludes
`damage_arc` and `hud_viewmodel` from the masked aggregate so the report keeps a
glass/material signal separate from HUD/viewmodel noise. It shares the same
`stock_speedframes=2` timing pin as the state routes, fires stock from the
mission/global timer, can repeat a stock gameplay-frame RNG seed with
`MGB64_ARES_RNG_SEED_REPEAT=1` so duplicate VI samples for the target gameplay
frame stay pinned, and can force stock `crosshair_angle`/`field_FFC` with
`MGB64_ARES_CROSSHAIR_SCRIPT=START:LEN:X:Y` or `START-END:X:Y`. Stock traces
expose the sampled values as `combat.crosshair` / `combat.screen` and the hook
metadata under `oracle.crosshair_force`. Native route captures can mirror this
with deterministic `GE007_AUTO_CROSSHAIR_SCRIPT=START:LEN:X:Y` or
`START-END:X:Y`, which writes native `crosshair_angle` and `field_FFC` before
bullet rays are generated. The checked-in Dam visual route uses this native
crosshair hook plus an impact-center pre-pixel guard, so route-origin misses
fail before screenshot artifacts are written. The wrapper now writes
`combat_health_compare_<route>.json` for this route; read it before interpreting
the `damage_arc` ROI, because stock/native damage timing can still differ even
when glass state and actor guards pass. That report includes checkpoint
`move.clock` / `move.dt` plus frame and global-timer deltas from first active
glass to first Bond health drop, which keeps route-speedframe cadence evidence
separate from renderer/material evidence. As of
`/tmp/mgb64_dam_visual_center_guard_1782495600`, the route passes native render
health, stock trace control, the field-only actor/glass guards, and the
impact-center guard (`impact=7`, room `107`, world center delta `3.707`), but
stock/native shard age, damage-HUD phase, and chr `12` foreground position still
differ. The checked-in route now also uses
`compare_require_health_match=true` plus
`compare_actor_position_tolerance=25.0` for chr `12`; captures that reproduce the
current health-phase or foreground-position mismatch fail before screenshot
artifacts are treated as clean pixel evidence.
`tools/score_visual_checkpoints.py` on
`/tmp/mgb64_dam_visual_prepixel_healthfail_1782502757` finds no strict active
checkpoint pair for this route: the best active pair matches health and first
shard timer but has chr `12` action `15 -> 8`, actor delta `27.252`, and visible
actor count `2 -> 3`.
Treat this as an evidence route rather than a pass/fail renderer gate: stale
route controls have missed the pane on a stock target-stage origin that first
appears at global `1147`, and the current pre-pixel guard is what prevents those
dirty captures from writing visual metrics. Routes that know their clean stock
origin can set `stock_require_first_gameplay_global`; the stock trace audit then
rejects the dirty origin before glass or pixel comparison. When capturing from
ares, the wrapper retries this route-control precondition up to
`MGB64_ARES_STOCK_ROUTE_CONTROL_RETRIES` times (default `4`) and preserves failed
attempt traces with `.attemptN` suffixes in the local artifact directory.

For native shard material/debug evidence, pair that route with:

```sh
GE007_TRACE_TEXSELECT=1 \
GE007_EFFECT_TRI_TRACE=1 \
GE007_EFFECT_TRI_TRACE_LABEL=glass_shards \
GE007_EFFECT_TRI_TRACE_AFTER_FRAME=108 \
GE007_EFFECT_TRI_TRACE_BUDGET=800 \
tools/movement_oracle_capture.sh \
  --route dam_regular_glass_shatter_visual_probe \
  --native-only --no-compare --no-build \
  --out-dir /tmp/mgb64_dam_shard_material_trace
```

The effect-triangle trace is read-only. It logs per-triangle emit/reject state,
render mode, other-mode high, combiner id, geometry flags, NDC bbox/area, UVs,
raw source VTX RGBA, post-light shade RGBA, and matrix/projection context. Use
it to distinguish shard allocation/projection bugs from material, lighting,
texture-generation, and combiner parity bugs.

### Save persistence

```sh
./tools/save_persistence_check.sh --no-build
```

Runs deterministic processes against an isolated `--savedir`. The first process
seeds Dam/Agent completion into folder 0; the second process seeds a
Dam-through-Runway Secret Agent range into folder 1; the final process starts
without the seed helper and verifies the state trace reloads both folders from
`ge007_eeprom.bin`. Use `--out-dir DIR` to preserve traces, logs, screenshots,
the isolated save directory, and the final EEPROM. The state trace includes the
`save` block while still in the frontend/title path, before a live player exists.
The Dam mission-flow smoke below separately proves that the scripted mission
success path itself writes folder 0 Dam/Agent completion and that a fresh process
reload observes it from the generated EEPROM.

### Native playability regression suite

```sh
tools/native_playability_regression_suite.sh --no-build \
  --out-dir "$(mktemp -d /tmp/mgb64_native_playability_regression.XXXXXX)"
```

This is the long-running local guard orchestrator for broad native-port posture.
It can build once, run CTest, then run focused ROM-backed gates for playability,
structured campaign route contracts and input traversal, Dam progression,
Surface II final flow, combat/guard/knife behavior, renderer parity, MP
split-screen, save persistence, and minimap coverage. Use `--skip-ctest` after a
separate full CTest pass. Use `--skip-combat` only when iterating outside combat.
Use `--full` when you want the broader all-level variants where the sub-gates
expose them.

### Campaign route smoke

```sh
tools/campaign_route_smoke.sh --no-build \
  --out-dir "$(mktemp -d /tmp/mgb64_campaign_route_smoke.XXXXXX)"
```

`tools/campaign_route_smoke.sh` runs JSON route contracts from
`tools/campaign_routes/` through the native binary, then audits trace evidence.
Each route runs with an isolated clean save directory under its artifact
directory so stale EEPROM files cannot satisfy persistence assertions.
Each passing route now prints a compact evidence line and writes an `evidence`
object into its `summary.json`. The evidence block reports the highest
conservative capability tier (`T0` boot/trace through `T5` stage loop),
stock-input vs. direct-setup status, opened door objects, collect types,
nonzero keyflags, objective/status evidence, report frame, and reload frame. The
top-level summary also records tier counts and route-class counts so broad runs
show whether they advanced traversal, interaction, mission-state, or stage-loop
coverage.
Use `tools/route_target_reach.py` when scouting a route toward a specific setup
object without making that target a pass/fail assertion yet. It reads route
artifacts, matches setup rows with repeated `--target KEY=VALUE` filters, and
reports best horizontal, same-floor, vertical, and full-distance approach. Pass
`--setup-dump existing/stage_pads.jsonl` to analyze older scout artifacts that
did not request their own setup dump.
The default routes currently cover input traversal for Dam, Facility, Surface 1,
Surface II, Bunker 1, Frigate, Statue, Train, Control, Caverns, and Cradle,
plus scripted Dam objective criteria progression, Dam guard-pressure combat,
Dam player-fire combat, a Dam scripted mission composite that joins native
traversal with objective/report/save-reload proof, Dam mission-report return and Surface II
final exit with save/reload persistence, Bunker 1 datathief
equipment/debug-dump regression, Facility door open/reverse interaction,
Facility stock-spawn bathroom-door traversal, Facility stock-spawn two-door
chain, Surface 1 key pickup, Bunker 1 stock-spawn door/collect and
two-door/collect chains, Cradle stock-spawn body-armour pickup, plus
Statue/Control/Caverns stock-spawn door traversal contracts. Dam has
both the existing ROM-oracle-backed spawn movement route and a native-only
multi-waypoint controller route that pushes deeper into the later Dam pad
cluster. Surface 1, Surface II, Frigate, Statue, Train, and Cradle also have
native-only multi-waypoint controller routes: Surface 1 reaches later snowfield
pads `80`, `79`, and `77`; Surface II drives from the outdoor stock spawn
through setup pads `30`, `71`, `70`, `69`, `68`, and broad late pad `78` with
more than 10,500 horizontal units of reach; Frigate reaches the
stern-side/pad-150 exterior sequence and a far deck pad `145` after more than
2,800 horizontal units; Statue reaches
park/lower-cluster pads `222`, `214`, `207`, and `208`; Train reaches the
room-3 car cluster around boundpads `12` and `14`; and Cradle reaches pad `121`
plus a later authored pad cluster. Statue also has an `input_interaction` route
that backs from stock spawn to door object `183`/pad `6`, sweeps view with
controller look input, presses real `B`, and asserts normal `door allow`,
`0->1`, opening displacement, and finish-open trace evidence. Cradle also has an `input_interaction` route
that backs from stock spawn to body-armour object `115`/pad `124` and asserts
object type `21` collect/free trace evidence. Input traversal routes are labelled
`input_traversal`; they require active
controller-path input automation, forbid direct state automation, check position
milestones, and reject mission-failure/KIA frontend flags. The ROM-oracle Dam
route inherits native input windows, config, frame count, and deterministic
speedframes from `tools/rom_oracle_routes/dam_strafe_matrix.json`. The native
Dam multi-waypoint route and the other traversal routes are campaign-route specs
with declarative `input_segments` controller windows. Each segment has `start`,
`duration`, and `inputs` fields, and compiles to the existing native
`GE007_AUTO_*` input windows. Supported input names include movement directions,
`a`, `b`, `fire`, `aim`, `start`, C-buttons, D-pad, look, menu/frontend,
reload, and weapon-cycle inputs. The route loader validates segment keys, input
names, and duplicate raw env collisions so route authors do not have to
hand-maintain comma-separated automation strings for normal controller input.
Scripted contracts can also use `input_segments` for their real-button
portions, such as Facility door `B` presses, Dam guard-fire input, and Surface
II final-pad activation.

The input traversal routes also carry setup-backed waypoint assertions through
`setup_target_milestones`. These match real stage setup pad rows from a
per-route `GE007_DUMP_STAGE_PADS` artifact and require the controller-driven
player trace to pass within a conservative horizontal threshold. This ties the
fragile traversal lanes to authored setup geography instead of only checking
relative displacement. The waypoint assertions are still traversal evidence, not
objective-completion proof.

Scripted contracts use `scripted_events` for deterministic setup hooks instead
of raw `GE007_AUTO_*` strings where the route harness has semantic coverage.
Supported sections cover pad warps, chr-relative warps, face-coordinate events,
forced player pose, tag damage, stage flag set/unset, guard AI assignment,
item add/equip, debug dump, mission end, and title-return exit delay. The
compiler emits the existing native automation env and records it in each route
summary as `scripted_event_env`; the loader rejects collisions with manually
authored raw env for the same native key. This keeps the contracts declarative
while preserving the native hooks already used by the validation binary. The
deterministic mission/equipment/door/pickup/combat contracts are labelled
`scripted_contract`; they prove mission, interaction, inventory, combat, and
stability contracts and provide a reusable route assertion format, but they are
not yet full organic objective-completion navigation routes.
Stock-spawn interaction routes such as Facility, Bunker 1, Control, and Caverns
are labelled `input_interaction`; they use only controller input and normal
interact paths, but still prove focused interaction/progression slices rather
than full mission navigation.

The route auditor fails on missing deterministic exit markers, GEASSERT rows,
render/DL counters, bad-command/crash/nan counters, missing position,
objective, generic state, or setup-target milestones, missing required input
automation, unexpected direct state automation, missing required log patterns
or regex log count assertions, mission failure/KIA flags, missing title return,
or title return before objective completion when the route requires it. Generic
state milestones can assert nested trace paths such as
`combat.health.actual_h`, `watch.hands.active[1]`, or `wr_raw.weaponnum`.
Tier reporting is informational and never relaxes these pass/fail assertions.
`setup_target_milestones` match route-authored target fields against a
per-route `GE007_DUMP_STAGE_PADS` artifact and compare player position to the
matched setup row with horizontal, full-distance, or Y-delta limits. This keeps
scripted placement contracts and traversal waypoint checks tied to real stage
setup objects instead of hard-coded player coordinates. Route specs can also
assert `save_completion` in the mission trace and `reload_save_completion`,
which restarts the binary against the route save directory without route
automation and verifies the persisted `save` block from the generated EEPROM.

The Dam mission-report and Surface II final-exit contracts use the reusable
reload assertion. Dam requires the scripted success path to reach menu `12`, set
folder 0 Dam/Agent completion in the mission trace, restart from the same
savedir, and observe that persisted completion again on title/menu frames.
Surface II requires the final pad path to complete Agent-relevant objectives,
return to title/menu state, set folder 0 Surface II/Agent completion, restart
from the same savedir, and observe the persisted completion again.

The Dam native mission composite contract is a bridge between traversal and
mission-state proof. It starts from normal Dam spawn, runs the same
controller-driven multi-waypoint lane through setup pads `10`, `293`, `287`,
and `288`, then uses declarative scripted events to advance Dam's real objective
criteria, trigger a success report, write folder 0 Dam/Agent completion, and
verify that completion after a fresh process reload. This proves the movement,
objective vector, report path, and persistence systems can compose in one route,
but it still does not claim organic alarm/modem/data/bungee placement.

The Surface II native multi-waypoint traversal contract is input-only. It starts
from the normal Surface II spawn at setup pad `30`, drives `forward` and `left`,
passes near setup pads `71`, `70`, `69`, and `68`, and reaches the broad late
outdoor area near pad `78` after more than 10,500 horizontal units of reach.
It requires active controller-path input, no direct state automation, stable
front-end/render counters, and setup-backed waypoint proximity. This proves a
long native outdoor traversal lane, not objective setup, final silo activation,
mission report, or persistence from an organic completion.

The Facility door contract uses deterministic setup only to place Bond at door
object `158`, then presses real `B` input twice. It requires two interact
`door allow` traces, a `0->1` open transition, opening displacement, a `1->2`
reverse-to-closing transition, closing displacement, moving-door camera
clearance hits with non-zero `hit_door_open`, setup-derived proximity to door
object `158`/pad `77`, and post-sequence health/stage stability. This makes the
door regression a campaign route contract without claiming stock-like Facility
navigation.

The Facility stock-spawn bathroom-door traversal contract is input-only. It
starts from normal Facility spawn, drives `forward`+`left`, sweeps view with
`look_left`, presses real `B`, and requires a normal `door allow` for bathroom
door object `159`, a `0->1` open transition, at least 20 opening displacement
rows, setup-derived proximity to object `159` pads `67` and `68`, more than
1,200 horizontal units of later reach, and stable Facility frontend state. This
proves one organic spawn-to-door-to-later-area Facility lane without claiming
objective completion or the scripted object-`158` door regression path.

The Facility stock-spawn two-door chain contract extends that lane. It opens
object `159`, doubles back to secondary door object `155` near pad `75`, and
opens that door through another real `B` interaction. It requires setup-derived
proximity to object `159` pads `67`/`68` and object `155` pad `75`, one
`door allow` and `0->1` open transition for each door, object-`155`
displacement plus finish-open rows, and stable Facility frontend state after
the second door opens. This is stronger route-authored interaction coverage,
but it still does not claim Facility objectives, keycards, bottling-room flow,
alarms, or mission completion.

The Frigate native multi-waypoint traversal contract is input-only. It starts
from normal Frigate spawn near pad `176`, drives `back`+`right` into the
stern-side/lifeboat cluster at pads `153`, `152`, `151`, and `150`, then drives
`forward`+`right` across pads `149`, `144`, `60`, and `145`. It requires more
than 2,800 horizontal units of reach with stable Frigate frontend state. This
proves a much deeper deck traversal lane than the short pad-150 route without
claiming objective, hostage, interior-door, or ending progress.

The Surface 1 key pickup contract uses deterministic setup only to place Bond on
the tagged key's pad. The normal proximity pickup path then emits
`collect begin`/`collect success`, raises inventory count, sets keyflags
`0x00000001`, proves setup-derived proximity to key pad `17`, and remains
stable through frame 180. This proves pickup and key inventory behavior without
claiming a stock-like route from Surface spawn to the hut/key area.

The Bunker 1 stock-spawn door/collect contract is input-only. It starts from the
normal Bunker 1 spawn, drives `forward`+`right`, presses real `B`, and requires
the first corridor door object `140` to emit a normal `door allow`, a `0->1`
open transition, and opening displacement rows. The route then continues into
the next-room prop cluster near setup object `32`/pad `10052`, emits normal
collect `begin`/`free` traces for object type `20`, and remains on Bunker 1
without mission-failure/KIA frontend flags. This proves one organic
spawn-to-door-to-pickup interaction path without claiming objective completion.

The Bunker 1 stock-spawn two-door/collect contract extends that lane. It opens
object `140`, collects the same next-room object type `20`, turns toward second
door object `138` near setup pad `14`, and opens it through another real `B`
interaction. It requires setup-derived proximity to object `140`/pad `16`,
object `32`/pad `10052`, and object `138`/pad `14`, plus `door allow`, `0->1`
transition, displacement, and finish-open rows for both doors. This is stronger
stock-spawn interaction coverage, but it still does not claim Bunker 1 keycard,
objective, datathief pickup, mission ending, report, or persistence progress.

The Train native multi-waypoint traversal contract is input-only. It starts from
the normal Train spawn at pad `186`, drives `forward`+`left`, reaches the room-3
car cluster near boundpads `12` and `14`, and requires more than 790 horizontal
units of reach with stable Train frontend state. This proves a deeper
narrow-car traversal lane than the short spawn movement guard without claiming
objective, hostage, door, or ending progress.

The Statue stock-spawn door traversal contract is input-only. It starts from
normal Statue spawn, backs/right-strafes to spawn-side door object `183` near
setup pad `6`, uses look input to enter the door interaction angle, and presses
real `B` once. It requires a normal `door allow`, `0->1` door-open transition,
opening displacement rows, a finish-open row, setup-derived proximity to stock
spawn pad `21` and door object `183`/pad `6`, and stable Statue frontend state.
This proves one organic spawn-to-door interaction lane without claiming Valentin,
flight-recorder, objective, ending, report, or persistence progress.

The Control stock-spawn door traversal contract is input-only. It drives
`forward`+`right`, presses real `B` at console-area door object `144`, requires
a normal `door allow`, `0->1` door-open transition, opening displacement rows,
and setup-derived proximity to door object `144`/pad `163`. It then pushes
forward into the later console-area cluster near pad `49` and remains on
Control without mission-failure/KIA frontend flags. This proves one organic
spawn-to-door-to-later-area interaction lane without claiming objective
completion.

The Caverns stock-spawn door traversal contract is input-only. It drives to
door object `144`, presses real `B`, backs off to clear the opening, then
pushes forward into the next room cluster near pad `191`. It requires a normal
`door allow`, `0->1` door-open transition, opening displacement rows,
setup-derived proximity to door object `144`/pad `108`, and stable Caverns
frontend state through the route exit. This proves one organic
spawn-to-door-to-next-room interaction lane without claiming objective
completion.

The Dam guard-pressure contract uses deterministic setup only to place Bond near
a live guard and put that guard on the persistent attack AI list. The normal
guard-owned shot path then emits `GUARD_BOND_SHOT` accumulator rows, increments
the guard firecount, damages Bond, and leaves the stage/front-end state stable.
The final health floor accepts one or two normal `0.0625` damage applications
while still requiring Bond survival, active Dam stage, and no mission failure.
This proves route-level combat pressure without claiming stock-like Dam
navigation or stealth/combat decision making.

The Dam player-fire contract uses deterministic setup only to place Bond near a
live guard, then holds real fire input. The normal player-owned weapon path must
emit accepted guard-hit events, advance weapon shot counters, apply lethal guard
damage, and remain stable through the route exit. This proves route-level
offensive combat without claiming stock-like Dam approach or aiming decisions.

### Minimap smoke

```sh
tools/minimap_smoke.sh --no-build \
  --out-dir "$(mktemp -d /tmp/mgb64_minimap_smoke.XXXXXX)"
```

The minimap smoke direct-boots all 20 solo stages by default. For each enabled
capture it audits screenshot health, render health, `GE007_MINIMAP_DUMP`, setup
pad/objective dumps, and `GE007_MINIMAP_OVERLAY_DUMP`. The minimap auditor
derives expected objective pins from stage setup criteria instead of hard-coding
per-level pins. The disabled pass repeats selected stages with
`Input.MinimapEnabled=0`, requires no queued snapshots and overlay `no_queue`,
and compares the disabled cache against the enabled reference for geometry
parity.

### Deterministic regression (pixel / state / audio)

```sh
# First, capture baselines from a known-good build:
./tools/regression_test.sh --baseline

# Then, before a change, compare against your baselines:
./tools/regression_test.sh

# Bootstrap / trace-only runs with no baselines yet:
./tools/regression_test.sh --allow-missing-baselines
```

Baselines are **not** shipped — they track a specific build's renderer output,
so they must be regenerated locally with `--baseline`. Without baselines the
default mode fails by design; use `--allow-missing-baselines` to skip
comparisons during bootstrap. The pixel lane needs Pillow; the state and audio
lanes are standard-library only.

FID-0135 makes the stock-reference presentation contract explicit. Regression
captures pin an exact `640x480` window, windowed mode, and `Video.HiDPI=0`, then
set `GE007_FIDELITY_CAPTURE=1`. In that mode `platformSaveScreenshot()` refuses
any non-4:3 readback source before GPU readback; `--screenshot-exit` reports exit 4 and
no BMP is written. Ordinary player/debug screenshots keep the prior behavior:
a widescreen source is aspect-preserving-resampled into the fixed 640x480 BMP
with a warning. The permanent three-case guard is:

```sh
tools/fidelity_capture_aspect_smoke.sh --no-build
```

It proves legacy 16:9 capture still succeeds, strict 16:9 capture fails closed,
and strict 4:3 capture succeeds on the default renderer (WebGPU in production).

### Renderer parity scenes

For known renderer compatibility defaults, capture compact local A/B scenes:

```sh
./tools/renderer_parity_capture.sh --no-build
```

The script writes screenshots, traces, comparison logs, screenshot-comparison
JSON, capture logs, per-capture JSON health files, and a top-level `summary.json` to
`/tmp/mgb64_renderer_parity_*` by default. Those artifacts are generated from
your ROM and must not be committed or redistributed. Each capture runs
`tools/audit_screenshot_health.py` and `tools/audit_render_trace.py` so
missing/blank screenshots, crash recoveries, unhandled GBI commands,
display-list resolve failures, non-finite trace values, and unexpected
room-render fallback records fail immediately. Scene comparisons are still
observational because they intentionally compare compatibility defaults against
diagnostic alternatives.

Current scenes:

| Scene | Default capture | Diagnostic capture | What to inspect |
| --- | --- | --- | --- |
| `facility_scissor` | Facility at frame 180 | `GE007_EXACT_ROOM_SCISSOR=1` | Interior seams and under-covered room edges when exact N64 scissor boxes are enabled. |
| `surface_sky_fog` | Surface 1 at frame 180 | `GE007_PORTAL_BFS=0` | Outdoor sky/fog state and room-admission side effects when falling back from portal BFS. |

Run one scene with:

```sh
./tools/renderer_parity_capture.sh --scene facility_scissor --no-build
./tools/renderer_parity_capture.sh --scene surface_sky_fog --no-build
```

For the promoted Surface fog/tree coverage-memory path, use the focused
runtime guard instead of overloading the broad parity scene:

```sh
./tools/surface_xlu_cvg_memory_regression.sh --no-build
./tools/surface_xlu_cvg_memory_regression.sh --no-build --msaa-values "0 4"
./tools/jungle_xlu_cvg_memory_regression.sh --no-build
```

It captures Surface 1 default versus `GE007_DISABLE_ROOM_XLU_CVG_MEMORY=1`,
checks screenshot health and render health, requires identical gameplay state,
and verifies that the default run promotes the fogged secondary-room XLU rows to
`alpha_rdp_cvg_memory` while the disabled run exposes them as unpromoted RDP
coverage-wrap candidates. The optional validation CTest runs the faster
`--msaa-values 0` lane. The Jungle wrapper uses the same harness for the
generated/locally transformed fogged room strips that lose display-list room
attribution; it allows the small env-alpha candidate class to remain unpromoted
by default.

The default coverage-memory backend snapshots only each promoted triangle's
screen-space bbox before drawing that triangle. Use
`GE007_DISABLE_RDP_CVG_SNAPSHOT_RECTS=1` (or
`GE007_RDP_CVG_SNAPSHOT_RECTS=0`) to restore the old full-viewport per-triangle
snapshot for A/B comparisons. Current proof:
`/tmp/mgb64_surface_rectsnap_ab_640` produced `0/307200` changed pixels for
rectangle snapshots versus full snapshots while keeping `873` promoted rows,
and `/tmp/mgb64_surface2_perf_rectsnap_final.LNzHKP` kept Surface 2 at
`2056x1257` to `avg_work=9.81 ms`, `max_work=15 ms`.

### Level-intro census

To map native authored-intro coverage across direct-boot stages, run:

```sh
./tools/intro_census_capture.sh --no-build
./tools/intro_census_capture.sh --all --no-build
```

The default quick run captures Dam and Cradle; `--all` captures the 20 supported
solo stages. Direct native `--level` boots default to the immediate first-person
handoff for playability/dev runs; set `GE007_ENABLE_LEVEL_INTRO=1` to exercise
authored level intros, or `GE007_DISABLE_LEVEL_INTRO=1` to force the skip in an
explicit fixture. Each intro-census trace is captured with
`GE007_ENABLE_LEVEL_INTRO=1`; its
screenshot is audited with `tools/audit_screenshot_health.py`; its trace is
audited with `tools/audit_render_trace.py`; then it is summarized by
`tools/summarize_intro_census.py`. The summary reports active intro camera
records, decoded swirl setup hashes, selected-camera fingerprints, Bond
render/animation counts, animation header hashes, held right-item counts, and
render-health maxima. The output directory defaults to
`/tmp/mgb64_intro_census_*` and includes both `summary.txt` and structured
`summary.json`, per-level screenshot/render audit JSON, a `captures.tsv` index,
and `capture_summary.json` for the capture-health layer; screenshots, JSONL
traces, logs, and summaries are ROM-derived local artifacts and must not be
committed.

Current native coverage proof: `/tmp/mgb64_intro_census_all_postfix_1782731865`
passed all 20 supported solo stages after hardening native portal projection for
invalid Aztec global-visibility portal references. The fixed Aztec path produces
1,199 records, active intro/swirl/Bond render coverage, zero render crashes,
zero NaNs, zero room-fallback records, and zero display-list resolver failures.

For per-trace fingerprints that can be compared against a stock-ROM oracle trace,
use:

```sh
./tools/intro_trace_summary.py /tmp/mgb64_intro_census_*/level_33.jsonl

./tools/summarize_intro_census.py \
  --require-active-intro \
  --require-swirl \
  --require-selected-camera \
  --require-bond-rendered \
  --require-bond-anim \
  --require-bond-anim-hash \
  --json-out /tmp/intro_census_summary.json \
  /tmp/mgb64_intro_census_*/level_*.jsonl

./tools/intro_trace_summary.py \
  --baseline /tmp/stock_dam_intro_swirl_bond_anim.jsonl \
  --test /tmp/native_dam_intro_swirl_bond_anim.jsonl \
  --require-frozen \
  --camera-modes swirl \
  --start-intro-timer 11 \
  --end-intro-timer 97 \
  --compare-profile setup,selected-camera,bond-anim \
  --min-matched-timers 20
```

`intro_trace_summary.py` emits stable setup, selected-camera, camera-path,
Bond-animation, and full-active digests. Bond-animation digests include the
animation header hash and frame count in addition to frame progression. In
compare mode it aligns records by `intro.timer` and checks the selected fields
with explicit tolerances. Use this as the lightweight inventory/diff layer while
adding new stock-backed intro routes; keep `tools/compare_intro_trace.py` for
strict vector/path parity on a specific aligned route window.

`tools/audit_oracle_trace.py --json-out PATH` writes the same movement/control
metrics that it prints, including failure counts. Validation wrappers use this
for compact evidence files instead of scraping human-readable audit output.

### Intro visual regression (pixel-level Bond body)

The census/oracle layers above check intro *actor state* (present, rendered,
animation hash). A shredded or floating Bond still passes those. The pixel
validator adds the three checks actor state cannot see:

```sh
./tools/intro_visual_regression.sh --no-build
```

It captures the Dam intro swirl at a deterministic screenshot frame under several
env configs and asserts each one's expected verdict from
`tools/analyze_intro_body.py`, which runs three checks over the screenshot plus
the `intro.bond_body` trace record:

1. **presence** — Bond's warm skin/tan silhouette covers the expected screen
   region (the de-aliased body is visible, not absent/collapsed).
2. **grounding** — the median `world_root.y - floor_y` from `intro.bond_body`
   stays within a bounded offset (projection-independent); fails on a large
   persistent vertical offset (floating Bond).
3. **shards** — no dark saturated-red outlier pixels (the degenerate red-shard
   signature) anywhere in the rendered frame.

The regression bakes in the negative controls (charter rule 3): `normal`,
`GE007_NO_INTRO_PHASE3=1`, and `GE007_NO_INTRO_ROOTMOTION=1` PASS;
`GE007_NO_BOND_BODY_FIX=1` FAILs presence (and the trace render-position count);
`GE007_INTRO_BODY_Y_OFFSET=300` FAILs grounding; and a self-test that injects the
dark-red shard signature into the good frame FAILs shards. Defaults are a Dam
route fixture — the port's frozen-intro camera does not project actor world
positions to screen reliably, so the Bond region is empirically measured, not
engine-projected. `audit_intro_trace.py` also gained optional
`--min-body-render-pos-count` / `--max-grounding-offset` gates over the same
`intro.bond_body` record for trace-only (screenshot-free) coverage. Screenshots,
traces, logs, and saves are ROM-derived local artifacts — do not commit them.

### Texgen (environment mapping) validation

The RSP texgen contract lives in `docs/RENDERING_ARCHITECTURE.md` §7; this is how to
re-derive it. The formula is shared by every env-mapped surface in the game, so a
divergence here is whole-game (FID-0140).

**Cheap guard (always run this first — ROM-free, sub-second):**

```sh
ctest --test-dir build -R texgen_coords --output-on-failure
```

**A/B isolation.** `GE007_TEXGEN_LEGACY=1` restores the pre-fix mapping (2× scale +
the spurious S mirror), verified byte-identical to pre-fix output:

```sh
env SDL_AUDIODRIVER=dummy GE007_MUTE=1 GE007_DETERMINISTIC_STABLE_COUNT=1 \
    GE007_TEXGEN_LEGACY=1 ./build/ge007 --rom baserom.u.z64 --deterministic \
    --screenshot-frame 1890 --screenshot-label legacy --screenshot-exit
```

Boot-sequence texgen scenes at the faithful config-override set used by
`tools/startup_visual_parity_capture.sh` (frames are for
`GE007_DETERMINISTIC_STABLE_COUNT=1`): **Nintendo logo** ~600–720, **Rare logo**
~905–985, **GoldenEye logo** ~1890. Matching ares oracle frames:
`MGB64_ARES_SCREENSHOT_FRAME=` 700/720, 860/880, 1950 respectively.

**Coordinate census (the decisive check).** Rather than eyeballing colour, measure
where the texgen coordinates land relative to the bound texture span
(`tile_width * 32`). Correct behaviour is 0% outside on both axes, with the median
near half a span; the defect showed 42–49% outside. Note the `[TEXGEN-MATERIAL]`
trace's `uvn` field is **not** usable for this — it is the post-tile-transform
sampling coordinate and can reflect a different tile than the draw used. Instrument
`U`/`V` directly in `gfx_sp_vertex` (or extend `gfx_texgen_coords`) instead.

**Env-map inspection.** `GE007_DUMP_LOADED_TEXTURES='*'` plus
`GE007_DUMP_LOADED_TEXTURE_DIR` writes each decoded texture as PPM. Worth knowing:
the GoldenEye logo's 32×32 map is **column-constant** (a pure vertical
red→yellow→white ramp), which is why an S-axis error is invisible on that surface
specifically — always confirm S-axis behaviour on a column-varying map.

**Comparing against ares.** Two confounds bite here. (1) The native capture is
globally brighter than the ares capture (cf. FID-0138), so compare
brightness-normalised hue/saturation or medians, never raw mean RGB. (2) The logos
are *spinning*, so pose-match before comparing colour — and note that a
luma-threshold IoU is itself confounded by brightness (a darker variant scores a
lower IoU for free). Either match on a low threshold that captures the silhouette,
or go pose-invariant by pooling percentiles across the whole rotation.

### Performance census & budgets

The performance regression harness (`docs/design/PERFORMANCE_PLAN.md`) measures per-level
frame time deterministically and enforces budgets.

```sh
# Frame-time census of all 20 levels (default vs XLU-copy-disabled A/B):
tools/perf_census.sh                       # -> baselines/perf_census_latest.csv
tools/perf_census.sh jungle cradle         # a subset

# Enforce budgets (hard floor 60fps / 16.6ms, target 120fps / 8.3ms):
python3 tools/perf_budget_check.py baselines/perf_census_latest.csv \
    --baseline baselines/perf_census_baseline.csv

# Both in one CTest-compatible gate (opt-in lane port_perf_budget_smoke):
tools/perf_budget_smoke.sh --no-build --levels "jungle dam surface2 cradle"
```

Each census run boots the port headless + deterministic and averages the
`GE007_PERF_TRACE` `work_ms`. Numbers are reference-hardware sensitive (GPU/driver
bound), so treat `baselines/perf_census_baseline.csv` as machine-relative and
re-baseline on a new machine. The `speedup` column (`default_ms / xluoff_ms`)
isolates the per-triangle framebuffer-copy defect described in the plan.

## Reading the artifacts

`regression_test.sh` captures per level into a temp dir (kept on failure or with
`--keep-artifacts`):

- `screenshot_<lvl>.bmp` — frame capture. `audit_screenshot_health.py` checks
  basic validity; `compare_screenshots.py` reports changed-pixel %, unique
  colors, and a sample grid; default fail threshold is 3.0% changed pixels in
  `regression_test.sh`. The comparator itself exits nonzero for invalid inputs
  such as size mismatches and supports `--json-out` plus
  `--max-changed-pct N` for strict standalone gates.
- `trace_<lvl>.jsonl` — per-frame state trace (schema below). `compare_state.py`
  reports the first divergent frame and field path. `regression_test.sh` also
  runs `tools/audit_render_trace.py` against every captured trace, even when
  baselines are missing and state comparison is skipped.
- `audio_<lvl>.raw` — first 300 frames of PCM by default, s16le stereo
  22050 Hz. Set `GE007_AUDIO_DUMP_FRAMES=N` to capture a longer window.
  `compare_audio.py` fails on large RMS delta, a noise-like zero-crossing jump,
  or sudden silence.

### Reference audio comparison

For music-fidelity work, `tools/compare_audio_reference.py` compares an MGB64
audio dump against a WAV/raw capture from an emulator or hardware reference. It
does not require sample-exact timing: the tool aligns the captures by amplitude
envelope, then reports RMS level, envelope correlation, broad spectral-band
similarity, gain-normalized spectral tilt, stereo balance/width, and
high-frequency band drift. This is the preferred non-listening check for changes
that affect music/reverb/mixer behavior.

Keep reference captures local. Raw/WAV captures of game audio are generated from
your ROM or hardware output and must not be committed, attached to issues, or
redistributed.

#### Startup music A/B workflow

1. Capture the MGB64 pre-SFX music stream and local gain-staging evidence.

```sh
mkdir -p /tmp/mgb64_audio_ref

tools/startup_music_reference_check.sh \
  --no-build \
  --rom baserom.u.z64 \
  --out-dir /tmp/mgb64_audio_ref
```

This 2400-frame window captures roughly 80 seconds of boot audio on the port:
legal screen, Nintendo logo, Rare logo, eye intro, GoldenEye logo, and the first
unattended display-cast/attract loop. It intentionally does not synthesize a
Start press; do not set `GE007_AUTO_START` for this reference lane. The wrapper
keeps the local pre-SFX raw dump, final mixed raw dump, audio trace, log, save
directory, summary, and screenshot under the output directory so ROM-derived
validation artifacts do not land in the repo root.

2. Capture the same no-input startup window from a reference path.

Acceptable references include original hardware line-out capture, an emulator
movie whose audio is known-good for this scene, or a local instrumented emulator
build that dumps mixed N64 audio before the host speaker backend.

For a movie capture, convert the audio to plain PCM WAV:

```sh
ffmpeg -i reference_startup_capture.mov -vn -ac 2 -ar 44100 \
  -c:a pcm_s16le /tmp/mgb64_audio_ref/reference_boot.wav
```

For a local instrumented ares build, use the same no-input startup sequence and
dump mixed N64 AI audio before host playback. Stock ares builds normally do
not expose this dump hook; the wrapper below fails clearly if the selected
binary does not honor `ARES_AUDIO_DUMP`.

```sh
tools/prepare_ares_audio_dump_build.sh

tools/ares_startup_audio_reference.sh \
  --ares-bin build/ares-audio-dump/ares/build-audio-dump/desktop-ui/ares.app/Contents/MacOS/ares \
  --rom baserom.u.z64 \
  --out-dir /tmp/mgb64_audio_ref
```

`prepare_ares_audio_dump_build.sh` clones ares into ignored local build space,
applies a small `ARES_AUDIO_DUMP` hook, and builds a local frontend binary. It
does not vendor ares and it does not create redistributable audio. The capture
wrapper runs ares with an isolated settings file, an isolated blank save
directory, kiosk mode, and `Input/Defocus=Allow` so background captures keep
advancing without picking up a local `.eeprom` next to the ROM.

For the longer startup reference used while tuning the title sequence music,
capture about 90 seconds of 22050 Hz stereo PCM and allow extra wall-clock time
for shader compilation/startup overhead:

```sh
tools/ares_startup_audio_reference.sh \
  --ares-bin build/ares-audio-dump/ares/build-audio-dump/desktop-ui/ares.app/Contents/MacOS/ares \
  --rom baserom.u.z64 \
  --out-dir /tmp/mgb64_audio_ref \
  --seconds 110 \
  --rate 22050 \
  --dump-frames 1984500
```

`1984500` frames is 90 seconds at 22050 Hz stereo. If your reference dump uses a
different rate, channel count, endian, or length, pass the correct raw metadata
and use `--duration`/start offsets in the comparison command. A 44100 Hz
reference also works; it just adds a reference-resampling step to the comparison.

3. Compare the captures and write segmented JSON output.

The wrapper can recapture MGB64 and compare in one command:

```sh
tools/startup_music_reference_check.sh \
  --no-build \
  --rom baserom.u.z64 \
  --out-dir /tmp/mgb64_audio_ref \
  --reference /tmp/mgb64_audio_ref/reference_boot.wav \
  --min-compared-seconds 60 \
  --report-only
```

For a raw 22050 Hz ares reference:

```sh
tools/startup_music_reference_check.sh \
  --no-build \
  --rom baserom.u.z64 \
  --out-dir /tmp/mgb64_audio_ref \
  --frames 2700 \
  --reference /tmp/mgb64_audio_ref/ares_boot_22050.raw \
  --reference-format raw \
  --reference-raw-rate 22050 \
  --min-compared-seconds 80 \
  --report-only
```

Use `--report-only` while tuning or when first onboarding a new reference
capture. Omit it once the current thresholds describe an accepted baseline that
should fail on future regressions.

#### Startup visual/title workflow

Use `tools/startup_visual_parity_capture.sh` for boot/title visual work. The
wrapper pins the native port to a 640x480 non-HiDPI original-look config,
enables the fail-closed fidelity-capture aspect contract, and captures a
1,800-frame native title trace, audits title-state progression with
`tools/audit_startup_trace.py`, and saves local screenshot checkpoints for the
legal screen, Nintendo/Rare logo phases, gunbarrel, blood overlay, and fades.
It also writes `native_phase_samples.tsv` and mirrors each native phase into
`captures.tsv`, `comparisons.tsv`, and `summary.json` so visual pairs can be
checked against `front.menu`, `gunbarrel_mode`, blood state, Rare rotation, and
Nintendo rotation/scale. The screenshot health gate is intentionally permissive
because valid startup frames can be sparse, solid red, or black; the trace audit
and phase index are the state gates.

```sh
tools/startup_visual_parity_capture.sh \
  --no-build \
  --rom baserom.u.z64 \
  --binary build/ge007 \
  --out-dir /tmp/mgb64_startup_visual
```

When a movement-oracle ares build is available, add stock checkpoints and a
phase-selected screenshot comparison. The default pair compares native frame 120
against stock frame 300, which aligns the legal-screen presentation rather than
raw process frame number.

```sh
tools/startup_visual_parity_capture.sh \
  --no-build \
  --rom baserom.u.z64 \
  --binary build/ge007 \
  --ares-bin build/ares-movement-oracle/ares/build-movement-oracle/desktop-ui/ares.app/Contents/MacOS/ares \
  --native-shot-frames "120" \
  --stock-shot-frames "300" \
  --out-dir /tmp/mgb64_startup_visual_legal
```

The legal-screen pair also guards the frontend model-fog path. A fixed native
capture should keep the active bboxes aligned and preserve the bright red seal
component; `/tmp/mgb64_startup_visual_legal_frontendfog_1782733239` measured 804
native warm pixels against 812 stock warm pixels, with the largest bright seal
component at roughly 53x55 versus stock 54x54. Keep all generated traces,
PPM/BMP files, and JSON comparisons local.

Use phase labels before interpreting logo diffs. A false Rare comparison was
native frame `600` versus stock frame `900`; the phase index shows native `600`
is still `nintendo` (`menu=1`, `nintendo_rotation=4.7124`) while stock `900`
is in the Rare logo window. The aligned current proof
`/tmp/mgb64_startup_phase_compare_rare_aligned` uses native `864`
(`phase=rare`, `rare=178`) against stock `900` and reports `9.1%` changed
pixels with matching warm-object footprint, so do not chase Rare texture
decode from the old `600:900` pair.

The remaining Nintendo-logo lead is RDP coverage/VI behavior, not simple phase,
fog, texture decode, or GL blend factors. The current sparse stock/native sweep
is `/tmp/mgb64_startup_nintendo_stock_sweep`; the best broad-metric checkpoint
is native `570` (`menu=1`, `timer=320`, `nintendo=4.1888`) against stock `630`,
with `9.29%` changed pixels and active bboxes `[101,165,311,151]` versus
`[113,166,290,152]`. That pair is still visibly too hard/white on native.

Stock RDP proof for that checkpoint is
`/tmp/mgb64_startup_nintendo_rdp_ops_630`: the main logo material emits `4228`
`TriShadeTextureZBuffer` draws with
`combine=0xfc127e24/0xfffff9fc`,
`other=0xef882c3f/0x00502048`, env `0`, image `0x8016cbd8`, and a 32x32
I-format texture. Native trace `/tmp/mgb64_startup_nintendo_tex_trace_1782741840`
matches the important draw state: `oml_raw=0x00502048`, no forced fog, ambient
white plus black directional light, combiner `0x009ffe4f19ffe4f1`, texture-gen
scale `2048,2048`, and a dumped 32x32 I8 payload with alpha from intensity
(`713/1024` nonzero alpha texels). The stock sidecar's texture-image `siz=2`
appears to be SETTIMG-side state; the native payload itself is not currently a
proven format mismatch.

Negative controls are now checked. `GE007_KEEP_TEXTURE_GEN_FOG=1` makes the logo
too dark (`0` bright pixels in
`/tmp/mgb64_startup_nintendo_sweep_keep_texgen_fog`). Global 3-point filtering,
forced output VI filtering, and RGB555 quantization did not close the gap.
`/tmp/mgb64_startup_nintendo_alpha_sweep_1782741365` shows default, premult, and
copy GL blend factors are pixel-identical, additive blending is worse, and
inverted alpha blanks the logo; `GE007_DIAG_ALPHA_FROM_TEX_INTENSITY_CC` is a
no-op because the combiner's final alpha is already `1`. A local final-alpha
probe reduced changed pixels only from `9.29%` to `9.22%` while increasing bright
pixels (`10970 -> 12088`, stock `8102`), so final alpha is not the fix. Next
useful work should target `G_RM_AA_OPA_SURF2` / `ALPHA_CVG_SEL` coverage and VI
AA semantics for texture-gen triangles, ideally with stock per-pixel RDP probes,
before changing native brightness, fog, or texture format paths.

The lower-level commands below are equivalent and useful when comparing
previously captured files with custom offsets or durations.

For a WAV/movie-derived reference:

```sh
python3 tools/compare_audio_reference.py \
  /tmp/mgb64_audio_ref/reference_boot.wav \
  /tmp/mgb64_audio_ref/mgb64_boot_music.raw \
  --test-format raw --test-raw-rate 22050 \
  --target-rate 22050 --max-offset-seconds 20 \
  --min-compared-seconds 60 \
  --segment-seconds 10 --segment-hop-seconds 5 \
  --print-bands --print-segments \
  --json-out /tmp/mgb64_audio_ref/boot_audio_compare.json
```

For a raw 44100 Hz ares reference:

```sh
python3 tools/compare_audio_reference.py \
  /tmp/mgb64_audio_ref/ares_boot_44100.raw \
  /tmp/mgb64_audio_ref/mgb64_boot_music.raw \
  --reference-format raw --reference-raw-rate 44100 \
  --test-format raw --test-raw-rate 22050 \
  --target-rate 22050 --max-offset-seconds 20 \
  --min-compared-seconds 60 \
  --segment-seconds 10 --segment-hop-seconds 5 \
  --print-bands --print-segments \
  --json-out /tmp/mgb64_audio_ref/boot_audio_compare.json
```

#### Interpreting the metrics

Use the metrics as a regression signal, not a proof of bit-perfect N64 audio.

- `alignment_envelope_corr` reports how well the tool could align the two
  captures before comparing them. Low alignment usually means the captures do
  not contain the same scene, have different input timing, or drift too much.
- `envelope_corr` reports amplitude-shape similarity after alignment. Low
  values usually point to timing/sequence mismatch before they point to mixer
  tone.
- `spectral_cosine` close to `1.0` means the broad frequency shape is similar;
  it is less sensitive to simple volume changes.
- `band_mae_db` is the average absolute per-band energy error. Lower is better;
  a sudden increase after an audio change is a regression signal.
- `relative_band_mae_db` removes the overall band bias first. Use it when a
  hardware/movie capture has a different recording level but you still need to
  compare tone.
- `high_band_mae_db` isolates the upper bands. High values are useful for
  catching thin, harsh, overly bright, or missing-frequency output.
- `high_relative_band_delta_db` preserves the high-frequency direction after
  removing simple level mismatch. Positive values mean the port is relatively
  brighter than the reference; negative values mean the port is relatively
  duller.
- `stereo.balance_delta_db`, `stereo.width_delta_db`, and
  `stereo.possible_channel_swap` catch L/R bus, panning, or channel-order
  mistakes that a mono downmix can hide.
- The top-level `diagnosis` block is a triage hint. Treat it as a pointer to the
  next mixer/sequence area to inspect, not as proof by itself.
- The `segments` JSON and `--print-segments` output identify the worst 10-second
  windows. Use those windows to map a mismatch back to a logo, intro, or
  attract-loop portion of the startup sequence.

## State trace schema (`--trace-state path.jsonl`)

One JSON object per frame, one per line. Common fields:

| Field | Meaning |
|-------|---------|
| `f` | frame index |
| `p` | has-player (1/0) |
| `pos` | `[x,y,z]` player position |
| `tris` | triangles drawn |
| `crashes` | cumulative DL crash-recovery count |
| `bad_cmds` | unhandled GBI command count |
| `dl` | DL failure sub-counters (mtx/vtx/dl/movemem/texture/settimg/skips) |
| `rooms` | room lookup + visibility + fallback state |
| `fog` | `[r,g,b]` plus `fog_mul`, `fog_off` |
| `geom` | geometry-mode bits (hex string; cull bits ignored by comparator) |
| `segs` | segment-load mask (hex string) |
| `combat` | aim / autoaim / crosshair / health / shots |
| `move` | speed, raw gait inputs, boost, head/previous positions, and timers |
| `scan.nearest`, `target_x`, `target_y` | per-guard AI / sense / visibility / damage |
| `wr_*` / `wl_*` | right/left-hand weapon-render state and switch-node bases |
| `front` | frontend / menu state (folder, gamemode, stage, briefing, cursor, mission result flags…) |
| `inv` | inventory count, keyflags (hex), GoldenEye-key flags |
| `nan` | count of non-finite values detected this frame |
| `stub_hits` | optional; `--accuracy-lane` fails if any `snd_*` counter is non-zero |

The `dl` counters should stay at zero in normal route-health audits. Treat
`unregistered_skip` as a hard provenance bug: a `G_DL` target did not belong to
any registered N64 region, static PC DL, current native GFX pool, current native
VTX pool, or extra heap PC-DL region. If a live log prints
`[GFX-DL] unregistered addr=...`, compare that address with the latest `[DYN]`
GFX/VTX allocation ranges first. Native VTX can contain runtime sub-DLs because
`dynAllocate` shares that pool with vertices and matrices, but fast3d now also
requires a plausible PC command stream before recursive dispatch. A
`non_dl_skip_pc` means a known PC range contained data that intentionally failed
that plausibility gate; it is different from an unknown/stale pointer.

Comparator behavior (`compare_state.py`): default float tolerance `0.1`; `geom`
masks the cull bits (`0x00003000`); a few late stages allow small `tris` drift;
corrupt lines (from DL crash-recovery longjmp) are skipped with a warning.

## Useful environment variables

| Variable | Effect |
|----------|--------|
| `GE007_MUTE=1` | start muted |
| `SDL_AUDIODRIVER=dummy` | silent SDL audio device (background runs) |
| `GE007_BACKGROUND=1` | hidden window, no input grab |
| `GE007_NO_VSYNC=1` | reliable `--screenshot-exit`, especially on macOS |
| `GE007_DETERMINISTIC_STABLE_COUNT=1` | stable synthetic frame time for deterministic validation |
| `GE007_DEBUG=1` | enable diagnostic stderr (used by spawn health) |
| `GE007_VERBOSE=1` | all diagnostic printf |
| `GE007_AUDIO_DUMP=path.raw` | dump first 300 final mixed PCM frames |
| `GE007_MUSIC_AUDIO_DUMP=path.raw` | dump first 300 libaudio/music PCM frames before native SFX mixing |
| `GE007_AUDIO_TRACE=path.jsonl` | trace per-frame audio queue, final PCM peak/rail hits, mixer, and SFX/player counters |
| `GE007_MUSIC_TRACE=path.jsonl` | trace music slot play/stop events |
| `GE007_MUSIC_TRACE_SNAPSHOT=1` | add per-frame music slot snapshots, including CSP ticks, tempo-derived `uspt`, queued events, and active voices |
| `GE007_MUSIC_MIDI_TRACE_JSONL=path.jsonl` | trace CSP MIDI note/control events with program, keymap, envelope, pitch, and wavetable metadata |
| `GE007_MUSIC_MUTE_PROGRAMS=list` / `GE007_MUSIC_SOLO_PROGRAMS=list` | mute or solo comma-separated music program numbers for local A/B diagnosis |
| `GE007_ENABLE_LIBAUDIO_LOWPASS=1` | opt into the experimental final libaudio/music de-emphasis filter |
| `GE007_DISABLE_LIBAUDIO_LOWPASS=1` | force the experimental final libaudio/music de-emphasis filter off even if enabled |
| `GE007_AUDIO_FILTER_TRACE_JSONL=path.jsonl` | trace ADPCM/resampler pulls; pair with `GE007_AUDIO_FILTER_TRACE_WAVE_BASE=0x...` to focus on one wavetable |
| `GE007_AUDIO_POLE_TRACE_JSONL=path.jsonl` | trace custom-FX pole-filter calls by coefficient, gain, state, and peak levels |
| `GE007_MIXER_POLE_SAMPLE_XOR=0|1` | diagnostic ABI1 pole-filter sample-lane override for A/B comparison |
| `GE007_MIXER_POLE_FC_XOR_MASK=mask` | diagnostic per-pole-section lane mask for startup music experiments; bits map to filter coefficients 4736, 6144, 6784, 8192, 8832 |
| `GE007_DISABLE_NATIVE_POLE_FILTER=1` | bypass native custom-FX pole filters for diagnosis |
| `GE007_SFX_TRACE_JSONL=path.jsonl` | trace SFX submits, voice starts/stops, volume/pan updates, owner-slot clears, stale post ignores, and native caller/line tags |
| `GE007_TRACE_CHRNUM=N` | add one native guard's AI/action/render/patrol state to `--trace-state` |
| `GE007_PERF_TRACE=1` / `GE007_PERF_TRACE_AFTER_FRAME=N` / `GE007_PERF_TRACE_BUDGET=N` | log per-frame wall-clock pacing (`interval_ms`, render/work time before cap delay, sleep delay, and sync overhead) for local performance triage |
| `GE007_XLU_SNAPSHOT_MODE=perbatch\|pertri` | translucent RDP-memory blend framebuffer-snapshot granularity (`docs/design/PERFORMANCE_PLAN.md` M1). Default `perbatch` (one framebuffer copy per draw batch); `pertri` restores the legacy per-triangle copy for exact-parity A/B |
| `GE007_DISABLE_ROOM_XLU_CVG_MEMORY=1` | disable the room XLU coverage-memory blend path entirely (kills the per-triangle framebuffer copy; the `xluoff` column in `tools/perf_census.sh`) |
| `MGB64_ARES_TRACE_CHRNUM=N` | add comparable stock-oracle guard tracking to route traces |
| `GE007_TRACE_MAGIC_TRAVEL=1` / `GE007_TRACE_MAGIC_TRAVEL_BUDGET=N` | log native `WAYMODE_MAGIC` patrol/gopos exit predicates for the `GE007_TRACE_CHRNUM` guard |
| `GE007_TRACE_OBJECTIVES=1` | add objective data to the state trace |
| `GE007_AUTO_MISSION_END_FRAME=N` / `GE007_AUTO_MISSION_END_RESULT=success|fail|abort|kia` | deterministic mission-end hook for frontend/end-state smoke tests |
| `GE007_AUTO_SET_STAGE_FLAGS_SCRIPT=FRAME:FLAGS[,..]` | deterministic objective/control hook; sets stage flags at the listed frames, with decimal or hex flags |
| `GE007_AUTO_UNSET_STAGE_FLAGS_SCRIPT=FRAME:FLAGS[,..]` | deterministic objective/control hook; clears stage flags at the listed frames, with decimal or hex flags |
| `GE007_AUTO_EXIT_ON_TITLE=1` / `GE007_AUTO_EXIT_ON_TITLE_DELAY=N` | exit deterministic validation after returning to the title stage |
| `GE007_AUTO_EXIT_FRAME=N` | exit deterministic validation on the first tick after frame `N`, so trace-based gates can require frame `N` was observed |
| `GE007_DOOR_FLOOR_COLLISION=0` / `GE007_DISABLE_DOOR_FLOOR_COLLISION=1` | disable native closed-door floor candidates for A/B; used by the Surface II hatch guard to reproduce the old drop-through path while keeping normal lateral closed-door collision intact |
| `GE007_AUTO_SELECT_LEVEL=name` / `GE007_AUTO_SELECT_DELAY=N` | frontend selector route for full/no-arg boot probes; prefer named levels such as `bunker1` when a numeric value could be a menu index |
| `GE007_AUTO_ADD_ITEM_FRAME=N` / `GE007_AUTO_ADD_ITEM=ID` | deterministic inventory injection hook; used by equipment crash repros such as Bunker datathief item `55` |
| `GE007_AUTO_EQUIP_ITEM_FRAME=N` / `GE007_AUTO_EQUIP_ITEM=ID` | deterministic equipment hook; pair with inventory injection for viewmodel/watch-item repros |
| `GE007_AMMO_ICON_FAULT=AMMOTYPE` / `GE007_AMMO_ICON_FAULT_INVALID=AMMOTYPE` | ammo HUD fault injection (M2.6): map one ammo type's icon to NULL, or to a poisoned entry (zero width, out-of-table index) that `portValidateImageEntry` must reject; the HUD must draw the placeholder glyph and count `hud_image_fault` |
| `GE007_AUTO_DEBUG_DUMP_FRAME=N` | request one live debug dump once the diagnostic frame counter reaches `N` |
| `GE007_DEBUG_DUMP_BONES=1` | opt in to live bone-matrix rows in debug dumps after dyn-vtx range validation; default dumps metadata only |
| `GE007_DEBUG_DUMP_RWDATA=1` | opt in to raw root rwdata byte sampling in debug dumps |
| `GE007_STEADY_VIEW=0|1` | live setting for `Input.SteadyView`; default `1` keeps the native world camera upright while movement/head animation still drive position and weapon sway |
| `GE007_NO_FOG=1`, `GE007_WIREFRAME=1`, `GE007_TEX_ONLY=1` | renderer debug toggles |
| `GE007_FOG_USE_LINEAR_DEPTH=1` | fog-depth negative control; remaps camera-space depth linearly and should not be used as the N64-parity default |
| `GE007_FORCE_POINT_FILTER=1`, `GE007_FORCE_LINEAR_FILTER=1`, `GE007_DISABLE_N64_FILTER=1` | texture-filter A/B probes for smearing, bilerp, and shader-filter issues |
| `GE007_DIAG_N64_FILTER_ALWAYS_3POINT=1`, `GE007_DIAG_N64_FILTER_NEAREST_THRESHOLD=N`, `GE007_DIAG_N64_FILTER_CLAMPED_NON_TEXEDGE_NEAREST_THRESHOLD=N` | N64 shader-filter controls; use to separate threshold policy from texture decode |
| `GE007_DIAG_SETTEX_CLAMPED_NON_TEXEDGE_N64_FILTER_ALWAYS_3POINT=0|1` | override the default clamped non-texture-edge `G_SETTEX` 3-point policy; use `0` as the negative control |
| `GE007_DIAG_LOADED_TILE_2TEX_N64_FILTER=1|*|cc-list` | opt-in diagnostic that enables shader-side N64 filtering for loaded-tile materials using both `TEXEL0` and `TEXEL1`; useful for LOD-combiner A/Bs such as glass shards |
| `GE007_FORCE_ROOM_POINT_FILTER=1` | negative control for room-geometry filtering; this intentionally bypasses the default N64 shader filter and should look harsher on Dam/Cradle/Surface |
| `GE007_TRACE_TEX_FOOTPRINT=1`, `GE007_TRACE_TEX_FOOTPRINT_BUDGET=N` | log `G_SETTILESIZE` decode-footprint decisions, including row pitch, visible row width, LOD state, and room-DL context |
| `GE007_DISABLE_LOADBLOCK_STRIDED_FOOTPRINT=1` | negative control for row-pitch smearing; disables the default LOADBLOCK strided decode footprint without changing source texture bytes |
| `GE007_TINT_TEX=min:max`, `GE007_SKIP_TEX=min:max` | tint or skip `G_SETTEX` texture-number ranges; these match stable game texture numbers, not transient GL upload ids |
| `GE007_DIAG_DISABLE_SHADER_CLAMP=1` | negative control for shader-side UV clamp; use only to prove clamp policy/coordinates are involved |
| `GE007_DIAG_DISABLE_SHADER_TILE_MASK=1` | negative control for shader-side N64 tile-mask wrap/mirror; use only to prove mask-period sampling is involved |
| `GE007_NO_SKY=1`, `GE007_SKIP_SKY=1`, `GE007_SKY_SCREENSPACE=1`, `GE007_SKY_UV_SCALE=N`, `GE007_DISABLE_SKY_BACKDROP_DEPTH=1`, `GE007_DISABLE_SKY_QUEUE=1` | sky isolation, legacy sky path, UV-scale probes, native sky backdrop-depth negative control, and native sky queue phase-order negative control |
| `GE007_BUILD_JOBS=N` | cap build parallelism (default 4) |

Fog regressions can masquerade as texture loss on distant stages. N64
`gSPFogPosition` values are specified from near plane to far plane, but the
effect is nonlinear after perspective transform; a narrow range such as
Cradle's 996..1000 ramp can legitimately fog much earlier than a linear
world-distance reading suggests. Use `GE007_TRACE_FOG_TRIANGLES=1` before
changing texture decode/filtering. The parity path uses `clip_z / clip_w`;
for negative or near-zero homogeneous `w`, keep the legacy saturated reciprocal
behavior so clipped geometry does not become incorrectly clear.
`GE007_FOG_USE_LINEAR_DEPTH=1` is only a negative control for proving that a
linear remap is causing over-clear distant geometry.

Room geometry should not default to point sampling. The game's room display
lists commonly request N64 texture filtering, and the native path routes that
through the shader-side N64 filter. A blanket room-nearest override bypasses
that path and shows up as low color diversity/blocky texture regression on
Cradle, Dam, Surface, and similar large room surfaces.

Texture regressions tend to fall into three separate classes. Keep them
separate during review so one fix does not hide another:

1. **Filter policy:** `GE007_DISABLE_N64_FILTER=1` falls back to ordinary GL
   bilinear sampling and can produce diagonal/near-plane smear on Dam and
   Cradle room geometry. The default path uses the shader-side N64 filter plus a
   nearest threshold. The footprint is normalized to the N64 VI/logical pixel
   grid so RenderScale and high-DPI windows do not change shader branch policy.
   Clamped non-texture-edge `G_SETTEX` materials default to the full shader
   3-point path instead of the footprint-based nearest shortcut. That keeps Dam
   room glass and nearby wall shading from hardening while avoiding the broader
   softness of global `GE007_DIAG_N64_FILTER_ALWAYS_3POINT=1`. Set
   `GE007_DIAG_SETTEX_CLAMPED_NON_TEXEDGE_N64_FILTER_ALWAYS_3POINT=0` only as a
   negative control to re-enable the shortcut for this class.
   `GE007_DIAG_LOADED_TILE_2TEX_N64_FILTER=cc-list` is a default-off negative
   control for loaded-tile LOD combiners that consume both `TEXEL0` and
   `TEXEL1`; it should be promoted only with material-specific stock evidence.
   `GE007_DIAG_N64_FILTER_ALWAYS_3POINT=1` is the negative control: it removes
   the threshold and should visibly soften large close surfaces.
2. **Decoded-source footprint:** texture-cache identity must include the decoded
   source row pitch as well as visible upload dimensions. N64 room materials can
   describe the same visible tile size from either packed `LOADBLOCK` bytes or a
   strided `LOADTILE`/sub-rect source; omitting the source pitch lets a later
   material reuse a GL texture decoded with the wrong stride, which looks like
   row smear even though the source bytes and sampler mode are correct.
   `GE007_DISABLE_LOADBLOCK_STRIDED_FOOTPRINT=1` is the negative control: if it
   barely changes a capture, the active defect is probably filter policy rather
   than decoded-source stride.
3. **Rare `G_SETTEX` tile state:** stale ordinary TMEM tile descriptors can
   survive next to the active texture-by-number state. A healthy
   `GE007_TRACE_SETTEX_MATERIAL_CC='*'` trace has `tilex0`/`tilex1` extended
   state and shader clamp/mask bits that agree with the decoded effective
   `G_SETTEX` tile state. If `tile0`/`tile1` says wrap but `opts` still carries
   `SHADER_OPT_TEXEL*_CLAMP_*`, native rendering will clamp repeated room
   coordinates to an edge row/column and stretch it across the surface. If
   `masks`/`maskt` describe a period smaller than the uploaded texture extent,
   the shader must apply that repeat/mirror period in texel space before
   filtering; ordinary GL repeat/mirror repeats the full upload and is not
   equivalent. Surface's active proof is a used two-texture material with
   `opts=0x18003012`, a 32x32 upload, and
   `tilex1=(2,0,4,4,8,0,62,62)`. The renderer should only trust an authored
   room tile descriptor when its format/size, clamp/wrap, masks, shift, offset,
   line, TMEM, and dimensions match the current `G_SETTEX` state.
4. **Material attribution:** `G_SETTEX` texture numbers and OpenGL upload ids
   are not interchangeable. Use `GE007_TINT_TEX`/`GE007_SKIP_TEX` to isolate
   stable game texture numbers, then confirm the material with
   `GE007_TRACE_SETTEX_MATERIAL_CC='*'`. Runway is a good counterexample: the
   same visual band can include texture 22, 1267, and ordinary room materials,
   so a single broad crop can make the wrong texture look guilty.
5. **Static image-table identity:** `texSelect()` image-table entries must reach
   fast3d as IMAGESEG-style static texture tokens, not transient heap pointers.
   The static path gives the renderer a stable cache key, preserves CI/TLUT
   palette identity, and tells RGBA16/RGBA32 importers to decode native texture
   words instead of raw byte order. If muzzle flares, explosions, or other
   global effect sprites turn purple, run
   `GE007_TRACE_TEXSELECT=1 GE007_TRACE_TEX_PIPELINE=1` and check for
   `cache=0x800... static=1` on the `TEX-SETTIMG` / `TEX-LOADBLOCK` rows. If
   guard faces, tan uniforms, red/tan paletted materials, or alarm lenses go
   gray, run the Dam palette probe below and check for no-palette/grayscale
   fallback rows.

The June 2026 renderer regression postmortem is in
[RENDERING_REGRESSION_NOTES.md](RENDERING_REGRESSION_NOTES.md). It summarizes the
glass, filter-footprint, decoded-footprint, `G_SETTEX`, and fog failure modes
that this instrumentation is meant to keep from recurring.

Renderer acceptance captures should use a clean config profile or explicit
overrides. A repo-root run can load local `ge007.ini`; values such as
`Video.FovY=75` deliberately change composition and pixel metrics. Large
Retina/high-DPI windows also multiply the GL drawable size when
`Video.HiDPI=1`, so leave the default `Video.HiDPI=0` for performance-sensitive
gameplay/perf captures unless the test is explicitly about native display-pixel
output. The
playability smoke lane pins its validation window by default; for stock visual
baselines, keep that default and add only the probe-specific overrides, such as
`--config-override Video.FovY=60 --config-override Video.RenderScale=1` or
`--config-override Video.RenderScale=2`. When comparing against an older binary
that does not support the current config-override path, use
`--no-default-config-overrides` on both captures or upgrade the reference binary
before trusting whole-frame screenshot deltas.

### Renderer diagnostics & experimental fixes (default OFF)

The renderer exposes a family of `GE007_DIAG_*` toggles for A/B-testing
candidate fixes without changing the default. The main default-off visual
experiment currently kept for reference is the **frontend/menu brightness**
color scale (see [PORT.md](../PORT.md)):

| Variable | Effect |
|----------|--------|
| `GE007_DIAG_NOPERSPECTIVE_SETTEX_TEXCOORDS=1` | noperspective texcoords for `settex` materials |
| `GE007_DIAG_NOPERSPECTIVE_CC=1|*|cc-list` | noperspective texcoords for matching effective combiner ids, including non-`G_SETTEX` loaded-tile materials; default-off material interpolation A/B |
| `GE007_DIAG_NOPERSPECTIVE_CC_INPUTS=1|*|cc-list` | noperspective shader inputs for matching effective combiner ids, including non-`G_SETTEX` loaded-tile materials; default-off material interpolation A/B |
| `GE007_DIAG_SETTEX_CC_COLOR_SCALE=1` | enable a combiner color scale on `settex` materials (menu brightness experiment) |
| `GE007_DIAG_SETTEX_CC_COLOR_SCALE_VALUE=N` | the scale factor (default `1.02`) |
| `GE007_DIAG_SETTEX_CC_ALPHA_SCALE=1|*|cc-list` | default-off diagnostic alpha scale for matching `settex` material combiners; use with tex filters for room-glass A/B only |
| `GE007_DIAG_SETTEX_CC_ALPHA_SCALE_VALUE=N` | alpha scale factor for the matched `settex` combiner (default `1.0`) |
| `GE007_DIAG_SETTEX_CC_ALPHA_SCALE_TEXSIZE=WxH` / `GE007_DIAG_SETTEX_CC_ALPHA_SCALE_TEXNUM=N` | optional filters for the diagnostic `settex` alpha scale; the pad-10092 room-glass probe uses `54x54` and texnum `654` |
| `GE007_DIAG_ROOM_ALPHA_ENV_SCALE=N` | default-off A/B for scaling env-alpha shader inputs on room-matrix `G_SETTEX` XLU materials; use only to test whether glass opacity responds to stronger/weaker env alpha |
| `GE007_DIAG_LOD_FRACTION=N` | force shader `LOD_FRACTION` input for A/B material tests; Dam glass parity proved `0` is default-equivalent and larger values are negative controls |
| `GE007_DIAG_SHADE_SCALE=N` | scale per-vertex shade RGB inputs for A/B material tests; use only to prove broad shade intensity is or is not involved |
| `GE007_DIAG_NO_SETTEX_LINEARIZE=1` | disable the `G_SETTEX` UV linearization path for A/B only; not a Dam glass fix by itself |
| `GE007_DIAG_SETTEX_MIRROR_TEX1=1` | force `G_SETTEX` texel1 coordinate mirroring for A/B only |
| `GE007_DIAG_DISABLE_SHADER_CLAMP=1` | negative control for shader-side UV clamp; use only to prove clamp policy/coordinates are involved |
| `GE007_DIAG_DISABLE_SHADER_TILE_MASK=1` | negative control for shader-side N64 tile-mask wrap/mirror; use only to prove mask-period sampling is involved |
| `GE007_DIAG_CONVERT_K4K5=1` | A/B combiner K4/K5 conversion; strong negative control on Dam glass |
| `GE007_DIAG_SWAP_IA8_NIBBLES=1|*|key-list` | default-off loaded-tile IA8 decode A/B; swaps intensity/alpha nibbles globally or for selected texture cache keys, useful for shard texture-payload semantics checks |
| `GE007_DIAG_IA8_CHANNEL_MODE=mode[:key-list]` | default-off loaded-tile IA8 channel A/B; modes are `rgb_from_alpha`, `alpha_from_intensity`, and `swap`, optionally scoped to texture cache keys |
| `GE007_DIAG_ALPHA_FROM_TEX_INTENSITY_CC=1|*|cc-list` | default-off loaded-tile shader A/B that makes sampled texture alpha come from sampled intensity for matching effective combiner ids; useful for active-shard alpha/coverage isolation without changing IA8 uploads |
| `GE007_DIAG_ALPHA_FROM_TEX_INTENSITY_MIX=N` | optional `0..1` mix factor for `GE007_DIAG_ALPHA_FROM_TEX_INTENSITY_CC`; the binary old behavior is `1.0`, while lower values are useful for non-promotable active-shard alpha sweeps |
| `GE007_DIAG_ZMODE_XLU_LESS=1` | A/B `ZMODE_XLU` depth compare as `GL_LESS` instead of default `GL_LEQUAL` |
| `GE007_DIAG_ZMODE_DEC_LESS=1` | A/B `ZMODE_DEC` depth compare as `GL_LESS` instead of default `GL_LEQUAL` |
| `GE007_DIAG_ZMODE_DEC_NO_POLY_OFFSET=1` | A/B disabling the default `ZMODE_DEC` polygon offset |
| `GE007_DIAG_ZMODE_DEC_OFFSET_FACTOR=N` / `GE007_DIAG_ZMODE_DEC_OFFSET_UNITS=N` | A/B custom `ZMODE_DEC` polygon offset values; defaults are `-2,-2` |
| `GE007_DIAG_ALPHA_BLEND=premult|add|copy|inv_alpha` | A/B alternate OpenGL blend factors for `GFX_BLEND_ALPHA`; default remains `SRC_ALPHA, ONE_MINUS_SRC_ALPHA` |
| `GE007_DIAG_XLU_COVERAGE_A2C=1|*|raw-mode-list` | default-off A/B that maps eligible raw `ZMODE_XLU` + `CLR_ON_CVG` + `FORCE_BL` alpha draws to OpenGL alpha-to-coverage; requires `Video.MSAA>0` to affect pixels |
| `GE007_DIAG_XLU_COVERAGE_WRAP_THIN_CC=1|*|cc-list` / `GE007_DIAG_XLU_COVERAGE_WRAP_THIN_RATE=N` | default-off shader A/B for matched loaded-tile alpha draws with raw `ZMODE_XLU` + `CVG_DST_WRAP` + `CLR_ON_CVG` + `IM_RD` + `FORCE_BL`; discards a deterministic screen-space fraction of fragments to test whether approximating color-on-coverage/wrap events explains active shard overdraw |
| `GE007_DIAG_XLU_COVERAGE_STENCIL_CC=1|*|cc-list` / `GE007_DIAG_XLU_COVERAGE_STENCIL_INCREMENT=1..8` | default-off backend A/B for matched loaded-tile alpha draws with raw `ZMODE_XLU` + `CVG_DST_WRAP` + `CLR_ON_CVG` + `IM_RD` + `FORCE_BL`; forces a stencil-backed scene target and uses the lower stencil bits as an approximate per-pixel coverage counter before allowing color writes |
| `GE007_DIAG_XLU_RDP_MEMORY_BLEND_CC=1|*|cc-list` | default-off backend/shader A/B for matched alpha draws, including loaded-tile and `G_SETTEX` materials, with raw `ZMODE_XLU` + `CVG_DST_WRAP` + `CLR_ON_CVG` + `IM_RD` + `FORCE_BL`; snapshots the current framebuffer per triangle and applies an RDP-style final blender/memory-color formula without GL fixed-function alpha blending |
| `GE007_DIAG_XLU_RDP_CVG_MEMORY_BLEND_CC=1|*|cc-list` | default-off backend/shader A/B for the same matched alpha draws, including `G_SETTEX`; passes per-triangle screen vertices to the shader, estimates an 8-sample coverage count, stores synthetic coverage in framebuffer alpha, and applies the `CLR_ON_CVG` memory-color rule before blending |
| `GE007_DISABLE_ROOM_XLU_CVG_MEMORY=1` / `GE007_ROOM_XLU_CVG_MEMORY=0` | disable the default material classifier that promotes fogged room-class `G_SETTEX` XLU coverage-wrap/color-on-coverage strips to `GFX_BLEND_ALPHA_RDP_CVG_MEMORY`, including generated/locally transformed strips with missing room-DL attribution only when `envA=255` and `primA=0`; this also disables the forced RGBA scene target used to keep framebuffer alpha as deterministic coverage memory, so use it as the focused Surface/Jungle tree-fog A/B escape hatch |
| `GE007_DISABLE_RDP_CVG_SNAPSHOT_RECTS=1` / `GE007_RDP_CVG_SNAPSHOT_RECTS=0` | disable the optimized bbox-only framebuffer snapshot for `GFX_BLEND_ALPHA_RDP_CVG_MEMORY` and restore the old full-viewport per-triangle copy; use only for visual/perf A/B because the default rectangle path is screenshot-identical on the Surface guard and avoids large-window stalls |
| `GE007_DISABLE_ROOM_XLU_SORT=1` / `GE007_SORT_ROOM_XLU=0` | disable default local secondary-room XLU far-to-near sorting; this is a broad A/B and should not be used as a first fix when coverage-memory owns the artifact |
| `GE007_DISABLE_ROOM_XLU_DEFER=1` / `GE007_SORT_ROOM_XLU_DEFER=0` | disable the cross-flush secondary-room XLU defer queue while leaving local room-XLU sorting enabled; useful for proving whether a visual delta is from batch ordering or material blending |
| `GE007_TRACE_GLASS_SHARD_COVERAGE=1` / `GE007_TRACE_GLASS_SHARD_COVERAGE_AFTER_FRAME=N` / `GE007_TRACE_GLASS_SHARD_COVERAGE_BUDGET=N` | read-only active-shard coverage-grid trace; logs one `[SHARD-COVERAGE]` row per active frame with raw N64 coverage flags, material/blend identity, and coarse bbox-cell overlap pressure |
| `GE007_DIAG_QUANTIZE_COMBINER=1` | A/B 8-bit quantization of combiner intermediates in the shader; keep as a negative/material precision probe |
| `GE007_DIAG_OUTPUT_RGB555=1|dither` | A/B final-output 5-bit RGB quantization, optionally with the existing 4x4 Bayer dither |
| `GE007_DISABLE_BULLET_IMPACTS=1` | skip bullet-impact decals for ownership isolation only; not a promotable visual fix |
| `GE007_DISABLE_PROP_BULLET_IMPACTS=1` | skip prop-attached bullet-impact creation/rendering only; use to test whether a native-only prop impact is perturbing RNG/state before a world impact |
| `GE007_SKIP_FP_WEAPON_RENDER=1` | skip native first-person weapon rendering for HUD/viewmodel ownership isolation |
| `GE007_SKIP_FP_WEAPON_PROJECTION=1` | skip the native first-person weapon projection load for A/B isolation of projection/screen-mapping issues |
| `GE007_GLASS_BULLET_IMPACT_NORMAL_OFFSET=N` | tune the default glass-crack decal lift (default `2.0`) |
| `GE007_GLASS_SHARD_FIXED_MTX=1` | A/B the old fixed-point matrix emission path for falling glass shards |
| `GE007_FIELD_10E0_SCALED=0` | A/B the stock-style unscaled `field_10E0` negative control; default native stores visibility-scaled `field_10E0`, which Surface/Dam large-room rendering depends on |
| `GE007_BULLET_IMPACT_NORMAL_OFFSET=N` | force a global decal lift for diagnostics |
| `GE007_TINTED_GLASS_MIN_OPACITY=N` | override the native tinted-glass render alpha floor (default `16`; old cloudy floor was `96`; `0` is raw N64-style distance opacity) |
| `GE007_FLAT_PROP_BULLET_IMPACTS=1` | force shade-only (legacy flat) prop-attached bullet impacts; textured bullet-hole decals are now the N64-faithful default for **all** props (crates/barrels/screens/glass), so this is the A/B escape hatch |
| `GE007_FLAT_BULLET_IMPACTS=1` | force shade-only bullet impacts globally |

Run visual experiments with these set, compare against the default visually (and
via the pixel lane), and promote to default only after sign-off. The full
`GE007_DIAG_SETTEX_*` set is enumerated in `src/platform/fast3d/gfx_pc.c` /
`gfx_opengl.c`.

Deterministic input/world scripting (`GE007_AUTO_*`, format `START:LEN` on
deterministic frames) can drive buttons, movement, mouse-look, guard warps,
damage, pickups, camera modes, and frontend navigation for focused probes.
Coordinate and direction scripts accept either one frame (`FRAME:x:y:z`) or an
inclusive frame range (`START-END:x:y:z`), which is useful when a visual oracle
must keep Bond's camera or bullet direction fixed through a screenshot frame.
The full set is in `src/platform/main_pc.c`, `src/platform/stubs.c`,
`src/game/gun.c`, and `src/platform/port_trace.c`.

Useful focused probes:

| Variable | Effect |
|----------|--------|
| `GE007_AUTO_CAMERA_MODE_FRAME=N` + `GE007_AUTO_CAMERA_MODE=swirl|death|death_mp|posend|fp` | force a camera mode for cinematic/viewer rendering probes; death modes wait for Bond to be dead |
| `GE007_AUTO_CAMERA_POSEND_PAD=P` | seed forced `posend` camera mode with a real setup pad, matching the AI `CameraLookAtBondFromPad` path |
| `GE007_AUTO_WARP_CHR_FRAME=N`, `GE007_AUTO_WARP_CHRNUM=C`, `GE007_AUTO_WARP_CHR_DISTANCE=D`, `GE007_AUTO_WARP_CHR_ANGLE=A` | place Bond near a guard at a deterministic frame |
| `GE007_AUTO_SET_CHR_AI_FRAME=N`, `GE007_AUTO_SET_CHR_AI_CHRNUM=C`, `GE007_AUTO_SET_CHR_AI_LIST=L` | force a guard onto a global AI list; use top-level lists only |
| `GE007_AUTO_AUTOAIM_SCRIPT=FRAME:0|1[,..]` | route-only toggle for Bond autoaim; useful when a direct native setup can acquire a target that the stock forced-pose checkpoint does not |
| `GE007_AUTO_FORCE_PLAYER_SCRIPT=START-END:X:Y:Z:YAW_DEG:PITCH_DEG[:EYE_OFFSET[:PAD]]` | hold Bond's deterministic camera pose/floor/tile basis across a range; use as the native counterpart to `MGB64_ARES_FORCE_PLAYER_SCRIPT` |
| `GE007_AUTO_FORCE_CHR_SCRIPT=START-END:CHR:X:Y:Z:YAW_DEG[:PAD[:STOP]]` | route-only native actor pose probe for composition debugging; optional `PAD` supplies a valid stan and optional `STOP=1` puts the actor in the standing-still helper, but this can perturb AI, bullets, or nearby glass, so keep stock/native state guards enabled |
| `GE007_AUTO_FACE_COORD_SCRIPT=FRAME:x:y:z[,..]` / `START-END:x:y:z` | face Bond toward a world coordinate once or across an inclusive frame range |
| `GE007_AUTO_AIM_DIR_SCRIPT=FRAME:x:y:z[,..]` / `START-END:x:y:z` | force the bullet direction once or across an inclusive frame range |
| `GE007_AUTO_DAMAGE_BOND_FRAME=N`, `GE007_AUTO_DAMAGE_BOND_AMOUNT=F` | inject deterministic Bond damage |
| `GE007_AUTO_PLAY_SFX_FRAME=N`, `GE007_AUTO_PLAY_SFX=S` | submit one SFX by public id; useful with `GE007_SFX_TRACE_JSONL` for chain/mix probes |
| `GE007_AUTO_FIRE=START:LEN` | hold fire for a deterministic input window |
| `GE007_AUTO_START=START:LEN` | hold Start for a deterministic frontend/gameplay input window |

Console diagnostics that can become noisy during normal gameplay are opt-in.
Use these when chasing the specific subsystem; leave them unset for release
smoke runs and human play sessions.

| Variable | Effect |
|----------|--------|
| `GE007_TRACE_AI_MOVE=1` | log selected guard AI movement/pathing decisions and route probes |
| `GE007_TRACE_TRAVEL_ANIM=1` | log guard travel-animation state transitions |
| `GE007_TRACE_DL_CONTEXT=1` | log display-list context overlap probes |
| `GE007_TRACE_N64_OML=1` | log selected N64 `G_SETOTHERMODE_L` state transitions |
| `GE007_TRACE_VIEWPORT=1` / `GE007_TRACE_VIEWPORT_AFTER_FRAME=N` / `GE007_TRACE_VIEWPORT_BUDGET=N` | log raw N64 viewport scale/translate, logical VI size, drawable size, aspect adjustment, and final drawable `xywh`; use when separating game viewport guard bands from presentation/crop bugs |
| `GE007_TRACE_BG_ROOM_PTRS=1` | log primary/secondary room display-list pointers as rooms render |
| `GE007_TRACE_RDP_RENDER_MODES=1` / `GE007_TRACE_RDP_RENDER_MODES_AFTER_FRAME=N` / `GE007_TRACE_RDP_RENDER_MODES_BUDGET=N` | log normalized per-triangle `[RDP-MODE]` rows after final native depth/blend/shader classification, including raw/effective othermodes, decoded coverage/blender flags, depth policy, final API blend, draw class, effect label, settex identity, and NDC bbox |
| `GE007_TRACE_RDP_RENDER_MODES_DRAWCLASS=name` / `GE007_TRACE_RDP_RENDER_MODES_EFFECT=label` | optional filters for `[RDP-MODE]`; use drawclass filters such as `room` or `effect`, and effect labels such as `glass_shards`, to keep broad render-mode captures bounded |
| `GE007_TRACE_ROOM_ALPHA=1` | log the first alpha-blended room triangles each frame, including raw/effective render mode, decoded N64 coverage/blender fields (`mode_decode`), depth flags, texture-edge classification, alpha sources, room attribution source, vertex/modelview/display-list rooms, room-DL command offset, and post-LUT combiner/options (`ROOM-ALPHA-CC`) |
| `GE007_TRACE_DRAWCLASS_BBOX=1` / `GE007_TRACE_DRAWCLASS_AFTER_FRAME=N` | log per-frame NDC and logical-screen bboxes for emitted triangles by draw class (`unknown`, `room`, `weapon`, `chrprop`, `effect`, `hud`); use with `GE007_TRACE_DRAWCLASS_TRIS=1` when separating foreground, room-alpha, effect, and HUD footprints |
| `GE007_BGORDER_PORTAL=0|1` | toggle the native `bgOrderPortal` room-direction pass; default `1` mirrors stock portal ordering, while `0` is an A/B control for pre-ordering room traversal |
| `GE007_BG_PORTAL_AABB_EXPAND=0|1` | toggle room AABB expansion from connected portal vertices; default `1` mirrors stock setup, while `0` is a focused room-bound A/B control |
| `GE007_PORTAL_BACKFACE_PROJECT_FALLBACK=0|1` | toggle projected-visible backface traversal in native portal BFS; default `0` keeps the old broad native fallback opt-in only |
| `GE007_PORTAL_PARENT_CLIP_MIN_SPAN=N` | skip parent-bbox clipping only when the parent window is narrower/shorter than `N`; default `0` means stock-style parent clipping always applies |
| `GE007_PORTAL_ACCEPTED_MIN_SPAN=N` | expand accepted portal bboxes to at least `N` pixels for A/B only; default `0` disables the old native widening that could admit stock-rejected portals |
| `GE007_PORTAL_ACCEPTED_PAD=N` / `_X=N` / `_Y=N` | pad accepted portal bboxes by a fixed screen-space margin for A/B; default `0`, clamped to the current screen bounds |
| `GE007_PORTAL_RETRY_SCREEN_CLIP=1` | retry portal screen clipping without the parent bbox after an empty clip for A/B only; default off |
| `GE007_PORTAL_LEGACY_PROJECT_CLAMP=1` | restore the old native `sub_GAME_7F0B5864` pre-clamp behavior for legacy over-admission A/B only; default returns stock-style raw projected bboxes |
| `GE007_PORTAL_NEAR_CLIP=0|1` | toggle native portal near-plane epsilon clipping; default `1` avoids huge near-plane projection values while preserving stock-style bbox rejection through caller clipping |
| `GE007_PORTAL_EDGE_RESCUE=0|1` | toggle the default native portal-edge rescue for rooms rejected because the projected portal aperture collapsed or failed; empty-bbox rescues can continue through connected portals with a bounded screen rect, while project-fail rescues default to trigger-only fallback handling |
| `GE007_PORTAL_EDGE_RESCUE_CONTINUE=0|1` / `GE007_PORTAL_EDGE_RESCUE_MIN_SPAN=N` / `GE007_PORTAL_EDGE_RESCUE_BACKFACE_FALLBACK=0|1` | tune continuation for collapsed portal-edge rescues; defaults keep continuation enabled with a 24px minimum continuation bbox and projected backface fallback only while draining rescued continuation portals |
| `GE007_PORTAL_PROJECT_FRUSTUM_FALLBACK=0|1` / `GE007_PORTAL_PROJECT_FRUSTUM_FALLBACK_MAX_EXTRA=N` | toggle guarded frustum rebuild after a portal projection failure; default `1` rebuilds only when the frustum result is at most `N` rooms broader than the portal result (`12` by default), avoiding broad over-admission while fixing small missing-room corrections such as Streets pad 129 |
| `GE007_PORTAL_PROJECT_RESCUE=0|1` / `GE007_PORTAL_PROJECT_RESCUE_ADMIT=0|1` / `GE007_PORTAL_PROJECT_RESCUE_CONTINUE=0|1` | tune project-fail rescue detection; default detects projected destination AABBs, does not directly admit the failed destination, and does not continue from project-fail candidates |
| `GE007_AUTO_NEIGHBOR_ROOMS=0|1` | legacy alias for `GE007_PORTAL_EDGE_RESCUE` when the canonical variable is unset |
| `GE007_NO_CAMERA_SEED_WALK=1` | **A/B opt-out for the T13b camera-seed walk (now DEFAULT ON).** For a detached authored camera (INTRO/FADESWIRL/SWIRL) the visibility pipeline now *walks* the BFS from the camera's resolved room (plus the same edge-rescue / frustum-fallback / visibility-supplement passes) so the camera's sightline fills instead of rendering flat sky-blue (Silo intro headline defect). The walk is DECOUPLED from the sim: it reproduces the T13 rendered set, marks the rooms it added beyond the default set **draw-only** (they draw but are invisible to `getROOMID_isRendered` / `PROPFLAG_ONSCREEN` / the deterministic `pcRandom` stream), then restores `room_rendered` + `room_neighbor_to_rendered` to the default snapshot. Setting `=1` restores the pre-T13b admit-only behavior — negative control: Silo intro blue-clear pixels 132→3681, Dam ~54→4074, byte-identical to pre-T13b. `GE007_CAMERA_SEED_WALK` (the retired T13 opt-in) is still accepted but is a no-op. Complements `GE007_NO_CAMERA_SEED_FIX` (which disables the whole camera-room seed). |
| `GE007_TRACE_DRAW_ONLY=1` | log the per-frame draw-only room count (`[DRAW-ONLY] frame=N camera_seed_room=R draw_only_rooms=K`). `draw_only_rooms` is the number of rooms the camera-seed walk added to the draw list but not to the sim-visible `room_rendered` set; it is `0` in normal gameplay (only a detached authored camera seeds the walk, so `camera_seed_room=-1` off-intro) and nonzero only during INTRO/FADESWIRL/SWIRL cameras. Read-only perf/invariant probe. |
| `GE007_TRACE_ROOM_CLASSIFY=1` / `GE007_TRACE_ROOM_CLASSIFY_AFTER_FRAME=N` / `GE007_TRACE_ROOM_CLASSIFY_BUDGET=N` | after the frame's default room admission, dump the portal adjacency graph once and classify every loaded-but-unrendered frustum-visible room as `dropped` (portal-adjacent to a rendered room — the M3.4 aperture class) or `far` (only reachable through a multi-hop portal chain). Root-cause tool for blank-area/blue-room defects; read-only. `_BUDGET` frames default 3. |
| `GE007_DRAW_NEIGHBOR_ROOMS=1` | broad one-hop neighbor room diagnostic; remains opt-in because it admits every marked portal neighbor with a fullscreen bbox and can over-admit unrelated rooms |
| `GE007_VIS_SUPPLEMENT=0|1` / `GE007_VIS_SUPPLEMENT_MAX_EXTRA=N` / `GE007_VIS_SUPPLEMENT_MAX_AABB_GAP=N` / `GE007_VIS_SUPPLEMENT_MAX_UNPROJECTED_AABB_GAP=N` | default-on bounded visibility supplement; after normal portal/global visibility it admits a small number of frustum-visible rooms whose AABBs are adjacent to already-rendered rooms, catching room-owned floor/ceiling seams without rendering all rooms; unprojectable candidates default to a stricter 1-unit adjacency gate |
| `GE007_TRACE_VIS_SUPPLEMENT=1` | log each room admitted by the bounded visibility supplement, including nearest rendered room, AABB distance, and diagnostic projected bbox |
| `GE007_FORCE_ADMIT_ROOMS='25,...'` | diagnostic-only room draw-list override; adds listed rooms after normal portal visibility with the current fullscreen view bbox, useful for proving whether a missing visual surface belongs to a specific room |
| `GE007_TRACE_ROOM_PROJECT=1` / `GE007_TRACE_ROOM_PROJECT_ROOM='23,24,25'` / `GE007_TRACE_ROOM_PROJECT_AFTER_FRAME=N` / `GE007_TRACE_ROOM_PROJECT_BUDGET=N` | log whether selected rooms are frustum-visible, AABB-projectable, marked rendered, and present in the draw list before/after forced admits |
| `GE007_TRACE_PORTAL_VERTS=1` / `GE007_TRACE_PORTAL_VERTS_IDX=N` / `GE007_TRACE_PORTAL_VERTS_AFTER_FRAME=N` | log transformed/projected vertices for portal projection probes |
| `GE007_TRACE_BLOOD_ANIM=1` | log native ROM-backed gunbarrel blood animation decode frames, including packed stream offsets, finish state, nonzero texel count, and max decoded nibble |
| `GE007_TRACE_TINTED_GLASS=1` / `GE007_TRACE_TINTED_GLASS_BUDGET=N` | log tinted-glass setup/update/render opacity, including raw opacity, render opacity, and the active min-opacity floor |
| `GE007_TRACE_BULLET_IMPACTS=1` | log bullet-impact create/render events, including whether prop-attached impacts used the textured or legacy flat path |
| `GE007_TRACE_BULLET_IMPACT_MATERIALS=1` / `GE007_TRACE_BULLET_IMPACT_MATERIALS_EFFECT=label` / `GE007_TRACE_BULLET_IMPACT_MATERIALS_AFTER_FRAME=N` / `GE007_TRACE_BULLET_IMPACT_MATERIALS_BUDGET=N` | log concise post-classification material state for labelled bullet-impact effect triangles, including raw/effective render mode, blend/depth flags, texture keys, vertex colors, UVs, and NDC bbox; use the effect filter for `bullet_impact_world` or `bullet_impact_prop_textured` when one family would otherwise consume the budget |
| `GE007_EFFECT_RANGE_TRACE=1` | log effect display-list label range registration and execute-frame summaries; useful when `GE007_EFFECT_TRI_TRACE` does not show the expected effect label. Effect label lookup now prefers the narrowest overlapping range, so broad `glass` ranges do not shadow more specific labels such as `bullet_impact_prop_textured` |
| `GE007_TRACE_GUARD_OBJECT_SHOTS=1` / `GE007_TRACE_GUARD_OBJECT_SHOTS_CHRNUM=N` / `GE007_TRACE_GUARD_OBJECT_SHOTS_PAD=N` / `GE007_TRACE_GUARD_OBJECT_SHOTS_BUDGET=N` | log guard-fired object collisions before object damage is applied; use this to distinguish player shots from guard-owned glass/object damage |
| `GE007_TRACE_GUARD_BOND_SHOTS=1` / `GE007_TRACE_GUARD_BOND_SHOTS_CHRNUM=N` / `GE007_TRACE_GUARD_BOND_SHOTS_BUDGET=N` | log guard-to-Bond shot accumulator decisions, including aim gate, clock/delta, `shotbondsum`, damage amount, and Bond health before/after |
| `GE007_TRACE_SHARDS=1` / `GE007_TRACE_SHARDS_AFTER_FRAME=N` | log large projected triangles and pathological clipping candidates; effect display-list ranges are labelled (for example `effect=glass_shards`) so shard-specific artifacts can be separated from rooms, props, and guard geometry |
| `GE007_EFFECT_TRI_TRACE=1` / `GE007_EFFECT_TRI_TRACE_LABEL=glass_shards` / `GE007_EFFECT_TRI_TRACE_AFTER_FRAME=N` / `GE007_EFFECT_TRI_TRACE_BUDGET=N` | read-only per-effect-triangle trace with emit/reject state, render mode, other-mode high, combiner id, geometry flags, NDC bbox, UVs, raw VTX RGBA, post-light shade, and transform context; use label filters to keep logs bounded |
| `GE007_EFFECT_TRI_TRACE_UNLABELED=1` / `GE007_EFFECT_TRI_TRACE_DRAWCLASS=effect` / `GE007_EFFECT_TRI_TRACE_EMITS_ONLY=1` | focused add-ons for `GE007_EFFECT_TRI_TRACE`; allow unlabeled commands to be tagged as `unlabeled`, filter rows by draw class name, and preserve the trace budget for emitted triangles by suppressing reject rows |
| `GE007_KEEP_TEXTURE_GEN_FOG=1` | diagnostic negative control that preserves `G_FOG` when `G_TEXTURE_GEN` is active; useful for the startup Nintendo logo, where the default no-fog path is over-bright but full preserved fog is too dark. Do not use as a broad parity default without scoping the affected material path |
| `GE007_TRACE_TEXGEN_MATERIALS_EFFECT=label` | optional effect-label filter for `GE007_TRACE_TEXGEN_MATERIALS`; use `glass_shards` to keep room/pane texgen rows from consuming the budget before the active shard display list |
| `GE007_DUMP_LOADED_TEXTURES=list` / `GE007_DUMP_LOADED_TEXTURES_AFTER_FRAME=N` / `GE007_DUMP_LOADED_TEXTURE_LIMIT=N` / `GE007_DUMP_LOADED_TEXTURE_DIR=path` / `GE007_DUMP_LOADED_TEXTURES_BYPASS_CACHE=1` | dump decoded traditional TMEM texture imports as PPM/PGM/source/info files; use `*` for all keys, or a cache-key list/range for focused texture payload checks. Cache bypass is diagnostic-only and forces a fresh upload/dump for matching keys |
| `GE007_TRACE_BLEND_CLASSIFY=1` | log first-seen raw render-mode classifications |
| `GE007_BLEND_AUDIT=1` / `GE007_BLEND_AUDIT_INTERVAL=N` | summarize all raw render modes seen by the Fast3D renderer; use the `edge` column to catch accidental alpha-test classification |
| `GE007_TRACE_VEHICLE_AI=1` / `GE007_TRACE_VEHICLE_AI_BUDGET=N` | log vehicle AI commands as they bind authored paths and target speeds |
| `GE007_TRACE_VEHICLE_STATE=1` / `GE007_TRACE_VEHICLE_STATE_BUDGET=N` / `GE007_TRACE_VEHICLE_STATE_INTERVAL=N` | log per-frame vehicle path, speed, waypoint, and movement decisions; use the interval to reduce noise |
| `GE007_VERBOSE=1` | broad legacy diagnostic output, including the focused logs above |

Use `tools/summarize_rdp_render_mode_trace.py` on `[RDP-MODE]` logs before
adding another render-mode special case. The summary groups raw modes by final
native API blend, depth mode, draw class, room-matrix/display-list ownership,
combiner/options, `G_SETTEX` identity, material alpha, and coverage flags. It
also highlights unpromoted `ZMODE_XLU` + `CVG_DST_WRAP` + `CLR_ON_CVG` +
`IM_RD` + `FORCE_BL` candidates that are still using ordinary alpha blending,
with the `room_cvg_reason` gate (`ok`, `ok_generated_room`, `fog`, `envA`,
`dl_room`, and so on) explaining why the default classifier did or did not
promote each row.

`tools/compare_glass_shard_pixel_oracle.py` measures active-glass screenshot
deltas under individual traced shard-piece masks. It uses the same logical
viewport crop as `tools/compare_glass_projected_visual.py`, rasterizes each
stock/native projected triangle pair, reports overlap pressure, color buckets,
luma/saturation deltas, and top offending piece indices. This is a spatial
attribution tool, not causality proof that the falling-shard draw pass wrote the
changed pixels. The active-shard wrapper emits the compact artifact by default:

```sh
tools/glass_active_visual_isolation_regression.sh --no-build \
  --out-dir /tmp/mgb64_glass_active_visual
```

For complete per-piece evidence, run the wrapper with all-sample projection
traces enabled on both native and stock ares:

```sh
GE007_TRACE_GLASS_PROJECTION_ALL=1 \
MGB64_ARES_TRACE_GLASS_PROJECTION_ALL=1 \
tools/glass_active_visual_isolation_regression.sh --no-build \
  --out-dir /tmp/mgb64_glass_shard_pixel_oracle_full
```

The local instrumented ares binary for this workspace is
`build/ares-movement-oracle/ares/build-movement-oracle/desktop-ui/ares.app/Contents/MacOS/ares`.
The first full-sample proof in `/tmp/mgb64_glass_shard_pixel_oracle_full`
shows `90/90` sampled pieces, `82` onscreen/rasterized triangle masks,
`89.980%` changed pixels in the shard-covered union, and a native near-white
bucket jump of `78 -> 1090`. The 2026-06-27 control changes how to use that
result: default native versus `GE007_GLASS_SHARDS=0` is byte-identical at the
screenshot level (`0/307200` changed) and `0.000%` changed under the same shard
masks in
`/tmp/mgb64_shards_off_native_active/default_vs_shards_off_shard_pixel_oracle.json`.
So the oracle proves co-location with projected shards, not that the default
falling-shard pass caused the pixels. Always pair stock/native shard-mask
findings with a native default-vs-`GE007_GLASS_SHARDS=0` control before assigning
ownership.

`tools/select_glass_shard_pixel_targets.py` bridges the projected-shard mask
oracle to stock ares single-pixel probes. It loads the same stock/native
projection traces and logical-viewport screenshots, rasterizes common projected
pieces, ranks changed covered pixels by RGB/luma disagreement, and emits stock
RDP logical targets:

```sh
python3 tools/select_glass_shard_pixel_targets.py \
  --baseline-trace /tmp/.../stock_dam_regular_glass_shatter_pad10092_impact_visual_probe.jsonl \
  --test-trace /tmp/.../native_dam_regular_glass_shatter_pad10092_impact_visual_probe.jsonl \
  --baseline-image /tmp/.../stock_dam_regular_glass_shatter_pad10092_impact_visual_probe.ppm \
  --test-image /tmp/.../native_dam_regular_glass_shatter_pad10092_impact_visual_probe.bmp \
  --route tools/rom_oracle_routes/dam_regular_glass_shatter_pad10092_impact_visual_probe.json \
  --top 12 \
  --json-out /tmp/shard_pixel_targets.json
```

The 2026-06-28 pad-`10092` recheck used this to catch a false lead: the
`projected_impact` ROI had `0` shard-mask candidate pixels, and the previous
center target was finally owned by an opaque/clamp room draw rather than
`0x0C1849D8` glass shards. Whole-mask selection produced concrete shard-covered
targets such as `149,209`, `201,207`, and `132,212` for bounded stock RDP pixel
probes. Use those selected targets before making shard-pixel ownership claims.

(P2 hygiene reap, FID-0005: `tools/compare_glass_shard_draw_trace.py`, the
forensic join from the pixel oracle's top offending shard pieces to native
`[EFFECT-TRI]`/`[TEXGEN-MATERIAL]` rows, was a one-off script and has been
removed. Preserved finding: the join proved the top pixel-oracle offenders are
normal `glass_shards` material draws — `cvg=wrap`, `clr_on_cvg=1`, raw mode
`0x0C1849D8`, combiner `0x00F38E4F020A2D12`, expected `56x54`/`32x27` texture
tiles, no CPU clipping, and a final-cycle formula matching ares/Parallel-RDP's
decoded blend path — and that a GL-side active-shard coverage-memory
approximation changes only `82/307200` pixels versus default native while
leaving the stock/native shard-mask delta at `89.980%`. Treat GL-side
active-shard coverage-memory approximation as a checked negative; do not
re-derive it without first proving the shard pass is visibly contributing.)

`GE007_SFX_TRACE_JSONL` uses public SFX ids for `public_sound` and one-based
`bank_public_sound`; `bank_sound` is the zero-based decoded-bank index. Owner
cleanup appears as `sndp_owner_clear` / `snd_owner_clear`, while stale handle
defense emits `owner_stale_clear`, `state_position_ignored`, or
`sndp_post_ignored`.

`GE007_AUDIO_TRACE` reports final mixed PCM health after music and SFX are
combined. `output_peak` is the absolute peak sample value for the frame
(`32768` means the negative rail was hit), and `output_rail_hits` counts samples
that landed exactly on `-32768` or `32767`. Sustained nonzero rail hits are a
strong signal of clipping or bad gain staging; occasional single hits during
explosions are worth comparing against a known-good baseline before treating as
a regression. Mixer counters such as `adpcm_dec_calls`, `resample_calls`,
`env_mixer_calls`, `mix_calls`, and `pole_filter_calls` identify which native
RSP-audio command families ran during the capture. `env_sample_xor` and
`pole_sample_xor` record the active ABI1 sample-lane diagnostics for that run;
the default is `0` unless a `GE007_MIXER_*` override is set. The
`*_clamp_hits` mixer fields are cumulative sample-saturation counters per
command family; the matching `*_clamp_delta` fields report only the current
audio frame. Use the deltas with `output_rail_hits` to distinguish final-output
rail hits from earlier ADPCM, resample, envmixer, aux-return mix, or pole-filter
saturation.

Relevant `ge007` command-line flags: `--rom PATH`, `--level N`
(33=Dam, 34=Facility, 24=Archives, 36=Surface 1), `--deterministic`,
`--background`, `--no-input-grab`, `--trace-state path.jsonl`,
`--screenshot-frame N` / `--screenshot-label L` / `--screenshot-exit`.

For headless split-screen multiplayer validation there is a parallel
direct-boot path: `--multiplayer` enters a match directly (bypassing the
frontend), `--players N` sets the player count (2-4), `--mp-stage NAME-or-id`
picks a multiplayer stage (`temple`, `complex`, `caves`, `library`, `basement`,
`stack`, `facility`, `bunker`, `archives`, `caverns`, `egypt`, or `random`; a
raw `MP_STAGE` index also works), and `--scenario NAME-or-id` selects the combat
mode (`normal` aka `deathmatch`/`combat`, `yolt`, `flagtag`, `goldengun`, `ltk`,
`2v2`, `3v1`, `2v1`). Passing any of `--players`, `--mp-stage`, or `--scenario`
implies `--multiplayer`. The MP direct-boot honors the same deterministic env
knobs as the solo path (`GE007_DETERMINISTIC*`, `GE007_AUTO_*`, the screenshot
and `--trace-state` hooks), so a 2-player run is scriptable:

```sh
env SDL_AUDIODRIVER=dummy GE007_MUTE=1 GE007_NO_VSYNC=1 \
  GE007_BACKGROUND=1 GE007_NO_INPUT_GRAB=1 \
  /path/to/build/ge007 --rom /path/to/baserom.u.z64 \
  --multiplayer --players 2 --mp-stage temple --scenario normal \
  --deterministic --trace-state mp_state.jsonl \
  --screenshot-frame 120 --screenshot-label mp_temple_2p \
  --screenshot-exit > run.log 2>&1
```

### Transparent room material probe

Glass, water, and similar authored level geometry live in secondary room display
lists. When these vanish while collision still works, first prove whether the
secondary room DL is missing or whether Fast3D classified the material wrong:

```sh
tmpdir="$(mktemp -d /tmp/mgb64_glass_probe.XXXXXX)"
cd "$tmpdir"
env SDL_AUDIODRIVER=dummy GE007_MUTE=1 GE007_NO_VSYNC=1 \
  GE007_BACKGROUND=1 GE007_NO_INPUT_GRAB=1 \
  GE007_TRACE_BG_ROOM_PTRS=1 GE007_TRACE_ROOM_ALPHA=1 \
  GE007_TRACE_BLEND_CLASSIFY=1 GE007_BLEND_AUDIT=1 \
  GE007_BLEND_AUDIT_INTERVAL=120 \
  /path/to/build/ge007 --rom /path/to/baserom.u.z64 \
  --level dam --deterministic \
  --screenshot-game-timer 220 --screenshot-label dam_glass_probe \
  --screenshot-exit > run.log 2>&1
```

The important `BLEND-AUDIT` invariant is that true XLU surface modes such as
`0x0C1849D8`, `0xC41049D8`, and `0x00504240` remain `ALPHA` but report
`edge no`. Explicit TEX_EDGE/cutout modes should still report `edge yes`. If
`BG-ROOM-PTR` shows secondary pointers as null for rooms with nonzero secondary
sizes, investigate room loading before touching renderer state. `ROOM-ALPHA`
uses vertex room ids when available, falls back to the active modelview room,
then the current room display-list range, so secondary-DL glass can still be
attributed even when runtime vertices carry `room_id=-1`.

For stock/native room-material parity, pair the native `ROOM-ALPHA` log with the
stock trace's `rooms.dl` array. Matching env-alpha lists and room-buffer hashes
prove the secondary room data exists on both sides, but any remaining mismatch
should move to inherited draw-time state such as fog, effective othermode,
combiner packing, texture sampling, depth compare/update policy, or translucent
draw order.

For visual parity probes that need the same gameplay state across branches, use
`--screenshot-game-timer N` instead of `--screenshot-frame N`. The frame trigger
counts SDL/render syncs and can fire before comparable player simulation time if
startup, intro, or loading behavior differs. The game-timer trigger captures once
`g_GlobalTimer >= N`, so branch-to-branch screenshots line up on the same mission
tick.

### Glass material probe

`tools/glass_material_regression.sh` is the focused Dam material regression for
tinted-glass opacity, prop-attached bullet impacts, and regular-glass shatter
health. It captures:

- tinted pane `10059` from a near pad-10060 offset, faced directly, with the
  default min opacity;
- the same tinted pane with `GE007_TINTED_GLASS_MIN_OPACITY=96`, restoring the
  old cloudy floor as a negative control;
- a Dam pad-100 prop impact with the default textured prop-impact path;
- the same prop impact with `GE007_FLAT_PROP_BULLET_IMPACTS=1`;
- a deterministic gameplay bullet shot from pad `103` into Dam regular glass,
  with `GE007_TRACE_GLASS=1`, `GE007_TRACE_SHARDS=1`, and object visibility
  trace for pad `10003`.

```sh
tools/glass_material_regression.sh --no-build \
  --out-dir "$(mktemp -d /tmp/mgb64_glass_material_regression.XXXXXX)"
```

A healthy run requires the near tinted pane to log raw `opacity=0` and
`renderOpacity=16` by default, then `renderOpacity=96` in the legacy-floor A/B.
The prop-impact half requires a pad-100 prop impact render with `flat=0` by
default and `flat=1` only when the legacy override is set. The regular-glass
shot requires a `GLASS-SHATTER` event in the expected fire window, a `10x9=90`
shard grid, sustained full-active shard trace frames, and clean render/screenshot
health.

The same regular-glass pass also guards active shard material state. It requires
at least `100` emitted `effect=glass_shards` material rows, at least `20`
`TEXGEN-MATERIAL` rows, raw/effective render mode `0C1849D8`,
`other_mode_h=00992C60`, combiner `00F38E4F020A2D12`, geometry mode `00060205`,
depth tuple `1,0,1,0,0x800`, and texture dimensions `56x54,32x27`. It also
fails on screen-spanning shard-effect geometry, any preclip pathological
`glass_shards` candidate, material triangles larger than the bounded active
shard envelope, too-short captures, or `[GFX-DL]` diagnostic rows. Treat this as
the native-only milestone-1 gate for Dam glass work. Stock-ROM
visual parity for crack decals, active shard presentation, bullet pass-through,
and pane disappearance should be investigated with the stock-backed
`dam_regular_glass_shatter_visual_probe` route plus the strict
`dam_regular_glass_shatter_probe` state route.

### Glass route parity probe

`tools/glass_route_parity_regression.sh` is the stock/native milestone-2 gate
for Dam regular-glass route parity. It runs the shatter-state route and the
stricter RNG-isolation route through the local instrumented ares oracle, then
audits the generated comparison JSON:

```sh
tools/glass_route_parity_regression.sh --no-build \
  --out-dir "$(mktemp -d /tmp/mgb64_glass_route_parity.XXXXXX)"
```

A healthy run requires clean native render-health, stock oracle trace control,
`90 -> 90` active shards, first shard timer `1 -> 1`, first shard position delta
`0`, prop position delta `0`, destroyed/removed pad `10004 -> 10004`, and, on
the shatter-state route, world-impact center delta within `5` units. The
RNG-isolation half additionally requires first active sampled shard parity with
`max_numeric_delta=0`.

The wrapper writes local route copies with `stock_require_first_gameplay_global`
empty and then gates semantic glass invariants itself. This is deliberate: the
state routes pass on both observed stock startup cadences, while the old visual
route remains sensitive to actor/HUD/shot-selection phase. Do not apply this
relaxation to a screenshot pixel route unless its pre-pixel actor, health/HUD,
impact, and shard-phase guards also pass.

`tools/glass_visual_oracle_regression.sh` is the stock/native milestone-3 gate
for the clean Dam static-glass visual oracle. It runs `dam_glass_visual_probe`
through the local instrumented ares oracle and then audits the generated
capture, health, actor, and screenshot-comparison JSON:

```sh
tools/glass_visual_oracle_regression.sh --no-build \
  --out-dir "$(mktemp -d /tmp/mgb64_glass_visual_oracle.XXXXXX)"
```

A healthy run requires clean native render-health, stock first gameplay global
`1146`, stock force-player and stan-resolution coverage, healthy stock/native
screenshots, full Bond health with inactive damage/health HUD timers, no active
glass shards or impact state, rendered actor-composition parity for chr `10`,
`11`, and `12` at the actual screenshot checkpoints, no `[GFX-DL]` log rows,
and broad scene-sanity visual ceilings for the whole logical viewport and the
`center_glass`, `left_room`, and `pp7_hud` regions. This gate proves the visual
fixture is clean and repeatable; it intentionally does not claim active-shard
pixel parity or solve the known material/presentation delta.

### Glass active visual isolation probe

`tools/glass_active_visual_isolation_regression.sh` is the stock/native
milestone-4 gate for first-active Dam regular-glass renderer isolation. It runs
`dam_regular_glass_shatter_rng_visual_probe` through the local instrumented ares
oracle, then audits the generated state, health, glass, actor-note, and visual
comparison JSON:

```sh
tools/glass_active_visual_isolation_regression.sh --no-build \
  --out-dir "$(mktemp -d /tmp/mgb64_glass_active_visual_isolation.XXXXXX)"
```

A healthy run requires clean native render-health, stock first gameplay global
`1146`, stock force-player and stan-resolution coverage, healthy screenshots,
full Bond health with inactive damage/health HUD timers, no `[GFX-DL]` log rows,
`90 -> 90` active shards, first shard timer `1 -> 1`, first shard position delta
`0`, destroyed/removed pad `10004 -> 10004`, prop-position delta `0`, and first
active sampled shard parity with `max_numeric_delta=0` and zero mismatches. The
script also requires the screenshot comparator to run and stay inside broad
sanity ceilings for the whole logical viewport, masked aggregate,
`glass_burst`, `damage_arc`, and `hud_viewmodel` regions.

Passing this gate means route control and first-active shard state are clean
enough to inspect active-shard presentation. It is not a final active-shard
pixel-parity gate. Actor composition is reported in
`glass_active_visual_isolation_summary.json` as a note rather than a failure,
because the current pad-`10004` view still has stock/native foreground actor
differences. Select a cleaner pane/view or isolate actor composition before
tightening pixel thresholds for final active-shard parity.

The wrapper also reports `impact_lifecycle` in
`glass_active_visual_isolation_summary.json`. This is intentionally report-only:
a dirty impact lifecycle means the route still answers shard
projection/material/containment questions, but cannot be used for final
pane/crack/decal pixel parity. Current proof
`/tmp/mgb64_glass_active_visual_crosshair_parity` passes the route after the
native forced-crosshair coordinate was adjusted to the stock-equivalent
`158:114`, while stock remains at `159:114`. It still reports
`impact_lifecycle=status=dirty`: checkpoint occupancy is `0 -> 1`, first impact
relative to first active glass is `+6 -> -36` frames, but first world-impact
center delta is now `0.000`. Treat that as a fixture-cleanliness finding, not a
renderer failure.

When investigating the active-shard presentation gap, add
`GE007_EFFECT_TRI_TRACE=1 GE007_EFFECT_TRI_TRACE_LABEL=glass_shards` and
`GE007_TRACE_TEXSELECT=1` to the visual route. The current known-good diagnostic
shape is bounded geometry with saturated raw VTX colors and grayscale post-light
shade under `G_LIGHTING|G_TEXTURE_GEN`, using the material tuple guarded above.
If a future run shows large emitted areas, missing raw colors, or a changed
material tuple, treat that as a different class of regression.
Check the route trace's `glass.sample`, `combat.crosshair`, and `combat.health`
checkpoint before interpreting burst-color or viewport-arc screenshot deltas.
The visual route now forces stock crosshair center and keeps a stable chr `12`
actor guard on passing active-glass captures, but the alternate stock
target-stage origin can still take the non-damaging local prop-impact path on
the same pane (`PROPDEF_GLASS`, object `104`, pad `10004`) before the world
impact, so shards never activate on that stock branch. It also carries
stock/native shard rotation, velocity, vertex-size, and damage-HUD phase
differences. Use
`dam_regular_glass_shatter_rng_isolation_probe` when a hard shard-state gate is
needed. The colored viewport arc is currently classified as HUD/damage
presentation noise unless a trace proves it is shard-owned.
The active-shard visual route also carries a `compare_actor_chrnums` guard for
chr `12`; it intentionally checks stable gameplay/composition fields, not
volatile presentation bits such as `rendered`. A failing
`actor_compare_<route>.json` means the screenshot is still composition
contaminated and should not be treated as a hard renderer oracle. Actor
position failures now include `dx`, `dy`, `dz`, and horizontal `xz` deltas in
both the comparator log and JSON so route work can distinguish horizontal
foreground drift from vertical root/floor differences. The Dam active-shard
visual route uses the screenshot-frame mode; if it fails, read
`baseline_frame`/`test_frame` and `baseline_global`/`test_global` before
adjusting screenshot timing.

### Glass impact visual isolation probe

`tools/glass_impact_visual_isolation_regression.sh` is the stock/native
pane/crack/decal companion to the first-active shard gate. It runs
`dam_regular_glass_shatter_rng_impact_visual_probe`, which screenshots the same
pad-`10004` shatter later in the burst: stock frame `2437` and native frame
`113`. This retiming keeps the exact first-active shard sample parity from the
RNG-isolation route, but aligns the screenshot checkpoint with active world
bullet-impact state on both sides.

```sh
tools/glass_impact_visual_isolation_regression.sh --no-build \
  --out-dir "$(mktemp -d /tmp/mgb64_glass_impact_visual.XXXXXX)"
```

This wrapper keeps the strict stock `first_gameplay_global=1146` route-control
precondition. It defaults `MGB64_ARES_VIDEO_BLOCKING=true` and
`MGB64_ARES_STOCK_ROUTE_CONTROL_RETRIES=8` for this cadence-sensitive stock
ares lane, while still honoring explicit environment overrides. Do not relax the
`1146` guard to accept `1147`: `/tmp/mgb64_glass_impact_relaxed_first_global_probe`
proved that branch can match health while having no active shards and no
destroyed pane. A temporary global-timer fire variant also broke the pane, but
was rejected because it shifted shard age and the impact center delta to
`9.079`, outside the `5.0` gate.

A healthy run requires clean native render-health, stock first gameplay global
`1146`, healthy screenshots, no `[GFX-DL]` log rows, full Bond health with
inactive damage/health HUD timers, `90 -> 90` active shards, first shard timer
`6 -> 6`, first shard position delta `0`, destroyed/removed pad
`10004 -> 10004`, exact first-active shard-sample parity, and impact comparison
`status=pass` with world impact center delta below `5.0`. The wrapper also
checks the screenshot checkpoint's selected first world-impact sample directly
and runs a projection comparison at stock frame `2437` / native frame `113`.

Current proof `/tmp/mgb64_glass_impact_checkpoint_search_focused` passes
with
projection parity `90 -> 90` active/projected shards, `86 -> 86` onscreen,
`0 -> 0` behind, max area `8.123% -> 8.123%`, union area
`65.862% -> 65.892%`, impact center delta `0.000`, and visual sanity metrics
`whole=89.162%`, `masked=88.512%`, `glass_burst=86.292%`,
`damage_arc=87.300%`, `hud_viewmodel=93.498%`. This is still not final pixel
parity, but it is now the better fixture for pane break, crack/decal, and
presentation-order work because the first-active route's impact lifecycle is
dirty by design.
The same summary now records the report-only actor-composition guard under
`visual_oracle`: `status=dirty` and
`usable_for_production_pixel_fix=false`. The screenshot checkpoint has stock
visible actors `[10,12]` and native visible actors `[10,12,45]`; chr `10`
differs on `onscreen`, chr `12` differs on `hidden_bits` and `action`, and chr
`12` position delta is `46.926`. Treat broad screenshot changed-percent as an
ownership/composition signal until that guard is clean or the comparison uses a
localized actor-masked impact oracle.
The same run now writes a localized report-only impact/decal pixel oracle under
`impact_pixel_oracle`. That oracle is stable but not promotable:
`status=masked_dirty`, `usable_for_production_pixel_fix=false`,
`impact_focus.masked_changed_pct=82.972`, and
`impact_focus.masked_excluded_pct=59.099` because the stock guard occluder must
hide most of the focus region. The unoccluded left-edge ROI still changes
`91.170%` with bright pixels `270 -> 73`, so this remains a report-only
composition signal, not a production pixel-fix gate.
The wrapper also writes a geometry-derived projected decal oracle with
`tools/compare_projected_impact_visual.py`. The pre-fix proof
`/tmp/mgb64_glass_impact_projected_oracle_focused` showed the selected world
impact quad matched in world space (`max_point_delta=0.000`) but projected
`51.370px` away on native. The fixed proof
`/tmp/mgb64_glass_impact_checkpoint_search_focused` keeps that projected center
delta to `0.055px` and hard-gates it at `<=1.0px`; its ROI remains report-only
for pixels (`changed=75.632%`) because the surrounding glass/actor presentation
is still dirty.
The same summary records `checkpoint_candidate_search`, generated by
`tools/score_impact_checkpoint_candidates.py`. The current focused proof scanned
`744` active impact checkpoint pairs, found `0` strict clean matches, and kept
the best projected decal center delta at `0.055px`; the best pair is still
actor-dirty because native visible actor `45` is present. That means this route
is useful for world-impact/decal geometry, but not for production pixel parity
until a cleaner route/view/mask exists.
The wrapper summary records report-only full impact-quad metrics under
`impact.quad_report` and creation provenance under `impact.creation_report`.
The native shot and native bullet-impact creation both occur on frame `69`; the
traced world ray is `[0.010734,0.021102,0.999720]`. Stock first observes the
same world impact at trace frame `2437`; native first observes it at trace frame
`72`. All four selected world-impact vertices now match stock exactly:
`max_point_delta=0.000`, `impact_center_delta=0.000`, and
`rounding_report.exact_vertex_match=true`.

The projected-impact pixel comparator can also be run directly against an
existing capture:

```sh
python3 tools/compare_projected_impact_visual.py \
  --baseline-frame 2437 \
  --test-frame 113 \
  --logical-size 320,240 \
  --logical-viewport 0,10,320,220 \
  --baseline-logical-frame active \
  --test-logical-frame full \
  stock_trace.jsonl native_trace.jsonl stock.ppm native.bmp
```

The fix is route-coordinate parity, not a global bullet-impact math change. A
native-only crosshair sweep showed that native `GE007_AUTO_CROSSHAIR_SCRIPT`
`158:114` produces the same stored impact quad as stock
`MGB64_ARES_CROSSHAIR_SCRIPT` `159:114`; native `159:114` was the source of the
old `3.026` center delta / `4.280` quad delta. Keep this coordinate split local
to the forced Dam pad-`10004` visual routes unless another route proves the same
screen-coordinate convention mismatch.

Do not re-add `GE007_AUTO_AIM_DIR_SCRIPT=69:0:0:1` to this route. The older
route-only aim script began at frame `70`, after the actual frame-`69` shot, so
it was a no-op. The negative proof `/tmp/mgb64_glass_impact_visual_aimdir69`
forced `+Z` on the real shot frame and moved the native impact to room `109`,
shifted shard timing, and failed the impact route. The accepted checked-in route
therefore relies on the natural stock-like screen ray and records that ray in
the summary.

Umbrella proof `/tmp/mgb64_dam_visual_suite_after_actor_probe` passes the
full Dam visual suite with the same hardened impact wrapper. The suite also
reconfirms the draw-distance/tunnel, effect texture, muzzle/explosion color,
Dam palette/alarm-red, glass material, and actor-masked glass gates.

### Glass contributor isolation probe

(P2 hygiene reap, FID-0005: `tools/glass_contributor_isolation_regression.sh`
and the ~18 one-off pad-10092/pad-10001/pad-10004 scout and probe scripts built
alongside it for this investigation — `glass_pad10092_room_draw_isolation.sh`,
`glass_pad10092_actor_ownership_isolation.sh`,
`glass_pad10092_presentation_alignment_probe.sh`,
`glass_pad10092_pixel_semantics_probe.sh`,
`glass_pad10092_texgen_roi_material_probe.sh`,
`glass_pad10092_room_glass_visibility_probe.sh`,
`glass_pad10092_room_glass_scalar_oracle_probe.sh`,
`glass_pad10092_room_glass_required_source_probe.sh`,
`glass_pad10092_room_glass_source_recon_probe.sh`,
`glass_pad10092_room_glass_pixel_oracle_probe.sh`,
`glass_pad10092_room_glass_settex_sample_probe.sh`,
`glass_pad10092_texgen_roi_pixel_correlation_probe.sh`,
`glass_pad10092_roi_pixel_semantics_probe.sh`,
`glass_pad10092_impact_sequence_scout.sh`, `glass_actor_clean_candidate_scout.sh`,
`glass_native_fixture_scout.sh`, `score_native_glass_fixture.py`, and
`analyze_room_glass_contribution_scalar.py` — have been removed as spent
one-off tooling (docs/BACKLOG_v0.4.0.md MG.5). The ~800 lines of exploratory
forensic narrative that previously lived in this section are preserved in git
history (see the repo log for this file around the reap commit) for anyone who
needs the full transcript.

Net finding, preserved here because it drove a landed fix: an extensive
native-only ownership sweep across the Dam active, impact, and pad-`10092`
impact fixtures — shard suppression, bullet-impact suppression,
weapon-render/fog toggles, blend/depth/alpha-scale variants, RDP
memory/coverage-memory approximations, and a per-pixel source/opacity
reconstruction of texnum `654` — ruled out every simple global toggle as a
pad-`10092` pixel-parity fix and localized the remaining mismatch to exact
room-glass `G_SETTEX` output composition. That investigation directly produced
the landed fix recorded in `docs/RENDERING_REGRESSION_NOTES.md` item 13 (room
`G_SETTEX` LOD endpoints are draw semantics, not matrix semantics;
`GE007_DISABLE_SETTEX_FOOTPRINT_LOD=1` is the focused A/B), which remains
guarded by the still-wired ctest
`python3 tools/check_room_glass_source_reconstruction_regression.py`. Full
pixel parity on the pad-`10092` tower-pane/impact-side ROIs remains open and is
tracked as presentation-parity work in
`docs/design/remaster-aaa/04-content-geometry-effects.md` (W4.E2), which keeps
the two still-live probes below current: `glass_active_visual_isolation_regression.sh`
and `glass_pad10092_impact_visual_regression.sh`.
For that branch, use `tools/glass_actor_masked_visual_regression.sh`. It runs
the permanent first-active route
`dam_regular_glass_shatter_pad10092_actor_masked_visual_probe` with
`movement_oracle_capture.sh --no-compare`, then applies the required checks in
the correct order:

```sh
tools/glass_actor_masked_visual_regression.sh --no-build \
  --out-dir "$(mktemp -d /tmp/mgb64_glass_actor_masked_visual.XXXXXX)"
```

The wrapper checks native render health, stock route control, screenshot health,
no `[GFX-DL]` warnings, health/HUD parity at stock frame `2541` and native trace
frame `126`, pad-`10092` glass state (`active=88 -> 88`, first timer
`1 -> 1`, first position delta `0`, prop-position delta `0`), and actor
composition scoring. It intentionally does not require strict actor parity:
chr `7` onscreen drift and chr `44` position drift are reported in the summary
JSON.

`tools/compare_actor_masked_visual.py` is the focused screenshot comparator used
by that wrapper. It reuses the logical-viewport crop model from
`compare_screenshots.py`, then reports full and masked metrics for named ROIs:

```sh
python3 tools/compare_actor_masked_visual.py \
  --logical-size 320,240 \
  --logical-viewport 0,10,320,220 \
  --baseline-logical-frame active \
  --test-logical-frame full \
  --region tower_pane:80,115,320,180 \
  --region impact_side:255,145,120,95 \
  --exclude-region lower_actor_cluster:145,160,215,125 \
  --exclude-region hud_viewmodel:360,300,255,130 \
  stock.ppm native.bmp
```

Latest proof: `/tmp/mgb64_shard_draw_join_dam_visual_suite2` passes the umbrella
Dam visual suite, including this actor-masked lane. The actor-masked artifact
writes `glass_actor_masked_visual_summary.json`, an actor-masked heatmap, and
`projection_*.json`. Key visual metrics are report-only because this fixture has
known actor/composition drift; the hard pass/fail signal is health, glass state,
actor mask coverage, and projection coverage.

This route intentionally does not require one exact stock first-gameplay global.
Current ares startup cadence can enter the same state-valid fixture through
either the `1147` or `1149` branch. The wrapper therefore validates the
stock/native screenshot checkpoint at stock frame `2541` and native frame `126`
instead of treating this actor-masked route as the first-active timer-1 proof.
The strict first-active proof remains
`tools/glass_active_visual_isolation_regression.sh`. Current projection proof is
`active/projected/onscreen=88 -> 88`, max area `0.206% -> 0.244%`, and union
`6.860% -> 6.119%` under native `scale=inv_vis_full`. This is a guarded
catastrophic-regression harness for active-shard visuals; it is not final
active-shard pixel parity.

For a single Dam-focused visual regression pass over the current sprint scope,
use `tools/dam_visual_regression_suite.sh`:

```sh
tools/dam_visual_regression_suite.sh --no-build \
  --out-dir "$(mktemp -d /tmp/mgb64_dam_visual_suite.XXXXXX)"
```

It runs, in order, `camera_basis_regression.sh`,
`dam_tunnel_visibility_regression.sh`, `effect_texture_regression.sh`,
`dam_palette_regression.sh`, `glass_material_regression.sh`, and
`glass_actor_masked_visual_regression.sh`, then
`glass_impact_visual_isolation_regression.sh`, and writes `index.tsv` and
`summary.json`. Latest proof:
`/tmp/mgb64_dam_visual_regression_suite_current` passed all seven gates.

### Bunker brightness probe

`tools/bunker_brightness_regression.sh` guards the reported Bunker-darkness
class without pretending Bunker should be bright. It pins Bunker 1 to an
isolated savedir and faithful 640x480 non-HiDPI video settings, enables the
fail-closed fidelity-capture aspect contract, then verifies the default capture
is not blank/pathologically black while render-health counters, room count, and
triangle count stay sane. It also captures a bright remaster A/B so the metric
proves sensitivity to saved visual-profile changes:

```sh
tools/bunker_brightness_regression.sh --no-build \
  --out-dir "$(mktemp -d /tmp/mgb64_bunker_brightness.XXXXXX)"
```

Current proof `/tmp/mgb64_bunker_brightness_current_guard` passes with default
center luma `67.71`, `0.00%` center black pixels, `7` rendered rooms, `1540`
triangles, and bright-A/B center luma `127.81`.

### Effect texture probe

`tools/effect_texture_regression.sh` is the focused guard for the purple
muzzle-flash and explosion-smoke regression. It first runs a direct first-person
Dam firing capture, dumps the visible muzzle `settex` textures `2157..2160`,
and audits decoded RGB/alpha for warm non-purple output. It then replays the Dam
regular-glass visual route with texture pipeline and display-cast material
tracing enabled, and checks the native log for static texture cache keys:

```sh
tools/effect_texture_regression.sh --no-build \
  --out-dir "$(mktemp -d /tmp/mgb64_effect_texture_regression.XXXXXX)"
```

A healthy run requires visible muzzle textures `2157..2160` to decode as 32x32
warm sprites with active alpha and low purple/low-green counts. It also requires
`tag=muzzle_flash img=2128 resolved_img=2128` and a matching `TEX-LOADBLOCK`
with `cache=0x8000000000000850 static=1 lods=1`, which proves the route-selected
muzzle texture still follows static texture provenance. Finally, it requires
explosion-smoke frames `2176..2181` to upload through
`0x8000000000000880..0885` with `static=1 lods=0`, dumps the decoded IA8 payloads,
and checks sampled smoke material rows for alpha texture passthrough with sane
warm-white vertex shade. A missing static cache key usually means `texSelect()`
fell back to a host pointer, which can make RGBA32 effect sprites decode with
the wrong channel order.

### Dam palette probe

`tools/dam_palette_regression.sh` guards the gray guard-face, gray tan-uniform,
missing red/tan paletted-material, and missing red alarm-lens regression class.
It captures Dam near live guards, dumps selected CI `settex` texture ids, audits
both native trace state and decoded PPM color statistics, then captures a framed
Dam alarm tag `0` lens from pad `10070`:

```sh
tools/dam_palette_regression.sh --no-build \
  --out-dir "$(mktemp -d /tmp/mgb64_dam_palette_regression.XXXXXX)"
```

A healthy run rejects `[GEASSERT]`, `[GFX-DL]`, `No palette`, and grayscale
fallback rows; requires static CI material trace coverage with TLUT evidence;
and requires decoded non-gray, red, and tan pixels in the sampled
guard/material textures. It also requires a strong red-pixel cluster in the
alarm close-up screenshot. This is a native material/color regression guard; it
is not a stock/native alarm-object visual parity oracle.

For texture payload questions, pair the same route with loaded-texture dumps:

```sh
GE007_DUMP_LOADED_TEXTURES='*' \
GE007_DUMP_LOADED_TEXTURES_AFTER_FRAME=108 \
GE007_DUMP_LOADED_TEXTURE_LIMIT=96 \
GE007_DUMP_LOADED_TEXTURE_DIR=/tmp/mgb64_loaded_tex_dumps \
tools/movement_oracle_capture.sh \
  --route dam_regular_glass_shatter_visual_probe \
  --native-only --no-compare --no-build \
  --out-dir /tmp/mgb64_dam_shard_loaded_tex_dump
```

The healthy shard-overlay payload is texture `654` as IA8: base import `56x54`
with a `4096` byte source chain, alpha max around `102`, and lower LOD aliases
for tile 1 (`27x27`) and tile 2 (`13x13`) at non-null TMEM-offset addresses.
If `TEXGEN-MATERIAL` shows active shards falling back to tile 0 because tile 1
has `addr=0x0`, treat that as a TMEM mip-alias regression. If tile 1 is live but
the stock color gap persists, continue with combiner/blender/coverage semantics.
Current `TEXGEN-MATERIAL` rows include `effect=...` when the triangle belongs to
a registered effect display-list range, so shard material rows can be filtered
with `effect=glass_shards` instead of inferred from nearby `EFFECT-TRI` rows.
Set `GE007_TRACE_TEXGEN_MATERIALS_EFFECT=glass_shards` when the budget must be
reserved for active shards; otherwise intact room/pane texgen rows can consume
the budget before the shard pass. Rows also include `mode_decode={...}` with
decoded z mode, coverage destination, coverage flags, both raw N64 blender
cycles, and `api_blend=...` for material-specific blend/coverage investigations.
Use `GE007_DIAG_IA8_CHANNEL_MODE=alpha_from_intensity:<keys>` or
`rgb_from_alpha:<keys>` when separating texture-payload channel questions from a
global IA8 decode swap. On the active Dam shard route, `alpha_from_intensity` is
the current best diagnostic lead, while `rgb_from_alpha` is a checked negative.
Use `GE007_DIAG_ALPHA_FROM_TEX_INTENSITY_CC=0x00f38e4f020a2d12` for the cleaner
shader-side version of that test: it preserves uploaded IA8 texture payloads and
changes only matched loaded-tile shader samples before the combiner consumes
`texVal0.a` / `texVal1.a`.

`GE007_TRACE_GLASS_SHARD_COVERAGE=1` is the next active-shard material tool. It
does not attempt exact RDP raster coverage; it bins emitted `effect=glass_shards`
triangle bboxes into a fixed `64x48` logical grid and logs raw-mode decode,
blend/API-blend identity, material consistency, and overlap pressure as
`[SHARD-COVERAGE]`. The focused proof
`/tmp/mgb64_m16_glass_material_coverage_trace` passes
`tools/glass_material_regression.sh` with `25` coverage rows, stable raw mode
`0C1849D8`, combiner `00F38E4F020A2D12`, `z=xlu`, `cvg=wrap`,
`aa/imrd/clr_on_cvg/force_bl=1`, `cvg_x_alpha/alpha_cvg=0`, `api_blend=alpha`,
zero material/blend mismatches, and coarse overlap pressure up to `max_cell=10`,
`max_overlap_cells=1231`, `max_avg_hits=2.98`. Use this to decide whether the
remaining glass gap is overdraw/coverage semantics before changing geometry,
projection, or texture payloads again.

`GE007_DIAG_XLU_COVERAGE_WRAP_THIN_CC=0x00f38e4f020a2d12` is a follow-up
negative-control approximation of the ares/Parallel-RDP `CLR_ON_CVG` finding: it
does not emulate RDP coverage memory, it only thins screen-space color writes for
matched active-shard fragments. `/tmp/mgb64_m17_glass_material_wrap_thin` proves
the option reaches the shard path with stable `opts=0x00180511`, unchanged raw
mode `0C1849D8`, `api_blend=alpha`, `25` coverage rows, and zero material/blend
mismatches. The active visual runs are negative: default rate `0.25`
(`/tmp/mgb64_m17_active_visual_wrap_thin`) stays near the alpha-intensity
failure (`projected changed=92.332%`, bright `410 -> 943`), and aggressive
`GE007_DIAG_XLU_COVERAGE_WRAP_THIN_RATE=0.05`
(`/tmp/mgb64_m17_active_visual_wrap_thin_rate005`) still moves the wrong way
(`projected changed=92.359%`, bright `410 -> 945`). Keep this as evidence that
the next fix needs real per-pixel coverage/color-on-coverage state or a closer
RDP blender model, not a random/screen-space thinning promotion.

`GE007_DIAG_XLU_COVERAGE_STENCIL_CC=0x00f38e4f020a2d12` is the M18 framebuffer
coverage-memory approximation. It creates a stencil-backed scene target when the
env is present, uses the lower three stencil bits as a per-pixel coverage counter,
and only allows color writes when the synthetic `coverage + memory_coverage`
would wrap for the configured increment. `/tmp/mgb64_m18_glass_material_stencil_inc4`
passes the material gate with stable `opts=0x00080511`, unchanged raw mode
`0C1849D8`, combiner `00F38E4F020A2D12`, `api_blend=alpha_cvg_wrap_stencil`,
`25` coverage rows, and zero material/blend mismatches. The full active visual
wrapper was blocked twice by ares stock-cadence drift (`first gameplay global
1147` instead of expected `1146`), so the pixel sweep used the last valid M17
stock capture against native-only M18 captures. Projection still passes
(`active/projected/onscreen/behind = 90/90/82/0`), but pixels do not improve:
increment `1` gives `projected changed=92.390%`, bright `410 -> 945`; increment
`4` gives `92.343%`, bright `410 -> 939`; increment `8` collapses to the M14
alpha-intensity baseline (`92.411%`, bright `410 -> 923`). Treat stencil coverage
as a negative approximation and move to a closer ares-style RDP blender/memory
color model.

`GE007_DIAG_XLU_RDP_MEMORY_BLEND_CC=0x00f38e4f020a2d12` is the M19
memory-color blender diagnostic. It disables fixed-function GL alpha blending for
the matched shard material, snapshots the current framebuffer before each
triangle, samples that memory color in the shader, and applies the active shard
final-cycle blend formula from the decoded raw mode. `/tmp/mgb64_m19_glass_material_rdp_memory`
passes the material gate with stable `opts=0x00280511`, unchanged raw mode
`0C1849D8`, combiner `00F38E4F020A2D12`, `api_blend=alpha_rdp_memory`, `25`
coverage rows, and zero material/blend mismatches. Native visual proof
`/tmp/mgb64_m19_native_rdp_memory` plus manual compare
`/tmp/mgb64_m19_manual_rdp_memory_compare` keeps projection matched
(`active/projected/onscreen/behind = 90/90/82/0`, max area `-0.029%`, union
`-0.243%`), but pixels remain in the alpha-failure cluster:
`projected changed=92.317%`, bright `410 -> 923`, near-white `190 -> 656`, and
warm `10 -> 0`. Treat memory-color-only blending as another negative
approximation; the next useful model must combine memory color with exact
coverage/color-on-coverage state or port a closer ares/Parallel-RDP
coverage/blender path.

`GE007_DIAG_XLU_RDP_CVG_MEMORY_BLEND_CC=0x00f38e4f020a2d12` is the M20
coverage-plus-memory diagnostic. It keeps the M19 framebuffer memory sample, adds
per-triangle NDC attributes, estimates the Parallel-RDP 8-sample coverage count
in the fragment shader, writes synthetic coverage back through framebuffer alpha,
and applies the ares `CLR_ON_CVG && !coverage_wrap` rule by returning memory
color when coverage does not wrap. `/tmp/mgb64_m20_glass_material_rdp_cvg_memory`
passes the material gate with stable `opts=0x00480511`, unchanged raw mode
`0C1849D8`, combiner `00F38E4F020A2D12`, `api_blend=alpha_rdp_cvg_memory`, `25`
coverage rows, and zero material/blend mismatches. The default material recheck
`/tmp/mgb64_m20_default_material_recheck` also passes with `api_blend=alpha`.
Native visual proof `/tmp/mgb64_m20_native_rdp_cvg_memory` plus manual compare
`/tmp/mgb64_m20_manual_rdp_cvg_memory_compare` keeps projection matched
(`90/90/82/0`, max area `-0.029%`, union `-0.243%`), but pixels remain
effectively unchanged from M19: `projected changed=92.317%`, bright
`410 -> 923`, near-white `190 -> 656`, warm `10 -> 0`, and test mean
`[44.47,46.53,41.56]`. The later formula-parity trace
`/tmp/mgb64_shard_rdp_cvg_formula_trace/shard_draw_trace_join_formula.json`
confirms the material mode is decoded consistently with ares/Parallel-RDP and
that the diagnostic API blend reaches the joined shard rows, but
`/tmp/mgb64_shard_rdp_cvg_active_gate` changes only `82/307200` native pixels and
does not improve the `89.980%` stock/native shard-mask delta. Treat
framebuffer-alpha coverage memory as a checked negative; the next useful step is
not another GL coverage approximation. First prove framebuffer ownership with the
native default-vs-`GE007_GLASS_SHARDS=0` control, then isolate pane break,
impact/crack decal, HUD/viewmodel, and presentation-order contributors.

(P2 hygiene reap, FID-0005: `tools/score_glass_projected_pixels.py`, the M21
projected-pixel classifier for this matched projection ROI, and
`tools/score_glass_projection_win.py`, the projection-containment comparator,
were one-off scripts and have been removed. Preserved finding: the M21 pass
confirmed the default renderer and the M19/M20 RDP memory-color diagnostics
fail in different ways, not just by a single coverage knob — the default
capture shifted stock luma/saturation/RGB buckets in one direction while the
RDP memory-color diagnostics moved into a brighter/desaturated cluster in
another — but the default-vs-shards-off control showed the falling-shard pass
is not visible in the default checkpoint at all, so framebuffer ownership must
be proven before pursuing pixel-color work. Separately, the historical
`sqrt_basis` shard-projection scale hypothesis improved containment over
`no_basis`, but the current `inv_vis_full` default (FID-0003) is stricter and
is the one guarded by the still-wired ctest `port_glass_shard_scale_guard`.)

For the Dam regular-glass full-screen shatter regression, run
`GE007_TRACE_SHARDS=1 GE007_EFFECT_TRI_TRACE=1
GE007_EFFECT_TRI_TRACE_LABEL=glass_shards
GE007_EFFECT_TRI_TRACE_DRAWCLASS=effect`. A healthy build rejects any
CPU-clipped active-shard triangle that expands to viewport-sized post-clip
coverage with `reason=postclip_glass_shard`; bounded `glass_shards` emits should
still appear. The focused proof artifact is
`/tmp/mgb64_dam_glass_visual_postclip_guard_173538`.

### Dam portal visibility probes

`tools/dam_portal_regression.sh` is the focused regression for the Dam pad-140
control-room/wall-contact over-admission view. It captures default portal BFS,
the old legacy widening bundle, and the broad `GE007_PORTAL_BFS=0` diagnostic.
A healthy run keeps the default room set tight, proves the old legacy bundle
still over-admits rooms, and verifies the default screenshot matches the
portal-BFS-off diagnostic at that frame.

```sh
tools/dam_portal_regression.sh --no-build \
  --out-dir "$(mktemp -d /tmp/mgb64_dam_portal_regression.XXXXXX)"
```

`tools/dam_tunnel_visibility_regression.sh` is the complementary Dam pad-164
tunnel under-admission guard. It verifies the default portal ordering and
portal-AABB expansion render the tunnel continuation and uses the script's
`pre_ordering` capture as the negative control that exposes the old blue-cap
view.

```sh
tools/dam_tunnel_visibility_regression.sh --no-build \
  --out-dir "$(mktemp -d /tmp/mgb64_dam_tunnel_visibility.XXXXXX)"
```

Keep `GE007_PORTAL_LEGACY_PROJECT_CLAMP=1`,
`GE007_PORTAL_PARENT_CLIP_MIN_SPAN=N`, `GE007_PORTAL_ACCEPTED_MIN_SPAN=N`, and
`GE007_PORTAL_RETRY_SCREEN_CLIP=1` as A/B-only controls unless a new stock-backed
route proves a specific legacy behavior is required.

### Render-camera clearance probe

`tools/render_camera_clearance_regression.sh` validates the native gameplay
render-eye clearance helper. It runs enabled/disabled pairs across Dam exterior
wall, long wall contact, Dam glass-area, Dam control-room, Dam moving-truck
contact, Surface, Runway tank, and Facility contact cases. The Dam matrix also
includes aim, crouch, aim+C-left/C-right peek input, weapon switching, and a
scripted look sweep.
The moving-truck case keeps the authored Dam vehicle moving before contact and
then asserts clearance against vehicle object `279` / pad `317`, so it guards
independently moving blockers rather than only static walls.
Runway coverage asserts `PROPDEF_TANK` contact; Facility coverage includes
closed-door contact, a normal `B` interaction that opens a door, a second
normal `B` interaction route that reverses that door into closing motion before
repositioning into the active clearance window, and a tinted-glass pad `10098`
contact tied to `GE007_TRACE_GLASS=1`. The enabled captures must emit
`[CAM-CLEARANCE]`, the disabled captures must not, and the final gameplay `pos`,
collision `col`, and current room must match between modes. This is the
acceptance guard that the helper is render-only. The trace includes blocker
metadata (`hit_prop_type`, `hit_obj_type`, `hit_obj`, `hit_pad`,
`hit_door_state`, `hit_door_open`), and the regression asserts the expected
object/pathblocker, tank-object, closed-door, opening-door, and closing-door hit
classes where those contacts are part of the case. For glass contacts that
resolve as room-edge/pathblocker collisions rather than prop hits, the script can
also assert `GLASS_TRACE` pad counts and `CAM-CLEARANCE` `tile_room` counts.
`summary.json` reports `enabled_hit_obj_types`, `enabled_hit_obj_ids`,
`enabled_glass_pads`, `enabled_tile_rooms`, `enabled_moving_door_hits`,
`enabled_door_state_hits`, and `enabled_vehicle_moves` for those guards. Cases
can override the default capture length for soak-style contact checks; raise
`GE007_TRACE_CAMERA_CLEARANCE_BUDGET=N` when a long capture needs more than the
default 240 clearance trace lines.

```sh
tools/render_camera_clearance_regression.sh --no-build \
  --out-dir "$(mktemp -d /tmp/mgb64_render_camera_clearance.XXXXXX)"
```

Keep `GE007_RENDER_CAMERA_CLEARANCE=0` as the negative-control A/B switch. A
failure where the enabled and disabled gameplay traces diverge should be treated
as a regression even if the screenshot looks better.

### Camera basis regression probe

`tools/camera_basis_regression.sh` validates the native live-look path used by
mouse and gamepad look while Bond is moving. It drives Dam with deterministic
forward motion plus a right-look sweep, pins `Input.SteadyView=1`, then compares
the traced facing vector against the expected vector from `theta` and `cam_up`
against the zero-roll up vector from `theta`/`vv_verta`. This catches
frame-order regressions where live look updates `vv_theta`/`vv_verta` after
movement but leaves `theta_transform`, `applied_view`, or the synced render
camera on a stale basis, and it also catches movement-coupled head-roll leaking
into the world camera.

```sh
tools/camera_basis_regression.sh --no-build \
  --out-dir "$(mktemp -d /tmp/mgb64_camera_basis.XXXXXX)"
```

Treat a failure here as a camera-basis regression even if controller-only route
captures still report `cam_up=[0,1,0]`. Use `GE007_STEADY_VIEW=0` or
`--config-override Input.SteadyView=0` only as a negative control to reproduce
the older gait/head-up roll path.

### Dam mission-flow smoke

`tools/dam_mission_flow_smoke.sh` validates the deterministic direct-boot mission
return path for Dam. It enables objective tracing, triggers the existing scripted
mission-success hook, waits until the title/menu path is observed, then asserts
that menu `12` is reached with `front.all_obj_complete=1`,
`front.mission_failed=0`, `front.bond_kia=0`, and all DL resolve counters still
zero. It also verifies that the mission-success trace reports folder 0
Dam/Agent completion, restarts the native binary against the same `--savedir`
without the mission-success hook, and asserts the reload trace reports
`save.valid[0]=1`, `save.level[0]=0`, and `save.difficulty[0]=0`. This is a
scripted end-state smoke; it does not replace an organic route that completes
Dam objectives and exits through the bungee jump.

```sh
tools/dam_mission_flow_smoke.sh --no-build \
  --out-dir "$(mktemp -d /tmp/mgb64_dam_mission_flow.XXXXXX)"
```

### Dam objective-progression smoke

`tools/dam_objective_progression_smoke.sh` validates Dam's real objective
criteria without pretending to be a navigation route. It direct-boots Dam,
destroys alarm tags `0..3` through `GE007_AUTO_DAMAGE_TAG_SCRIPT`, sets the
modem/data/bungee stage flags through `GE007_AUTO_SET_STAGE_FLAGS_SCRIPT`, and
requires the traced objective vector to advance one objective at a time before
all four objectives are complete. It also dumps setup objective criteria and
alarm tags, audits the runtime setup against the expected Dam definitions, and
keeps DL resolve counters, bad commands, and crashes at zero.

```sh
tools/dam_objective_progression_smoke.sh --no-build \
  --out-dir "$(mktemp -d /tmp/mgb64_dam_objective_progression.XXXXXX)"
```

This is objective-condition coverage. It does not replace a full organic Dam
route that moves from spawn through alarms/modem/data/bungee with stock-like
combat, navigation, and end-state timing.

### Dam progression aggregate smoke

`tools/dam_progression_smoke.sh` keeps the CTest
`port_dam_progression_smoke` lane active by composing the current strongest Dam
sub-gates: deterministic spawn movement, objective criteria progression, and
mission-flow return plus persisted save reload. This is still scripted
progression coverage, not an organic route solver.

```sh
tools/dam_progression_smoke.sh --no-build \
  --out-dir "$(mktemp -d /tmp/mgb64_dam_progression.XXXXXX)"
```

### Surface II final-flow smoke

`tools/surface2_final_flow_smoke.sh` validates the two scripted contracts around
the Surface II silo transition. The hatch capture places Bond on top of the
closed silo hatch and requires the traced floor to stay on the door collision
surface; the disabled control runs the same capture with
`GE007_DOOR_FLOOR_COLLISION=0` and requires the old drop-through floor to
reappear. The final-exit capture completes the Agent-relevant objectives,
places Bond on final pad `289`, presses A, and requires `obj.all_complete=1`
before the title/menu return. This is not a full organic Surface II route.

```sh
tools/surface2_final_flow_smoke.sh --no-build \
  --out-dir "$(mktemp -d /tmp/mgb64_surface2_final_flow.XXXXXX)"
```

### GFX DL provenance regression

`tools/gfxdl_provenance_regression.sh` is the focused long-playback gate for
`[GFX-DL] unregistered` regressions. It runs a deterministic hidden-window trace
with `GE007_TRACE_FLOW_ONLY=1`, exits through `GE007_AUTO_EXIT_FRAME`, and fails
if stderr contains `[GFX-DL]`, if any display-list resolve counter becomes
nonzero, or if bad-command/crash counters increment. The default case is Surface
1 level `36` through frame `11600`, with setup counts matching the submitted
playback log (`136` bound pads, `251` waypoints, `25` waygroups, `16` patrol
paths, `47` AI lists, `9` guards, `203` objects). The pasted failure signature
is repeated dynamic-pool addresses around frame `11450`; on a fixed build this
case should cross that window with `unregistered_skip=0` and no `[GFX-DL]`
stderr rows.

```sh
tools/gfxdl_provenance_regression.sh --no-build \
  --out-dir "$(mktemp -d /tmp/mgb64_gfxdl_provenance.XXXXXX)"
```

Use `--level`, `--frames`, `--auto-forward`, and `--expected-setup` to reuse the
same gate for other long level playback reports. Use `--no-expected-setup` when
the level setup is intentionally variable or the report is only a shared-renderer
smoke.

### Vehicle AI/path probe

Scripted vehicles should receive their authored AI list from setup data, bind a
path, then ramp toward the scripted speed while the object handler advances along
waypoints. Dam's intro truck is a compact regression probe:

```sh
tmpdir="$(mktemp -d /tmp/mgb64_truck_probe.XXXXXX)"
cd "$tmpdir"
env SDL_AUDIODRIVER=dummy GE007_MUTE=1 GE007_NO_VSYNC=1 \
  GE007_BACKGROUND=1 GE007_NO_INPUT_GRAB=1 \
  GE007_ENABLE_LEVEL_INTRO=1 GE007_INTRO_CAMERA_INDEX=5 \
  GE007_TRACE_VEHICLE_AI=1 GE007_TRACE_VEHICLE_AI_BUDGET=40 \
  GE007_TRACE_VEHICLE_STATE=1 GE007_TRACE_VEHICLE_STATE_BUDGET=160 \
  GE007_TRACE_VEHICLE_STATE_INTERVAL=10 \
  /path/to/build/ge007 --rom /path/to/baserom.u.z64 \
  --level dam --deterministic --trace-state state.jsonl \
  --screenshot-frame 80 --screenshot-label dam_truck_probe \
  --screenshot-exit > run.log 2>&1
```

The expected startup signature is `obj=279`, `pad=317`, AI list `0x040A`,
`path_id=7`, `path_len=13`, `raw_speed=512`, `speedaim=3.333333`, and
`speedtime=120`. Subsequent `VEHICLE_STATE` lines should keep `path_id=7`, point
at `waypoint=17` / `target_pad=312` initially, emit `move_accepted`, and show the
position changing as speed ramps up.

If `VEHICLE_STATE` ticks appear with `path_id=-1`, `speed=0`, and `aim=0` while
no `VEHICLE_AI` lines appear, inspect setup prop conversion and `ailistFindById`
resolution for vehicle/aircraft props. If AI lines appear but movement stalls,
look for `missing_switch_3`, `move_rejected_nav`, `move_rejected_footprint`, and
waypoint-sentinel handling in the object handler. The frame-80 probe is short by
design: it proves authored vehicle startup movement without turning into a broad
Dam intro/rendering soak.

## ROM movement oracle

The public ROM-comparison lane for player movement is documented in
[ROM_COMPARISON.md](ROM_COMPARISON.md). It uses authored route specs from
`tools/rom_oracle_routes/`, native `--trace-state` captures, and an optional
local instrumented ares checkout that dumps the same movement fields from stock
RDRAM. The route audit rejects captures that keep frontend/menu input active
after gameplay starts and can require minimum gameplay-input and moving-record
counts before comparison. Movement counts are scoped to the requested
target-stage player. It can also require a minimum horizontal player position
delta for native/direct gameplay smokes; standard ROM movement routes cap
suppressed menu attempts and require a clean menu-to-gameplay gap so the
bootstrap script cannot keep trying to press Start/A after handoff. Stock-backed
movement, intro, and glass comparisons emit `compare_<route>.json` artifacts
with the same pass/fail, threshold, and max-delta evidence used by the text
logs. Intro routes can compare the decoded swirl setup
fingerprint, stock-selected authored camera fingerprint, authored camera timer,
and Bond actor/action/animation fields before checking camera-path vectors. The
Dam coverage is split into a static selected-camera path route and a
timer-aligned swirl/Bond-animation route so duplicate stock video samples do not
masquerade as game ticks. Routes can also opt into the native render-health
audit, which fails on crash recoveries, bad GBI commands, display-list resolve
failures, room-render fallbacks, or non-finite trace values.
Generated traces, screenshots, saves, emulator logs, and the local ares checkout
are ROM-derived/local artifacts and must stay out of git. Expected pre-pixel
visual failures are still useful artifacts: inspect `summary_<route>.json` first,
then follow its `health_compare_json`, `actor_compare_json`, and `compare_json`
paths. A failed pre-pixel visual route intentionally has no `visual_compare_json`.

Local ares path for this workspace:

```sh
build/ares-movement-oracle/ares/build-movement-oracle/desktop-ui/ares.app/Contents/MacOS/ares
```

Check that path before searching `PATH`, `/tmp`, or `/Applications`; rebuild it
with `tools/prepare_ares_movement_oracle_build.sh` if it is missing or if a new
route/trace field such as `glass_projection` is expected but absent. The Linux
binary from the same build lives at
`build/ares-movement-oracle/ares/build-movement-oracle/desktop-ui/ares`.

## Advanced / dev-only (not shipped)

These families live in the development tree only. They depend on artifacts that
aren't part of a contributor checkout, so they're intentionally excluded:

- **Broad emulator parity / oracle** (`compare_texrect_trace.py`,
  `parse_ares_dyn_gfx_dump.py`, legacy GDB-driven `ares_*`) — need a local ares
  emulator build, a GDB stack, screen-capture permissions, and a stock ROM
  driven through GDB. The public movement-only ares hook is the narrow supported
  exception; see the ROM movement oracle section above.
- **RAMROM replay** (`ramrom_*`, `build_ramrom_*_rom.py`,
  `native_ramrom_playback_smoke.sh`) — need stock-ROM demo tables, generated
  steering ROMs, the N64 toolchain, and `/tmp` golden artifacts.
- **Real soundplayer packs** (`sndplayer_real_*`) — need a separately built real
  `ALSndPlayer` binary and produce large reviewer WAV/listening packs.
- **Curated visual references** (`menu_visual_*`, `visual_parity_*`, `parity_*`)
  — compare against golden packs/baselines not present here.
- **The aggregate orchestrator** (`validate_suite.sh`) — wires dozens of lanes
  including all of the above; use `validate_quick.sh` instead.

Reintroducing these would pull in emulator / ROM-derived artifacts and break
clean-checkout reproducibility, which is why the public repo ships only the
self-contained subset above.
