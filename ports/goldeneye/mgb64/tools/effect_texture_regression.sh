#!/bin/bash
#
# effect_texture_regression.sh -- Guard purple muzzle/explosion texture regressions.
#
# This lane replays the Dam firing/glass route with texture selection and
# pipeline tracing enabled. It asserts that texSelect() effects reach fast3d as
# static game texture cache keys, not raw heap pointers. Explosion-smoke frames
# are sampled by this fixture, so the lane also dumps the loaded texture uploads
# that actually feed GL and audits decoded RGB/alpha plus material state so
# purple-channel regressions do not pass on texture ID provenance alone. It also
# runs a direct first-person Dam firing capture and audits the visible muzzle
# settex textures 2157-2160.
#
set -euo pipefail
cd "$(dirname "$0")/.."

source tools/validation_common.sh

OUT_DIR="/tmp/mgb64_effect_texture_regression_$$"
BUILD_DIR="$(validation_default_build_dir)"
BINARY=""
ROM=""
DO_BUILD=1
TIMEOUT_SECONDS=90

usage() {
    cat <<'USAGE'
Usage: tools/effect_texture_regression.sh [options]

Options:
  --out-dir DIR        output directory (default: /tmp/...)
  --rom PATH           ROM path (default: movement_oracle_capture.sh default)
  --binary PATH        native binary path (default: movement_oracle_capture.sh default)
  --build-dir DIR      CMake build directory (default: movement_oracle_capture.sh default)
  --no-build           reuse an existing native binary
  --timeout SECONDS    native capture timeout (default: 90)

Artifacts are ROM-derived local validation data. Do not commit captured traces,
screenshots, logs, or generated audit summaries.
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --out-dir) OUT_DIR="$2"; shift 2 ;;
        --rom) ROM="$2"; shift 2 ;;
        --binary) BINARY="$2"; shift 2 ;;
        --build-dir) BUILD_DIR="$2"; shift 2 ;;
        --no-build) DO_BUILD=0; shift ;;
        --timeout) TIMEOUT_SECONDS="$2"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown arg: $1" >&2; usage >&2; exit 2 ;;
    esac
done

if [[ ! "$TIMEOUT_SECONDS" =~ ^[1-9][0-9]*$ ]]; then
    echo "FAIL: --timeout must be a positive integer: $TIMEOUT_SECONDS" >&2
    exit 2
fi

if [[ -z "$BINARY" ]]; then
    BINARY="$(validation_binary_path "$BUILD_DIR")"
else
    BINARY="$(validation_resolve_path "$BINARY")"
fi

if [[ -z "$ROM" ]]; then
    ROM="$(validation_default_rom)"
else
    ROM="$(validation_resolve_path "$ROM")"
fi

if [[ "$DO_BUILD" -eq 1 ]]; then
    validation_configure_build "$BUILD_DIR"
    validation_build "$BUILD_DIR"
fi

validation_require_binary "$BINARY"
validation_require_file "$ROM" "ROM"

capture_args=(
    --route dam_regular_glass_shatter_visual_probe
    --native-only
    --no-compare
    --out-dir "$OUT_DIR"
    --timeout "$TIMEOUT_SECONDS"
    --no-build
    --rom "$ROM"
    --binary "$BINARY"
    --build-dir "$BUILD_DIR"
)

echo "=== Effect Texture Regression ==="
echo "  out-dir: $OUT_DIR"
echo "  binary:  $BINARY"
echo "  rom:     $ROM"

mkdir -p "$OUT_DIR/loaded_texture_dumps" "$OUT_DIR/muzzle_settex_dumps"

muzzle_log="$OUT_DIR/visible_muzzle_settex.log"
rm -f "$OUT_DIR"/muzzle_settex_dumps/ge007_muzzle_settex_*.ppm \
      "$OUT_DIR"/muzzle_settex_dumps/ge007_muzzle_settex_*.alpha.pgm

