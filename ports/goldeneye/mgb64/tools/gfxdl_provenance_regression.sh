#!/bin/bash
#
# gfxdl_provenance_regression.sh -- Guard long-playback dynamic DL provenance.
#
# The default case reproduces the Surface 1 setup from a user playback log that
# previously printed repeated `[GFX-DL] unregistered` rows around frame 11450.
# This is a trace-health lane, not a screenshot lane: it runs through a target
# deterministic frame with GE007_AUTO_EXIT_FRAME and asserts that display-list
# resolve counters and diagnostic log rows remain clean.
#
set -euo pipefail
cd "$(dirname "$0")/.."

source tools/validation_common.sh

BUILD_DIR="$(validation_default_build_dir)"
BINARY=""
ROM="$(validation_default_rom)"
DO_BUILD=1
TIMEOUT_SECONDS=300
OUT_DIR="/tmp/mgb64_gfxdl_provenance_$$"
LEVEL=36
FRAMES=11600
AUTO_FORWARD=""
EXPECTED_SETUP="136,251,25,16,47,9,203"
LEVEL_SET=0
EXPECTED_SETUP_SET=0

usage() {
    cat <<'USAGE'
Usage: tools/gfxdl_provenance_regression.sh [options]

Options:
  --out-dir DIR             output directory (default: /tmp/...)
  --level N                 level id (default: 36, Surface 1)
  --frames N                trace through deterministic frame N (default: 11600)
  --auto-forward START:LEN  deterministic forward-input window
                            default is 70:(frames - 100)
  --expected-setup CSV      bound,waypoints,waygroups,patrols,ailists,guards,objects
                            default matches the submitted Surface 1 setup
  --no-expected-setup       skip setup-count assertion
  --rom PATH                ROM path (default: ./baserom.u.z64)
  --binary PATH             native binary path (default: build/ge007)
  --build-dir DIR           CMake build directory (default: build)
  --no-build                reuse an existing native binary
  --timeout SECONDS         capture timeout (default: 300)

Artifacts are ROM-derived local validation data. Do not commit captured traces,
logs, or generated audit summaries.
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --out-dir) OUT_DIR="$2"; shift 2 ;;
        --level) LEVEL="$2"; LEVEL_SET=1; shift 2 ;;
        --frames) FRAMES="$2"; shift 2 ;;
        --auto-forward) AUTO_FORWARD="$2"; shift 2 ;;
        --expected-setup) EXPECTED_SETUP="$2"; EXPECTED_SETUP_SET=1; shift 2 ;;
        --no-expected-setup) EXPECTED_SETUP=""; EXPECTED_SETUP_SET=1; shift ;;
        --rom) ROM="$2"; shift 2 ;;
        --binary) BINARY="$2"; shift 2 ;;
        --build-dir) BUILD_DIR="$2"; shift 2 ;;
        --no-build) DO_BUILD=0; shift ;;
        --timeout) TIMEOUT_SECONDS="$2"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown arg: $1" >&2; usage >&2; exit 2 ;;
    esac
done

if [[ ! "$LEVEL" =~ ^[1-9][0-9]*$ ]]; then
    echo "FAIL: --level must be a positive integer: $LEVEL" >&2
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

if [[ "$LEVEL_SET" -eq 1 && "$LEVEL" != "36" && "$EXPECTED_SETUP_SET" -eq 0 ]]; then
    EXPECTED_SETUP=""
fi

if [[ -z "$AUTO_FORWARD" ]]; then
    forward_len=$((FRAMES > 100 ? FRAMES - 100 : FRAMES))
    AUTO_FORWARD="70:${forward_len}"
fi
if [[ ! "$AUTO_FORWARD" =~ ^[0-9]+:[1-9][0-9]*$ ]]; then
    echo "FAIL: --auto-forward must use START:LEN: $AUTO_FORWARD" >&2
    exit 2
fi
if [[ -n "$EXPECTED_SETUP" && ! "$EXPECTED_SETUP" =~ ^[0-9]+,[0-9]+,[0-9]+,[0-9]+,[0-9]+,[0-9]+,[0-9]+$ ]]; then
    echo "FAIL: --expected-setup must be seven comma-separated integers: $EXPECTED_SETUP" >&2
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
SAVE_DIR="$OUT_DIR/save"

rm -f "$TRACE" "$LOG" "$SUMMARY"
mkdir -p "$SAVE_DIR"

echo "=== GFX DL Provenance Regression ==="
echo "  out-dir: $OUT_DIR"
echo "  binary:  $BINARY"
echo "  ROM:     $ROM"
echo "  level:   $LEVEL"
echo "  frames:  $FRAMES"
echo "  auto-forward: $AUTO_FORWARD"
if [[ -n "$EXPECTED_SETUP" ]]; then
    echo "  expected-setup: $EXPECTED_SETUP"
fi

if ! validation_run_with_timeout "$TIMEOUT_SECONDS" \
    env -u GE007_DEBUG \
        SDL_AUDIODRIVER="${GE007_VALIDATION_SDL_AUDIODRIVER:-dummy}" \
        GE007_MUTE=1 \
        GE007_DETERMINISTIC_STABLE_COUNT=1 \
        GE007_NO_VSYNC=1 \
        GE007_BACKGROUND=1 \
        GE007_NO_INPUT_GRAB=1 \
        GE007_TRACE_FLOW_ONLY=1 \
        GE007_AUTO_FORWARD="$AUTO_FORWARD" \
        GE007_AUTO_EXIT_FRAME="$FRAMES" \
        "$BINARY" \
        --savedir "$SAVE_DIR" \
        --rom "$ROM" \
        --level "$LEVEL" \
        --deterministic \
        --trace-state "$TRACE" >"$LOG" 2>&1; then
    echo "FAIL: GFX DL provenance capture failed" >&2
    tail -80 "$LOG" | sed 's/^/  /' >&2
    exit 1
