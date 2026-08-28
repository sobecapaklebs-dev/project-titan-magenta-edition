#!/bin/bash
#
# surface2_final_flow_smoke.sh -- Guard Surface II hatch floor and final-exit flow.
#
# This is scripted contract coverage, not an organic Surface II navigation route.
# It proves that a closed hatch door can contribute the player's floor, that the
# old drop-through behavior is still visible with the A/B gate disabled, and
# that the objective-complete final-pad path reaches the title/menu flow.
#
set -euo pipefail
cd "$(dirname "$0")/.."

source tools/validation_common.sh

BUILD_DIR="$(validation_default_build_dir)"
BINARY=""
ROM="$(validation_default_rom)"
DO_BUILD=1
TIMEOUT_SECONDS=90
OUT_DIR="/tmp/mgb64_surface2_final_flow_$$"
LEVEL=43
HATCH_FRAMES=130
EXIT_FRAMES=520
HATCH_FORCE_SCRIPT="80-90:0.00:8.88:-2221.00:0:0:167.31:9"
EXIT_FORCE_SCRIPT="80-90:0.0:277.33:-105.62:180:0:167.31:289"
EXIT_DAMAGE_SCRIPT="60:0:2000,61:1:2000"
EXIT_A_SCRIPT="160:162"

usage() {
    cat <<'USAGE'
Usage: tools/surface2_final_flow_smoke.sh [options]

Options:
  --out-dir DIR          output directory (default: /tmp/...)
  --level N              level id (default: 43, Surface II)
  --hatch-frames N       closed-hatch capture exit frame (default: 130)
  --exit-frames N        final-exit capture max frame (default: 520)
  --rom PATH             ROM path (default: ./baserom.u.z64)
  --binary PATH          native binary path (default: build/ge007)
  --build-dir DIR        CMake build directory (default: build)
  --no-build             reuse an existing native binary
  --timeout SECONDS      per-capture timeout (default: 90)

Artifacts are ROM-derived local validation data. Do not commit captured traces,
logs, or generated audit summaries.
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --out-dir) OUT_DIR="$2"; shift 2 ;;
        --level) LEVEL="$2"; shift 2 ;;
        --hatch-frames) HATCH_FRAMES="$2"; shift 2 ;;
        --exit-frames) EXIT_FRAMES="$2"; shift 2 ;;
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
    "hatch-frames:$HATCH_FRAMES" \
    "exit-frames:$EXIT_FRAMES" \
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

SUMMARY="$OUT_DIR/summary.json"
rm -f "$SUMMARY"

echo "=== Surface II Final Flow Smoke ==="
echo "  out-dir:      $OUT_DIR"
echo "  binary:       $BINARY"
echo "  ROM:          $ROM"
echo "  level:        $LEVEL"
echo "  hatch-frames: $HATCH_FRAMES"
echo "  exit-frames:  $EXIT_FRAMES"

run_hatch_case() {
    local name="$1"
    shift

    local case_dir="$OUT_DIR/hatch_${name}"
    local trace="$case_dir/state.jsonl"
    local log="$case_dir/run.log"
    local save_dir="$case_dir/save"
    local extra_env=("$@")

    mkdir -p "$case_dir" "$save_dir"
    rm -f "$trace" "$log"

    echo "  capture hatch ${name}"
    if ! validation_run_with_timeout "$TIMEOUT_SECONDS" \
        env -u GE007_DEBUG \
            SDL_AUDIODRIVER="${GE007_VALIDATION_SDL_AUDIODRIVER:-dummy}" \
            GE007_MUTE=1 \
            GE007_DETERMINISTIC_STABLE_COUNT=1 \
            GE007_NO_VSYNC=1 \
            GE007_BACKGROUND=1 \
            GE007_NO_INPUT_GRAB=1 \
            GE007_DISABLE_LEVEL_INTRO=1 \
            GE007_AUTO_FORCE_PLAYER_SCRIPT="$HATCH_FORCE_SCRIPT" \
            GE007_AUTO_EXIT_FRAME="$HATCH_FRAMES" \
            ${extra_env[@]+"${extra_env[@]}"} \
            "$BINARY" \
            --savedir "$save_dir" \
            --rom "$ROM" \
            --level "$LEVEL" \
            --deterministic \
            --trace-state "$trace" \
            --config-override Video.WindowWidth=640 \
            --config-override Video.WindowHeight=480 \
            --config-override Video.WindowMode=windowed >"$log" 2>&1; then
        echo "FAIL: Surface II hatch ${name} capture failed" >&2
        tail -80 "$log" | sed 's/^/  /' >&2
        exit 1
    fi

    if grep -qF "[GEASSERT]" "$log"; then
        echo "FAIL: GEASSERT fired during Surface II hatch ${name} capture" >&2
        grep -F "[GEASSERT]" "$log" | head -5 | sed 's/^/  /' >&2
        exit 1
    fi
    if ! grep -qF "deterministic frame exit observed" "$log"; then
        echo "FAIL: deterministic frame-exit marker missing for Surface II hatch ${name}" >&2
        tail -80 "$log" | sed 's/^/  /' >&2
        exit 1
    fi
    if [[ ! -s "$trace" ]]; then
        echo "FAIL: trace was not written for Surface II hatch ${name}: $trace" >&2
        exit 1
    fi
}