echo "=== Visible first-person muzzle settex capture ==="
validation_run_with_timeout "$TIMEOUT_SECONDS" \
    env -u GE007_DEBUG \
    SDL_AUDIODRIVER="$(validation_silent_audio_driver)" \
    GE007_MUTE=1 \
    GE007_DETERMINISTIC_STABLE_COUNT=1 \
    GE007_NO_VSYNC=1 \
    GE007_BACKGROUND=1 \
    GE007_NO_INPUT_GRAB=1 \
    GE007_DISABLE_LEVEL_INTRO=1 \
    GE007_AUTO_FIRE=80:80 \
    GE007_TRACE_SETTEX=1 \
    GE007_DUMP_MUZZLE_SETTEX=1 \
    GE007_DUMP_MUZZLE_SETTEX_DIR="$OUT_DIR/muzzle_settex_dumps" \
    "$BINARY" --rom "$ROM" --level dam --deterministic \
    --screenshot-frame 180 \
    --screenshot-label effect_texture_visible_muzzle \
    --screenshot-exit >"$muzzle_log" 2>&1

for tex in 2157 2158 2159 2160; do
    if ! grep -Eq "\\[MUZZLE_SETTEX_DUMP\\].*tex=${tex}([[:space:]]|$)" "$muzzle_log"; then
        echo "FAIL: visible muzzle settex dump did not include texture $tex" >&2
        tail -80 "$muzzle_log" | sed 's/^/  /' >&2
        exit 1
    fi
done

GE007_TRACE_TEXSELECT=1 \
GE007_TRACE_TEX_PIPELINE=1 \
GE007_TRACE_SETTEX=1 \
GE007_TRACE_DISPLAYCAST_MATERIALS=1 \
GE007_TRACE_DISPLAYCAST_MATERIALS_ALL=1 \
GE007_TRACE_DISPLAYCAST_MATERIALS_AFTER_FRAME=70 \
GE007_TRACE_DISPLAYCAST_MATERIALS_BUDGET=8000 \
GE007_DUMP_LOADED_TEXTURES=0x8000000000000880,0x8000000000000881,0x8000000000000882,0x8000000000000883,0x8000000000000884,0x8000000000000885 \
GE007_DUMP_LOADED_TEXTURE_DIR="$OUT_DIR/loaded_texture_dumps" \
GE007_DUMP_LOADED_TEXTURES_BYPASS_CACHE=1 \
GE007_DUMP_LOADED_TEXTURE_LIMIT=64 \
    tools/movement_oracle_capture.sh "${capture_args[@]}"

log="$OUT_DIR/native_dam_regular_glass_shatter_visual_probe.log"
if [[ ! -s "$log" ]]; then
    echo "FAIL: missing native capture log: $log" >&2
    exit 1
fi

require_log_pattern() {
    local label="$1"
    local pattern="$2"

    if ! grep -Eq "$pattern" "$log"; then
        echo "FAIL: missing $label in $log" >&2
        echo "  pattern: $pattern" >&2
        tail -80 "$log" | sed 's/^/  /' >&2
        exit 1
    fi
}

require_log_pattern \
    "muzzle texSelect source" \
    "tag=muzzle_flash img=2128 resolved_img=2128"
require_log_pattern \
    "muzzle static RGBA32 cache key" \
    "\\[TEX-LOADBLOCK\\].*cache=0x8000000000000850 static=1 lods=1"

for key in 0880 0881 0882 0883 0884 0885; do
    require_log_pattern \
        "explosion smoke static cache key 0x${key}" \
        "\\[TEX-LOADBLOCK\\].*cache=0x800000000000${key} static=1 lods=0"
done

python3 - "$OUT_DIR/loaded_texture_dumps" "$log" "$OUT_DIR/effect_texture_summary.json" "$OUT_DIR/muzzle_settex_dumps" "$muzzle_log" <<'PY'
import json
import re
import sys
from pathlib import Path

