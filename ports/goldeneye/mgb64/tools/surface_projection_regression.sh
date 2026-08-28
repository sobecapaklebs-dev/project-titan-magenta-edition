#!/bin/bash
#
# surface_projection_regression.sh -- Guard Surface 1 field_10E0 projection scale.
#
# The regression this catches made Surface render mostly blue sky while player
# movement traces stayed valid. Default native rendering must keep the map in
# view; GE007_FIELD_10E0_SCALED=0 is retained as the negative control that
# reproduces the sky-dominant projection.
#
set -euo pipefail
cd "$(dirname "$0")/.."

source tools/validation_common.sh

BUILD_DIR="$(validation_default_build_dir)"
BINARY=""
ROM="$(validation_default_rom)"
DO_BUILD=1
TIMEOUT_SECONDS=90
OUT_DIR="/tmp/mgb64_surface_projection_$$"
FRAMES=240

usage() {
    cat <<'USAGE'
Usage: tools/surface_projection_regression.sh [options]

Options:
  --out-dir DIR        output directory (default: /tmp/...)
  --frames N           screenshot/exit frame (default: 240)
  --rom PATH           ROM path (default: ./baserom.u.z64)
  --binary PATH        native binary path (default: build/ge007)
  --build-dir DIR      CMake build directory (default: build)
  --no-build           reuse an existing native binary
  --timeout SECONDS    per-capture timeout (default: 90)

Artifacts are ROM-derived local validation data. Do not commit captured traces,
screenshots, logs, or generated audit summaries.
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --out-dir) OUT_DIR="$2"; shift 2 ;;
        --frames) FRAMES="$2"; shift 2 ;;
        --rom) ROM="$2"; shift 2 ;;
        --binary) BINARY="$2"; shift 2 ;;
        --build-dir) BUILD_DIR="$2"; shift 2 ;;
        --no-build) DO_BUILD=0; shift ;;
        --timeout) TIMEOUT_SECONDS="$2"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown arg: $1" >&2; usage >&2; exit 2 ;;
    esac
done

if [[ ! "$FRAMES" =~ ^[1-9][0-9]*$ ]]; then
    echo "FAIL: --frames must be a positive integer: $FRAMES" >&2
    exit 2
fi
if [[ ! "$TIMEOUT_SECONDS" =~ ^[1-9][0-9]*$ ]]; then
    echo "FAIL: --timeout must be a positive integer: $TIMEOUT_SECONDS" >&2
    exit 2
fi

if [[ -z "$BINARY" ]]; then
    BINARY="$(validation_binary_path "$BUILD_DIR")"
else
    BINARY="$(validation_resolve_path "$BINARY")"
fi
ROM="$(validation_resolve_path "$ROM")"

if [[ "$DO_BUILD" -eq 1 ]]; then
    validation_configure_build "$BUILD_DIR" >/dev/null
    validation_build "$BUILD_DIR" >/dev/null
fi

validation_require_binary "$BINARY"
validation_require_file "$ROM" "ROM"

mkdir -p "$OUT_DIR"
OUT_DIR="$(cd "$OUT_DIR" && pwd)"

CONFIG_OVERRIDES=(
    "Input.AdsEnabled=0"
    "Input.AdsMovePenalty=0"
    "Input.SteadyView=1"
    "Video.RemasterFX=0"
    "Video.RenderScale=1"
    "Video.MSAA=0"
    "Video.FovY=60"
    "Video.ViewmodelFov=60"
    "Video.RetroFilter=off"
    "Video.GradePresets=0"
    "Video.Tonemap=0"
    "Video.Bloom=0"
    "Video.Vignette=0"
    "Video.Saturation=1"
    "Video.Contrast=1"
    "Video.Brightness=0"
)

playability_args=(
    --level 36
    --pattern forward
    --frames "$FRAMES"
    --min-horizontal-delta 1
    --binary "$BINARY"
    --rom "$ROM"
    --no-build
    --timeout "$TIMEOUT_SECONDS"
)
for override in "${CONFIG_OVERRIDES[@]}"; do
    playability_args+=(--config-override "$override")
done

