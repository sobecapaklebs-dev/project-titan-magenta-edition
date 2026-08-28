#!/bin/bash
#
# dam_objective_progression_smoke.sh -- Guard Dam objective criteria progression.
#
# This is a deterministic objective-condition smoke, not a navigation route. It
# exercises the real Dam objective criteria by destroying alarm tags 0..3 and
# setting the stage flags consumed by modem/data/bungee objective logic. The
# audit requires the per-objective status vector to advance one objective at a
# time before all four objectives are complete.
#
set -euo pipefail
cd "$(dirname "$0")/.."

source tools/validation_common.sh

BUILD_DIR="$(validation_default_build_dir)"
BINARY=""
ROM="$(validation_default_rom)"
DO_BUILD=1
TIMEOUT_SECONDS=90
OUT_DIR="/tmp/mgb64_dam_objective_progression_$$"
LEVEL=33
FRAMES=240
ALARM_SCRIPT="80:0:2000,82:1:2000,84:2:2000,86:3:2000"
FLAG_SCRIPT="120:0x100,160:0x400,200:0x1000"

usage() {
    cat <<'USAGE'
Usage: tools/dam_objective_progression_smoke.sh [options]

Options:
  --out-dir DIR          output directory (default: /tmp/...)
  --level N              level id (default: 33, Dam)
  --frames N             trace through deterministic frame N (default: 240)
  --alarm-script SCRIPT  GE007_AUTO_DAMAGE_TAG_SCRIPT for alarm tags
                         default: 80:0:2000,82:1:2000,84:2:2000,86:3:2000
  --flag-script SCRIPT   GE007_AUTO_SET_STAGE_FLAGS_SCRIPT for objective flags
                         default: 120:0x100,160:0x400,200:0x1000
  --rom PATH             ROM path (default: ./baserom.u.z64)
  --binary PATH          native binary path (default: build/ge007)
  --build-dir DIR        CMake build directory (default: build)
  --no-build             reuse an existing native binary
  --timeout SECONDS      capture timeout (default: 90)

Artifacts are ROM-derived local validation data. Do not commit captured traces,
stage dumps, logs, or generated audit summaries.
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --out-dir) OUT_DIR="$2"; shift 2 ;;
        --level) LEVEL="$2"; shift 2 ;;
        --frames) FRAMES="$2"; shift 2 ;;
        --alarm-script) ALARM_SCRIPT="$2"; shift 2 ;;
        --flag-script) FLAG_SCRIPT="$2"; shift 2 ;;
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
    "timeout:$TIMEOUT_SECONDS"; do
    name="${pair%%:*}"
    value="${pair#*:}"
    if [[ ! "$value" =~ ^[1-9][0-9]*$ ]]; then
        echo "FAIL: --$name must be a positive integer: $value" >&2
        exit 2
    fi
done
if [[ -z "$ALARM_SCRIPT" || -z "$FLAG_SCRIPT" ]]; then
    echo "FAIL: --alarm-script and --flag-script must not be empty" >&2
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
STAGE_DUMP="$OUT_DIR/stage_dump.jsonl"
SUMMARY="$OUT_DIR/summary.json"
SAVE_DIR="$OUT_DIR/save"

rm -f "$TRACE" "$LOG" "$STAGE_DUMP" "$SUMMARY"
mkdir -p "$SAVE_DIR"

