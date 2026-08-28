# Renderer Regression Notes

This note records the texture/fog/glass regressions that were chased during the
June 2026 playability pass. It is meant to keep future renderer work from
reintroducing the same failure modes.

Generated screenshots, traces, contact sheets, emulator logs, and ROM-derived
captures must stay local. Do not commit artifacts from `/tmp/mgb64_*`.

## What Failed

The visible symptoms were level-specific but shared renderer causes:

- Dam glass collision still worked while the visible glass and bullet crack
  decals disappeared, rendered in the wrong place, or only appeared during the
  authored intro.
- Cradle could crash during startup after animation/model setup and later
  showed severe texture smearing and blocky room surfaces.
- Dam, Surface, Bunker, Depot, and other large-room or sky-heavy levels showed
  texture regressions after modern display/FOV work even though the same levels
  were playable.
- Some fog-heavy captures looked like missing texture detail when the immediate
  issue was actually fog-depth semantics.

## Root Causes

1. **Room alpha and prop glass used multiple render paths.**
   Dam glass was not one effect. Secondary room alpha, prop-type glass material
   classification, and bullet-crack decal placement all had to survive the
   native display-list path. The collision path was correct, which is why Bond
   could hit invisible panes while the visible alpha/decal work was still wrong.

2. **N64 texture-filter threshold policy was too aggressive for clamped room
   materials.**
   The shader-side N64 filter uses screen derivatives to decide when to take the
   nearest branch versus the three-point path. Those derivatives come from the
   OpenGL framebuffer, so Retina/high render scale made the footprint smaller
   than it would be in the original VI/logical image. The default path
   normalizes that derivative through `uN64FilterScale` in
   `src/platform/fast3d/gfx_opengl.c`, but a later `0.05` threshold override for
   clamped, non-texture-edge materials pushed close room textures into constant
   three-point filtering. Bunker's yellow warning sign was the clearest probe:
   the `0.05` path smeared it, while the normal `1.0` threshold restored texel
   structure without forcing blanket point sampling.

3. **Decoded texture source footprint is part of cache identity.**
   The same visible tile dimensions can come from packed `LOADBLOCK` data or a
   strided `LOADTILE`/sub-rect source. Caching only the visible upload dimensions
   lets a later material reuse bytes decoded with the wrong row pitch, producing
   row smear while sampler state and source addresses look plausible. The native
   texture path tracks decoded line size, full-image line size, and decoded
   footprint in `src/platform/fast3d/gfx_pc.c`.

4. **Rare `G_SETTEX` can coexist with stale ordinary tile state.**
   Texture-by-number materials need clamp/wrap/shift/offset information from the
   active `G_SETTEX` command, not whatever ordinary tile descriptor happened to
   be live. If stale clamp bits survive, repeated room coordinates clamp to an
   edge row or column and stretch that across a surface. The renderer now derives
   `G_SETTEX` tile state from the active command and treats stale ordinary tile
   state as diagnostic evidence, not the source of truth.

5. **Fog can mimic texture failure.**
   GoldenEye's fog values are interpreted through the N64-style projected depth
   path, not a simple world-distance ramp. Cradle's narrow fog range is a good
   example: forcing linear depth can make distant geometry look clearer, but
   that is a negative control, not the parity default.

6. **Modern scene targets can clobber cached GL texture state.**
   `Video.RenderScale > 1` and MSAA render through an offscreen scene target.
   FBO setup temporarily binds the scene color texture on GL texture unit 0. If
   that binding is not restored, Fast3D's cached sampler state still believes the
   current game texture is live, but the GPU samples the scene target instead.
   The failure looks like sky/cloud or texture detail disappearing only under
   RenderScale/MSAA; the same capture at `Video.RenderScale=1` still looks
   correct. Scene-target setup in `src/platform/fast3d/gfx_opengl.c` must restore
   active texture and unit-0 binding before queued triangles flush.

7. **Validation config can masquerade as renderer state.**
   The native binary writes `ge007.ini` on clean exit, including env-applied
   settings. If a smoke run reuses an artifact directory or lets the default
   1440x810 window drive captures on a high-DPI display, later attempts can
   render through a much larger scene target than intended. That mostly changes
   sky/backdrop sampling and can look like a texture regression in pixel deltas.
   The playability harness now pins a 640x480 validation window by default and
   records any explicit config overrides in `summary.json`.

