#!/bin/bash
#
# dam_mission_flow_smoke.sh -- Guard deterministic Dam mission-report return.
#
# This is a scripted end-state smoke, not an organic objective-completion route.
# It direct-boots Dam, triggers the deterministic mission-success hook, and
# verifies the frontend reaches the mission-report path without failure/KIA flags.
# It then restarts without the mission-success hook and verifies the completed
# Dam/Agent save state is visible from the same savedir.
#
set -euo pipefail
cd "$(dirname "$0")/.."

source tools/validation_common.sh

BUILD_DIR="$(validation_default_build_dir)"
BINARY=""
ROM="$(validation_default_rom)"
DO_BUILD=1
TIMEOUT_SECONDS=90
OUT_DIR="/tmp/mgb64_dam_mission_flow_$$"
LEVEL=33
MISSION_END_FRAME=120
EXIT_DELAY=60
RELOAD_FRAME=240

usage() {
    cat <<'USAGE'
Usage: tools/dam_mission_flow_smoke.sh [options]

Options:
  --out-dir DIR             output directory (default: /tmp/...)
  --level N                 level id (default: 33, Dam)
  --mission-end-frame N     frame to trigger scripted success (default: 120)
  --exit-delay N            frames to wait on title before exiting (default: 60)
  --rom PATH                ROM path (default: ./baserom.u.z64)
  --binary PATH             native binary path (default: build/ge007)
  --build-dir DIR           CMake build directory (default: build)
  --no-build                reuse an existing native binary
  --timeout SECONDS         capture timeout (default: 90)

Artifacts are ROM-derived local validation data. Do not commit captured traces,
screenshots, logs, or generated audit summaries.
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --out-dir) OUT_DIR="$2"; shift 2 ;;
        --level) LEVEL="$2"; shift 2 ;;
        --mission-end-frame) MISSION_END_FRAME="$2"; shift 2 ;;
        --exit-delay) EXIT_DELAY="$2"; shift 2 ;;
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
    "mission-end-frame:$MISSION_END_FRAME" \
    "exit-delay:$EXIT_DELAY" \
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
SAVE_DIR="$OUT_DIR/save"
RELOAD_TRACE="$OUT_DIR/reload_trace.jsonl"
RELOAD_LOG="$OUT_DIR/reload.log"

rm -f "$TRACE" "$LOG" "$SUMMARY" "$RELOAD_TRACE" "$RELOAD_LOG"
mkdir -p "$SAVE_DIR"

echo "=== Dam Mission Flow Smoke ==="
echo "  out-dir: $OUT_DIR"
echo "  binary:  $BINARY"
echo "  ROM:     $ROM"
echo "  level:   $LEVEL"
echo "  mission-end-frame: $MISSION_END_FRAME"
echo "  exit-delay: $EXIT_DELAY"
echo "  reload-frame: $RELOAD_FRAME"

if ! validation_run_with_timeout "$TIMEOUT_SECONDS" \
    env -u GE007_DEBUG \
        SDL_AUDIODRIVER="${GE007_VALIDATION_SDL_AUDIODRIVER:-dummy}" \
        GE007_MUTE=1 \
        GE007_DETERMINISTIC_STABLE_COUNT=1 \
        GE007_NO_VSYNC=1 \
        GE007_BACKGROUND=1 \
        GE007_NO_INPUT_GRAB=1 \
        GE007_TRACE_FLOW_ONLY=1 \
        GE007_TRACE_OBJECTIVES=1 \
        GE007_AUTO_MISSION_END_FRAME="$MISSION_END_FRAME" \
        GE007_AUTO_MISSION_END_RESULT=success \
        GE007_AUTO_EXIT_ON_TITLE=1 \
        GE007_AUTO_EXIT_ON_TITLE_DELAY="$EXIT_DELAY" \
        "$BINARY" \
        --savedir "$SAVE_DIR" \
        --rom "$ROM" \
        --level "$LEVEL" \
        --deterministic \
        --trace-state "$TRACE" >"$LOG" 2>&1; then
    echo "FAIL: mission-flow capture failed" >&2
    tail -60 "$LOG" | sed 's/^/  /' >&2
    exit 1
fi

if grep -qF "[GEASSERT]" "$LOG"; then
    echo "FAIL: GEASSERT fired during mission-flow smoke" >&2
    grep -F "[GEASSERT]" "$LOG" | head -5 | sed 's/^/  /' >&2
    exit 1
fi
if ! grep -qF "deterministic title return observed" "$LOG"; then
    echo "FAIL: auto title-exit marker was not observed" >&2
    tail -60 "$LOG" | sed 's/^/  /' >&2
    exit 1
fi
if [[ ! -s "$TRACE" ]]; then
    echo "FAIL: trace was not written: $TRACE" >&2
    tail -60 "$LOG" | sed 's/^/  /' >&2
    exit 1
fi

