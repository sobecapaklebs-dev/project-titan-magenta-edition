#!/bin/bash
#
# camera_basis_regression.sh -- Guard live-look camera basis synchronization.
#
# Live mouse/gamepad look is applied after the normal Bond movement update on
# the native path. This lane drives Dam forward movement plus scripted mouse-look
# and verifies the derived movement/render basis (`facing`) is refreshed to match
# the live yaw (`theta`) before the PC room camera sync, and that the world
# camera up vector stays on the yaw/pitch zero-roll basis with SteadyView on.
# A stale or rolled basis makes the world appear to shear or lean while moving.
#
set -euo pipefail
cd "$(dirname "$0")/.."

source tools/validation_common.sh

BUILD_DIR="$(validation_default_build_dir)"
BINARY=""
ROM="$(validation_default_rom)"
DO_BUILD=1
TIMEOUT_SECONDS=90
OUT_DIR="/tmp/mgb64_camera_basis_$$"
LEVEL=33
FRAMES=240
LOOK_STEP=8
MAX_BASIS_ERROR=0.001
MAX_UP_ERROR=0.002

usage() {
    cat <<'USAGE'
Usage: tools/camera_basis_regression.sh [options]

Options:
  --out-dir DIR        output directory (default: /tmp/...)
  --level N            level id (default: 33, Dam)
  --frames N           screenshot/exit frame (default: 240)
  --look-step N        scripted mouse-look step (default: 8)
  --max-error N        max facing-vs-theta vector error (default: 0.001)
  --max-up-error N     max world-camera up-vector error (default: 0.002)
  --rom PATH           ROM path (default: ./baserom.u.z64)
  --binary PATH        native binary path (default: build/ge007)
  --build-dir DIR      CMake build directory (default: build)
  --no-build           reuse an existing native binary
  --timeout SECONDS    capture timeout (default: 90)

Artifacts are ROM-derived local validation data. Do not commit captured traces,
screenshots, logs, or generated audit summaries.
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --out-dir) OUT_DIR="$2"; shift 2 ;;
        --level) LEVEL="$2"; shift 2 ;;
        --frames) FRAMES="$2"; shift 2 ;;
        --look-step) LOOK_STEP="$2"; shift 2 ;;
        --max-error) MAX_BASIS_ERROR="$2"; shift 2 ;;
        --max-up-error) MAX_UP_ERROR="$2"; shift 2 ;;
        --rom) ROM="$2"; shift 2 ;;
        --binary) BINARY="$2"; shift 2 ;;
        --build-dir) BUILD_DIR="$2"; shift 2 ;;
        --no-build) DO_BUILD=0; shift ;;
        --timeout) TIMEOUT_SECONDS="$2"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown arg: $1" >&2; usage >&2; exit 2 ;;
    esac
done

for pair in \
    "level:$LEVEL" \
    "frames:$FRAMES" \
    "look-step:$LOOK_STEP" \
    "timeout:$TIMEOUT_SECONDS"; do
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

if [[ "$DO_BUILD" -eq 1 ]]; then
    validation_configure_build "$BUILD_DIR" >/dev/null
    validation_build "$BUILD_DIR" >/dev/null
fi

validation_require_binary "$BINARY"
validation_require_file "$ROM" "ROM"
validation_acquire_runtime_lock

cleanup() {
    validation_release_runtime_lock
}
trap cleanup EXIT INT TERM

mkdir -p "$OUT_DIR"
OUT_DIR="$(cd "$OUT_DIR" && pwd)"

TRACE="$OUT_DIR/trace.jsonl"
LOG="$OUT_DIR/run.log"
SUMMARY="$OUT_DIR/summary.json"

rm -f "$TRACE" "$LOG" "$SUMMARY" "$OUT_DIR"/camera_basis*.bmp
mkdir -p "$OUT_DIR/save"

echo "=== Camera Basis Regression ==="
echo "  out-dir: $OUT_DIR"
echo "  binary:  $BINARY"
echo "  ROM:     $ROM"
echo "  level:   $LEVEL"
echo "  frames:  $FRAMES"
echo "  look-step: $LOOK_STEP"

if ! validation_run_with_timeout "$TIMEOUT_SECONDS" \
    env -u GE007_DEBUG \
        SDL_AUDIODRIVER="${GE007_VALIDATION_SDL_AUDIODRIVER:-dummy}" \
        GE007_MUTE=1 \
        GE007_DETERMINISTIC_STABLE_COUNT=1 \
        GE007_NO_VSYNC=1 \
        GE007_BACKGROUND=1 \
        GE007_NO_INPUT_GRAB=1 \
        GE007_AUTO_FORWARD=70:150 \
        GE007_AUTO_LOOK_RIGHT=70:150 \
        GE007_AUTO_LOOK_STEP="$LOOK_STEP" \
        "$BINARY" \
        --savedir "$OUT_DIR/save" \
        --rom "$ROM" \
        --level "$LEVEL" \
        --deterministic \
        --trace-state "$TRACE" \
        --screenshot-frame "$FRAMES" \
        --screenshot-label camera_basis \
        --config-override Input.SteadyView=1 \
        --screenshot-exit >"$LOG" 2>&1; then
    echo "FAIL: camera basis capture failed" >&2
    tail -40 "$LOG" | sed 's/^/  /' >&2
    exit 1