8. **Sky UV scale is a calibrated repeat factor, not a raw passthrough.**
   GoldenEye's sky coordinates arrive in world-repeat space and then pass
   through the native VBO texture packing path. Defaulting `GE007_SKY_UV_SCALE`
   to `1.0` stretched the cloud texture into broad bands on Depot, Surface,
   Frigate, Cradle, and other sky-heavy levels. A `3.5` backend repeat creates
   high-frequency horizon fans on Frigate and Cradle because the game already
   applies environment `CloudRepeat`/`WaterRepeat` before the native VBO pack.
   The native default is `1.5`, which keeps cloud detail without those horizon
   fan artifacts while preserving split-screen viewport-aware sky draw.

9. **Frigate's secondary water/backdrop room shell should not shade.**
   Room 57's secondary display list uses texture 655, IA8 54x54, XLU coverage
   wrap render state (`0x0C1849D8`) and no depth update. Treating it as ordinary
   alpha exposes large room-shell triangles over the ocean/sky. The native
   shader path now classifies that material signature and forces alpha to zero;
   `GE007_TRACE_ROOM_ALPHA=1`, `GE007_SKIP_ROOM_DL=secondary`, and
   `GE007_TINT_ROOM_DL=secondary` are the focused controls.

10. **Texture attribution must use stable `G_SETTEX` texture numbers.**
   `settex_gl_tex_id` is an OpenGL upload id and changes with cache/upload
   order. Using it for `GE007_TINT_TEX` or `GE007_SKIP_TEX` made focused probes
   point at the wrong material after unrelated renderer changes. The tint/skip
   path now matches `settex_texturenum`, which is the game texture number logged
   by `GE007_TRACE_SETTEX_MATERIAL_CC`. Runway's foreground is the practical
   example: texture 22 and texture 1267 can both overlap the same screenshot
   band, so crop-level color metrics are not enough without texture-number tint
   and material traces at the actual capture frame.

11. **Deterministic input freeze must not suppress scripted look probes.**
    The screenshot and oracle lanes use `--deterministic`, which sets
    `g_freezeInput` so live keyboard, mouse, and gamepad input cannot leak into
    local captures. Scripted `GE007_AUTO_LOOK_*` probes are different: they are
    authored capture input and must still reach the native mouse-look path. A
    broken gate suppressed all native look while `g_freezeInput` was set, so
    "look up" comparisons measured different camera composition instead of
    sky/fog/texture output. The fixed path keeps RAMROM playback isolated,
    ignores live gamepad right-stick while frozen, and lets
    `platformGetMouseDelta()` return scripted mouse deltas under freeze.