run_exit_case() {
    local case_dir="$OUT_DIR/final_exit"
    local trace="$case_dir/state.jsonl"
    local log="$case_dir/run.log"
    local save_dir="$case_dir/save"

    mkdir -p "$case_dir" "$save_dir"
    rm -f "$trace" "$log"

    echo "  capture final exit"
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
            GE007_DISABLE_LEVEL_INTRO=1 \
            GE007_AUTO_DAMAGE_TAG_SCRIPT="$EXIT_DAMAGE_SCRIPT" \
            GE007_AUTO_FORCE_PLAYER_SCRIPT="$EXIT_FORCE_SCRIPT" \
            GE007_AUTO_A="$EXIT_A_SCRIPT" \
            GE007_AUTO_EXIT_ON_TITLE=1 \
            GE007_AUTO_EXIT_ON_TITLE_DELAY=8 \
            GE007_AUTO_EXIT_FRAME="$EXIT_FRAMES" \
            "$BINARY" \
            --savedir "$save_dir" \
            --rom "$ROM" \
            --level "$LEVEL" \
            --deterministic \
            --trace-state "$trace" \
            --config-override Video.WindowWidth=640 \
            --config-override Video.WindowHeight=480 \
            --config-override Video.WindowMode=windowed >"$log" 2>&1; then
        echo "FAIL: Surface II final-exit capture failed" >&2
        tail -80 "$log" | sed 's/^/  /' >&2
        exit 1
    fi

    if grep -qF "[GEASSERT]" "$log"; then
        echo "FAIL: GEASSERT fired during Surface II final-exit capture" >&2
        grep -F "[GEASSERT]" "$log" | head -5 | sed 's/^/  /' >&2
        exit 1
    fi
    if ! grep -qF "deterministic title return observed" "$log"; then
        echo "FAIL: auto title-exit marker missing for Surface II final-exit capture" >&2
        tail -80 "$log" | sed 's/^/  /' >&2
        exit 1
    fi
    if [[ ! -s "$trace" ]]; then
        echo "FAIL: final-exit trace was not written: $trace" >&2
        exit 1
    fi
}

run_hatch_case default
run_hatch_case disabled GE007_DOOR_FLOOR_COLLISION=0
run_exit_case

python3 - "$OUT_DIR" "$SUMMARY" "$LEVEL" "$HATCH_FRAMES" <<'PY'
import json
import sys
from pathlib import Path

out_dir = Path(sys.argv[1])
summary_path = Path(sys.argv[2])
level = int(sys.argv[3])
hatch_frames = int(sys.argv[4])

def load_jsonl(path):
    records = []
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if line:
            records.append(json.loads(line))
    return records

default_records = load_jsonl(out_dir / "hatch_default" / "state.jsonl")
disabled_records = load_jsonl(out_dir / "hatch_disabled" / "state.jsonl")
exit_records = load_jsonl(out_dir / "final_exit" / "state.jsonl")

def hatch_stats(records):
    window_start = max(95, hatch_frames - 35)
    window = [
        r for r in records
        if window_start <= int(r.get("f", -1)) <= hatch_frames
        and "floor" in r
    ]
    floors = [float(r["floor"]) for r in window]
    active_stages = sorted({
        int((r.get("front") or {}).get("active_stage", -999))
        for r in window
        if isinstance(r.get("front"), dict)
    })
    return {
        "window_start": window_start,
        "records": len(records),
        "window_records": len(window),
        "min_floor": min(floors) if floors else None,
        "max_floor": max(floors) if floors else None,
        "last_floor": float(window[-1]["floor"]) if window else None,
        "active_stages": active_stages,
    }

