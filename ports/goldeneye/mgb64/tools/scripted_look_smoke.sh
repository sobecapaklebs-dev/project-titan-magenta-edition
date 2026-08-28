#!/bin/bash
#
# scripted_look_smoke.sh -- Verify deterministic scripted look is not suppressed
# by the input-freeze path used by local capture lanes.
#
# Captures and traces are generated from the user's ROM and must stay local.
#
set -euo pipefail
cd "$(dirname "$0")/.."

source tools/validation_common.sh

BUILD_DIR="$(validation_default_build_dir)"
BINARY=""
ROM="$(validation_default_rom)"
DO_BUILD=1
TIMEOUT_SECONDS=45
OUT_DIR="/tmp/mgb64_scripted_look_smoke_$$"
LEVEL=36
FRAMES=220
INPUT_WINDOW="80:100"
MIN_PITCH_DELTA="0.20"
MAX_BASELINE_ABS_PITCH="0.04"

usage() {
    cat <<'USAGE'
Usage: tools/scripted_look_smoke.sh [options]

Options:
  --level LEVELID             raw LEVELID to boot (default: 36)
  --input-window START:LEN    deterministic GE007_AUTO_LOOK_UP window (default: 80:100)
  --frames N                  deterministic screenshot/exit frame (default: 220)
  --min-pitch-delta F         minimum baseline-vs-look pitch change (default: 0.20)
  --max-baseline-abs-pitch F  max baseline absolute pitch drift (default: 0.04)
  --out-dir DIR               output directory (default: /tmp/...)
  --rom PATH                  ROM path (default: ./baserom.u.z64)
  --binary PATH               native binary path (default: build/ge007)
  --build-dir DIR             CMake build directory (default: build)
  --no-build                  reuse an existing native binary
  --timeout SECONDS           per-capture timeout (default: 45)

Artifacts are ROM-derived local validation data. Do not commit captured traces,
screenshots, logs, or generated summaries.
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --level) LEVEL="$2"; shift 2 ;;
        --input-window) INPUT_WINDOW="$2"; shift 2 ;;
        --frames) FRAMES="$2"; shift 2 ;;
        --min-pitch-delta) MIN_PITCH_DELTA="$2"; shift 2 ;;
        --max-baseline-abs-pitch) MAX_BASELINE_ABS_PITCH="$2"; shift 2 ;;
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

if [[ ! "$LEVEL" =~ ^-?[0-9]+$ ]]; then
    echo "FAIL: --level must be a raw integer LEVELID: $LEVEL" >&2
    exit 2
fi
if [[ ! "$FRAMES" =~ ^[1-9][0-9]*$ ]]; then
    echo "FAIL: --frames must be a positive integer: $FRAMES" >&2
    exit 2
fi
if [[ ! "$TIMEOUT_SECONDS" =~ ^[1-9][0-9]*$ ]]; then
    echo "FAIL: --timeout must be a positive integer: $TIMEOUT_SECONDS" >&2
    exit 2
fi
if [[ ! "$INPUT_WINDOW" =~ ^[0-9]+:[1-9][0-9]*$ ]]; then
    echo "FAIL: --input-window must use START:LEN: $INPUT_WINDOW" >&2
    exit 2
fi
if ! python3 - "$MIN_PITCH_DELTA" "$MAX_BASELINE_ABS_PITCH" <<'PY'
import math
import sys

for value in sys.argv[1:]:
    try:
        parsed = float(value)
    except ValueError:
        raise SystemExit(1)
    if not math.isfinite(parsed) or parsed < 0.0:
        raise SystemExit(1)
raise SystemExit(0)
PY
then
    echo "FAIL: pitch thresholds must be non-negative finite numbers" >&2
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
SUMMARY_JSON="$OUT_DIR/summary.json"

validation_acquire_runtime_lock
trap 'validation_release_runtime_lock' EXIT INT TERM