fi

if [[ ! -s "$TRACE" ]]; then
    echo "FAIL: trace was not written: $TRACE" >&2
    tail -40 "$LOG" | sed 's/^/  /' >&2
    exit 1
fi

python3 - "$TRACE" "$SUMMARY" "$MAX_BASIS_ERROR" "$MAX_UP_ERROR" <<'PY'
import json
import math
import sys
from pathlib import Path

trace_path = Path(sys.argv[1])
summary_path = Path(sys.argv[2])
max_error_allowed = float(sys.argv[3])
max_up_error_allowed = float(sys.argv[4])

records = []
for line in trace_path.read_text().splitlines():
    if line.strip():
        records.append(json.loads(line))

max_error = -1.0
max_record = None
max_up_error = -1.0
max_up_record = None
theta_values = []
moving_records = 0

for record in records:
    frame = record.get("f", -1)
    if frame < 70:
        continue

    theta = record.get("theta")
    facing = record.get("facing")
    if theta is None or not isinstance(facing, list) or len(facing) != 3:
        continue

    speed = (record.get("move") or {}).get("speed") or [0.0, 0.0]
    if math.hypot(float(speed[0]), float(speed[1])) > 0.1:
        moving_records += 1

    theta_values.append(float(theta))
    radians = math.radians(float(theta))
    expected = [-math.sin(radians), 0.0, math.cos(radians)]
    error = math.sqrt(sum((float(facing[i]) - expected[i]) ** 2 for i in range(3)))

    if error > max_error:
        max_error = error
        max_record = {
            "frame": frame,
            "global": (record.get("move") or {}).get("global"),
            "theta": theta,
            "facing": facing,
            "expected": expected,
            "error": error,
            "speed": speed,
        }

    up = record.get("cam_up")
    view_basis = record.get("view_basis") or {}
    pitch = float(view_basis.get("vv_verta", 0.0))
    if isinstance(up, list) and len(up) == 3:
        pitch_radians = math.radians(pitch)
        expected_up = [
            math.sin(radians) * math.sin(pitch_radians),
            math.cos(pitch_radians),
            -math.cos(radians) * math.sin(pitch_radians),
        ]
        up_error = math.sqrt(
            sum((float(up[i]) - expected_up[i]) ** 2 for i in range(3))
        )
        if up_error > max_up_error:
            max_up_error = up_error
            max_up_record = {
                "frame": frame,
                "global": (record.get("move") or {}).get("global"),
                "theta": theta,
                "pitch": pitch,
                "cam_up": up,
                "expected_up": expected_up,
                "error": up_error,
                "headup": view_basis.get("headup"),
                "headlook": view_basis.get("headlook"),
                "speed": speed,
            }

theta_delta = max(theta_values) - min(theta_values) if theta_values else 0.0
summary = {
    "status": "pass",
    "records": len(records),
    "moving_records": moving_records,
    "theta_delta": theta_delta,
    "max_basis_error": max_error,
    "max_basis_error_allowed": max_error_allowed,
    "max_record": max_record,
    "max_up_error": max_up_error,
    "max_up_error_allowed": max_up_error_allowed,
    "max_up_record": max_up_record,
}

failures = []
if len(records) < 60:
    failures.append(f"too few records: {len(records)}")
if moving_records < 60:
    failures.append(f"too few moving records: {moving_records}")
if theta_delta < 90.0:
    failures.append(f"scripted look did not rotate enough: theta_delta={theta_delta:.3f}")
if max_error < 0.0:
    failures.append("no basis records found")
elif max_error > max_error_allowed:
    failures.append(
        f"basis mismatch too high: {max_error:.6f} > {max_error_allowed:.6f}"
    )
if max_up_error < 0.0:
    failures.append("no up-vector records found")
elif max_up_error > max_up_error_allowed:
    failures.append(
        f"camera up-vector roll too high: {max_up_error:.6f} > {max_up_error_allowed:.6f}"
    )

if failures:
    summary["status"] = "fail"
    summary["failures"] = failures

summary_path.write_text(json.dumps(summary, indent=2) + "\n")

if failures:
    print("FAIL: camera basis regression")
    for failure in failures:
        print(f"  {failure}")
    if max_record:
        print("  max_record:", json.dumps(max_record, sort_keys=True))
    if max_up_record:
        print("  max_up_record:", json.dumps(max_up_record, sort_keys=True))
    raise SystemExit(1)

print("PASS: camera basis regression")
print(f"  records={len(records)} moving_records={moving_records}")
print(f"  theta_delta={theta_delta:.3f}")
print(f"  max_basis_error={max_error:.6f} allowed={max_error_allowed:.6f}")
print(f"  max_up_error={max_up_error:.6f} allowed={max_up_error_allowed:.6f}")
print(f"summary_json: {summary_path}")
PY

echo "artifacts: $OUT_DIR"