12. **Surface fog/tree backdrops need RDP coverage-memory blending, not a
    draw-order patch.**
    Surface 1's distant tree/fog strips are fogged secondary-room `G_SETTEX`
    alpha draws with `ZMODE_XLU`, `CVG_DST_WRAP`, `CLR_ON_CVG`, `IM_RD`, depth
    compare on, and depth update off. Treating that class as ordinary GL alpha
    blending lets distant fog/tree layers accumulate through nearer room alpha
    differently from the RDP. The default renderer now routes the narrow
    fogged secondary-room coverage-wrap class through the RDP coverage-memory
    shader/backend path; `GE007_DISABLE_ROOM_XLU_CVG_MEMORY=1` is the focused
    escape hatch. The OpenGL backend now also treats that default promotion as a
    scene-target requirement, so coverage-memory draws sample a known RGBA8
    target rather than the window backbuffer. Normal blended color draws preserve
    the scene-target alpha channel, leaving it as synthetic RDP coverage memory
    for later coverage-wrap/color-on-coverage blends instead of blended opacity
    history. 2026-06-28 backend validation: the focused CTest guard set passed,
    Surface default versus disabled coverage-memory at frame 360 still produced a
    focused `2.593%` screenshot delta with identical movement
    (`/tmp/mgb64_surface_cvg_memory_backend_1782654476`), default still matched
    the explicit `0x00f78e4f0ebe2d12` diagnostic exactly
    (`/tmp/mgb64_surface_cvg_memory_diag_match_1782654512`), Dam glass material
    regression passed (`/tmp/mgb64_glass_material_cvg_backend_1782654534`), and
    Dam/Surface playability smoke passed
    (`/tmp/mgb64_playability_dam_surface_cvg_backend_1782654560`). Surface's
    look-sweep proof is intentionally material-level:
    disabling the promoted class changes 35,815 pixels at frame 360, while the
    promoted default matches the old explicit
    `GE007_DIAG_XLU_RDP_CVG_MEMORY_BLEND_CC=0x00f78e4f0ebe2d12` diagnostic
    exactly. By contrast, disabling the secondary-room XLU deferred sort was a
    0-pixel result on that route, and disabling local room XLU sort changed only
    351 pixels. Keep Dam glass and Frigate room-57 shell out of this gate:
    Dam's glass material suite should still report shard coverage as
    `api_blend=alpha`, and Frigate room 57 should remain non-fog
    alpha-suppressed (`opts=0x01043f11`, `api_blend=alpha`).
    The repeatable guard is now `tools/surface_xlu_cvg_memory_regression.sh`.
    Fresh current-build proof `/tmp/mgb64_surface_rdp_phase2_1782659808`
    reproduced the same class: default promoted `898` room XLU wrap rows to
    `alpha_rdp_cvg_memory`, disabling the classifier left `898` ordinary alpha
    rows and `898` unpromoted coverage candidates, gameplay state remained
    identical for `359` compared frames, and the screenshot delta was
    `9,621 / 307,200` pixels (`3.132%`).
    The follow-up generalized the same classifier to generated or locally
    transformed room-class `G_SETTEX` strips whose room command address and
    room-matrix attribution are absent, but only after the same fogged XLU
    coverage-wrap state, `envA=255`, and `primA=0` material gates pass. Current
    cross-level proof `/tmp/mgb64_cvg_gate_matrix_after_1782661301` promotes the
    generated fogged rows in Jungle (`1,951` `ok_generated_room` rows; `2,062`
    promoted total), Dam (`28` generated rows), and Depot (`36` generated rows),
    while Frigate still promotes zero rows because its candidates fail on
    `fog=0` or `water_suppress`. The focused Jungle guard
    `/tmp/mgb64_jungle_xlu_cvg_guard_final` passed with `779` budgeted
    promoted rows, `810` disabled-run candidates, a `0.507%` screenshot delta,
    and identical gameplay state for `239` compared frames. The original Surface
    guard `/tmp/mgb64_surface_xlu_cvg_guard_final` still passed with
    `873` promoted rows, `873` disabled-run candidates, and a `2.139%` delta.
    The remaining Jungle `envA` candidates are a checked negative:
    `/tmp/mgb64_jungle_envA_cvg_ab_1782661591` promoted all `93` leftovers via
    the diagnostic but produced a `0`-pixel delta, so the `envA=255` default gate
    stays narrow.
    2026-06-28 performance follow-up: the default promoted path must not copy
    the full viewport once per triangle. At a `2056x1257` Surface 2 window, the
    old full-snapshot path averaged about `24 ms` of pre-cap work; disabling the
    classifier dropped to about `8 ms`, proving the slowdown was framebuffer
    snapshot bandwidth rather than bloom/post-FX. The OpenGL backend now copies
    only the promoted triangle's screen-space bbox into the same full-size
    snapshot texture before drawing that triangle, preserving the shader's
    `gl_FragCoord` lookup semantics while avoiding full-window copies. A/B
    proof `/tmp/mgb64_surface_rectsnap_ab_640` is screenshot-identical to the
    old full-copy path (`0/307200` changed pixels, `873` promoted rows), the
    focused guard `/tmp/mgb64_surface_xlu_cvg_memory_rectsnap_guard` still
    passes (`873` promoted rows, `2.089%` default-vs-disabled delta), and the
    final large-window Surface 2 perf probe
    `/tmp/mgb64_surface2_perf_rectsnap_final.LNzHKP` reports
    `avg_work=9.81 ms`, `max_work=15 ms`. Keep
    `GE007_DISABLE_RDP_CVG_SNAPSHOT_RECTS=1` as the exact full-copy A/B only.

