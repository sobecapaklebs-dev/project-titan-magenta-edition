#!/bin/bash
#
# campaign_route_smoke.sh -- Run structured native campaign route contracts.
#
# These routes are stronger than process-only smokes, but the default contracts
# are still scripted mission contracts, not complete organic campaign routes.
#
set -euo pipefail
cd "$(dirname "$0")/.."

source tools/validation_common.sh

BUILD_DIR="$(validation_default_build_dir)"
BINARY=""
ROM="$(validation_default_rom)"
DO_BUILD=1
TIMEOUT_SECONDS=90
OUT_DIR="/tmp/mgb64_campaign_route_smoke_$$"
ROUTES=()

usage() {
    cat <<'USAGE'
Usage: tools/campaign_route_smoke.sh [options]

Options:
  --route NAME|PATH      campaign route JSON; may be repeated
  --out-dir DIR          output directory (default: /tmp/...)
  --rom PATH             ROM path (default: ./baserom.u.z64)
  --binary PATH          native binary path (default: build/ge007)
  --build-dir DIR        CMake build directory (default: build)
  --no-build             reuse an existing native binary
  --timeout SECONDS      per-route timeout (default: 90)
  --list                 list known campaign routes

Artifacts are ROM-derived local validation data. Do not commit captured traces,
logs, saves, or generated audit summaries.
USAGE
}

LIST_ONLY=0
while [[ $# -gt 0 ]]; do
    case "$1" in
        --route) ROUTES+=("$2"); shift 2 ;;
        --out-dir) OUT_DIR="$2"; shift 2 ;;
        --rom) ROM="$2"; shift 2 ;;
        --binary) BINARY="$2"; shift 2 ;;
        --build-dir) BUILD_DIR="$2"; shift 2 ;;
        --no-build) DO_BUILD=0; shift ;;
        --timeout) TIMEOUT_SECONDS="$2"; shift 2 ;;
        --list) LIST_ONLY=1; shift ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown arg: $1" >&2; usage >&2; exit 2 ;;
    esac
done

if [[ "$LIST_ONLY" -eq 1 ]]; then
    python3 tools/campaign_route_smoke.py --list --binary /dev/null --rom /dev/null --out-dir /tmp
    exit 0
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

validation_acquire_runtime_lock
trap 'validation_release_runtime_lock' EXIT INT TERM

args=(
    --binary "$BINARY"
    --rom "$ROM"
    --out-dir "$OUT_DIR"
    --timeout "$TIMEOUT_SECONDS"
)
if [[ "${#ROUTES[@]}" -gt 0 ]]; then
    for route in "${ROUTES[@]}"; do
        args+=(--route "$route")
    done
fi

python3 tools/campaign_route_smoke.py "${args[@]}"