dump_dir = Path(sys.argv[1])
log_path = Path(sys.argv[2])
summary_path = Path(sys.argv[3])
muzzle_dump_dir = Path(sys.argv[4])
muzzle_log_path = Path(sys.argv[5])

expected = {
    "0x8000000000000880": {"texturenum": 2176, "label": "explosion_smoke_0", "fmt": "3", "siz": "1", "max_purple_pct": 10.0},
    "0x8000000000000881": {"texturenum": 2177, "label": "explosion_smoke_1", "fmt": "3", "siz": "1", "max_purple_pct": 10.0},
    "0x8000000000000882": {"texturenum": 2178, "label": "explosion_smoke_2", "fmt": "3", "siz": "1", "max_purple_pct": 10.0},
    "0x8000000000000883": {"texturenum": 2179, "label": "explosion_smoke_3", "fmt": "3", "siz": "1", "max_purple_pct": 10.0},
    "0x8000000000000884": {"texturenum": 2180, "label": "explosion_smoke_4", "fmt": "3", "siz": "1", "max_purple_pct": 10.0},
    "0x8000000000000885": {"texturenum": 2181, "label": "explosion_smoke_5", "fmt": "3", "siz": "1", "max_purple_pct": 10.0},
}

failures = []
stats = {}
muzzle_stats = {}

def read_info(path):
    out = {}
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        if "=" in line:
            key, value = line.split("=", 1)
            out[key] = value
    return out

def read_pnm(path, magic):
    raw = path.read_bytes()
    idx = 0
    tokens = []
    while len(tokens) < 4:
        while idx < len(raw) and raw[idx] in b" \t\r\n":
            idx += 1
        if idx < len(raw) and raw[idx:idx + 1] == b"#":
            while idx < len(raw) and raw[idx] not in b"\r\n":
                idx += 1
            continue
        start = idx
        while idx < len(raw) and raw[idx] not in b" \t\r\n":
            idx += 1
        tokens.append(raw[start:idx].decode("ascii"))
    if tokens[0] != magic:
        raise ValueError(f"{path}: expected {magic}, got {tokens[0]}")
    width = int(tokens[1])
    height = int(tokens[2])
    maxval = int(tokens[3])
    if maxval != 255:
        raise ValueError(f"{path}: expected maxval 255, got {maxval}")
    if idx < len(raw) and raw[idx] in b" \t\r\n":
        idx += 1
    return width, height, raw[idx:]

def parse_alpha_nonzero(value):
    if not value:
        return 0
    match = re.match(r"([0-9]+)/([0-9]+)", value)
    return int(match.group(1)) if match else 0

def find_info(cache_key):
    for path in sorted(dump_dir.glob("ge007_loaded_tex_*.info.txt")):
        info = read_info(path)
        if info.get("cache_key", "").lower() == cache_key.lower():
            return path, info
    return None, None

def parse_tuple_ints(text, key, count):
    match = re.search(rf"{re.escape(key)}=\(([^)]*)\)", text)
    if not match:
        return None
    values = []
    for item in match.group(1).split(","):
        item = item.strip()
        if not item:
            continue
        try:
            values.append(int(item, 0))
        except ValueError:
            return None
    return values if len(values) == count else None

def parse_scalar(text, key):
    match = re.search(rf"{re.escape(key)}=([^ ]+)", text)
    return match.group(1) if match else None