13. **Room `G_SETTEX` LOD endpoints are draw semantics, not matrix semantics.**
    Dam's pad10092 room-glass source trace showed `G_SETTEX` trilerp draws
    with valid two-scale footprints and `LOD_FRACTION` in the RGB combiner even
    when the modelview room-matrix flag was false. Gating footprint LOD on that
    matrix bit left the target draw at `lod=0`, producing a black source
    fragment before any framebuffer blend was considered. The default renderer
    now allows footprint-derived LOD for room-class, XLU, `G_SETTEX` materials
    that own both texture endpoints and consume `LOD_FRACTION`; use
    `GE007_DISABLE_SETTEX_FOOTPRINT_LOD=1` as the focused A/B control. This is
    intentionally a source-sampling fix only: Dam's final center-glass handoff
    still needs the broader RDP framebuffer-memory/coverage blend work before
    stock pixel parity should be expected. The room-glass source oracle must
    validate both settex units and the decoded two-cycle combiner, not just a
    tile0 `texel * shade` shortcut; keep
    `python3 tools/check_room_glass_source_reconstruction_regression.py` green
    when changing settex sampling, combiner diagnostics, or framebuffer-owner
    probes.

14. **Early renderer classifiers must decode raw combiners as raw combiners.**
    `gfx_cc_get_features()` expects the generated shader id, not Rare/N64's raw
    packed combine mode. Pre-shader decisions such as N64 filter eligibility,
    alpha-from-intensity diagnostics, and RDP framebuffer-memory promotion must
    use a raw-combiner scan until `gfx_generate_cc()` has produced the packed
    shader ids. After room alpha LUTs, eye overrides, or tint overrides, rescan
    the effective combiner before classifying blend/memory behavior.

15. **Native sky must replay in display-list phase and behave as backdrop.**
    `skyRender()` builds normal GBI setup/fill commands and raw RDP sky
    triangles. The native port cannot interpret those raw RDP triangle packets,
    but it also must not draw them immediately while the game is still building
    the frame. Native sky triangles are now queued during `skyRender()` and
    replayed from PC-only `G_NOOP` markers when `gfx_run_dl()` reaches the
    original raw-RDP command phase. That preserves fill/setup ordering, keeps
    split-screen viewport ownership local to the player pane, and avoids
    mutating the previous/current GL framebuffer outside normal DL execution.
    The earlier backdrop-depth policy still matters during replay: sky renders
    at far clip depth, depth-compares against existing room depth, and does not
    update depth, so it fills untouched backdrop pixels without cutting through
    rooms. Use `GE007_DISABLE_SKY_QUEUE=1` as the phase-order negative control
    and `GE007_DISABLE_SKY_BACKDROP_DEPTH=1` as the depth-owner negative
    control.

16. **Final-owner gaps need draw-boundary probes before behavior changes.**
    A `[TRI-PIXEL]` pre/post discontinuity does not automatically mean an
    unhandled RDP opcode exists. Screen-space rectangles are converted through
    `gfx_sp_tri1()`, and secondary-room XLU can be queued for later sorted
    emission. The native pixel probes now carry a `rect` object for
    `FILLRECT`/`TEXRECT` triangles, `GE007_TRACE_TRI_PIXEL_RECT_ONLY=1` isolates
    those rows, and `GE007_TRACE_ROOM_XLU_DEFER_PIXEL=1` brackets sorted
    deferred-room batches. If the remaining gap sits next to native sky,
    `GE007_TRACE_SKY_PREP_PIXEL=1` brackets sky setup and emit-state
    checkpoints. Dam pad10092 logical `[94,95]` ruled out rects and
    deferred-room XLU for the remaining `[8,8,8] -> [10,10,10]` local gap:
    68 rect rows changed zero pixels and the route emitted no
    deferred-room-XLU pixel rows. The sky-prep probe then showed
    `prepare_begin` already at `[10,10,10]`, proving the change happened before
    native sky setup. Queue replay fixes that phase-order class: the refreshed
    Dam pad10092 `[94,95]` default probe has zero same-frame `[TRI-PIXEL]`
    post/pre jumps, while `GE007_DISABLE_SKY_QUEUE=1` restores the old
    tri1080-to-sky gap. Treat draw-boundary probes as trace-routing evidence
    before changing blend, depth, fog, or texture policy.

    WebGPU probes must also make their command-stream boundary real. FID-0145
    fixed their previous-frame read by partially submitting the open scene,
    sampling the live scene texture, and resuming color/depth with Load/Store.
    The normal no-readback gameplay path remains a single end-of-frame submit;
    `port_webgpu_live_readback_guard` guards both sides of that contract.

