# MGB64 Visual Modes & Remaster Flags

MGB64 ships with an optional **visual remaster** layered on top of the faithful
port. **Everything is feature-flagged**, so you choose how it looks — from the
pixel-faithful original, to a clean HD upscale, to the full cinematic remaster.
All flags are live `Video.*`/`Input.*` settings (also settable per-launch via
`--config-override` or `GE007_*` env vars), and `Video.Gamma` is always honored.

> Run from the repo root (where your `baserom.u.z64` symlink lives).

## The three modes

### 1. Faithful original (pixel-accurate)
The stock N64 look and feel. No HD textures, no post-FX, classic FOV, no modern
crosshair/hitmarkers, vanilla gamepad aim, no minimap. **One flag:**
```bash
./build/ge007 --level 33 --faithful
```
`--faithful` pins the whole pre-remaster baseline for that launch **only**. A
faithful session is **read-only for config** — `ge007.ini` is never rewritten
(not on quit, not from the in-game menu, and `--config-set`/`--reset-config` are
not persisted while it is active), so it never disturbs your saved remaster
config. It is exactly equivalent to:
`Video.RemasterFX=0 Video.RenderScale=1 Video.MSAA=0 Video.TexturePack=""
Video.FovY=60 Video.ViewmodelFov=60 Input.ModernCrosshair=0 Input.HitMarkers=0
Input.ReticleTargetFeedback=0 Input.ViewmodelSway=0 Input.GamepadLookCurve=1.0
Input.GamepadDeadzone=0.2441 Input.GamepadRadialDeadzone=0 Input.GamepadFpsScale=0
Input.MinimapEnabled=0`.

An explicit `--config-override`/`GE007_*` still wins over the preset (e.g.
`--faithful --config-override Video.FovY=90` = faithful but wide FOV). Two things
`--faithful` intentionally leaves alone: `Input.SteadyView` (a PC mouse/stick-look
correctness default, not a cinematic effect — toggle it separately if you want the
gait camera roll) and the compile-time effect-slot count.

### 2. Faithful upscale (original look, higher fidelity)
The original art direction, but with **HD textures + supersampling** — crisp, not
re-graded. `RemasterFX=0` drops the cinematic color work; HD textures and SSAA are
*fidelity*, not *look*, so they stay. **One flag:**
```bash
# Faithful look at 2× SSAA, HD-pack-ready (read-only session, like --faithful):
./build/ge007 --level 33 --faithful-hd
# ...with your HD pack:
GE007_TEXTURE_PACK="$HOME/ge007_dam_hd" ./build/ge007 --level 33 --faithful-hd
```
`--faithful-hd` is the one-flag equivalent of `Video.RemasterFX=0 Video.RenderScale=2`
plus the faithful FOV/crosshair/aim baseline. It does **not** pin `Video.TexturePack`,
so any pack you supply is honored; it needs no Metal backend (no SSAO/post-FX).

### 3. Full remaster (default — cinematic)
HD textures + SSAA + FXAA + bloom + color grade + filmic tonemap + **SSAO** + the rest.
```bash
# One switch — the full immaculate remaster, all post-FX incl. SSAO:
./build/ge007 --level 33 --remaster
# (add HD textures with GE007_TEXTURE_PACK=... ; --config-override Video.RenderScale=4 for max SSAA)
```
`--remaster` is the mirror of `--faithful`: it pins RemasterFX + SSAO + bloom/FXAA/
tonemap/grade/vignette/sharpen/dither + 2× SSAA for that launch, and env/
`--config-override` still win. **On macOS it selects the native Metal renderer
(`GE007_RENDERER=metal`)** because SSAO op-hangs Apple's deprecated GL-over-Metal
translator — the native Metal backend is what makes SSAO available on macOS. On
Linux/Windows the native GL path runs the same effects (SSAO included) directly.
Manual equivalent: `GE007_RENDERER=metal ./build/ge007 --level 33 --config-override Video.Ssao=1`.

