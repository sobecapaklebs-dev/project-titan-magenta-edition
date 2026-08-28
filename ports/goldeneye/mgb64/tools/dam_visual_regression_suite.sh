#!/bin/bash
#
# dam_visual_regression_suite.sh -- Run the focused Dam visual regression gates.
#
# This wrapper covers the playability regressions currently tracked for Dam:
# camera-basis tilt, tunnel draw-distance/portal under-admission, purple
# muzzle/explosion textures, paletted guard/alarm colors, glass material health,
# the pad10092 actor-masked active-shard visual fixture, and the pad10004
# impact-aligned pane/decal visual fixture.
#
set -euo pipefail
cd "$(dirname "$0")/.."

source tools/validation_common.sh

BUILD_DIR="$(validation_default_build_dir)"
BINARY=""
ROM="$(validation_default_rom)"
ARES_BIN=""
DO_BUILD=1
TIMEOUT_SECONDS=240
OUT_DIR="/tmp/mgb64_dam_visual_regression_suite_$$"

usage() {
    cat <<'USAGE'
Usage: tools/dam_visual_regression_suite.sh [options]

Options:
  --out-dir DIR        output directory (default: /tmp/...)
  --rom PATH           ROM path (default: ./baserom.u.z64)
  --binary PATH        native binary path (default: build/ge007)
  --build-dir DIR      CMake build directory (default: build)
  --ares-bin PATH      instrumented ares binary for stock-backed glass gate
  --no-build           reuse an existing native binary
  --timeout SECONDS    per-gate timeout (default: 240)

Artifacts are ROM-derived local validation data. Do not commit captured traces,
screenshots, logs, emulator output, or generated summaries.
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --out-dir) OUT_DIR="$2"; shift 2 ;;
        --rom) ROM="$2"; shift 2 ;;
        --binary) BINARY="$2"; shift 2 ;;
        --build-dir) BUILD_DIR="$2"; shift 2 ;;
        --ares-bin) ARES_BIN="$2"; shift 2 ;;
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
ROM="$(validation_resolve_path "$ROM")"

if [[ -z "$ARES_BIN" ]]; then
    if [[ -x "build/ares-movement-oracle/ares/build-movement-oracle/desktop-ui/ares.app/Contents/MacOS/ares" ]]; then
        ARES_BIN="build/ares-movement-oracle/ares/build-movement-oracle/desktop-ui/ares.app/Contents/MacOS/ares"
    else
        ARES_BIN="build/ares-movement-oracle/ares/build-movement-oracle/desktop-ui/ares"
    fi
fi
ARES_BIN="$(validation_resolve_path "$ARES_BIN")"

if [[ "$DO_BUILD" -eq 1 ]]; then
    validation_configure_build "$BUILD_DIR" >/dev/null
    validation_build "$BUILD_DIR" >/dev/null
fi

validation_require_binary "$BINARY"
validation_require_file "$ROM" "ROM"
validation_require_binary "$ARES_BIN"

mkdir -p "$OUT_DIR"
OUT_DIR="$(cd "$OUT_DIR" && pwd)"
INDEX_TSV="$OUT_DIR/index.tsv"
SUMMARY_JSON="$OUT_DIR/summary.json"

printf 'gate\tstatus\tout_dir\tsummary\n' >"$INDEX_TSV"

echo "=== Dam Visual Regression Suite ==="
echo "  out-dir: $OUT_DIR"
echo "  binary:  $BINARY"
echo "  ROM:     $ROM"
echo "  ares:    $ARES_BIN"
echo "  timeout: $TIMEOUT_SECONDS"

run_gate() {
    local gate="$1"
    local script="$2"
    local case_dir="$OUT_DIR/$gate"
    shift 2

    echo ""
    echo "=== gate: $gate ==="
    if "$script" \
        --no-build \
        --binary "$BINARY" \
        --rom "$ROM" \
        --out-dir "$case_dir" \
        --timeout "$TIMEOUT_SECONDS" \
        "$@"
    then
        local summary
        summary="$(find "$case_dir" -maxdepth 3 -type f \( -name 'summary.json' -o -name '*summary*.json' \) | sort | head -1 || true)"
        printf '%s\tpass\t%s\t%s\n' "$gate" "$case_dir" "$summary" >>"$INDEX_TSV"
        echo "PASS: $gate"
    else
        local status=$?
        printf '%s\tfail\t%s\t\n' "$gate" "$case_dir" >>"$INDEX_TSV"
        echo "FAIL: $gate" >&2
        exit "$status"
    fi
}

run_gate camera_basis tools/camera_basis_regression.sh
run_gate tunnel_visibility tools/dam_tunnel_visibility_regression.sh
run_gate effect_texture tools/effect_texture_regression.sh
run_gate dam_palette tools/dam_palette_regression.sh
run_gate glass_material tools/glass_material_regression.sh
run_gate glass_actor_masked tools/glass_actor_masked_visual_regression.sh --ares-bin "$ARES_BIN"
run_gate glass_impact_visual tools/glass_impact_visual_isolation_regression.sh --ares-bin "$ARES_BIN"

python3 - "$INDEX_TSV" "$SUMMARY_JSON" <<'PY'
import csv
import json
import sys
from pathlib import Path

index_path = Path(sys.argv[1])
summary_path = Path(sys.argv[2])
rows = []
with index_path.open("r", encoding="utf-8") as handle:
    reader = csv.DictReader(handle, delimiter="\t")
    for row in reader:
        rows.append(row)

failures = [row for row in rows if row.get("status") != "pass"]
payload = {
    "status": "fail" if failures else "pass",
    "failures": failures,
    "gates": rows,
}
summary_path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")

if failures:
    raise SystemExit(1)
PY

echo ""
echo "PASS: Dam visual regression suite"
echo "summary_json: $SUMMARY_JSON"
echo "index: $INDEX_TSV"
echo "artifacts: $OUT_DIR"