def parse_smoke_materials(log):
    required = {f"0x800000000000088{i}" for i in range(4)}
    rows_by_key = {key: [] for key in required}
    for line in log.read_text(encoding="utf-8", errors="replace").splitlines():
        if "[DISPLAYCAST-MATERIAL]" not in line:
            continue
        match = re.search(r"load0=\{[^}]*key=(0x[0-9a-fA-F]+)\}", line)
        if not match:
            continue
        key = match.group(1).lower()
        if key not in rows_by_key:
            continue
        map_c = parse_tuple_ints(line, "mapC", 7)
        map_a = parse_tuple_ints(line, "mapA", 7)
        shade0 = parse_tuple_ints(line, "shade0", 4)
        tile0 = re.search(r"tile0=\{[^}]*fmt=([0-9]+) siz=([0-9]+)[^}]*wh=([0-9]+),([0-9]+)", line)
        row = {
            "frame": int(parse_scalar(line, "frame") or "-1"),
            "cc": parse_scalar(line, "cc"),
            "opts": parse_scalar(line, "opts"),
            "blend": parse_scalar(line, "blend"),
            "alpha": parse_scalar(line, "alpha"),
            "fog": parse_scalar(line, "fog"),
            "texedge": parse_scalar(line, "texedge"),
            "tex_used": parse_tuple_ints(line, "tex_used", 2),
            "sampler_linear": parse_tuple_ints(line, "sampler_linear", 2),
            "mapC": map_c,
            "mapA": map_a,
            "shade0": shade0,
        }
        if tile0:
            row["tile0"] = {
                "fmt": tile0.group(1),
                "siz": tile0.group(2),
                "width": int(tile0.group(3)),
                "height": int(tile0.group(4)),
            }
        rows_by_key[key].append(row)
    return rows_by_key

def color_stats_for_paths(rgba_path, alpha_path=None):
    if not rgba_path.exists():
        raise FileNotFoundError(f"missing rgba dump: {rgba_path}")
    w, h, rgb = read_pnm(rgba_path, "P6")
    if alpha_path is not None and alpha_path.exists():
        aw, ah, alpha = read_pnm(alpha_path, "P5")
        if (aw, ah) != (w, h):
            raise ValueError(f"{alpha_path}: alpha size {(aw, ah)} != rgb {(w, h)}")
    else:
        alpha = bytes([255]) * (w * h)

    active = 0
    purple = 0
    low_green = 0
    sum_r = 0
    sum_g = 0
    sum_b = 0
    bright = 0
    for i, a in enumerate(alpha):
        if a == 0:
            continue
        r = rgb[i * 3 + 0]
        g = rgb[i * 3 + 1]
        b = rgb[i * 3 + 2]
        active += 1
        sum_r += r
        sum_g += g
        sum_b += b
        if max(r, g, b) >= 48:
            bright += 1
        if r >= 48 and b >= 48 and g + 24 < r and g + 24 < b:
            purple += 1
        if max(r, b) >= 48 and g * 2 + 16 < max(r, b):
            low_green += 1

    if active:
        avg = [sum_r / active, sum_g / active, sum_b / active]
        purple_pct = 100.0 * purple / active
        low_green_pct = 100.0 * low_green / active
        bright_pct = 100.0 * bright / active
    else:
        avg = [0.0, 0.0, 0.0]
        purple_pct = 0.0
        low_green_pct = 0.0
        bright_pct = 0.0

    return {
        "width": w,
        "height": h,
        "active_pixels": active,
        "avg_rgb": avg,
        "purple_pixels": purple,
        "purple_pct_active": purple_pct,
        "low_green_pct_active": low_green_pct,
        "bright_pct_active": bright_pct,
    }

def color_stats(info):
    rgba_path = Path(info.get("rgba_path", ""))
    alpha_path = Path(info.get("alpha_path", ""))
    return color_stats_for_paths(rgba_path, alpha_path)

