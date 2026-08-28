#!/bin/bash
#
# save_persistence_check.sh -- Verify save data survives a process restart.
#
# This is a focused native-port smoke test:
#   1. Start with an isolated --savedir.
#   2. Seed Dam/Agent completion into folder 0 through the deterministic helper.
#   3. Restart and seed a Dam-through-Runway Secret Agent range into folder 1.
#   4. Restart without seeding and confirm both folders reload from EEPROM.
#
set -euo pipefail
cd "$(dirname "$0")/.."

source tools/validation_common.sh

BUILD_DIR="$(validation_default_build_dir)"
BINARY=""
ROM="$(validation_default_rom)"
DO_BUILD=1
TIMEOUT_SECONDS=45
TIMEOUT_BIN="$(validation_resolve_timeout_cmd)"
FRAME=420
OUT_DIR=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --build-dir) BUILD_DIR="$2"; shift 2 ;;
        --binary) BINARY="$2"; shift 2 ;;
        --rom) ROM="$2"; shift 2 ;;
        --out-dir) OUT_DIR="$2"; shift 2 ;;
        --no-build) DO_BUILD=0; shift ;;
        --timeout) TIMEOUT_SECONDS="$2"; shift 2 ;;
        --frame) FRAME="$2"; shift 2 ;;
        -h|--help)
            echo "Usage: $0 [--rom PATH] [--binary PATH] [--build-dir DIR] [--out-dir DIR] [--no-build]"
            exit 0 ;;
        *) echo "Unknown arg: $1" >&2; exit 2 ;;
    esac
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
validation_acquire_runtime_lock

REMOVE_ARTIFACTS=1
if [[ -n "$OUT_DIR" ]]; then
    mkdir -p "$OUT_DIR"
    OUT_DIR="$(cd "$OUT_DIR" && pwd)"
    TMPDIR="$OUT_DIR/save"
    TRACE_PREFIX="$OUT_DIR/ge007_save"
    REMOVE_ARTIFACTS=0
    rm -rf "$TMPDIR"
    mkdir -p "$TMPDIR"
    rm -f "${TRACE_PREFIX}"*.jsonl "${TRACE_PREFIX}"*.log
else
    TMPDIR="$(mktemp -d /tmp/ge007_save_persist.XXXXXX)"
    TRACE_PREFIX="/tmp/ge007_save_$$"
fi
SEED0_TRACE="${TRACE_PREFIX}_seed_folder0.jsonl"
SEED1_TRACE="${TRACE_PREFIX}_seed_folder1.jsonl"
RELOAD_TRACE="${TRACE_PREFIX}_reload.jsonl"
SEED0_LOG="${TRACE_PREFIX}_seed_folder0.log"
SEED1_LOG="${TRACE_PREFIX}_seed_folder1.log"
RELOAD_LOG="${TRACE_PREFIX}_reload.log"

cleanup() {
    if [[ "$REMOVE_ARTIFACTS" -eq 1 ]]; then
        rm -rf "$TMPDIR"
        rm -f "${TRACE_PREFIX}"*.jsonl "${TRACE_PREFIX}"*.log
    fi
    validation_release_runtime_lock
}
trap cleanup EXIT INT TERM

run_game() {
    local trace="$1"
    local label="$2"
    shift 2

    local cmd=()
    if [[ -n "$TIMEOUT_BIN" ]]; then
        cmd=("$TIMEOUT_BIN" --kill-after=5 "$TIMEOUT_SECONDS" "$BINARY")
    else
        cmd=("$BINARY")
    fi

    (
        cd "$TMPDIR"
        validation_automation_env "${cmd[@]}" \
            --rom "$ROM" \
            --savedir "$TMPDIR" \
            --deterministic \
            --trace-state "$trace" \
            --screenshot-frame "$FRAME" \
            --screenshot-label "$label" \
            --screenshot-exit
    )
}