## Master switches

| Setting | Default | Effect |
|---|---|---|
| **`Video.RemasterFX`** | `1` | Master post-FX switch. `0` = bypass **all** cinematic post-FX (grade, tonemap, bloom, vignette, sharpen, dither, FXAA) → faithful look. HD textures + SSAA unaffected. |
| **`Video.TexturePack`** | *(empty)* | Directory of an HD texture pack (`textures/tok####.png`). Empty = stock textures. |
| **`Video.RenderScale`** | `2.0` | Internal supersampling (1.0–4.0). Fidelity only; never changes the look. |
| **`Video.HiDPI`** | `0` | Retina/high-DPI drawable opt-in. Keep off for steadier large-window performance; use `1` when native display-pixel sharpness is worth the fill-rate cost. |

## All visual flags

| Setting | Default | Notes |
|---|---|---|
| `Video.FovY` | `50` | Vertical FOV (45–105). 50 keeps the classic ~75° horizontal on 16:9 (60 was the 4:3 original and looks fisheye-wide on widescreen). |
| `Video.ViewmodelFov` | `50` | Weapon FOV; `0` = follow world FOV. |
| `Video.Saturation` / `Contrast` / `Brightness` | `1.15` / `1.08` / `0.04` | Output color grade (`1`/`1`/`0` = identity). |
| `Video.Tonemap` | `1` | Gentle filmic shadow-lift + highlight rolloff. |
| `Video.Bloom` (+`BloomThreshold`/`Intensity`) | `1` (0.8/0.5) | Light bleed on bright areas. |
| `Video.Ssao` (+`SsaoRadius`/`SsaoIntensity`) | `0` (0.5/1.0) | Screen-space ambient occlusion: depth-based contact darkening in crevices/corners/under geometry. Needs `RemasterFX=1`. Default-off (opt-in cost). Runs at verified GL parity on the default **WebGPU** backend on all platforms, including macOS (planar v1 — the historical Apple GL-over-Metal hang was specific to the old GL fallback and does not affect WebGPU or Metal). `--remaster` turns it on; no backend override is needed to get it. |
| `GE007_RENDERER` | `webgpu` | **WebGPU (wgpu-native) is now the default backend** (built by default; `-DMGB64_WEBGPU_BACKEND=OFF` for a GL/Metal-only binary). `gl`/`opengl` selects the OpenGL fallback (retained one release); `metal` selects the native Metal backend (macOS only; explicitly selectable, no longer forced by `--remaster` as of 2026-07-16 — WebGPU carries remaster post-FX/SSAO at GL parity). The flip is sim-invariant — determinism tapes are byte-exact on WebGPU. |

### WebGPU backend (`GE007_RENDERER=webgpu`)

The WebGPU backend (`gfx_webgpu.c`) is the in-progress single cross-platform
renderer built on **wgpu-native** — one backend that dispatches to Metal
(macOS), D3D12/Vulkan (Windows), and Vulkan (Linux/handheld), replacing the
GL+Metal fork. It implements the same Fast3D `GfxRenderingAPI` seam as GL/Metal,
generating WGSL combiner shaders at runtime.