for cache_key, rule in expected.items():
    info_path, info = find_info(cache_key)
    if info_path is None:
        failures.append(f"{rule['label']}: missing loaded texture dump for {cache_key}")
        continue
    try:
        tex_stats = color_stats(info)
    except Exception as exc:
        failures.append(f"{rule['label']}: {exc}")
        continue

    alpha_nonzero = parse_alpha_nonzero(info.get("alpha_nonzero", ""))
    entry = {
        "texturenum": rule["texturenum"],
        "cache_key": cache_key,
        "label": rule["label"],
        "info_path": str(info_path),
        "fmt": info.get("fmt"),
        "siz": info.get("siz"),
        "alpha_nonzero": alpha_nonzero,
        **tex_stats,
    }
    stats[cache_key] = entry

    if "fmt" in rule and info.get("fmt") != rule["fmt"]:
        failures.append(f"{rule['label']}: fmt={info.get('fmt')} expected {rule['fmt']}")
    if "siz" in rule and info.get("siz") != rule["siz"]:
        failures.append(f"{rule['label']}: siz={info.get('siz')} expected {rule['siz']}")
    if alpha_nonzero < rule.get("min_alpha_nonzero", 1):
        failures.append(
            f"{rule['label']}: alpha_nonzero={alpha_nonzero} < {rule.get('min_alpha_nonzero', 1)}"
        )
    if tex_stats["active_pixels"] == 0:
        failures.append(f"{rule['label']}: no active decoded pixels")
    if tex_stats["purple_pct_active"] > rule["max_purple_pct"]:
        failures.append(
            f"{rule['label']}: purple_pct={tex_stats['purple_pct_active']:.2f}% "
            f"> {rule['max_purple_pct']:.2f}%"
        )

    avg_r, avg_g, avg_b = tex_stats["avg_rgb"]
    if rule["texturenum"] != 2128 and tex_stats["width"] != 64:
        failures.append(f"{rule['label']}: width={tex_stats['width']} expected 64")
    if rule["texturenum"] != 2128 and tex_stats["height"] != 64:
        failures.append(f"{rule['label']}: height={tex_stats['height']} expected 64")
    if rule["texturenum"] != 2128 and abs(avg_r - avg_g) > 2.0:
        failures.append(f"{rule['label']}: decoded smoke red/green mismatch avg={tex_stats['avg_rgb']}")
    if rule["texturenum"] != 2128 and abs(avg_r - avg_b) > 4.0:
        failures.append(f"{rule['label']}: decoded smoke red/blue mismatch avg={tex_stats['avg_rgb']}")
    if rule["texturenum"] != 2128 and tex_stats["low_green_pct_active"] > rule["max_purple_pct"]:
        failures.append(
            f"{rule['label']}: low_green_pct={tex_stats['low_green_pct_active']:.2f}% "
            f"> {rule['max_purple_pct']:.2f}%"
        )

muzzle_log = muzzle_log_path.read_text(encoding="utf-8", errors="replace") if muzzle_log_path.exists() else ""
for texnum in range(2157, 2161):
    label = f"visible_muzzle_{texnum}"
    rgba_path = muzzle_dump_dir / f"ge007_muzzle_settex_{texnum}.ppm"
    alpha_path = muzzle_dump_dir / f"ge007_muzzle_settex_{texnum}.alpha.pgm"

    if f"tex={texnum}" not in muzzle_log:
        failures.append(f"{label}: missing MUZZLE_SETTEX_DUMP log row")
    try:
        tex_stats = color_stats_for_paths(rgba_path, alpha_path)
    except Exception as exc:
        failures.append(f"{label}: {exc}")
        continue

    avg_r, avg_g, avg_b = tex_stats["avg_rgb"]
    entry = {
        "texturenum": texnum,
        "label": label,
        "rgba_path": str(rgba_path),
        "alpha_path": str(alpha_path),
        **tex_stats,
    }
    muzzle_stats[str(texnum)] = entry

    if tex_stats["width"] != 32:
        failures.append(f"{label}: width={tex_stats['width']} expected 32")
    if tex_stats["height"] != 32:
        failures.append(f"{label}: height={tex_stats['height']} expected 32")
    if tex_stats["active_pixels"] < 700:
        failures.append(f"{label}: active_pixels={tex_stats['active_pixels']} < 700")
    if tex_stats["purple_pct_active"] > 5.0:
        failures.append(f"{label}: purple_pct={tex_stats['purple_pct_active']:.2f}% > 5.00%")
    if tex_stats["low_green_pct_active"] > 5.0:
        failures.append(f"{label}: low_green_pct={tex_stats['low_green_pct_active']:.2f}% > 5.00%")
    if avg_r < 220.0 or avg_g < 220.0 or avg_b < 80.0:
        failures.append(f"{label}: visible muzzle avg too dark/wrong-hued avg={tex_stats['avg_rgb']}")
    if avg_g + 12.0 < avg_r:
        failures.append(f"{label}: green channel collapsed relative to red avg={tex_stats['avg_rgb']}")
    if avg_b + 40.0 > avg_g:
        failures.append(f"{label}: blue channel too high for warm muzzle avg={tex_stats['avg_rgb']}")