python3 - "$TRACE" "$SUMMARY" "$MISSION_END_FRAME" <<'PY'
import json
import sys
from pathlib import Path

trace_path = Path(sys.argv[1])
summary_path = Path(sys.argv[2])
mission_end_frame = int(sys.argv[3])

records = []
for line in trace_path.read_text(encoding="utf-8").splitlines():
    line = line.strip()
    if line:
        records.append(json.loads(line))

objective_records = [r for r in records if isinstance(r.get("obj"), dict)]
front_records = [r for r in records if isinstance(r.get("front"), dict)]
report_records = [r for r in front_records if int(r["front"].get("menu", -999)) == 12]
success_report_records = [
    r for r in report_records
    if int(r["front"].get("all_obj_complete", 0)) == 1
    and int(r["front"].get("mission_failed", 1)) == 0
    and int(r["front"].get("bond_kia", 1)) == 0
]
title_records = [
    r for r in front_records
    if int(r["front"].get("loaded_stage", -1)) == 90
    and int(r["front"].get("active_stage", -1)) == 90
]

initial_objectives = [
    r for r in objective_records
    if int(r.get("f", 0)) < mission_end_frame
    and int(r["obj"].get("count", 0)) == 4
]
initial_incomplete = [
    r for r in initial_objectives
    if r["obj"].get("statuses") == [0, 0, 0, 0]
]

transitions = []
previous = None
for record in front_records:
    front = record["front"]
    key = (
        int(front.get("menu", -999)),
        int(front.get("menu_pending", -999)),
        int(front.get("loaded_stage", -999)),
        int(front.get("active_stage", -999)),
        int(front.get("all_obj_complete", 0)),
        int(front.get("mission_failed", 0)),
        int(front.get("bond_kia", 0)),
    )
    if key != previous:
        transitions.append({"frame": record.get("f"), "front": front})
        previous = key

max_dl = {
    "dl_fail": 0,
    "unregistered_skip": 0,
    "non_dl_skip_pc": 0,
    "non_dl_skip_n64": 0,
}
for record in records:
    dl = record.get("dl") or {}
    for key in max_dl:
        try:
            max_dl[key] = max(max_dl[key], int(dl.get(key, 0)))
        except (TypeError, ValueError):
            pass

def completion_frames(frames, folder, level_id, difficulty_id):
    hits = []
    for frame in frames:
        save = frame.get("save") or {}
        valid = save.get("valid") or []
        level = save.get("level") or []
        difficulty = save.get("difficulty") or []
        if len(valid) > folder and len(level) > folder and len(difficulty) > folder:
            try:
                if (
                    int(valid[folder]) == 1
                    and int(level[folder]) == level_id
                    and int(difficulty[folder]) == difficulty_id
                ):
                    hits.append(frame.get("f"))
            except (TypeError, ValueError):
                pass
    return hits

mission_save_hits = completion_frames(records, 0, 0, 0)

summary = {
    "status": "pass",
    "records": len(records),
    "objective_records": len(objective_records),
    "front_records": len(front_records),
    "initial_objective_records": len(initial_objectives),
    "initial_incomplete_records": len(initial_incomplete),
    "first_report_frame": report_records[0]["f"] if report_records else None,
    "first_success_report_frame": success_report_records[0]["f"] if success_report_records else None,
    "first_title_frame": title_records[0]["f"] if title_records else None,
    "mission_save_completion_frames": mission_save_hits,
    "first_mission_save_completion_frame": mission_save_hits[0] if mission_save_hits else None,
    "max_dl": max_dl,
    "transitions": transitions,
}

failures = []
if len(records) < mission_end_frame:
    failures.append(f"too few trace records: {len(records)}")
if len(objective_records) < 30:
    failures.append(f"too few objective records: {len(objective_records)}")
if not initial_objectives:
    failures.append("Dam objective list was not traced before mission end")
elif not initial_incomplete:
    failures.append("initial Dam objective statuses were not observed as incomplete")
if not report_records:
    failures.append("mission report menu 12 was never reached")
if not success_report_records:
    failures.append("mission report was not reached with all_obj_complete=1, mission_failed=0, bond_kia=0")
if not title_records:
    failures.append("title stage was not observed after mission end")
if not mission_save_hits:
    failures.append("mission-success trace never reported folder 0 Dam/Agent completion in save state")
for key, value in max_dl.items():
    if value != 0:
        failures.append(f"display-list resolve counter {key} reached {value}")

if failures:
    summary["status"] = "fail"
    summary["failures"] = failures

summary_path.write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")

if failures:
    print("FAIL: Dam mission-flow smoke")
    for failure in failures:
        print(f"  {failure}")
    print(f"summary_json: {summary_path}")
    raise SystemExit(1)