- **Build:** `cmake -B build-webgpu -DMGB64_WEBGPU_BACKEND=ON . && cmake --build build-webgpu --target ge007`. The option is OFF by default, so the shipping `ge007` links no wgpu symbols and stays byte-identical.
- **Run:** `GE007_RENDERER=webgpu build-webgpu/ge007 --level dam`.
- **Status:** renders the game at near-parity with GL (geometry, textures, depth, blend, viewport/scissor, screenshots/readback). Not yet at pixel-parity: the N64 3-point texture filter and the minimap/radar overlay are not yet ported, and the exact coverage/RDP-memory blend variants approximate to alpha. Not the default — the flip is gated on owner gameplay validation per the release doctrine.
- **Debug:** `GE007_WEBGPU_DUMP_FRAME=<n>` writes presented frame `n` to `/tmp/webgpu_frame_<n>.ppm`.
| `Video.Vignette` | `0.15` | Soft edge falloff. |
| `Video.Fxaa` / `Video.Sharpen` | `1` / `0.15` | Edge AA / adaptive sharpen. |
| `Video.OutputDither` | `1` | Anti-banding ordered dither. |
| `Video.GradePresets` | `1` | Subtle per-level mood grade. |
| `Video.MSAA` | `0` | Multisample AA (redundant with SSAA; off by default). |
| `Video.HiDPI` | `0` | High-DPI drawable opt-in; large Retina windows can be fill-rate bound, especially on outdoor fog/alpha-heavy stages. |
| `Input.ModernCrosshair` / `Input.HitMarkers` | `1` / `1` | Modern reticle / on-hit markers. |
| `Input.ReticleTargetFeedback` | `1` | Reticle turns green on an enemy. |
| `Input.ViewmodelSway` | `1.0` | Subtle weapon sway. |
| `Input.Gamepad{LookCurve,Deadzone,RadialDeadzone,FpsScale}` | curve | Modern pad aim feel. |

Each is independent — e.g. keep the HD textures + SSAA but turn off only bloom:
`--config-override Video.Bloom=0`.

## Opt-in lighting (default-off, under review)

One lighting feature is merged but **default-off** and **not** part of
`--remaster` yet — enable it explicitly to try it:

| Flag | Env var | What it does |
|---|---|---|
| `Video.EnvSmoothNormals` (+`Video.EnvRelightBlend`, 0–1, default 0.6) | `GE007_ENV_SMOOTH_NORMALS=1` | Relights static room surfaces with CPU-averaged smooth normals against the level sun, erasing the baked per-quad "tiger-stripe" lighting seams on cliffs/floors (most visible on Dam). |

It is gated so the all-off render is byte-identical, and the blend strength /
default-on decision are still being tuned.

**Shoot out the lights** (`GE007_SHOOT_OUT_LIGHTS`) is now **default-ON** — a
faithfulness restoration (survey class C3): shooting a ceiling/wall light fixture
darkens its surface (with a spark) and it stays dark, exactly as on N64. Verified
W4.E1.T2 (6,210 fixture triangles across bunker1/silo/control, 0 DIVERGE) and
sim-invariant (vertex-shade only — collision/LOS/auto-aim untouched).
`GE007_SHOOT_OUT_LIGHTS=0` is the A/B escape hatch (render byte-identical to the
old stub).

## HD texture pipeline

HD packs are built **from your own ROM's static settex dumps** with the GPU AI-upscaler in
[`tools/texpack`](../tools/texpack/README.md) (Real-ESRGAN, with seam-safe tiling
and anti-hallucination for tiny textures):
```bash
tools/texpack/fetch_realesrgan.sh                       # one-time
GE007_DUMP_SETTEX_TEXTURES='*' GE007_DUMP_SETTEX_DIR=/tmp/td ./build/ge007 --level 33 --deterministic
python3 tools/texpack/build_pack.py --dump /tmp/td --out ~/ge007_hd --model realesrgan-x4plus-anime
GE007_TEXTURE_PACK=~/ge007_hd ./build/ge007 --level 33
```

The runtime loader consumes `textures/tok####.png` only. Hash-key
`GE007_DUMP_LOADED_TEXTURES` dumps are not runtime-loadable yet.

### ⚠️ Copyright / provenance — packs stay local
Texture dumps, AI-upscaled packs, and `synth_texture.py --match` outputs are
**derivative works of the original game art** (personal-use, your own ROM) and are
**never committed**: image/dump formats are gitignored and the
`scripts/ci/check_no_rom_data.sh` contamination guard hard-fails on tracked images.
The repo ships first-party tooling, the public-domain `stb_image.h` decoder, and
only vetted distributable art/presets. The loader reads a user-supplied pack at
runtime (default empty = stock).
