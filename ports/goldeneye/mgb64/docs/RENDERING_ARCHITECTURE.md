<!-- Authored 2026-07-01 alongside docs/design/PERFORMANCE_PLAN.md (the "Make It Rip"
performance pass). Purpose: give future contributors the mental model of the
Fast3D render path, mark where the per-primitive hot-path boundary is, and record
the two performance case studies that motivated the rule below. -->

# MGB64 Rendering Architecture (native port)

This is the map of how a frame gets drawn in the native port, written for anyone
touching the renderer. It is deliberately short. The companion docs are
`docs/design/PERFORMANCE_PLAN.md` (budgets + the perf harness),
`docs/VISUAL_MODES.md` (feature flags), and
`docs/FRAME_TIMING_ARCHITECTURE.md` (frame *rate* — the fixed 60 Hz sim timestep,
why play is 60 while benchmarks report ~120, and why >60 is a sim rewrite).

## 1. The one rule

> **Nothing runs per-primitive unless it draws that primitive.**

The per-vertex and per-triangle code paths execute tens of thousands of times per
frame. Diagnostics, room/effect attribution, provenance bookkeeping, string
matching, and single-level visual hacks do **not** belong there unless gated so
they cost nothing when inactive. Two shipped defects (both fixed in the M1/M2 pass)
came from breaking this rule; see §4. When in doubt, hoist the work to
per-command / per-frame, memoize it, or gate it behind a latched flag.

## 2. The frame pipeline

Game code (decomp) builds N64 display lists and hands the root DL to the port:

```
game frame
  └─ gfx_run_dl(root)                         src/platform/fast3d/gfx_pc.c
       ├─ (per frame) reset counters, bump diag memo generation
       ├─ gfx_run_dl_pc(cmd)                  PC/native Gfx interpreter (one switch/opcode)
       │     ├─ G_MTX / G_VTX / G_TRI / G_SETTEX / G_SETCOMBINE / …
       │     ├─ G_DL → recurse (PC subtree) or → gfx_process_n64_dl (N64 room DL)
       │     └─ gfx_draw_class_for_cmd_addr()  categorize cmd (HUD/world/…) — real render input
       ├─ gfx_process_n64_dl(bytes)           big-endian N64 room display lists
       │     ├─ gfx_sp_vertex()               software T&L: transform + light + texgen per vertex
       │     └─ gfx_sp_tri1() / clip fans     assemble triangles → gfx_emit_loaded_triangle()
       │            └─ gfx_flush()            when a batch is full or state changes
       │                 └─ gfx_rapi->draw_triangles()   → gfx_opengl.c
       └─ gfx_end_frame()                     output filter (FXAA/bloom/grade/tonemap), swap
```

Backend (`src/platform/fast3d/gfx_opengl.c`) turns each flushed batch into one
`glBufferData` upload + `glDrawArrays`, plus the end-of-frame full-screen output
pass (the remaster post-FX). The GL/Metal backend is single-threaded with the
game/interpreter — there is **no** separate render thread, so per-frame CPU cost
in `gfx_pc.c` is the frame's critical path.

### Batching & flush
A "batch" is a run of triangles sharing GL state. `gfx_flush()` is triggered by a
shader/combine change, blend change, texture/sampler bind, depth-mode change,
viewport/scissor change, or the buffer filling (`MAX_BUFFERED = 1024` tris). Each
flush is one draw call. Post-M1/M2, draw submission is a small fraction of frame
time (~7% on the heaviest level) — the frame is CPU-DL-interpreter + T&L bound,
not draw-call bound. Do not add draw-call "optimizations" without a profile that
shows submission-bound frames.

## 3. Translucency & the RDP memory-blend (the subtle part)