print("PASS: Dam mission-flow smoke")
print(f"  records={len(records)} objective_records={len(objective_records)}")
print(f"  first_report_frame={summary['first_report_frame']}")
print(f"  first_success_report_frame={summary['first_success_report_frame']}")
print(f"  first_title_frame={summary['first_title_frame']}")
print(f"  first_mission_save_completion_frame={summary['first_mission_save_completion_frame']}")
print(f"  max_dl={json.dumps(max_dl, sort_keys=True)}")
print(f"summary_json: {summary_path}")
PY

if [[ ! -s "$SAVE_DIR/ge007_eeprom.bin" ]]; then
    echo "FAIL: mission-flow run did not create ge007_eeprom.bin" >&2
    exit 1
fi

if ! GE007_AUTO_START='20:3,80:3,140:3,200:3' \
     validation_run_with_timeout "$TIMEOUT_SECONDS" \
    env -u GE007_DEBUG \
        SDL_AUDIODRIVER="${GE007_VALIDATION_SDL_AUDIODRIVER:-dummy}" \
        GE007_MUTE=1 \
        GE007_DETERMINISTIC_STABLE_COUNT=1 \
        GE007_NO_VSYNC=1 \
        GE007_BACKGROUND=1 \
        GE007_NO_INPUT_GRAB=1 \
        GE007_TRACE_FLOW_ONLY=1 \
        "$BINARY" \
        --savedir "$SAVE_DIR" \
        --rom "$ROM" \
        --deterministic \
        --trace-state "$RELOAD_TRACE" \
        --screenshot-frame "$RELOAD_FRAME" \
        --screenshot-label dam_mission_reload \
        --screenshot-exit >"$RELOAD_LOG" 2>&1; then
    echo "FAIL: mission-flow save reload capture failed" >&2
    tail -60 "$RELOAD_LOG" | sed 's/^/  /' >&2
    exit 1
fi

if grep -qF "[GEASSERT]" "$RELOAD_LOG"; then
    echo "FAIL: GEASSERT fired during mission-flow save reload" >&2
    grep -F "[GEASSERT]" "$RELOAD_LOG" | head -5 | sed 's/^/  /' >&2
    exit 1
fi
if [[ ! -s "$RELOAD_TRACE" ]]; then
    echo "FAIL: reload trace was not written: $RELOAD_TRACE" >&2
    tail -60 "$RELOAD_LOG" | sed 's/^/  /' >&2
    exit 1
fi

python3 - "$RELOAD_TRACE" "$SUMMARY" <<'PY'
import json
import sys
from pathlib import Path

reload_trace_path = Path(sys.argv[1])
summary_path = Path(sys.argv[2])

records = []
for line in reload_trace_path.read_text(encoding="utf-8").splitlines():
    line = line.strip()
    if line:
        records.append(json.loads(line))

def completion_frames(frames, folder, level_id, difficulty_id):
    hits = []
    for frame in frames:
        save = frame.get("save") or {}
        valid = save.get("valid") or []
        level = save.get("level") or []
        difficulty = save.get("difficulty") or []
        if len(valid) > folder and len(level) > folder and len(difficulty) > folder:
            try:
                if (
                    int(valid[folder]) == 1
                    and int(level[folder]) == level_id
                    and int(difficulty[folder]) == difficulty_id
                ):
                    hits.append(frame.get("f"))
            except (TypeError, ValueError):
                pass
    return hits

front_records = [r for r in records if isinstance(r.get("front"), dict)]
title_records = [
    r for r in front_records
    if int(r["front"].get("loaded_stage", -1)) == 90
    and int(r["front"].get("active_stage", -1)) == 90
]
reload_save_hits = completion_frames(records, 0, 0, 0)

summary = json.loads(summary_path.read_text(encoding="utf-8"))
summary["reload_records"] = len(records)
summary["reload_front_records"] = len(front_records)
summary["first_reload_title_frame"] = title_records[0]["f"] if title_records else None
summary["reload_save_completion_frames"] = reload_save_hits
summary["first_reload_save_completion_frame"] = reload_save_hits[0] if reload_save_hits else None

failures = []
if not records:
    failures.append("reload trace had no records")
if not title_records:
    failures.append("reload trace never observed title stage")
if not reload_save_hits:
    failures.append("reload trace did not report folder 0 Dam/Agent completion from persisted save")

if failures:
    summary["status"] = "fail"
    summary.setdefault("failures", []).extend(failures)

summary_path.write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")

if failures:
    print("FAIL: Dam mission-flow reload persistence")
    for failure in failures:
        print(f"  {failure}")
    print(f"summary_json: {summary_path}")
    raise SystemExit(1)

print("PASS: Dam mission-flow reload persistence")
print(f"  reload_records={len(records)} reload_front_records={len(front_records)}")
print(f"  first_reload_title_frame={summary['first_reload_title_frame']}")
print(f"  first_reload_save_completion_frame={summary['first_reload_save_completion_frame']}")
print(f"summary_json: {summary_path}")
PY

echo "artifacts: $OUT_DIR"
