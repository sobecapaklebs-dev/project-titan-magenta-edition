#!/bin/bash
#
# soak_stability.sh -- Long headless deterministic stability soak lane.
#
# Runs build/ge007 headless over a representative stage set for a configurable
# duration, captures the --trace-state JSONL, and pipes each trace through
# tools/audit_render_trace.py --max-crashes 0. Hard-fails on any crash,
# recovery, bad command, NaN, or DL-resolve failure.
#
# Captures are generated from the user's ROM and must stay local; do not commit
# traces, screenshots, logs, or audit summaries from /tmp.
#
set -euo pipefail
cd "$(dirname "$0")/.."

source tools/validation_common.sh

BUILD_DIR="$(validation_default_build_dir)"
BINARY=""
ROM="$(validation_default_rom)"
DO_BUILD=1
TIMEOUT_SECONDS=600
OUT_DIR="/tmp/mgb64_soak_stability_$$"
FRAMES=5400
LEVELS="33 41 27"
ALL_LEVELS="33 34 22 26 36 35 9 20 43 27 24 29 30 25 37 23 39 41 28 32"

usage() {
    cat <<'USAGE'
Usage: tools/soak_stability.sh [options]

Options:
  --all                    soak all 20 supported solo stages
  --level LIST             raw LEVELID list, quoted if multiple (default: "33 41 27")
  --frames N               deterministic soak length in frames (default: 5400)
  --out-dir DIR            output directory (default: /tmp/...)
  --rom PATH               ROM path (default: ./baserom.u.z64)
  --binary PATH            native binary path (default: build/ge007)
  --build-dir DIR          CMake build directory (default: build)
  --no-build               reuse an existing native binary
  --timeout SECONDS        per-stage timeout (default: 600)

Artifacts are ROM-derived local validation data. Do not commit captured traces,
screenshots, logs, or generated audit summaries.
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --all) LEVELS="$ALL_LEVELS"; shift ;;
        --level) LEVELS="$2"; shift 2 ;;
        --frames) FRAMES="$2"; shift 2 ;;
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

if [[ ! "$FRAMES" =~ ^[1-9][0-9]*$ ]]; then
    echo "FAIL: --frames must be a positive integer: $FRAMES" >&2
    exit 2
fi
if [[ ! "$TIMEOUT_SECONDS" =~ ^[1-9][0-9]*$ ]]; then
    echo "FAIL: --timeout must be a positive integer: $TIMEOUT_SECONDS" >&2
    exit 2
fi
for lvl in $LEVELS; do
    if [[ ! "$lvl" =~ ^-?[0-9]+$ ]]; then
        echo "FAIL: --level list contains a non-integer LEVELID: $lvl" >&2
        exit 2
    fi
done

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

TIMEOUT_CMD="$(validation_resolve_timeout_cmd)"
if [[ -n "$TIMEOUT_CMD" ]]; then
    TIMEOUT_BIN="$TIMEOUT_CMD"
    TIMEOUT_ARGS=(--kill-after=5 "$TIMEOUT_SECONDS")
else
    TIMEOUT_BIN="env"
    TIMEOUT_ARGS=()
fi

SUMMARY_FILE="$OUT_DIR/summary.tsv"
printf 'level\tframes\trecords\tstatus\n' >"$SUMMARY_FILE"

FAILED=0
PASSED=0
TOTAL=0

run_attempt() {
    local lvl="$1"
    local trace="$OUT_DIR/level_${lvl}.jsonl"
    local log="$OUT_DIR/level_${lvl}.log"
    local render_log="$OUT_DIR/level_${lvl}.render.txt"
    local render_json="$OUT_DIR/level_${lvl}.render.json"
    local records=0

    rm -f "$trace" "$log" "$render_log" "$render_json"

    if ! (
        cd "$OUT_DIR"
        env -u GE007_DEBUG \
            SDL_AUDIODRIVER="${GE007_VALIDATION_SDL_AUDIODRIVER:-dummy}" \
            GE007_MUTE=1 \
            GE007_DETERMINISTIC_STABLE_COUNT=1 \
            GE007_NO_VSYNC=1 \
            GE007_BACKGROUND=1 \
            GE007_NO_INPUT_GRAB=1 \
            GE007_DEBUG=1 \
            GE007_ASSERT_ON_FAIL=0 \
            GE007_DISABLE_LEVEL_INTRO=1 \
            "$TIMEOUT_BIN" "${TIMEOUT_ARGS[@]}" \
            "$BINARY" \
            --rom "$ROM" \
            --level "$lvl" \
            --deterministic \
            --trace-state "$trace" \
            --screenshot-frame "$FRAMES" \
            --screenshot-label "soak_${lvl}_$$" \
            --screenshot-exit
    ) >"$log" 2>&1; then
        echo "    process: FAIL"
        tail -20 "$log" | sed 's/^/      /'
        return 1
    fi
    echo "    process: PASS"

    if [[ ! -s "$trace" ]]; then
        echo "    trace: FAIL (missing or empty)"
        tail -20 "$log" | sed 's/^/      /'
        return 1
    fi
    records="$(grep -c '^{' "$trace" 2>/dev/null || true)"
    records="${records:-0}"
    echo "    trace: PASS ($records records)"

    if python3 tools/audit_render_trace.py \
        --label "soak level $lvl" \
        --max-crashes 0 \
        --max-bad-cmds 0 \
        --max-nan 0 \
        --max-dl-counter 0 \
        --json-out "$render_json" \
        "$trace" >"$render_log" 2>&1; then
        echo "    render_health: PASS"
    else
        echo "    render_health: FAIL"
        sed -n '1,16p' "$render_log" | sed 's/^/      /'
        printf '%s\t%s\t%s\tfail\n' "$lvl" "$FRAMES" "$records" >>"$SUMMARY_FILE"
        return 1
    fi

    printf '%s\t%s\t%s\tpass\n' "$lvl" "$FRAMES" "$records" >>"$SUMMARY_FILE"
    return 0
}

echo "=== Soak Stability ==="
echo "  out-dir:  $OUT_DIR"
echo "  binary:   $BINARY"
echo "  ROM:      $ROM"
echo "  levels:   $LEVELS"
echo "  frames:   $FRAMES"
echo "  timeout:  $TIMEOUT_SECONDS"

for lvl in $LEVELS; do
    TOTAL=$((TOTAL + 1))

    echo ""
    echo "=== Soak: Level $lvl ==="
    if run_attempt "$lvl"; then
        echo "  level: PASS"
        PASSED=$((PASSED + 1))
    else
        echo "  level: FAIL (crash/recovery/bad-cmd/nan/DL-resolve detected)"
        FAILED=$((FAILED + 1))
    fi
done

echo ""
echo "=== Soak Stability: $PASSED/$TOTAL passed, $FAILED failed ==="
echo "  artifacts: $OUT_DIR"
echo "  summary:   $SUMMARY_FILE"
exit "$FAILED"
