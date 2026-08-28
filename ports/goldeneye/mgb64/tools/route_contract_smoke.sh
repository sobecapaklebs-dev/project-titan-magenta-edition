#!/bin/bash
#
# route_contract_smoke.sh -- Validate public ROM-oracle route contracts.
#
# The default mode is ROM-free: validate route JSON and generated native/ares
# adapters. Use --native-smoke with a ROM and native binary to run each route
# through movement_oracle_capture.sh in native-only mode.
#
set -euo pipefail
cd "$(dirname "$0")/.."

source tools/validation_common.sh

BUILD_DIR="$(validation_default_build_dir)"
BINARY=""
ROM="$(validation_default_rom)"
DO_BUILD=1
NATIVE_SMOKE=0
TIMEOUT_SECONDS=90
OUT_DIR="/tmp/mgb64_route_contract_smoke_$$"
ROUTES=()

usage() {
    cat <<'USAGE'
Usage: tools/route_contract_smoke.sh [options]

Options:
  --all                  validate all built-in route specs (default)
  --route NAME|PATH      validate one route; may be repeated
  --native-smoke         also run native-only route captures
  --out-dir DIR          output directory for native-smoke artifacts
  --rom PATH             ROM path for native-smoke (default: ./baserom.u.z64)
  --binary PATH          native binary path (default: build/ge007)
  --build-dir DIR        CMake build directory (default: build)
  --no-build             reuse an existing native binary
  --timeout SECONDS      per-route native-smoke timeout (default: 90)

Compatibility no-ops accepted for older optional validation wiring:
  --tag, --artifact-health, --artifact-report-lane,
  --artifact-expected-route-label

Generated traces, screenshots, logs, emulator settings, and saves are
ROM-derived local artifacts. Do not commit them.
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --all) ROUTES=(); shift ;;
        --route) ROUTES+=("$2"); shift 2 ;;
        --native-smoke) NATIVE_SMOKE=1; shift ;;
        --out-dir) OUT_DIR="$2"; shift 2 ;;
        --rom) ROM="$2"; shift 2 ;;
        --binary) BINARY="$2"; shift 2 ;;
        --build-dir) BUILD_DIR="$2"; shift 2 ;;
        --no-build) DO_BUILD=0; shift ;;
        --timeout) TIMEOUT_SECONDS="$2"; shift 2 ;;
        --tag) shift 2 ;;
        --artifact-health) shift ;;
        --artifact-report-lane) shift 2 ;;
        --artifact-expected-route-label) shift 2 ;;
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

if [[ "${#ROUTES[@]}" -eq 0 ]]; then
    while IFS=$'\t' read -r route_name _rest; do
        if [[ -n "$route_name" ]]; then
            ROUTES+=("$route_name")
        fi
    done < <(python3 tools/rom_oracle_route.py list)
fi

if [[ "${#ROUTES[@]}" -eq 0 ]]; then
    echo "FAIL: no ROM-oracle routes found" >&2
    exit 2
fi

if [[ "$NATIVE_SMOKE" -eq 1 ]]; then
    if [[ "$DO_BUILD" -eq 1 ]]; then
        validation_configure_build "$BUILD_DIR" >/dev/null
        validation_build "$BUILD_DIR" >/dev/null
    fi
    validation_require_binary "$BINARY"
    validation_require_file "$ROM" "ROM"
    mkdir -p "$OUT_DIR"
    OUT_DIR="$(cd "$OUT_DIR" && pwd)"
fi

FAILED=0
PASSED=0

safe_name() {
    printf '%s' "$1" | tr -c 'A-Za-z0-9_.-' '_'
}

validate_route_contract() {
    local route="$1"
    local route_path
    local route_name
    local native_env
    local stock_env
    local ares_input
    local actor_compare_args
    local compare_kind

    if ! route_path="$(python3 tools/rom_oracle_route.py resolve "$route")"; then
        return 1
    fi
    if ! python3 tools/rom_oracle_route.py validate "$route_path"; then
        return 1
    fi
    if ! route_name="$(python3 tools/rom_oracle_route.py field "$route_path" name)"; then
        return 1
    fi
    if ! compare_kind="$(python3 tools/rom_oracle_route.py field "$route_path" compare_kind)"; then
        return 1
    fi
    if ! native_env="$(python3 tools/rom_oracle_route.py native-env "$route_path")"; then
        return 1
    fi
    if ! stock_env="$(python3 tools/rom_oracle_route.py stock-env "$route_path")"; then
        return 1
    fi
    if ! ares_input="$(python3 tools/rom_oracle_route.py ares-input "$route_path")"; then
        return 1
    fi
    if ! actor_compare_args="$(python3 tools/rom_oracle_route.py actor-compare-args "$route_path")"; then
        return 1
    fi

    if [[ -z "$route_name" ]]; then
        echo "FAIL: route has empty name: $route_path" >&2
        return 1
    fi
    case "$compare_kind" in
        movement|intro|visual|glass) ;;
        *)
            echo "FAIL: route has unsupported compare_kind: $route_name $compare_kind" >&2
            return 1
            ;;
    esac
    if [[ "$ares_input" != \#\ mgb64\ ares\ input\ script\ v1* ]]; then
        echo "FAIL: route did not emit an ares input script header: $route_name" >&2
        return 1
    fi
    if grep -q $'\r' <<<"$native_env$stock_env$ares_input$actor_compare_args"; then
        echo "FAIL: route adapters contain CRLF bytes: $route_name" >&2
        return 1
    fi

    return 0
}

for route in "${ROUTES[@]}"; do
    echo ""
    echo "=== Route Contract: $route ==="
    if validate_route_contract "$route"; then
        echo "  contract: PASS"
    else
        echo "  contract: FAIL"
        FAILED=$((FAILED + 1))
        continue
    fi

    if [[ "$NATIVE_SMOKE" -eq 1 ]]; then
        # Intro-oracle routes (compare_kind=intro) have their own dedicated,
        # heavier native gate (ctest port intro_oracle_dam_route + the intro
        # census/audit tooling). Running a native capture for every one here as
        # well would multiply this smoke's wall-clock past its ctest timeout
        # (there are dozens of per-stage intro routes). Contract/schema
        # validation above still covers them; skip only their native capture.
        route_compare_kind="$(python3 tools/rom_oracle_route.py field "$route" compare_kind 2>/dev/null || echo "")"
        if [[ "$route_compare_kind" == "intro" ]]; then
            echo "  native_smoke: SKIP (intro route -- covered by intro_oracle gate)"
            continue
        fi
        route_name="$(python3 tools/rom_oracle_route.py field "$route" name)"
        route_out="$OUT_DIR/$(safe_name "$route_name")"
        if tools/movement_oracle_capture.sh \
            --route "$route" \
            --native-only \
            --no-compare \
            --no-build \
            --binary "$BINARY" \
            --rom "$ROM" \
            --out-dir "$route_out" \
            --timeout "$TIMEOUT_SECONDS"
        then
            echo "  native_smoke: PASS ($route_out)"
        else
            echo "  native_smoke: FAIL ($route_out)"
            FAILED=$((FAILED + 1))
            continue
        fi
    fi

    PASSED=$((PASSED + 1))
done

echo ""
echo "=== Route Contract Summary ==="
echo "  passed: $PASSED"
echo "  failed: $FAILED"

if [[ "$FAILED" -ne 0 ]]; then
    exit 1
fi

echo "PASS: route contract smoke"