material_rows = parse_smoke_materials(log_path)
material_stats = {}
for cache_key in sorted(material_rows):
    rows = material_rows[cache_key]
    material_stats[cache_key] = {
        "rows": len(rows),
        "sample": rows[:3],
    }
    if not rows:
        failures.append(f"explosion smoke material: missing sampled draw rows for {cache_key}")
        continue
    good_rows = 0
    for row in rows:
        shade = row.get("shade0") or [0, 0, 0, 0]
        tile = row.get("tile0", {})
        if (
            row.get("cc") == "0x00f39e4f1f39e4f1"
            and row.get("blend") == "alpha"
            and row.get("alpha") == "1"
            and row.get("texedge") == "0"
            and row.get("tex_used") == [1, 0]
            and row.get("sampler_linear") == [0, 0]
            and row.get("mapC") == [4, 0, 0, 0, 0, 0, 0]
            and row.get("mapA") == [4, 0, 0, 0, 0, 0, 0]
            and tile.get("fmt") == "3"
            and tile.get("siz") == "1"
            and tile.get("width") == 64
            and tile.get("height") == 64
            and shade[0] >= 220
            and shade[1] >= 220
            and shade[2] >= 160
            and shade[1] + 16 >= shade[2]
        ):
            good_rows += 1
    if good_rows == 0:
        failures.append(f"explosion smoke material: no sane passthrough alpha rows for {cache_key}")

summary = {
    "status": "fail" if failures else "pass",
    "muzzle_provenance": {
        "texturenum": 2128,
        "cache_key": "0x8000000000000850",
        "fixture": "route-selected effect texture; visible first-person muzzle textures are audited separately",
    },
    "visible_muzzle_stats": muzzle_stats,
    "stats": stats,
    "material_stats": material_stats,
    "failures": failures,
}
summary_path.write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n")

if failures:
    print("FAIL: effect decoded texture regression")
    for failure in failures:
        print(f"  - {failure}")
    raise SystemExit(1)

print("PASS: effect decoded texture/material regression")
for cache_key in sorted(stats):
    item = stats[cache_key]
    print(
        "  {label}: tex={texturenum} key={cache_key} size={width}x{height} avg=({avg0:.1f},{avg1:.1f},{avg2:.1f}) purple={purple:.2f}%".format(
            label=item["label"],
            texturenum=item["texturenum"],
            cache_key=item["cache_key"],
            width=item["width"],
            height=item["height"],
            avg0=item["avg_rgb"][0],
            avg1=item["avg_rgb"][1],
            avg2=item["avg_rgb"][2],
            purple=item["purple_pct_active"],
        )
    )
for texnum in sorted(muzzle_stats, key=int):
    item = muzzle_stats[texnum]
    print(
        "  {label}: tex={texturenum} size={width}x{height} active={active_pixels} avg=({avg0:.1f},{avg1:.1f},{avg2:.1f}) purple={purple:.2f}%".format(
            label=item["label"],
            texturenum=item["texturenum"],
            width=item["width"],
            height=item["height"],
            active_pixels=item["active_pixels"],
            avg0=item["avg_rgb"][0],
            avg1=item["avg_rgb"][1],
            avg2=item["avg_rgb"][2],
            purple=item["purple_pct_active"],
        )
    )
PY

echo "PASS: visible muzzle, muzzle provenance, and sampled explosion smoke texture/material paths are sane"
echo "summary_json: $OUT_DIR/effect_texture_summary.json"
echo "artifacts: $OUT_DIR"