17. **Shader sampling must honor N64 tile masks before filtering.**
    GL repeat/mirror repeats the full uploaded texture. N64 tile masks can
    repeat or mirror a smaller texel-period inside that upload, so masked axes
    must be wrapped in shader texel space before any nearest, bilinear, or
    N64 3-point samples are taken. The default renderer now emits per-axis mask
    shader flags only for used texture units, passes signed mask periods
    (negative means mirror), wraps the sample coordinate before filtering, and
    promotes masked linear textures to shader N64 filtering so every 3-point
    tap sees the same effective tile period. Use
    `GE007_DIAG_DISABLE_SHADER_TILE_MASK=1` as the focused negative control.
    2026-06-28 proof: a bounded Surface probe logged 80
    `[SETTEX-MATERIAL-CC]` rows with 76 active used `TEXEL1` S/T mask rows; the
    first active row was `opts=0x18003012`, `texnum=1264`, `tex_used=(1,1)`,
    and `tilex1=(2,0,4,4,8,0,62,62)` on a 32x32 upload. A tightened Dam trace
    logged zero mask bits in 200 rows, proving the option gate does not emit
    dead unused-`TEXEL1` variants. Surface renderer parity capture still passed
    with the shader tile-mask path live.

18. **Portal edge rescue covers empty-aperture one-hop under-admission.**
    Train room 51's rear office windows can face exterior/shutter geometry at a
    grazing angle where portal 59 to room 53 passes the room/side/depth checks,
    but its projected portal aperture collapses to an empty 2D bbox. Plain
    portal BFS then keeps only the current room, leaving a sky/backdrop-heavy
    window fill instead of the adjacent Train geometry behind the slats.
    `GE007_DRAW_NEIGHBOR_ROOMS=1` proves the missing-room class, but remains too
    broad because it admits every one-hop neighbor with a fullscreen bbox. The
    default path now records only empty-aperture portal rejects, then rescues the
    exact destination room after normal BFS if the source room rendered, the
    destination is still a marked one-hop neighbor, and the destination room AABB
    intersects the viewport. Rescued rooms do not enqueue further portals. Keep
    `GE007_PORTAL_EDGE_RESCUE=0` as the negative control, and use
    `tools/train_window_backdrop_regression.sh` before changing portal BFS,
    native sky backdrop depth, or neighbor-room policy.

## Guardrails

Use these habits before accepting renderer changes:

- Do not make `GE007_FORCE_POINT_FILTER`, `GE007_FORCE_LINEAR_FILTER`,
  `GE007_FORCE_ROOM_POINT_FILTER`, `GE007_DISABLE_N64_FILTER`, or
  `GE007_FOG_USE_LINEAR_DEPTH` default behavior. They are A/B probes.
- Treat renderer changes as suspect if they alter screenshot composition while
  a repo-root `ge007.ini` is changing `Video.FovY` or `Video.RenderScale`. For
  stock-ish captures, pass explicit config overrides.
- Before trusting a visual comparison, run the renderer from a build that
  matches the branch under review. Local untracked `.c` files are picked up by
  the CMake source glob, so use a clean worktree or check
  `git ls-files --others --exclude-standard 'src/**/*.c'` first.
- Keep automated renderer captures on the pinned 640x480 validation window
  unless the test is specifically exercising user-window/high-DPI behavior.
  Use `--config-override Video.RenderScale=2` or `Video.MSAA=4` for targeted
  scene-target probes, and keep the overrides visible in the artifact summary.
- For fogged secondary-room `G_SETTEX` XLU artifacts, check the RDP
  coverage-memory route before expanding sort/order heuristics. The strongest
  Surface signal was `GE007_DISABLE_ROOM_XLU_CVG_MEMORY=1` versus default, not
  `GE007_DISABLE_ROOM_XLU_DEFER=1` or `GE007_DISABLE_ROOM_XLU_SORT=1`.
  Use `tools/surface_xlu_cvg_memory_regression.sh --no-build` for a repeatable
  Surface proof; add `--msaa-values "0 4"` when touching scene-target resolve,
  coverage-alpha storage, or framebuffer snapshot code.
