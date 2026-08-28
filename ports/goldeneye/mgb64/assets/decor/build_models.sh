#!/usr/bin/env bash
# Rebuild ALL Surface showcase decor models (deterministic: same args =>
# byte-identical outputs). Models are generated/fetched, never tracked — the
# repo ships only this recipe + the placement manifest, keeping the tree
# binary-free. Sources are CC0 (Poly Haven scans + first-party art).
#
# Heads-up on size: the photoscan sources total ~1.5 GB of one-time downloads
# into the gitignored .cache/ (pine_tree_01 alone is ~900 MB). Decimation
# runs locally via tools/decor/decimate_gltf.py.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
CACHE="$HERE/.cache"
MODELS="$HERE/models"
mkdir -p "$CACHE" "$MODELS"

# ---- fetch a Poly Haven model export (gltf + bin + textures) into cache ----
ph_fetch() { # <asset_id>
  local id="$1" dst="$CACHE/$1"
  if [ -s "$dst/${id}_1k.gltf" ]; then return 0; fi
  mkdir -p "$dst/textures"
  python3 - "$id" "$dst" <<'PY'
import json, sys, urllib.request
aid, dst = sys.argv[1], sys.argv[2]
api = json.load(urllib.request.urlopen(
    "https://api.polyhaven.com/files/" + aid))
inc = api["gltf"]["1k"]["gltf"]
urllib.request.urlretrieve(inc["url"], f"{dst}/{aid}_1k.gltf")
for rel, meta in inc["include"].items():
    urllib.request.urlretrieve(meta["url"], f"{dst}/{rel}")
PY
}

# ---- photoscans: fetch + decimate (args mirror the committed manifest;
#      each output carries a provenance.json with these args) ----
DEC="$REPO/tools/decor/decimate_gltf.py"
ph_url() { echo "https://polyhaven.com/a/$1"; }

ph_fetch fir_sapling_medium
python3 "$DEC" "$CACHE/fir_sapling_medium/fir_sapling_medium_1k.gltf" \
  --name fir_hd --grid 170 --ambient 0.85 --snow 0.45 --license CC0 \
  --url "$(ph_url fir_sapling_medium)" --cutout-material twig --out "$MODELS"

ph_fetch fir_tree_01
python3 "$DEC" "$CACHE/fir_tree_01/fir_tree_01_1k.gltf" \
  --name fir_big --grid 220 --ambient 0.85 --snow 0.5 --license CC0 \
  --url "$(ph_url fir_tree_01)" --cutout-material twig --out "$MODELS"

ph_fetch pine_tree_01
python3 "$DEC" "$CACHE/pine_tree_01/pine_tree_01_1k.gltf" \
  --name pine_big --grid 240 --ambient 0.85 --snow 0.5 --license CC0 \
  --url "$(ph_url pine_tree_01)" --cutout-material twig --out "$MODELS"

ph_fetch boulder_01
python3 "$DEC" "$CACHE/boulder_01/boulder_01_1k.gltf" \
  --name boulder_a --grid 90 --ambient 0.8 --snow 0.75 --license CC0 \
  --url "$(ph_url boulder_01)" --out "$MODELS"

ph_fetch namaqualand_boulder_03
python3 "$DEC" "$CACHE/namaqualand_boulder_03/namaqualand_boulder_03_1k.gltf" \
  --name boulder_b --grid 90 --ambient 0.8 --snow 0.75 --license CC0 \
  --url "$(ph_url namaqualand_boulder_03)" --out "$MODELS"

ph_fetch namaqualand_boulder_05
python3 "$DEC" "$CACHE/namaqualand_boulder_05/namaqualand_boulder_05_1k.gltf" \
  --name boulder_c --grid 90 --ambient 0.8 --snow 0.75 --license CC0 \
  --url "$(ph_url namaqualand_boulder_05)" --out "$MODELS"

ph_fetch dead_tree_trunk
python3 "$DEC" "$CACHE/dead_tree_trunk/dead_tree_trunk_1k.gltf" \
  --name dead_trunk --grid 120 --ambient 0.8 --snow 0.6 --license CC0 \
  --url "$(ph_url dead_tree_trunk)" --out "$MODELS"

ph_fetch dead_tree_trunk_02
python3 "$DEC" "$CACHE/dead_tree_trunk_02/dead_tree_trunk_02_1k.gltf" \
  --name dead_trunk_b --grid 120 --ambient 0.8 --snow 0.6 --license CC0 \
  --url "$(ph_url dead_tree_trunk_02)" --out "$MODELS"

# ---- first-party generated trees (classic N64-path set + HD set) ----
BARK="$CACHE/pine_tree_01/textures/pine_tree_01_bark_diff_1k.jpg"
GEN="$REPO/tools/decor/gen_tree3d.py"
COMMON=(--bark "$BARK" --bark-license CC0
        --bark-url "$(ph_url pine_tree_01)" --out "$MODELS")
python3 "$GEN" --name spruce_a --seed 7  --whorls 10 --cards 6 "${COMMON[@]}"
python3 "$GEN" --name spruce_b --seed 23 --whorls 12 --cards 7 "${COMMON[@]}"
python3 "$GEN" --name spruce_c --seed 41 --whorls 9  --cards 6 --snow 0.75 "${COMMON[@]}"
HD=(--detail high --tex-size 512)
python3 "$GEN" --name spruce_hd_a --seed 7  --whorls 22 --cards 7 "${HD[@]}" "${COMMON[@]}"
python3 "$GEN" --name spruce_hd_b --seed 23 --whorls 26 --cards 8 "${HD[@]}" "${COMMON[@]}"
python3 "$GEN" --name spruce_hd_c --seed 41 --whorls 20 --cards 7 --snow 0.8 "${HD[@]}" "${COMMON[@]}"

echo "decor models ready: $MODELS"
