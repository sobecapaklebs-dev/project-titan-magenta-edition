# pikaVR 720p Composition Report — BuildFix23

## CP01 — Reference path audit

### REFERENCE_OUTPUT
- Physical mode: 1280x720 progressive.
- Visible framebuffer descriptor: 1280x720.
- Double buffering: 2.
- PSM: GS PSM16.

### REFERENCE_ALLOCATION
- GS PSM16 page geometry: 64x64 pixels, 8192 bytes/page.
- 1280 pixels = 20 GS pages horizontally.
- 720 visible lines require 12 GS page rows: `ceil(720/64)=12`.
- Storage height is therefore 12*64 = **768** lines.
- One scanout allocation: 20*12*8192 = **1,966,080 bytes**.
- Two framebuffers: **3,932,160 bytes**.
- Compact resource region before framebuffer 0: **262,144 bytes** including page-alignment padding.
- Total: **4,194,304 bytes (4 MiB)**.

### REFERENCE_REASON
The extra 48 storage rows are required by the two-dimensional GS PSM16 page geometry, not by the 720p timing or visible CRTC window. The renderer exposes only 720 visible lines; `storage_height=768` exists so the allocation covers twelve complete 64-pixel GS page rows. Removing those rows without changing the storage strategy would under-model the page footprint and is therefore not part of CP01.

### Compact resource region
Material order before the first page-aligned framebuffer is:
- Ribbon/wave texture 128x32 PSM32: 16,384 bytes.
- indexed font 384x192 PSM8: 73,728 bytes.
- indexed UI atlas 512x256 PSM8: 131,072 bytes.
- shared alpha CLUT 16x16 RGBA32: 1,024 bytes.
- avatar/transient scratch 256x64 PSM16: 32,768 bytes.
- payload subtotal: 254,976 bytes.
- page alignment gap before framebuffer 0: 7,168 bytes.
- compact region total: 262,144 bytes.

### Reference VRAM state
- `vram_total`: 4,194,304 bytes.
- `vram_framebuffers`: 3,932,160 bytes.
- compact resident resources + alignment: 262,144 bytes.
- `vram_free`: 0 bytes.
- permanent headroom: 0 bytes.

The reference path remains unchanged and is the BuildFix23 default until CP04 makes an evidence-based decision.

## CP02 — Optimized composition allocation plan

The optimized plan is now materialized as a pure host-testable graphics planning module and is compiled into the kernel object list without changing the active reference renderer.

- Internal visible composition: **960x540 PSM16**.
- Internal GS storage: **960x576**, 15x9 PSM16 pages = **1,105,920 bytes**.
- Physical output: **1280x720p**.
- Output GS storage: **1280x768**, 20x12 PSM16 pages = **1,966,080 bytes**.
- Optimized framebuffer footprint: **3,072,000 bytes**.
- Reference framebuffer footprint: **3,932,160 bytes**.
- Raw framebuffer reduction: **860,160 bytes**.

This CP deliberately does not promote or present the optimized path. CP03 owns deterministic scaling/presentation and text/UI-quality architecture; CP04 owns the default decision and reserved headroom.

## CP03 — Deterministic scaling and text/UI geometry

The optimized path is now implemented as an explicit opt-in TitanVideo policy. The BuildFix22 reference path remains the default and remains double-buffered.

### Optimized presentation path
- Internal composition: 960x540 visible, 960x576 GS storage, PSM16.
- Output: 1280x720 visible, 1280x768 GS storage, PSM16.
- Scale filter: LINEAR for the composition surface.
- Texture sample bounds use half-texel centers: (0.5, 0.5) through (959.5, 539.5).
- CLAMP is explicit to the 960x540 visible source, preventing sampling into hidden storage rows.
- Existing material texture POINT/LINEAR semantics remain separate and unchanged.

### Text/UI strategy evaluated
CP03 implements strategy A: the complete shell composition, including UI/text, is rendered into the 960x540 internal target and scaled once to 1280x720.

Strategy B (low-resolution scene/effects followed by a separate native-output UI/text pass) was not implemented in CP03 because it would require splitting the existing shell compositor and duplicating synchronization/state work before target evidence showed that the extra pass is justified.

Host geometry proves that logical placement/size is preserved: the 960->1280 and 540->720 scale factors exactly cancel the reduced internal mapping relative to the 854x448 logical canvas. This proves geometry, not perceived font sharpness.

### CP04 quality gate
Visual sharpness, shimmering, scanout behavior and real frame cost cannot be established by host contracts. The optimized path currently uses one internal composition surface plus one output scanout. Unlike the reference path's two output scanouts, the host environment cannot prove this single-scanout presentation is tear-free on PS2 hardware. CP04 must include that limitation in the default-path decision rather than silently promoting optimized mode.
