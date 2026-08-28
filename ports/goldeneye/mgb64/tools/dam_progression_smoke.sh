#!/bin/bash
#
# dam_progression_smoke.sh -- Aggregate the current native Dam progression gates.
#
# This is a deterministic progression guard, not a full organic route solver. It
# keeps the declared CTest hook active by composing the strongest current Dam
# checks: spawn movement, objective criteria advancement, and mission-report
# return after success.
#
set -euo pipefail
cd "$(dirname "$0")/.."

source tools/validation_common.sh

BUILD_DIR="$(validation_default_build_dir)"
BINARY=""
ROM="$(validation_default_rom)"
DO_BUILD=1
TIMEOUT_SECONDS=90
OUT_DIR="/tmp/mgb64_dam_progression_$$"
LEVEL=33

usage() {
    cat <<'USAGE'
Usage: tools/dam_progression_smoke.sh [options]

Options:
  --out-dir DIR          output directory (default: /tmp/...)
  --level N              level id (default: 33, Dam)
  --rom PATH             ROM path (default: ./baserom.u.z64)
  --binary PATH          native binary path (default: build/ge007)
  --build-dir DIR        CMake build directory (default: build)
  --no-build             reuse an existing native binary
  --timeout SECONDS      per-subgate timeout (default: 90)

Artifacts are ROM-derived local validation data. Do not commit captured traces,
stage dumps, screenshots, logs, or generated audit summaries.
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --out-dir) OUT_DIR="$2"; shift 2 ;;
        --level) LEVEL="$2"; shift 2 ;;
        --rom) ROM="$2"; shift 2 ;;
        --binary) BINARY="$2"; shift 2 ;;
        --build-dir) BUILD_DIR="$2"; shift 2 ;;
        --no-build) DO_BUILD=0; shift ;;
        --timeout) TIMEOUT_SECONDS="$2"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown arg: $1" >&2; usage >&2; exit 2 ;;
    esac
done

for pair in "level:$LEVEL" "timeout:$TIMEOUT_SECONDS"; do
    name="${pair%%:*}"
    value="${pair#*:}"
    if [[ ! "$value" =~ ^[1-9][0-9]*$ ]]; then
        echo "FAIL: --$name must be a positive integer: $value" >&2
        exit 2
    fi
done

if [[ -z "$BINARY" ]]; then
    BINARY="$(validation_binary_path "$BUILD_DIR")"
else
    BINARY="$(validation_resolve_path "$BINARY")"
fi
ROM="$(validation_resolve_path "$ROM")"
BUILD_DIR="$(validation_resolve_path "$BUILD_DIR")"

if [[ "$DO_BUILD" -eq 1 ]]; then
    validation_configure_build "$BUILD_DIR" >/dev/null
    validation_build "$BUILD_DIR" >/dev/null
fi

validation_require_binary "$BINARY"
validation_require_file "$ROM" "ROM"

mkdir -p "$OUT_DIR"
OUT_DIR="$(cd "$OUT_DIR" && pwd)"
SUMMARY="$OUT_DIR/summary.tsv"
printf 'gate\tstatus\tartifact\n' >"$SUMMARY"

FAILED=0

run_gate() {
    local name="$1"
    shift
    local artifact="$OUT_DIR/$name"

    mkdir -p "$artifact"

    echo ""
    echo "=== Dam Progression Gate: $name ==="
    if "$@" --out-dir "$artifact"; then
        printf '%s\tpass\t%s\n' "$name" "$artifact" >>"$SUMMARY"
        echo "PASS: $name"
    else
        printf '%s\tfail\t%s\n' "$name" "$artifact" >>"$SUMMARY"
        echo "FAIL: $name"
        return 1
    fi
}

echo "=== Dam Progression Smoke ==="
echo "  out-dir: $OUT_DIR"
echo "  binary:  $BINARY"
echo "  ROM:     $ROM"
echo "  level:   $LEVEL"

run_gate movement \
    tools/playability_smoke.sh \
    --no-build \
    --binary "$BINARY" \
    --rom "$ROM" \
    --level "$LEVEL" \
    --pattern "forward forward_left forward_right" \
    --frames 240 \
    --timeout "$TIMEOUT_SECONDS" || FAILED=$((FAILED + 1))

run_gate objectives \
    tools/dam_objective_progression_smoke.sh \
    --no-build \
    --binary "$BINARY" \
    --rom "$ROM" \
    --level "$LEVEL" \
    --timeout "$TIMEOUT_SECONDS" || FAILED=$((FAILED + 1))

run_gate mission_flow \
    tools/dam_mission_flow_smoke.sh \
    --no-build \
    --binary "$BINARY" \
    --rom "$ROM" \
    --level "$LEVEL" \
    --timeout "$TIMEOUT_SECONDS" || FAILED=$((FAILED + 1))

echo ""
echo "=== Dam Progression Summary ==="
cat "$SUMMARY"
echo "  artifacts: $OUT_DIR"
exit "$FAILED"