Some N64 translucent materials (anti-aliased "wrap-color-on-coverage, read
framebuffer memory" mode — e.g. Dam glass, water, foliage edges) blend against
the *current framebuffer color*. GL fixed-function blending can't express the N64
read term, so the port emulates it: it copies the framebuffer into a TEXTURE2
"snapshot" that the fragment shader samples as the memory term
(`gfx_opengl_copy_framebuffer_snapshot`, classification in `gfx_pc.c`
`gfx_rdp_memory_blend_class_for_draw`).

- The snapshot is taken **once per batch** (`GFX_XLU_SNAPSHOT_PER_BATCH`, default):
  one framebuffer copy over the union of the batch's triangle rects, then one
  draw. This is correct because same-material batch triangles (glass panes,
  foliage cards) are coplanar / non-overlapping; a fragment reads the same
  framebuffer memory it would per-triangle.
- `GE007_XLU_SNAPSHOT_MODE=pertri` restores the exact legacy per-triangle copy
  (one framebuffer copy + GPU pipeline stall per triangle). Use it only for A/B —
  it is the pre-M1 behavior and is 3–7× slower on translucency-heavy levels.
- `GE007_DISABLE_ROOM_XLU_CVG_MEMORY=1` turns the emulation off entirely (plain
  alpha blending). This is the `xluoff` column in `tools/perf_census.sh`.

## 4. Two case studies (why the rule exists)

Both were features built for one level (Dam glass), shipped default-on globally,
running in the per-primitive hot path. Both are fixed; they are here as warnings.

1. **Per-triangle framebuffer copy (M1).** The RDP memory-blend copied the
   framebuffer once *per translucent triangle*, serializing the GPU on every XLU
   tri. Jungle (wall-to-wall alpha foliage) ran at 18 fps. Fix: snapshot per
   batch (§3). 7.3× on Jungle. Lesson: **framebuffer readback/GL state churn is a
   batch-level operation, never per-primitive.**

2. **Per-triangle O(rooms) attribution scan (M2).** `gfx_diag_room_cmd_offset`
   linear-scanned every room's DL range to attribute a triangle to a room — for
   diagnostics — *per triangle*, unconditionally. On room-heavy Dam this was the
   dominant cost (50 fps). Fix: it's a pure function of the command address +
   per-frame-stable room table, so memoize it per command. 60→137 fps, byte-
   identical. Lesson: **diagnostic attribution is per-command at most; memoize or
   gate it.** The related per-vertex effect-label `strstr` was hoisted out of the
   vertex loop for the same reason.

## 5. Measuring & guarding performance

- `GE007_PERF_TRACE=1` logs per-frame `work_ms` (the real cost; FPS = 1000/work_ms).
- `tools/perf_census.sh` — deterministic headless frame-time census of all 20
  levels (default vs `xluoff` A/B).
- `tools/perf_budget_check.py` / `tools/perf_budget_smoke.sh` — enforce the budget
  (hard floor 60 fps / 16.6 ms; target 120 fps / 8.3 ms) against
  `baselines/perf_census_baseline.csv`. Registered as the opt-in
  `port_perf_budget_smoke` CTest lane.
- Full recipes: `docs/INSTRUMENTATION.md` → "Performance census & budgets".

## 6. Governance for perf-sensitive features

Every feature that adds per-frame or (especially) per-primitive work must ship with:
1. **A scope** — which levels/materials it applies to. A one-level feature must not
   tax the other nineteen (default-on-global is how both §4 defects happened).
2. **A budget** — it must keep every level within `docs/design/PERFORMANCE_PLAN.md` §6.
   Run `tools/perf_census.sh` before/after.
3. **An A/B escape hatch** — a `GE007_*` env knob (and, for visual features, a
   screenshot-oracle parity check), so any regression is one variable away from
   isolation. See the existing `GE007_XLU_SNAPSHOT_MODE`,
   `GE007_DISABLE_ROOM_XLU_CVG_MEMORY`, `Video.RenderScale`, `--faithful`.

## 7. Fidelity contract: RSP texture-gen (environment mapping)

`G_TEXTURE_GEN` is the RSP's sphere/environment mapping: instead of using authored
UVs, the vertex's S/T come from dotting its normal with the two `gSPLookAt` vectors.
GE uses it for the boot logos (GoldenEye / Nintendo / Rare), the intro cast models,
the reflective weapons (Golden Gun, Ruger, both knives, Silver & Gold PP7 — see the
`gSPLookAt` gate at `gun.c:15133`), and some in-world `class=room` geometry.

There is **exactly one** implementation — `gfx_texgen_coords()` in
`src/platform/fast3d/gfx_texgen.h`, called from `gfx_sp_vertex()`. Every backend
(WebGPU native, web/wasm, and the legacy GL/Metal fallbacks) consumes the float
`u`/`v` it produces, so this header *is* the whole class. Get it wrong and every
env-mapped surface in the game is wrong at once — which is exactly what happened.

**The contract, from the F3DEX2 microcode (`Mr-Wiseguy/f3dex2` f3dex2.s):**

```
vmudh vPairST, vOne, $v31[5]   ; ST  = 0x4000
vmacf vPairST, $v3,  $v21[0h]  ; ST += 0x4000 * dot
vmudm $v3, vPairST, vVpMisc    ; (ST * scale) >> 16
```

so `coord = 0x4000*(1 + dot)*scale / 0x10000 = (1 + dot) * scale / 4`. The two
consequences that matter, and that are easy to get wrong:

- **The coordinate spans only HALF of `texture_scaling_factor`.** GE authors its
  env materials with `scale == tex_width * 64`, while one texture span is
  `tex_width * 32` in the 5.5 fixed-point domain. So the full normal sweep covers
  the environment map *exactly once*, and a camera-facing normal (`dot == 0`)
  samples the map's **centre**. This is a structural relationship, not a
  coincidence of one asset: it holds for the 32-texel logo map (scale 2048) and
  the 54-texel in-world map (scale 3456) alike.
- **Overrun is not harmless.** The env-map tiles are `G_TX_CLAMP` (logos) or
  `G_TX_WRAP` (in-world), so a coordinate past the end is pinned to the border
  texel or wrapped into the wrong region — it does not politely fall off.

**The 2026-07-20 defect (FID-0140), as a warning.** Three divergences coexisted
here, all with the same effect — roughly half of all texgen coordinates landing
outside the map:

1. the divisor was 2, not 4 (2× the reference);
2. a trailing `U = s_scale - U` S mirror that exists in no reference
   implementation nor in the microcode; and
3. the `[-1, 1]` clamp on the dot sat *inside* the `G_TEXTURE_GEN_LINEAR` branch —
   a branch GE never takes — so the live path ran unclamped.

Player-visible result: the boot "GoldenEye" wordmark rendered grey/white over its
top two-thirds instead of gold, because its flat, camera-facing letter faces sampled
the env map's pale border row rather than its gold interior.

Three lessons worth keeping:

- **A single shared vertex-stage formula is a whole-game blast radius.** Treat
  `gfx_texgen.h` the way §1 asks you to treat the hot path: change it only with a
  console A/B in hand.
- **An in-code comment is not evidence.** `RENDERER_SIM_AUDIT_2026-07-06.md`
  finding 15 spotted the 2× and its verify pass dismissed it as "a deliberate
  domain match" *because the comment said so*. See the RETRACTION on that entry.
- **Untested subsystems drift silently.** There was no test, baseline, or golden
  image covering texgen anywhere in the repo, which is why the miss survived.

**Guards now in place:**

- `tests/test_texgen_coords.c` → CTest `texgen_coords`. ROM-free; pins
  `dot=-1 → map origin`, `dot=0 → map centre`, `dot=+1 → exactly one span`, S/T
  symmetry, out-of-range clamping, the int16 bound at max scale, and the legacy
  A/B. It fails on revert of any of the three divergences.
- `GE007_TEXGEN_LEGACY=1` restores the pre-fix mapping (verified byte-identical to
  pre-fix output) for one-variable isolation.
- Because `|dot| <= 1` is now enforced on the live path, `U`/`V` are bounded by
  `scale/2 <= 32767` and always fit the `short` staging in `gfx_sp_vertex` — this
  is what closes the texgen limb of FID-0020 by construction.

**Console evidence behind the current mapping.** The three boot logos are the sharp
cases (GoldenEye logo saturation 0.82 vs console 0.82, was 0.06; Rare logo hue 38.9°
vs 36.8°, was 141°; Nintendo logo median luma 83 vs 86, was 114). In-world
`class=room` texgen was validated separately via
`tools/movement_oracle_capture.sh --route dam_monitor_probe_visual`, localising the
texgen pixels by diffing native-against-itself under `GE007_TEXGEN_LEGACY=1` — a
useful trick, since it needs no trace and finds exactly the affected set. That
surface is low-amplitude (no A/B pixel delta above 60 over a mean of ~34/255), so
the result is directional: where the signal clears the route's baseline
native-vs-console noise (delta >30), the fix is 27% closer to console; below that
it is inside the noise. Treat the logos as the authority.

**Known-unvalidated corner:** the `G_TEXTURE_GEN_LINEAR` branch. No GE display list
ever *sets* that bit (it appears only in `gSPClearGeometryMode` masks), so it is
dead code, pinned to the reference form (`acos(-dot)/(2*pi) * scale`) but never
compared against console. Note also that the hardware does not compute an arccosine
at all — it evaluates an odd cubic through (-1,0), (0,0.5), (1,1) — so every HLE
`acos` here is an approximation worth ~5% of the texture. If content ever starts
setting that bit, this branch needs real validation first.