run_case() {
    local name="$1"
    shift
    local case_dir="$OUT_DIR/$name"

    rm -rf "$case_dir"
    mkdir -p "$case_dir"
    echo "  capture: $name"
    if ! "$@" tools/playability_smoke.sh "${playability_args[@]}" --out-dir "$case_dir" >"$case_dir/run.log" 2>&1; then
        echo "FAIL: Surface projection capture failed for $name" >&2
        tail -80 "$case_dir/run.log" | sed 's/^/  /' >&2
        exit 1
    fi
}

echo "=== Surface Projection Regression ==="
echo "  out-dir: $OUT_DIR"
echo "  binary:  $BINARY"
echo "  ROM:     $ROM"
echo "  frames:  $FRAMES"

run_case default env
run_case unscaled_control env GE007_FIELD_10E0_SCALED=0

python3 - "$OUT_DIR" <<'PY'
import csv
import json
import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    raise SystemExit(
        "FAIL: Pillow is required for Surface projection metrics. "
        "Install it with: python3 -m pip install pillow"
    )

root = Path(sys.argv[1])
cases = ("default", "unscaled_control")
failures = []

def read_summary(case):
    path = root / case / "summary.tsv"
    if not path.exists():
        failures.append(f"{case}: missing movement summary {path}")
        return {}
    with path.open("r", encoding="utf-8", newline="") as handle:
        rows = list(csv.DictReader(handle, delimiter="\t"))
    if not rows:
        failures.append(f"{case}: empty movement summary")
        return {}
    return rows[0]

def sky_stats(case):
    path = root / case / "level_36_forward.bmp"
    if not path.exists():
        failures.append(f"{case}: missing screenshot {path}")
        return {"sky_pct": 100.0, "mean_luma": 0.0}
    with Image.open(path) as image:
        rgb = image.convert("RGB")
        total = rgb.width * rgb.height
        sky = 0
        luma_sum = 0.0
        for r, g, b in rgb.getdata():
            luma_sum += 0.2126 * r + 0.7152 * g + 0.0722 * b
            if b > 80 and b > r + 20 and b > g + 10:
                sky += 1
    return {
        "sky_pct": 100.0 * sky / float(total),
        "mean_luma": luma_sum / float(total),
    }

summary = {case: read_summary(case) for case in cases}
stats = {case: sky_stats(case) for case in cases}

for case in cases:
    row = summary.get(case) or {}
    try:
        delta = float(row.get("max_horizontal_delta", "0"))
    except ValueError:
        delta = 0.0
    if delta < 1.0:
        failures.append(f"{case}: movement delta {delta:.3f} < 1.0")

if stats["default"]["sky_pct"] > 25.0:
    failures.append(
        f"default sky-dominance {stats['default']['sky_pct']:.2f}% > 25.00%"
    )
if stats["unscaled_control"]["sky_pct"] < 50.0:
    failures.append(
        "unscaled_control did not reproduce sky-dominance: "
        f"{stats['unscaled_control']['sky_pct']:.2f}% < 50.00%"
    )
if stats["unscaled_control"]["sky_pct"] <= stats["default"]["sky_pct"] + 30.0:
    failures.append(
        "unscaled_control too close to default sky percentage: "
        f"{stats['unscaled_control']['sky_pct']:.2f}% <= "
        f"{stats['default']['sky_pct']:.2f}% + 30.00%"
    )

out = {
    "status": "fail" if failures else "pass",
    "stats": stats,
    "movement": summary,
    "failures": failures,
}
(root / "summary.json").write_text(json.dumps(out, indent=2, sort_keys=True) + "\n")

if failures:
    print("FAIL: Surface projection regression")
    for failure in failures:
        print(f"  - {failure}")
    raise SystemExit(1)

print("PASS: Surface projection regression")
print(
    "  sky: default=%.2f%% unscaled_control=%.2f%%"
    % (stats["default"]["sky_pct"], stats["unscaled_control"]["sky_pct"])
)
print(
    "  mean_luma: default=%.2f unscaled_control=%.2f"
    % (stats["default"]["mean_luma"], stats["unscaled_control"]["mean_luma"])
)
PY

echo "summary_json: $OUT_DIR/summary.json"
echo "artifacts: $OUT_DIR"