fi

if grep -qF "[GEASSERT]" "$LOG"; then
    echo "FAIL: GEASSERT fired during GFX DL provenance capture" >&2
    grep -F "[GEASSERT]" "$LOG" | head -5 | sed 's/^/  /' >&2
    exit 1
fi
if grep -qF "[GFX-DL]" "$LOG"; then
    echo "FAIL: GFX-DL diagnostic rows observed" >&2
    grep -F "[GFX-DL]" "$LOG" | head -20 | sed 's/^/  /' >&2
    exit 1
fi
if ! grep -qF "deterministic frame exit observed" "$LOG"; then
    echo "FAIL: deterministic frame-exit marker was not observed" >&2
    tail -80 "$LOG" | sed 's/^/  /' >&2
    exit 1
fi
if [[ ! -s "$TRACE" ]]; then
    echo "FAIL: trace was not written: $TRACE" >&2
    tail -80 "$LOG" | sed 's/^/  /' >&2
    exit 1
fi

python3 - "$TRACE" "$LOG" "$SUMMARY" "$FRAMES" "$LEVEL" "$EXPECTED_SETUP" <<'PY'
import json
import re
import sys
from pathlib import Path

trace_path = Path(sys.argv[1])
log_path = Path(sys.argv[2])
summary_path = Path(sys.argv[3])
target_frames = int(sys.argv[4])
level = int(sys.argv[5])
expected_setup_arg = sys.argv[6]

records = []
for line in trace_path.read_text(encoding="utf-8").splitlines():
    line = line.strip()
    if line:
        records.append(json.loads(line))

log_text = log_path.read_text(encoding="utf-8", errors="replace")
setup = {}
patterns = {
    "bound_pads": r"\[SETUP-PC\] Converted (\d+) bound pads",
    "waypoints": r"\[SETUP-PC\] Converted (\d+) waypoints",
    "waygroups": r"\[SETUP-PC\] Converted (\d+) waygroups",
    "patrol_paths": r"\[SETUP-PC\] Converted (\d+) patrol paths",
    "ai_lists": r"\[SETUP-PC\] Converted (\d+) AI lists",
    "guards": r"\[SETUP-PC\] propDefs: (\d+) guards,",
    "objects": r"\[SETUP-PC\] propDefs: \d+ guards, (\d+) objects",
}
for key, pattern in patterns.items():
    match = re.search(pattern, log_text)
    setup[key] = int(match.group(1)) if match else None

expected_setup = None
if expected_setup_arg:
    values = [int(part) for part in expected_setup_arg.split(",")]
    expected_setup = dict(zip(
        ["bound_pads", "waypoints", "waygroups", "patrol_paths", "ai_lists", "guards", "objects"],
        values,
    ))

max_dl = {
    "mtx_fail": 0,
    "vtx_fail": 0,
    "dl_fail": 0,
    "movemem_fail": 0,
    "texture_fail": 0,
    "settimg_fail": 0,
    "non_dl_skip_pc": 0,
    "non_dl_skip_n64": 0,
    "unregistered_skip": 0,
}
max_bad_cmds = 0
max_crashes = 0
last = None
for record in records:
    last = record
    dl = record.get("dl") or {}
    for key in max_dl:
        try:
            max_dl[key] = max(max_dl[key], int(dl.get(key, 0)))
        except (TypeError, ValueError):
            pass
    max_bad_cmds = max(max_bad_cmds, int(record.get("bad_cmds", 0)))
    max_crashes = max(max_crashes, int(record.get("crashes", 0)))

last_frame = int(last.get("f", 0)) if last else 0
front = last.get("front") if isinstance(last, dict) else None

summary = {
    "status": "pass",
    "level": level,
    "target_frames": target_frames,
    "records": len(records),
    "last_frame": last_frame,
    "setup": setup,
    "expected_setup": expected_setup,
    "max_dl": max_dl,
    "max_bad_cmds": max_bad_cmds,
    "max_crashes": max_crashes,
    "last_front": front,
}

failures = []
if not records:
    failures.append("no trace records")
elif last_frame < target_frames:
    failures.append(f"trace ended before requested frame: {last_frame} < {target_frames}")
if expected_setup is not None:
    for key, expected in expected_setup.items():
        actual = setup.get(key)
        if actual != expected:
            failures.append(f"setup {key} mismatch: {actual} != {expected}")
for key, value in max_dl.items():
    if value != 0:
        failures.append(f"display-list resolve counter {key} reached {value}")
if max_bad_cmds != 0:
    failures.append(f"bad_cmds reached {max_bad_cmds}")
if max_crashes != 0:
    failures.append(f"crashes reached {max_crashes}")

if failures:
    summary["status"] = "fail"
    summary["failures"] = failures

summary_path.write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")

if failures:
    print("FAIL: GFX DL provenance regression")
    for failure in failures:
        print(f"  {failure}")
    print(f"summary_json: {summary_path}")
    raise SystemExit(1)

print("PASS: GFX DL provenance regression")
print(f"  level={level} records={len(records)} last_frame={last_frame}")
print("  setup=" + json.dumps(setup, sort_keys=True))
print("  max_dl=" + json.dumps(max_dl, sort_keys=True))
print(f"  max_bad_cmds={max_bad_cmds} max_crashes={max_crashes}")
print(f"summary_json: {summary_path}")
PY

echo "artifacts: $OUT_DIR"