echo "=== Dam Objective Progression Smoke ==="
echo "  out-dir: $OUT_DIR"
echo "  binary:  $BINARY"
echo "  ROM:     $ROM"
echo "  level:   $LEVEL"
echo "  frames:  $FRAMES"
echo "  alarm-script: $ALARM_SCRIPT"
echo "  flag-script:  $FLAG_SCRIPT"

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
        GE007_STAGEFLAG_TRACE=1 \
        GE007_STAGEFLAG_TRACE_MASK=0x1F00 \
        GE007_STAGEFLAG_TRACE_BUDGET=32 \
        GE007_DUMP_STAGE_PADS="$STAGE_DUMP" \
        GE007_DUMP_STAGE_PADS_FRAME=2 \
        GE007_AUTO_DAMAGE_TAG_SCRIPT="$ALARM_SCRIPT" \
        GE007_AUTO_SET_STAGE_FLAGS_SCRIPT="$FLAG_SCRIPT" \
        GE007_AUTO_EXIT_FRAME="$FRAMES" \
        "$BINARY" \
        --savedir "$SAVE_DIR" \
        --rom "$ROM" \
        --level "$LEVEL" \
        --deterministic \
        --trace-state "$TRACE" >"$LOG" 2>&1; then
    echo "FAIL: Dam objective progression capture failed" >&2
    tail -80 "$LOG" | sed 's/^/  /' >&2
    exit 1
fi

if grep -qF "[GEASSERT]" "$LOG"; then
    echo "FAIL: GEASSERT fired during Dam objective progression capture" >&2
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
    exit 1
fi
if [[ ! -s "$STAGE_DUMP" ]]; then
    echo "FAIL: stage dump was not written: $STAGE_DUMP" >&2
    exit 1
fi

python3 - "$TRACE" "$STAGE_DUMP" "$LOG" "$SUMMARY" "$FRAMES" <<'PY'
import json
import re
import sys
from pathlib import Path

trace_path = Path(sys.argv[1])
stage_dump_path = Path(sys.argv[2])
log_path = Path(sys.argv[3])
summary_path = Path(sys.argv[4])
target_frame = int(sys.argv[5])

records = []
for line in trace_path.read_text(encoding="utf-8").splitlines():
    line = line.strip()
    if line:
        records.append(json.loads(line))

stage_records = []
for line in stage_dump_path.read_text(encoding="utf-8").splitlines():
    line = line.strip()
    if line:
        stage_records.append(json.loads(line))

objective_defs = [r for r in stage_records if r.get("kind") == "objective"]
criteria = [r for r in stage_records if r.get("kind") == "objective_criterion"]
tags = [r for r in stage_records if r.get("kind") == "tag"]

criteria_by_objective = {}
for row in criteria:
    criteria_by_objective.setdefault(row.get("objective"), []).append(
        (row.get("type_name"), row.get("ref"))
    )

expected_criteria = {
    0: [
        ("objective_destroy_object", 0),
        ("objective_destroy_object", 1),
        ("objective_destroy_object", 2),
        ("objective_destroy_object", 3),
        ("objective_end", 0),
    ],
    1: [
        ("objective_complete_condition", 0x100),
        ("objective_fail_condition", 0x2000),
        ("objective_fail_condition", 0x800),
        ("objective_end", 0),
    ],
    2: [
        ("objective_complete_condition", 0x400),
        ("objective_fail_condition", 0x200),
        ("objective_end", 0),
    ],
    3: [
        ("objective_complete_condition", 0x1000),
        ("objective_end", 0),
    ],
}

def obj_records():
    for record in records:
        obj = record.get("obj")
        if isinstance(obj, dict):
            yield record

objective_records = list(obj_records())

def first_at_or_after(frame, predicate):
    for record in objective_records:
        if int(record.get("f", -1)) >= frame and predicate(record):
            return record
    return None

def statuses(record):
    return list((record.get("obj") or {}).get("statuses") or [])

milestones = [
    ("initial", 1, [0, 0, 0, 0]),
    ("alarms_destroyed", 95, [1, 0, 0, 0]),
    ("modem_flag", 125, [1, 1, 0, 0]),
    ("data_flag", 165, [1, 1, 1, 0]),
    ("bungee_flag", 205, [1, 1, 1, 1]),
]
milestone_hits = {}
for name, frame, expected in milestones:
    milestone_hits[name] = first_at_or_after(frame, lambda r, e=expected: statuses(r) == e)

