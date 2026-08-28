#!/bin/bash
#
# native_playability_regression_suite.sh -- Orchestrate the native gameplay
# regression gates used before/after promoted playability changes.
#
# ROM-backed artifacts stay in /tmp by default and must not be committed.
#
set -euo pipefail
cd "$(dirname "$0")/.."

source tools/validation_common.sh

BUILD_DIR="$(validation_default_build_dir)"
BINARY=""
ROM="$(validation_default_rom)"
DO_BUILD=1
OUT_DIR="/tmp/mgb64_native_playability_regression_$$"
FULL=0
RUN_SAVE=1
RUN_MP=1
RUN_MINIMAP=1
RUN_RENDERER=1
RUN_PLAYABILITY=1
RUN_CAMPAIGN=1
RUN_COMBAT=1
RUN_CTEST=1

usage() {
    cat <<'USAGE'
Usage: tools/native_playability_regression_suite.sh [options]

Options:
  --full                 run broader ROM-backed variants where available
  --out-dir DIR          output directory (default: /tmp/...)
  --rom PATH             ROM path (default: ./baserom.u.z64)
  --binary PATH          native binary path (default: build/ge007)
  --build-dir DIR        CMake build directory (default: build)
  --no-build             reuse an existing native binary
  --skip-ctest           skip CTest
  --skip-playability     skip playability_smoke.sh
  --skip-campaign        skip campaign progression flow gates
  --skip-combat          skip combat/guard/knife gameplay gates
  --skip-renderer        skip renderer_parity_capture.sh
  --skip-mp              skip mp_smoke.sh
  --skip-save            skip save_persistence_check.sh
  --skip-minimap         skip minimap_smoke.sh

This is an orchestration convenience for local guard discipline. It does not
replace focused failure triage; inspect the per-gate artifact directories.
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --full) FULL=1; shift ;;
        --out-dir) OUT_DIR="$2"; shift 2 ;;
        --rom) ROM="$2"; shift 2 ;;
        --binary) BINARY="$2"; shift 2 ;;
        --build-dir) BUILD_DIR="$2"; shift 2 ;;
        --no-build) DO_BUILD=0; shift ;;
        --skip-ctest) RUN_CTEST=0; shift ;;
        --skip-playability) RUN_PLAYABILITY=0; shift ;;
        --skip-campaign) RUN_CAMPAIGN=0; shift ;;
        --skip-combat) RUN_COMBAT=0; shift ;;
        --skip-renderer) RUN_RENDERER=0; shift ;;
        --skip-mp) RUN_MP=0; shift ;;
        --skip-save) RUN_SAVE=0; shift ;;
        --skip-minimap) RUN_MINIMAP=0; shift ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown arg: $1" >&2; usage >&2; exit 2 ;;
    esac
done

if [[ -z "$BINARY" ]]; then
    BINARY="$(validation_binary_path "$BUILD_DIR")"
else
    BINARY="$(validation_resolve_path "$BINARY")"
fi
ROM="$(validation_resolve_path "$ROM")"
BUILD_DIR="$(validation_resolve_path "$BUILD_DIR")"

mkdir -p "$OUT_DIR"
OUT_DIR="$(cd "$OUT_DIR" && pwd)"
SUMMARY="$OUT_DIR/summary.tsv"
printf 'gate\tstatus\tartifact\n' >"$SUMMARY"

if [[ "$DO_BUILD" -eq 1 ]]; then
    validation_configure_build "$BUILD_DIR" >/dev/null
    validation_build "$BUILD_DIR" >/dev/null
fi

validation_require_binary "$BINARY"
if [[ "$RUN_PLAYABILITY" -eq 1 || "$RUN_CAMPAIGN" -eq 1 || "$RUN_COMBAT" -eq 1 || "$RUN_RENDERER" -eq 1 || "$RUN_MP" -eq 1 || "$RUN_SAVE" -eq 1 || "$RUN_MINIMAP" -eq 1 ]]; then
    validation_require_file "$ROM" "ROM"
fi