run_seed_case() {
    local folder="$1"
    local spec="$2"
    local label="$3"
    local trace="$4"
    local log="$5"
    local expected_log="$6"
    local description="$7"

    if ! GE007_AUTO_START='20:3,80:3,140:3,200:3,260:3,320:3,380:3' \
         GE007_AUTO_UNLOCK_SOLO="$spec" \
         GE007_AUTO_UNLOCK_FOLDER="$folder" \
         run_game "$trace" "$label" >"$log" 2>&1; then
        echo "FAIL: ${description} seed run failed"
        tail -40 "$log" | sed 's/^/  /'
        exit 1
    fi

    if [[ ! -s "$TMPDIR/ge007_eeprom.bin" ]]; then
        echo "FAIL: ${description} seed run did not create ge007_eeprom.bin"
        exit 1
    fi

    if ! grep -q "$expected_log" "$log"; then
        echo "FAIL: ${description} seed run did not report expected deterministic unlock"
        echo "      expected log fragment: $expected_log"
        tail -40 "$log" | sed 's/^/  /'
        exit 1
    fi

    echo "  seed: PASS (${description})"
}

echo "=== Save Persistence ==="
if [[ -n "$OUT_DIR" ]]; then
    echo "  out-dir: $OUT_DIR"
fi
echo "  savedir: $TMPDIR"

run_seed_case \
    0 \
    '0:0:123' \
    "save_seed_folder0_$$" \
    "$SEED0_TRACE" \
    "$SEED0_LOG" \
    'seeded 1 solo unlock(s) folder=0 stages=0-0 difficulty=0 visible=1' \
    'folder 0 Dam/Agent completion written'

run_seed_case \
    1 \
    '0-2:1:456' \
    "save_seed_folder1_$$" \
    "$SEED1_TRACE" \
    "$SEED1_LOG" \
    'seeded 3 solo unlock(s) folder=1 stages=0-2 difficulty=1 visible=3' \
    'folder 1 Dam-through-Runway/Secret Agent range written'

if ! GE007_AUTO_START='20:3,80:3,140:3,200:3,260:3,320:3,380:3' \
     run_game "$RELOAD_TRACE" "save_reload_$$" >"$RELOAD_LOG" 2>&1; then
    echo "FAIL: reload run failed"
    tail -40 "$RELOAD_LOG" | sed 's/^/  /'
    exit 1
fi

if grep -q "seeded 1 solo unlock" "$RELOAD_LOG"; then
    echo "FAIL: reload run unexpectedly reseeded save data"
    exit 1
fi
if grep -q "seeded 3 solo unlock" "$RELOAD_LOG"; then
    echo "FAIL: reload run unexpectedly reseeded range save data"
    exit 1
fi

python3 - "$SEED0_TRACE" "$SEED1_TRACE" "$RELOAD_TRACE" <<'PY'
import json
import sys

EXPECTATIONS = (
    (0, 0, 0, "folder 0 Dam/Agent"),
    (1, 2, 1, "folder 1 Runway/Secret Agent"),
)

def load(path):
    frames = []
    with open(path, "r", encoding="utf-8", errors="replace") as handle:
        for line in handle:
            if not line.startswith("{"):
                continue
            frames.append(json.loads(line))
    if not frames:
        raise SystemExit(f"FAIL: no state frames in {path}")
    return frames

def completion_frames(frames, folder, level_id, difficulty_id):
    hits = []
    for frame in frames:
        save = frame.get("save") or {}
        valid = save.get("valid") or []
        level = save.get("level") or []
        difficulty = save.get("difficulty") or []
        if len(valid) > folder and len(level) > folder and len(difficulty) > folder:
            if valid[folder] == 1 and level[folder] == level_id and difficulty[folder] == difficulty_id:
                hits.append(frame.get("f"))
    return hits

def assert_expectations(label, path, expectations):
    frames = load(path)
    for folder, level_id, difficulty_id, description in expectations:
        hits = completion_frames(frames, folder, level_id, difficulty_id)
        if not hits:
            last = frames[-1].get("save")
            raise SystemExit(f"FAIL: {label} trace did not report {description}; last save={last}")
        print(f"  {label} trace: PASS {description} ({len(hits)} frame(s), last={hits[-1]})")

assert_expectations("seed folder 0", sys.argv[1], EXPECTATIONS[:1])
assert_expectations("seed folder 1", sys.argv[2], EXPECTATIONS)
assert_expectations("reload", sys.argv[3], EXPECTATIONS)
PY

echo "=== Save Persistence: PASS ==="