log_text = log_path.read_text(encoding="utf-8", errors="replace")
stageflag_rows = re.findall(r"^\[STAGEFLAG_TRACE\].*$", log_text, re.MULTILINE)
stageflag_after = []
for row in stageflag_rows:
    match = re.search(r"after=0x([0-9A-Fa-f]+)", row)
    if match:
        stageflag_after.append(int(match.group(1), 16))

max_dl = {
    "dl_fail": 0,
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
        max_dl[key] = max(max_dl[key], int(dl.get(key, 0)))
    max_bad_cmds = max(max_bad_cmds, int(record.get("bad_cmds", 0)))
    max_crashes = max(max_crashes, int(record.get("crashes", 0)))

last_frame = int(last.get("f", 0)) if last else 0
final_record = milestone_hits.get("bungee_flag")
final_obj = final_record.get("obj") if final_record else None

summary = {
    "status": "pass",
    "records": len(records),
    "objective_records": len(objective_records),
    "stage_objectives": objective_defs,
    "criteria_by_objective": {str(k): v for k, v in sorted(criteria_by_objective.items())},
    "alarm_tags": [r for r in tags if r.get("tag") in (0, 1, 2, 3)],
    "milestones": {
        name: {
            "frame": hit.get("f") if hit else None,
            "statuses": statuses(hit) if hit else None,
            "flags": (hit.get("obj") or {}).get("flags") if hit else None,
        }
        for name, hit in milestone_hits.items()
    },
    "stageflag_trace_rows": stageflag_rows,
    "last_stageflags": f"0x{stageflag_after[-1]:08X}" if stageflag_after else None,
    "last_frame": last_frame,
    "final_obj": final_obj,
    "max_dl": max_dl,
    "max_bad_cmds": max_bad_cmds,
    "max_crashes": max_crashes,
}

failures = []
if last_frame < target_frame:
    failures.append(f"trace ended before requested frame: {last_frame} < {target_frame}")
if len(objective_defs) != 4:
    failures.append(f"expected 4 Dam objectives, found {len(objective_defs)}")
for objective, expected in expected_criteria.items():
    actual = criteria_by_objective.get(objective, [])
    if actual != expected:
        failures.append(f"objective {objective} criteria mismatch: {actual!r} != {expected!r}")
if len([r for r in tags if r.get("tag") in (0, 1, 2, 3)]) != 4:
    failures.append("alarm tags 0..3 were not all present in setup dump")
for name, hit in milestone_hits.items():
    if hit is None:
        expected = next(item[2] for item in milestones if item[0] == name)
        failures.append(f"missing objective status milestone {name}: {expected}")
if final_obj is None or final_obj.get("statuses") != [1, 1, 1, 1]:
    failures.append("final objective statuses did not reach [1,1,1,1]")
if final_obj is None or int(final_obj.get("flags", "0"), 16) & 0x1500 != 0x1500:
    failures.append("final objective flags do not include 0x1500")
if final_obj is not None and int(final_obj.get("flags", "0"), 16) & 0x2A00:
    failures.append("failure objective flags were set")
for required_after in (0x100, 0x500, 0x1500):
    if required_after not in stageflag_after:
        failures.append(f"missing stage flag trace after=0x{required_after:08X}")
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
    print("FAIL: Dam objective progression smoke")
    for failure in failures:
        print(f"  {failure}")
    print(f"summary_json: {summary_path}")
    raise SystemExit(1)

print("PASS: Dam objective progression smoke")
print(f"  records={len(records)} objective_records={len(objective_records)}")
for name, hit in milestone_hits.items():
    print(f"  {name}: frame={hit.get('f')} statuses={statuses(hit)} flags={(hit.get('obj') or {}).get('flags')}")
print(f"  max_dl={json.dumps(max_dl, sort_keys=True)} max_bad_cmds={max_bad_cmds} max_crashes={max_crashes}")
print(f"summary_json: {summary_path}")
PY

echo "artifacts: $OUT_DIR"
