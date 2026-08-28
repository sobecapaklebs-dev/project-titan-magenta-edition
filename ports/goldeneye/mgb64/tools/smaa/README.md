# SMAA lookup-table generation

This directory generates the two lookup-table (LUT) textures used by the
[SMAA](http://www.iryoku.com/smaa/) (Enhanced Subpixel Morphological
Antialiasing) filter. The LUTs are emitted as committed C headers so the game
build never depends on Python, Pillow, or a network fetch.

## Files

| File | Origin | License |
| --- | --- | --- |
| `AreaTex.py` | Vendored **verbatim** from the SMAA reference implementation, `Scripts/AreaTex.py`. Generates the SMAA *area* texture. | MIT — see `LICENSE` |
| `SearchTex.py` | Vendored **verbatim** from the SMAA reference implementation, `Scripts/SearchTex.py`. Generates the SMAA *search* texture. | MIT — see `LICENSE` |
| `LICENSE` | The SMAA reference implementation's license (`LICENSE.txt`), copied verbatim. | MIT |
| `gen_luts.py` | First-party MGB64 wrapper. Reuses the vendored numeric kernels to emit the two committed C headers deterministically. | MGB64 (MIT) |

## Provenance

- **Upstream:** <https://github.com/iryoku/smaa>
- **Commit:** `71c806a838bdd7d517df19192a20f0c61b3ca29d` (2013-11-06)
- **Authors / copyright:** Jorge Jimenez, Jose I. Echevarria, Belen Masia,
  Fernando Navarro, Diego Gutierrez (see `LICENSE`).
- **License:** MIT. The upstream `AreaTex.py` / `SearchTex.py` scripts carry only
  a functional description comment; the license text lives in the repository's
  `LICENSE.txt`, vendored here as `LICENSE`.

This is the project's one-time network fetch for these scripts. Everything
afterward — LUT generation and verification — is fully offline and
deterministic. No ROM-derived data is involved at any step (Tier A2, rail R2).

## Generated headers

`gen_luts.py` writes two headers under `src/platform/fast3d/`:

| Header | Symbol | Dimensions | Format | Size |
| --- | --- | --- | --- | --- |
| `smaa_area_tex.h` | `g_smaa_area_tex` | 160 x 560 | RG8 (2 bytes/pixel) | 179 200 bytes |
| `smaa_search_tex.h` | `g_smaa_search_tex` | 64 x 16 | R8 (1 byte/pixel) | 1 024 bytes |

Each header also defines `SMAA_AREATEX_{WIDTH,HEIGHT,PITCH,SIZE}` /
`SMAA_SEARCHTEX_{WIDTH,HEIGHT,PITCH,SIZE}` for the upload path. The byte arrays
are byte-identical to the canonical SMAA reference textures
(`Textures/AreaTex.h` / `Textures/SearchTex.h` upstream).

## How `gen_luts.py` reuses the vendored scripts

- **Area texture:** the area math (`areaortho`, `areadiag`, the placement
  tables, the subsample offsets) is imported **unmodified** from `AreaTex.py`
  and assembled without Pillow. `AreaTex.py` does `from PIL import Image` at
  import time but the imported kernels never call PIL; the wrapper installs a
  lightweight stub when Pillow is absent, so no Pillow dependency is required.
- **Search texture:** the delta logic (`bilinear`, the edge reverse-lookup,
  `deltaLeft`, `deltaRight`) is re-expressed from `SearchTex.py`. That script's
  module body runs its texture build at import time and uses a Pillow transpose
  constant removed in Pillow >= 10, so it cannot be imported as a library; the
  re-expressed logic is verified to reproduce the canonical bytes exactly.

## Usage

```sh
# Regenerate both headers in place (runs the area brute-force sampling; a
# few minutes single-threaded):
python3 tools/smaa/gen_luts.py

# CI / verification: regenerate in memory and fail if the committed headers
# differ (byte-identical reproducibility gate):
python3 tools/smaa/gen_luts.py --check
```
