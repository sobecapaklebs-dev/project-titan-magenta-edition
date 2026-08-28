# `tools/texpack` — HD texture-pack pipeline

Offline tooling to build an **HD texture pack** from your own ROM's static settex
textures, using GPU-accelerated AI super-resolution (Real-ESRGAN, Vulkan →
Metal/MoltenVK on macOS) **and** first-party procedural synthesis
(`synth_texture.py`) for tiled surfaces.

This is the *content-authoring* half of the texture remaster. The *runtime* half —
the in-engine loader that consumes the pack (Engine B) — is already shipped.

> ⚠️ **Dumps, AI-upscaled packs, and `synth_texture.py --match` outputs are
> ROM-derived and personal-use only.** They are derivative works of the original game
> art; they are gitignored and must never be committed or redistributed. Generic
> procedural output (`--mean/--sd`, no ROM match) and vetted PD/CC0/original art are
> the distributable path. The repo ships only first-party code; the upscaler binary is
> fetched on demand into a gitignored cache.

## One-time setup

```bash
tools/texpack/fetch_realesrgan.sh          # pinned, checksum-verified, into .bin/ (gitignored)
pip install -r tools/texpack/requirements.txt   # Pillow (decode dumps) + numpy (procedural synth)
```

## Workflow

```bash
# 1. Dump a level's static settex textures from the game (token-keyed):
GE007_DUMP_SETTEX_TEXTURES='*' GE007_DUMP_SETTEX_DIR=/tmp/td \
  ./build/ge007 --level 33 --deterministic        # ... your usual launch flags

# 2. Upscale the dump into a pack (GPU; ~seconds per level):
python3 tools/texpack/build_pack.py --dump /tmp/td --out ~/ge007_hd

# 3. Play with the pack (the in-game loader, Engine B, ships today):
GE007_TEXTURE_PACK=~/ge007_hd ./build/ge007 --level 33
```

Output matches the loader key: `<out>/textures/tok####.png` (zero-padded 4-digit
settex token, e.g. `tok0022.png`; `texture_pack.c:45`).

Only static settex tokens load today. `GE007_DUMP_LOADED_TEXTURES` hash-key dumps are
not packable yet because the runtime loader has no hash-key lookup path.

## Options

| Flag | Default | Notes |
|------|---------|-------|
| `--scale` | `4` | 2 / 3 / 4 |
| `--model` | `realesrgan-x4plus` | also `realesrgan-x4plus-anime`, `realesrnet-x4plus`, `realesr-animevideov3` |
| `--route` | off | route model by `draw_class` via a `*.texmanifest.csv` (reader exists; the C manifest emit is not built yet): photo model for world/prop/character, anime model for HUD |
| `--realesrgan` / `--models` | `.bin/...` | override the fetched binary/model paths |

## How it works

`build_pack.py` decodes each static settex dump (`.ppm` today; `.png` once the
engine dumps PNG directly), parses the settex token from the filename, normalizes it
to `tok####`, batches by model, and runs `realesrgan-ncnn-vulkan` in directory mode
on the GPU. Real-ESRGAN super-resolution is deterministic (feed-forward — same input
always yields the same output), so a pack is reproducible.

Two quality safeguards run automatically:
- **Seam-safe tiling** — textures whose edges wrap (detected by edge-matching) are
  tiled 3×3, upscaled, then center-cropped, so the result stays seamless across mapped
  quads instead of showing broken-wrap seams. Disable with `--no-seamless`.
- **Anti-hallucination** — tiny source textures (≤16 px) use deterministic Lanczos
  instead of the AI model, which otherwise invents junk on small ambiguous tiles
  (e.g. a "smiley face" on a hubcap).

**Model tip:** for GoldenEye's flat, low-color textures, `--model realesrgan-x4plus-anime`
is usually cleaner (less speckle) than the default `realesrgan-x4plus`.

See [docs/VISUAL_MODES.md](../../docs/VISUAL_MODES.md) for how the pack plugs into the
faithful / remaster visual modes.

