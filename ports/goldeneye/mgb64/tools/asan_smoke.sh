#!/bin/bash
#
# asan_smoke.sh -- Short deterministic ASan/UBSan pass over a few stages.
#
# Configures a separate -DSANITIZE=ON build, then runs build/ge007 headless
# over a small representative stage set with ASan/UBSan halting on the first
# error. Report-only by default (exit 0); pass --gate to fail the lane when a
# sanitizer error is observed.
#
# Captures are generated from the user's ROM and must stay local; do not commit
# traces, screenshots, logs, or audit summaries from /tmp.
#
set -euo pipefail
cd "$(dirname "$0")/.."

source tools/validation_common.sh

BUILD_DIR="build-asan"
BINARY=""
ROM="$(validation_default_rom)"
DO_BUILD=1
GATE=0
TIMEOUT_SECONDS=180
OUT_DIR="/tmp/mgb64_asan_smoke_$$"
FRAMES=600
LEVELS="33 41"

usage() {
    cat <<'USAGE'
Usage: tools/asan_smoke.sh [options]

Options:
  --level LIST             raw LEVELID list, quoted if multiple (default: "33 41")
  --frames N               deterministic exit frame (default: 600)
  --gate                   fail the lane on any sanitizer error (default: report-only)
  --out-dir DIR            output directory (default: /tmp/...)
  --rom PATH               ROM path (default: ./baserom.u.z64)
  --binary PATH            native ASan binary path (default: build-asan/ge007)
  --build-dir DIR          CMake build directory (default: build-asan)
  --no-build               reuse an existing ASan binary
  --timeout SECONDS        per-stage timeout (default: 180)

The default mode is report-only and always exits 0. Use --gate in CI to make
sanitizer findings fail the build.

Artifacts are ROM-derived local validation data. Do not commit captured traces,
screenshots, logs, or generated audit summaries.
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --level) LEVELS="$2"; shift 2 ;;
        --frames) FRAMES="$2"; shift 2 ;;
        --gate) GATE=1; shift ;;
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
    validation_configure_build "$BUILD_DIR" -DSANITIZE=ON >/dev/null
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
printf 'level\tstatus\tsanitizer_hits\n' >"$SUMMARY_FILE"

FINDINGS=0
PASSED=0
TOTAL=0

run_attempt() {
    local lvl="$1"
    local log="$OUT_DIR/level_${lvl}.log"
    local hits=0
    local proc_rc=0

    rm -f "$log"

    (
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
            ASAN_OPTIONS=halt_on_error=1:detect_leaks=0 \
            UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
            "$TIMEOUT_BIN" "${TIMEOUT_ARGS[@]}" \
            "$BINARY" \
            --rom "$ROM" \
            --level "$lvl" \
            --deterministic \
            --screenshot-frame "$FRAMES" \
            --screenshot-label "asan_${lvl}_$$" \
            --screenshot-exit
    ) >"$log" 2>&1 || proc_rc=$?
    if [[ "$proc_rc" -ne 0 ]]; then
        echo "    process: NONZERO EXIT (rc=$proc_rc)"
    else
        echo "    process: clean exit"
    fi

    hits="$(grep -cE 'AddressSanitizer|UndefinedBehaviorSanitizer|ERROR: AddressSanitizer|runtime error:' "$log" 2>/dev/null || true)"
    hits="${hits:-0}"

    if [[ "$hits" -ne 0 ]]; then
        echo "    sanitizer: FINDINGS ($hits)"
        grep -nE 'AddressSanitizer|UndefinedBehaviorSanitizer|runtime error:' "$log" | head -8 | sed 's/^/      /'
        printf '%s\treport\t%s\n' "$lvl" "$hits" >>"$SUMMARY_FILE"
        return 1
    fi

    # AUDIT-0012: a nonzero process exit with no sanitizer banner (crash without a
    # banner, timeout, loader/linker failure, unusable binary, bad-CLI regression)
    # is still a FAILED run — the requested gameplay interval never certified
    # clean. Do not record it as clean.
    if [[ "$proc_rc" -ne 0 ]]; then
        echo "    sanitizer: NO BANNER but process failed (rc=$proc_rc) — run did not complete"
        tail -8 "$log" | sed 's/^/      /'
        printf '%s\tprocfail\t%s\n' "$lvl" "$proc_rc" >>"$SUMMARY_FILE"
        return 1
    fi

    echo "    sanitizer: clean"
    printf '%s\tclean\t0\n' "$lvl" >>"$SUMMARY_FILE"
    return 0
}

echo "=== ASan/UBSan Smoke ==="
echo "  out-dir:  $OUT_DIR"
echo "  binary:   $BINARY"
echo "  ROM:      $ROM"
echo "  levels:   $LEVELS"
echo "  frames:   $FRAMES"
echo "  gate:     $GATE"

for lvl in $LEVELS; do
    TOTAL=$((TOTAL + 1))

    echo ""
    echo "=== ASan: Level $lvl ==="
    if run_attempt "$lvl"; then
        echo "  level: CLEAN"
        PASSED=$((PASSED + 1))
    else
        echo "  level: SANITIZER FINDINGS"
        FINDINGS=$((FINDINGS + 1))
    fi
done

echo ""
echo "=== ASan/UBSan Smoke: $PASSED/$TOTAL clean, $FINDINGS with findings ==="
echo "  artifacts: $OUT_DIR"
echo "  summary:   $SUMMARY_FILE"

if [[ "$GATE" -eq 1 ]]; then
    exit "$FINDINGS"
fi
exit 0