run_gate() {
    local name="$1"
    shift
    local artifact="$OUT_DIR/$name"
    mkdir -p "$artifact"

    echo ""
    echo "=== Gate: $name ==="
    if "$@" --out-dir "$artifact"; then
        printf '%s\tpass\t%s\n' "$name" "$artifact" >>"$SUMMARY"
        echo "PASS: $name"
    else
        printf '%s\tfail\t%s\n' "$name" "$artifact" >>"$SUMMARY"
        echo "FAIL: $name"
        return 1
    fi
}

FAILED=0

echo "=== Native Playability Regression Suite ==="
echo "  out-dir: $OUT_DIR"
echo "  build:   $BUILD_DIR"
echo "  binary:  $BINARY"
echo "  ROM:     $ROM"
echo "  full:    $FULL"

if [[ "$RUN_CTEST" -eq 1 ]]; then
    echo ""
    echo "=== Gate: ctest ==="
    if ctest --test-dir "$BUILD_DIR" --output-on-failure; then
        printf 'ctest\tpass\t%s\n' "$BUILD_DIR" >>"$SUMMARY"
        echo "PASS: ctest"
    else
        printf 'ctest\tfail\t%s\n' "$BUILD_DIR" >>"$SUMMARY"
        FAILED=$((FAILED + 1))
    fi
fi

if [[ "$RUN_PLAYABILITY" -eq 1 ]]; then
    PLAYABILITY_ARGS=(tools/playability_smoke.sh --no-build --binary "$BINARY" --rom "$ROM")
    if [[ "$FULL" -eq 1 ]]; then
        PLAYABILITY_ARGS+=(--all --timeout 90)
    fi
    run_gate playability "${PLAYABILITY_ARGS[@]}" || FAILED=$((FAILED + 1))
fi

if [[ "$RUN_CAMPAIGN" -eq 1 ]]; then
    run_gate campaign_routes tools/campaign_route_smoke.sh --no-build --binary "$BINARY" --rom "$ROM" || FAILED=$((FAILED + 1))
    run_gate campaign_dam tools/dam_progression_smoke.sh --no-build --binary "$BINARY" --rom "$ROM" || FAILED=$((FAILED + 1))
    run_gate campaign_surface2_final tools/surface2_final_flow_smoke.sh --no-build --binary "$BINARY" --rom "$ROM" || FAILED=$((FAILED + 1))
fi

if [[ "$RUN_COMBAT" -eq 1 ]]; then
    run_gate combat_hidden_guard tools/hidden_guard_contract_smoke.sh --no-build --binary "$BINARY" --rom "$ROM" || FAILED=$((FAILED + 1))
    run_gate combat_knife_impact tools/knife_impact_smoke.sh --no-build --binary "$BINARY" --rom "$ROM" || FAILED=$((FAILED + 1))
    run_gate combat_knife_throw_sfx tools/knife_throw_sfx_smoke.sh --no-build --binary "$BINARY" --rom "$ROM" || FAILED=$((FAILED + 1))
fi

if [[ "$RUN_RENDERER" -eq 1 ]]; then
    run_gate renderer tools/renderer_parity_capture.sh --no-build --binary "$BINARY" --rom "$ROM" || FAILED=$((FAILED + 1))
fi

if [[ "$RUN_MP" -eq 1 ]]; then
    run_gate mp tools/mp_smoke.sh --no-build --binary "$BINARY" --rom "$ROM" || FAILED=$((FAILED + 1))
fi

if [[ "$RUN_SAVE" -eq 1 ]]; then
    run_gate save tools/save_persistence_check.sh --no-build --binary "$BINARY" --rom "$ROM" || FAILED=$((FAILED + 1))
fi

if [[ "$RUN_MINIMAP" -eq 1 ]]; then
    MINIMAP_ARGS=(tools/minimap_smoke.sh --no-build --binary "$BINARY" --rom "$ROM")
    if [[ "$FULL" -ne 1 ]]; then
        MINIMAP_ARGS+=(--level "33 34 26 41" --toggle-level "33 34 26 41")
    fi
    run_gate minimap "${MINIMAP_ARGS[@]}" || FAILED=$((FAILED + 1))
fi

echo ""
echo "=== Native Playability Regression Suite Summary ==="
cat "$SUMMARY"
echo "  artifacts: $OUT_DIR"
exit "$FAILED"