## When upscaling isn't enough: procedural surface synthesis

Real-ESRGAN is great for *detailed* art (signs, crates, characters) but it degrades
big **tiled** surfaces: the N64 source is tiny (e.g. the Dam ground is 64×32, the
rock wall 64×64) so there's no real detail to recover — the AI smooths the ground
gravel into a flat smear and melts the rock speckle into oily blobs. For those hero
surfaces, **synthesize** a high-res seamless texture from scratch instead:

```bash
# identify the surface's token, then synthesize a tone-matched seamless replacement
# from your own dump (local-only / Tier B):
python3 tools/texpack/synth_texture.py gravel \
    --match dump/ge007_settex_0022.rgba.ppm --size 1024x512 \
    --out ~/ge007_dam_hd/textures/tok0022.png        # Dam ground
python3 tools/texpack/synth_texture.py rock \
    --match dump/ge007_settex_0949.rgba.ppm --size 1024x1024 \
    --out ~/ge007_dam_hd/textures/tok0949.png        # Dam rock wall
```

For distributable procedural output, do **not** use `--match`; set tone explicitly:

```bash
python3 tools/texpack/synth_texture.py gravel \
    --mean 64 --sd 22 --size 1024x512 \
    --out ~/ge007_dam_hd/textures/tok0022.png
```

`synth_texture.py` builds the surface from **band-pass + spectral (FFT) noise** —
seamless and isotropic by construction (the DFT is periodic, so opposite edges match
with no grid and no directional banding). Two properties make the result drop-in:

- **Histogram-matched** to the dump original (`--match`): the synthetic surface gets
  the original's *exact* tonal distribution — mean, contrast, **and** the bright
  pebble flecks — so it reads at the same brightness in-game (the level's vertex-baked
  lighting is unchanged). This is local-only/Tier B. Use `--no-hist` to match only
  mean/sd, or `--mean/--sd` to set tone explicitly for the distributable route.
- **Tile-uniform** (high-passed): large-scale brightness variation is removed so the
  texture looks consistent across a big tiled plane instead of cloudy/blotchy — the
  way the original's pure high-frequency speckle behaves.

The generator is **first-party math — no AI, no scraped assets**. Output with generic
tone is distributable; output with `--match` is treated as ROM-derived. The tool
writes runtime pack PNGs only (`tok####.png`). It's gameplay-neutral by the same
contract: the loader keeps native tile dims, so UVs are unchanged. Deterministic
(same args + `--seed` ⇒ same texture). Identifying a surface's token: paint candidate
tokens distinct hues in a throwaway pack and read the on-screen color back (vertex
shading is a grayscale multiply, so hue is preserved).

## Alternatives / future

- **Algorithmic** (deterministic, no AI): swap in `xbrz`/`scale2x` for pixel-art/UI tiles.
- **Open-licensed substitution** (the other *distributable* route): replace generic
  surfaces with independent PD/CC0/original art, or attribution/share-alike assets
  only after `NOTICE` and compatibility review. The manual half ships as
  `cc0_import.py` (resize → optional wrap-padded tile-uniform high-pass → chroma
  control → tone-lock to the curated avgRGB → optional I-format intensity-alpha →
  `tok####.png` + a `tok####.provenance.json` record); Surface 1's 36 curated
  heroes are its production use (`overrides/surface1.json`,
  `docs/design/remaster-aaa/09-surface-showcase.md`). The automatic
  `--cc0-library`/Router track is still future work (see
  `docs/design/REMASTER_ROADMAP.md` §3 and §6 P2).
- **First-party generated art** (also distributable): `gen_treeline.py` draws the
  distant snowy-conifer hillside bands (layered spruce silhouettes) that AI
  upscaling melts — deterministic, horizontally seamless, with an
  `--alpha-cutout` mode for translucent XLU overlay strips.
- **DDS/BCn** output for VRAM/load wins on a full ~3001-token pack (a later iteration).