default_hatch = hatch_stats(default_records)
disabled_hatch = hatch_stats(disabled_records)

objective_records = [r for r in exit_records if isinstance(r.get("obj"), dict)]
front_records = [r for r in exit_records if isinstance(r.get("front"), dict)]
obj_complete_records = [
    r for r in objective_records
    if int(r["obj"].get("all_complete", 0)) == 1
]
title_records = [
    r for r in front_records
    if int(r["front"].get("loaded_stage", -1)) == 90
    and int(r["front"].get("active_stage", -1)) == 90
]
bad_front_records = [
    r for r in front_records
    if int(r["front"].get("mission_failed", 0)) != 0
    or int(r["front"].get("bond_kia", 0)) != 0
]

max_dl = {
    "dl_fail": 0,
    "unregistered_skip": 0,
    "non_dl_skip_pc": 0,
    "non_dl_skip_n64": 0,
}
for record in exit_records:
    dl = record.get("dl") or {}
    for key in max_dl:
        try:
            max_dl[key] = max(max_dl[key], int(dl.get(key, 0)))
        except (TypeError, ValueError):
            pass

summary = {
    "status": "pass",
    "default_hatch": default_hatch,
    "disabled_hatch": disabled_hatch,
    "exit_records": len(exit_records),
    "objective_records": len(objective_records),
    "first_obj_complete_frame": obj_complete_records[0]["f"] if obj_complete_records else None,
    "first_title_frame": title_records[0]["f"] if title_records else None,
    "final_statuses": objective_records[-1]["obj"].get("statuses") if objective_records else None,
    "final_difficulties": objective_records[-1]["obj"].get("difficulties") if objective_records else None,
    "max_dl": max_dl,
}

failures = []
if default_hatch["window_records"] < 20:
    failures.append(f"too few default hatch window records: {default_hatch['window_records']}")
if disabled_hatch["window_records"] < 20:
    failures.append(f"too few disabled hatch window records: {disabled_hatch['window_records']}")
if default_hatch["min_floor"] is None or default_hatch["min_floor"] < -170.0:
    failures.append(f"default closed hatch did not hold floor above -170: {default_hatch['min_floor']}")
if default_hatch["max_floor"] is None or default_hatch["max_floor"] > -140.0:
    failures.append(f"default closed hatch floor was unexpectedly high: {default_hatch['max_floor']}")
if default_hatch["active_stages"] and default_hatch["active_stages"] != [level]:
    failures.append(f"default hatch left level {level}: active_stages={default_hatch['active_stages']}")
if disabled_hatch["min_floor"] is None or disabled_hatch["min_floor"] > -250.0:
    failures.append(f"disabled door-floor control did not reproduce the old drop: {disabled_hatch['min_floor']}")
if not obj_complete_records:
    failures.append("Surface II objective trace never reached obj.all_complete=1")
if not title_records:
    failures.append("Surface II final-pad path never reached title stage 90")
if obj_complete_records and title_records and obj_complete_records[0]["f"] >= title_records[0]["f"]:
    failures.append(
        f"objective completion did not precede title transition: "
        f"obj={obj_complete_records[0]['f']} title={title_records[0]['f']}"
    )
if bad_front_records:
    failures.append(
        f"mission_failed/bond_kia was set in final flow at frame {bad_front_records[0]['f']}"
    )
if any(max_dl.values()):
    failures.append(f"DL resolve counters changed during final flow: {max_dl}")

if failures:
    summary["status"] = "fail"
    summary["failures"] = failures
    summary_path.write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print("FAIL: Surface II final-flow smoke failed")
    for failure in failures:
        print(f"  - {failure}")
    raise SystemExit(1)

summary_path.write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8")
print("PASS: Surface II final-flow smoke")
print(
    f"  default hatch floor {default_hatch['min_floor']:.2f}..{default_hatch['max_floor']:.2f}; "
    f"disabled min {disabled_hatch['min_floor']:.2f}"
)
print(
    f"  obj_complete_frame={summary['first_obj_complete_frame']} "
    f"title_frame={summary['first_title_frame']} "
    f"statuses={summary['final_statuses']}"
)
PY

echo "  summary: $SUMMARY"