- When using an older checkout or `main` binary as a visual reference, verify
  that it actually honors the same config path. Older binaries can ignore
  `--config-override`, fall back to a 1440x810 default window, and make a
  healthy renderer look zoomed or composition-shifted when compared to the
  pinned 640x480 branch captures. Either compare two pinned builds or compare
  both with default overrides disabled. The current `playability_smoke.sh`
  audits generated `ge007.ini` values after each run so this fails fast for the
  normal validation lane.
- When reviewing texture-cache changes, check decoded row pitch and full-source
  footprint, not just visible width/height/format/address.
- When reviewing `G_SETTEX` changes, trace material clamp/shift/offset state
  with `GE007_TRACE_SETTEX_MATERIAL_CC='*'` before changing global sampler
  policy.
- When reviewing `G_SETTEX` tile changes, inspect both `tilex*` extended state
  and mask option bits (`0x02000000` through `0x10000000`). Mask bits should
  appear only for texture units consumed by `tex_used`.
- When reviewing `G_SETTEX` LOD changes, inspect the material trace `lodgate`
  fields before tuning color or alpha. For authored room-XLU trilerp, the
  durable proof is draw class, raw room-XLU mode, valid `G_SETTEX` tile 1, and
  packed RGB `LOD_FRACTION`; do not require the room-matrix bit to be true.
- When an early renderer decision needs texture-use information, do not feed a
  raw combine mode into `gfx_cc_get_features()`. Use raw combiner fields until
  a `ColorCombiner` exists, then use the generated shader ids.
- When isolating a suspect `G_SETTEX` material, use
  `GE007_TINT_TEX=min:max` or `GE007_SKIP_TEX=min:max`. These match stable game
  texture numbers, not transient GL texture ids.
- Do not add special low nearest thresholds for clamped non-texture-edge room
  materials. Use
  `GE007_DIAG_N64_FILTER_CLAMPED_NON_TEXEDGE_NEAREST_THRESHOLD=N` to A/B that
  class; `0.05` is a known Bunker close-texture smear regression, and the
  default `1.0` threshold is the validated baseline.
- Keep `GE007_DIAG_DISABLE_SHADER_CLAMP=1` as a negative control only. It is
  useful for proving clamp policy is involved, but a Runway pass showed it does
  not explain every apparent material color delta.
- Treat `GE007_SKY_UV_SCALE=1.0` as a negative control. It is useful for proving
  sky UV bugs, but it is not the calibrated native default.
- Treat `GE007_DISABLE_SKY_BACKDROP_DEPTH=1` as a negative control. It should
  only be used to prove native sky depth ownership; default sky should not
  overwrite pixels already owned by room depth.
- Treat `GE007_DISABLE_SKY_QUEUE=1` as a negative control. It restores native
  sky's old out-of-band draw timing and should only be used to prove a
  frame-phase or marker-replay hypothesis.
- When a pixel-owner trace jumps between logged triangles, first rerun with
  `GE007_TRACE_TRI_PIXEL_RECT_ONLY=1` and
  `GE007_TRACE_ROOM_XLU_DEFER_PIXEL=1`. Do not patch draw order, alpha, or fog
  policy until rectangles and deferred secondary-room XLU have been proved in
  or out.
- When the remaining owner jump is adjacent to native sky, add
  `GE007_TRACE_SKY_PREP_PIXEL=1`. In the default path, expect `queue_*` events
  during display-list construction and `replay_*` events during `gfx_run_dl()`;
  if `GE007_DISABLE_SKY_QUEUE=1` restores an unexplained jump, the evidence
  points at frame-phase scheduling or marker replay, not sky texture sampling
  or fog math.
- Do not use `g_freezeInput` as a blanket native-look gate. It should remove
  live input from deterministic captures while preserving authored
  `GE007_AUTO_LOOK_*` probes. Use `tools/scripted_look_smoke.sh` before
  accepting changes in the native mouse/right-stick handoff.