run_capture() {
    local variant="$1"
    local look_env="$2"
    local label="scripted_look_${variant}_$$"
    local trace="$OUT_DIR/${variant}.jsonl"
    local log="$OUT_DIR/${variant}.log"
    local screenshot_src="$OUT_DIR/screenshot_${label}.bmp"
    local screenshot_dst="$OUT_DIR/${variant}.bmp"
    local env_cmd=()

    rm -f "$trace" "$log" "$screenshot_src" "$screenshot_dst"

    echo "  capture: $variant${look_env:+ $look_env}"
    env_cmd=(env -u GE007_DEBUG
        SDL_AUDIODRIVER="${GE007_VALIDATION_SDL_AUDIODRIVER:-dummy}"
        GE007_MUTE=1
        GE007_DETERMINISTIC_STABLE_COUNT=1
        GE007_NO_VSYNC=1
        GE007_BACKGROUND=1
        GE007_NO_INPUT_GRAB=1
        GE007_DISABLE_LEVEL_INTRO=1)
    if [[ -n "$look_env" ]]; then
        env_cmd+=("$look_env")
    fi
    env_cmd+=("$BINARY"
        --savedir "$OUT_DIR"
        --rom "$ROM"
        --level "$LEVEL"
        --deterministic
        --trace-state "$trace"
        --screenshot-frame "$FRAMES"
        --screenshot-label "$label"
        --screenshot-exit)

    if ! (
        cd "$OUT_DIR"
        validation_run_with_timeout "$TIMEOUT_SECONDS" "${env_cmd[@]}"
    ) >"$log" 2>&1; then
        echo "FAIL: capture failed for $variant"
        tail -40 "$log" | sed 's/^/  /'
        exit 1
    fi

    if grep -qF "[GEASSERT]" "$log"; then
        echo "FAIL: GEASSERT fired during $variant"
        grep -F "[GEASSERT]" "$log" | head -5 | sed 's/^/  /'
        exit 1
    fi
    if [[ ! -s "$trace" ]]; then
        echo "FAIL: missing state trace for $variant: $trace"
        exit 1
    fi
    if [[ ! -s "$screenshot_src" ]]; then
        echo "FAIL: missing screenshot for $variant: $screenshot_src"
        tail -40 "$log" | sed 's/^/  /'
        exit 1
    fi
    mv "$screenshot_src" "$screenshot_dst"
}

echo "=== Scripted Look Smoke ==="
echo "  out-dir:              $OUT_DIR"
echo "  binary:               $BINARY"
echo "  ROM:                  $ROM"
echo "  level:                $LEVEL"
echo "  input-window:         $INPUT_WINDOW"
echo "  frames:               $FRAMES"
echo "  min-pitch-delta:      $MIN_PITCH_DELTA"
echo "  max-baseline-pitch:   $MAX_BASELINE_ABS_PITCH"

run_capture "baseline" ""
run_capture "look_up" "GE007_AUTO_LOOK_UP=$INPUT_WINDOW"

python3 - "$OUT_DIR/baseline.jsonl" "$OUT_DIR/look_up.jsonl" "$SUMMARY_JSON" \
    "$LEVEL" "$INPUT_WINDOW" "$FRAMES" "$MIN_PITCH_DELTA" "$MAX_BASELINE_ABS_PITCH" <<'PY'
import json
import math
import sys
from pathlib import Path

baseline_path = Path(sys.argv[1])
look_path = Path(sys.argv[2])
summary_path = Path(sys.argv[3])
level = int(sys.argv[4])
input_window = sys.argv[5]
frames = int(sys.argv[6])
min_pitch_delta = float(sys.argv[7])
max_baseline_abs_pitch = float(sys.argv[8])


def records(path: Path):
    out = []
    with path.open("r", encoding="utf-8", errors="replace") as handle:
        for line in handle:
            if line.startswith("{"):
                out.append(json.loads(line))
    if not out:
        raise SystemExit(f"FAIL: no JSON records in {path}")
    return out


def pitch_value(record):
    move = record.get("move") or {}
    value = move.get("pitch")
    if not isinstance(value, (int, float)) or not math.isfinite(value):
        raise SystemExit("FAIL: trace is missing finite move.pitch")
    return float(value)


baseline = records(baseline_path)
look = records(look_path)
baseline_final = pitch_value(baseline[-1])
look_final = pitch_value(look[-1])
baseline_max_abs = max(abs(pitch_value(record)) for record in baseline)
pitch_delta = baseline_final - look_final

failures = []
if baseline_max_abs > max_baseline_abs_pitch:
    failures.append(
        f"baseline pitch drift {baseline_max_abs:.6f} > {max_baseline_abs_pitch:.6f}"
    )
if pitch_delta < min_pitch_delta:
    failures.append(
        f"scripted look pitch delta {pitch_delta:.6f} < {min_pitch_delta:.6f}"
    )

summary = {
    "status": "fail" if failures else "pass",
    "level": level,
    "frames": frames,
    "input_window": input_window,
    "min_pitch_delta": min_pitch_delta,
    "max_baseline_abs_pitch": max_baseline_abs_pitch,
    "baseline_final_pitch": baseline_final,
    "baseline_max_abs_pitch": baseline_max_abs,
    "look_final_pitch": look_final,
    "pitch_delta": pitch_delta,
    "failures": failures,
    "baseline_trace": str(baseline_path),
    "look_trace": str(look_path),
}
with summary_path.open("w", encoding="utf-8") as handle:
    json.dump(summary, handle, indent=2, sort_keys=True)
    handle.write("\n")

print(
    "summary:"
    f" status={summary['status']}"
    f" baseline_final={baseline_final:.6f}"
    f" look_final={look_final:.6f}"
    f" pitch_delta={pitch_delta:.6f}"
    f" baseline_max_abs={baseline_max_abs:.6f}"
)
print(f"summary_json: {summary_path}")
if failures:
    for failure in failures:
        print(f"  {failure}")
    raise SystemExit(1)
PY

echo ""
echo "=== Scripted Look Smoke: PASS ==="
echo "Artifacts are local ROM-derived validation data. Keep them out of git."