- Use fog traces before changing texture decode for distant low-detail scenes.
- When a visual change appears only with `Video.RenderScale > 1` or MSAA, first
  compare against `Video.RenderScale=1`, then audit raw GL state restored by the
  scene-target/output-filter helpers. Do not assume the material trace is wrong;
  queued VBOs can be correct while the GL binding they flush against is stale.
- RDP framebuffer-memory blend paths must snapshot a single-sample color
  source. With `Video.MSAA>0`, resolve the MSAA scene target into `g_scene_fbo`
  before copying into the shader snapshot texture; copying directly from the
  multisample draw FBO is not a portable GL path. The focused Surface guard is
  default versus `GE007_DISABLE_ROOM_XLU_CVG_MEMORY=1` at both `Video.MSAA=0`
  and `Video.MSAA=4`.
- RDP coverage-memory blend paths must not use ordinary alpha-blend output alpha
  as their coverage store. Keep `tools/check_rdp_coverage_memory_backend_guard.py`
  green when changing scene-target enablement, blend-mode state, or
  framebuffer-memory shader setup.

## Validation

The broad native acceptance lane is:

```sh
./tools/playability_smoke.sh --all --no-build \
  --rom /path/to/baserom.u.z64 \
  --binary build/ge007
```

That run direct-boots the 20 supported solo stages, drives deterministic
gameplay input, audits movement/render counters, audits screenshot health, and
writes a `contact_sheet.png` for manual visual review. It proves the levels
boot, move, render nonblank frames, and avoid known render-health counters; it
does not prove hardware-perfect output.

For branch-vs-reference visual comparisons, first confirm the captures have the
same viewport/composition. A high changed-pixel percentage is expected if one
binary was captured through the pinned 640x480 validation window and the other
used the old 1440x810 default. Once the aspect is matched, filter/sky/fog
changes should be reviewed by region and with material traces rather than by
whole-frame changed-pixel percentage alone.

`playability_smoke.sh` pins `Video.WindowWidth=640`,
`Video.WindowHeight=480`, and `Video.WindowMode=windowed` by default so
generated `ge007.ini` state and high-DPI desktop geometry do not change the
capture lane. For scene-target validation, add explicit overrides:

```sh
./tools/playability_smoke.sh --all --no-build \
  --rom /path/to/baserom.u.z64 \
  --binary build/ge007 \
  --config-override Video.RenderScale=2 \
  --config-override Video.FovY=60
```

For smaller renderer A/B captures:

```sh
./tools/renderer_parity_capture.sh --no-build \
  --rom /path/to/baserom.u.z64 \
  --binary build/ge007
```

Before trusting broad look-direction screenshot deltas, run:

```sh
./tools/scripted_look_smoke.sh --no-build \
  --rom /path/to/baserom.u.z64 \
  --binary build/ge007
```

This asserts that deterministic input freeze is still blocking live input while
authored `GE007_AUTO_LOOK_UP` changes the gameplay pitch by a measurable amount.
If it fails, visual comparisons that rely on `GE007_AUTO_LOOK_*` are not valid.

For ROM-backed movement/intro parity, the public stock-oracle route set is
currently Dam-focused. `stock_level` in a route gates and audits the target raw
`LEVELID`; it does not navigate the stock frontend to arbitrary missions by
itself. Add a verified stock menu route or direct-stage hook before claiming
stock-backed parity for another level.

When an instrumented ares binary is available, `movement_oracle_capture.sh`
also asks stock ares to dump a local PPM framebuffer and health-checks that
screenshot. Use that as the first emulator-pixel sanity check before drawing
conclusions from native-only captures. For routes that spend extra emulator time
in menus, set `stock_screenshot_frame` to the gameplay-equivalent frame instead
of trusting the final process frame; the Dam forward route uses this to avoid
capturing the mission-failed report screen. The stock screenshot is generated
from the user's ROM and must remain local.

## Manual Priority List

After the automated run is green, manually inspect these first because they
exercise the failure modes above:

- Dam: guard-tower glass, bullet crack decals, truck body and wheels, outdoor
  rock/road textures.
- Surface 1/2: large snow fields, sky/fog boundary, close wall texture filtering.
- Cradle: long bridge texture stability, fog depth, guard faces/weapons, no
  startup crash.
- Silo, Depot, Train, Caverns: repeated room textures, long corridors, large
  flat surfaces, and alpha/transparency edges.
