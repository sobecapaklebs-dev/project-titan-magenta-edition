#!/bin/bash
#
# render_camera_clearance_regression.sh -- Validate native render-eye clearance.
#
# This lane exercises close-contact wall/glass/interior cases with the render
# camera clearance enabled and disabled, including aim/crouch/lean/look and
# longer contact variants. The enabled run must actually apply the temporary
# render-eye push; the disabled run must not. Gameplay state must stay identical
# between the two runs, proving the helper does not mutate Bond's collision or
# player position.
#
set -euo pipefail
cd "$(dirname "$0")/.."

source tools/validation_common.sh

BUILD_DIR="$(validation_default_build_dir)"
BINARY=""
ROM="$(validation_default_rom)"
DO_BUILD=1
TIMEOUT_SECONDS=90
OUT_DIR="/tmp/mgb64_render_camera_clearance_$$"
FRAMES=180

usage() {
    cat <<'USAGE'
Usage: tools/render_camera_clearance_regression.sh [options]

Options:
  --out-dir DIR        output directory (default: /tmp/...)
  --frames N           screenshot/exit frame (default: 180)
  --rom PATH           ROM path (default: ./baserom.u.z64)
  --binary PATH        native binary path (default: build/ge007)
  --build-dir DIR      CMake build directory (default: build)
  --no-build           reuse an existing native binary
  --timeout SECONDS    per-capture timeout (default: 90)

Artifacts are ROM-derived local validation data. Do not commit captured traces,
screenshots, logs, or generated audit summaries.
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --out-dir) OUT_DIR="$2"; shift 2 ;;
        --frames) FRAMES="$2"; shift 2 ;;
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

# label|level|pad|min_enabled_clearance_lines|input_envs|required_hit_prop_types|required_moving_door_hits|required_door_state_hits|required_hit_obj_types|required_glass_pads|required_tile_rooms|case_frames|required_hit_obj_ids|required_vehicle_moves
# required_hit_prop_types entries are comma-separated PROP_TYPE values. Use N:M
# to require at least M clearance trace lines for type N.
# required_hit_obj_types uses the same N:M format with PROPDEF_TYPE values.
# required_glass_pads uses GLASS_TRACE pad IDs, and required_tile_rooms uses
# CAM-CLEARANCE tile_room values, both with the same optional N:M count format.
# required_hit_obj_ids uses CAM-CLEARANCE hit_obj values, and
# required_vehicle_moves uses VEHICLE_STATE move_accepted obj ids.
# input_envs are comma-separated by default. Use semicolon separators for cases
# where an env value itself needs a comma.
CASES=(
    "dam_exterior_wall|33|75|80|GE007_AUTO_FORWARD=70:80|1:80"
    "dam_exterior_wall_long_contact|33|75|120|GE007_AUTO_FORWARD=70:140|1:120"
    "dam_exterior_wall_soak|33|75|900|GE007_AUTO_FORWARD=70:500,GE007_TRACE_CAMERA_CLEARANCE_BUDGET=1200|1:900|||3:900||69:900|600"
    "dam_glass_area|33|100|90|GE007_AUTO_FORWARD=70:80|1:90"
    "dam_glass_area_aim|33|100|90|GE007_AUTO_FORWARD=70:80,GE007_AUTO_AIM=75:80|1:90"
    "dam_glass_area_crouch|33|100|60|GE007_AUTO_FORWARD=70:80,GE007_AUTO_CROUCH=60:4|1:60"
    "dam_glass_area_aim_lean_left|33|100|60|GE007_AUTO_FORWARD=70:80,GE007_AUTO_AIM=75:80,GE007_AUTO_CLEFT=85:50|1:60"
    "dam_glass_area_aim_lean_right|33|100|60|GE007_AUTO_FORWARD=70:80,GE007_AUTO_AIM=75:80,GE007_AUTO_CRIGHT=85:50|1:60"
    "dam_control_room|33|140|30|GE007_AUTO_FORWARD=70:80|"
    "dam_control_room_look_sweep|33|140|10|GE007_AUTO_FORWARD=70:80,GE007_AUTO_LOOK_LEFT=75:25,GE007_AUTO_LOOK_RIGHT=100:25,GE007_AUTO_LOOK_STEP=8|"
    "dam_control_room_weapon_switch|33|140|10|GE007_AUTO_FORWARD=70:80,GE007_AUTO_WEAPON_NEXT=80:4|"
    "dam_moving_truck_contact|33|317|80|GE007_AUTO_WARP_PAD_FORWARD_OFFSET=-350,GE007_AUTO_FACE_COORD_FRAME=45,GE007_AUTO_FACE_COORD_X=18137.72,GE007_AUTO_FACE_COORD_Y=22.00,GE007_AUTO_FACE_COORD_Z=16457.68,GE007_TRACE_VEHICLE_STATE=1,GE007_TRACE_VEHICLE_STATE_BUDGET=120,GE007_TRACE_VEHICLE_STATE_INTERVAL=10|1:80|||39:80||135:80|120|279:80|279:20"
    "surface_spawn_wall|36|1|10|GE007_AUTO_FORWARD=70:80|"
    "runway_tank_contact|35|44|80|GE007_AUTO_WARP_PAD_FORWARD_OFFSET=-400,GE007_AUTO_FACE_COORD_FRAME=45,GE007_AUTO_FACE_COORD_X=14189.79,GE007_AUTO_FACE_COORD_Y=-550,GE007_AUTO_FACE_COORD_Z=-4465.71|1:80|||45:80"
    "facility_spawn_door_contact|34|1|80|GE007_AUTO_LEFT=70:80|2:10"
    "facility_opening_door_contact|34|1|20|GE007_AUTO_WARP_SCRIPT=40:10077:60:-80:0 70:10077:0:80:0,GE007_AUTO_FACE_COORD_SCRIPT=45:616.67:-260.00:411.94 72:616.67:-260.00:411.94,GE007_AUTO_B=55:4|2:10|10|1:10"
    "facility_closing_door_contact|34|1|20|GE007_AUTO_WARP_SCRIPT=40:10077:60:-80:0 72:10077:0:80:0;GE007_AUTO_FACE_COORD_SCRIPT=45:616.67:-260.00:411.94 74:616.67:-260.00:411.94;GE007_AUTO_B=55:4,68:4|2:10|10|2:10"
    "facility_tinted_glass_contact|34|29|80|GE007_AUTO_WARP_PAD_FORWARD_OFFSET=-220,GE007_AUTO_FACE_COORD_FRAME=45,GE007_AUTO_FACE_COORD_X=914.23,GE007_AUTO_FACE_COORD_Y=-258.60,GE007_AUTO_FACE_COORD_Z=592.63,GE007_AUTO_FORWARD=70:100,GE007_TRACE_GLASS=1|||||10098:80|19:80"
)

run_capture() {
    local label="$1"
    local mode="$2"
    local level="$3"
    local pad="$4"
    local input_spec="$5"
    local frame_count="$6"
    shift 6
    local case_dir="$OUT_DIR/$label/$mode"
    local trace="$case_dir/state.jsonl"
    local log="$case_dir/run.log"
    local shot="$case_dir/screenshot_${label}_${mode}.bmp"
    local input_env=()
    local item

    if [[ "$input_spec" == *";"* ]]; then
        IFS=';' read -ra input_env <<< "$input_spec"
    else
        IFS=',' read -ra input_env <<< "$input_spec"
    fi
    for item in "${input_env[@]}"; do
        if [[ ! "$item" =~ ^[A-Za-z_][A-Za-z0-9_]*= ]]; then
            echo "FAIL: invalid input env spec: $item" >&2
            exit 2
        fi
    done

    mkdir -p "$case_dir"
    rm -f "$trace" "$log" "$shot" "$case_dir/render.json" "$case_dir/render.txt" \
        "$case_dir/screenshot.json" "$case_dir/screenshot.txt"

    echo "  capture: $label $mode (${frame_count}f)"
    if ! (
        cd "$case_dir"
        validation_run_with_timeout "$TIMEOUT_SECONDS" \
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
            GE007_AUTO_WARP_FRAME=40 \
            GE007_AUTO_WARP_PAD="$pad" \
            GE007_RENDER_CAMERA_EXTRA_CLEARANCE=6 \
            GE007_TRACE_CAMERA_CLEARANCE=1 \
            "${input_env[@]}" \
            "$@" \
            "$BINARY" \
            --savedir "$case_dir" \
            --rom "$ROM" \
            --level "$level" \
            --deterministic \
            --trace-state "$trace" \
            --screenshot-frame "$frame_count" \
            --screenshot-label "${label}_${mode}" \
            --screenshot-exit
    ) >"$log" 2>&1; then
        echo "FAIL: capture failed for $label $mode" >&2
        tail -40 "$log" | sed 's/^/  /' >&2
        exit 1
    fi

    if grep -qF "[GEASSERT]" "$log"; then
        echo "FAIL: GEASSERT fired during $label $mode" >&2
        grep -F "[GEASSERT]" "$log" | head -5 | sed 's/^/  /' >&2
        exit 1
    fi
    if [[ ! -s "$trace" ]]; then
        echo "FAIL: missing state trace for $label $mode: $trace" >&2
        exit 1
    fi
    if [[ ! -s "$shot" ]]; then
        echo "FAIL: missing screenshot for $label $mode: $shot" >&2
        tail -40 "$log" | sed 's/^/  /' >&2
        exit 1
    fi

    python3 tools/audit_render_trace.py \
        --label "render camera clearance $label $mode" \
        --json-out "$case_dir/render.json" \
        "$trace" >"$case_dir/render.txt"
    python3 tools/audit_screenshot_health.py \
        --label "render camera clearance $label $mode" \
        --json-out "$case_dir/screenshot.json" \
        "$shot" >"$case_dir/screenshot.txt"
}

echo "=== Render Camera Clearance Regression ==="
echo "  out-dir: $OUT_DIR"
echo "  binary:  $BINARY"
echo "  ROM:     $ROM"
echo "  frames:  $FRAMES (default)"

for entry in "${CASES[@]}"; do
    IFS='|' read -r label level pad _min_cc input_spec _required_hit_types _required_moving_door_hits _required_door_state_hits _required_hit_obj_types _required_glass_pads _required_tile_rooms case_frames _required_hit_obj_ids _required_vehicle_moves <<< "$entry"
    if [[ -z "$case_frames" ]]; then
        case_frames="$FRAMES"
    fi
    if [[ ! "$case_frames" =~ ^[1-9][0-9]*$ ]]; then
        echo "FAIL: invalid frame count for $label: $case_frames" >&2
        exit 2
    fi
    run_capture "$label" enabled "$level" "$pad" "$input_spec" "$case_frames"
    run_capture "$label" disabled "$level" "$pad" "$input_spec" "$case_frames" \
        GE007_RENDER_CAMERA_CLEARANCE=0
done

python3 - "$OUT_DIR" "$FRAMES" "${CASES[@]}" <<'PY'
import json
import math
import re
import sys
from pathlib import Path

root = Path(sys.argv[1])
default_frames = int(sys.argv[2])
case_specs = sys.argv[3:]

def load_last_trace(path):
    last = None
    with path.open("r", encoding="utf-8") as handle:
        for line in handle:
            line = line.strip()
            if line:
                last = json.loads(line)
    if last is None:
        raise SystemExit(f"FAIL: no trace records: {path}")
    return last

def vector_delta(a, b, key):
    av = a.get(key)
    bv = b.get(key)
    if not isinstance(av, list) or not isinstance(bv, list) or len(av) != len(bv):
        return None
    return math.sqrt(sum((float(x) - float(y)) ** 2 for x, y in zip(av, bv)))

def read_render_json(label, mode):
    path = root / label / mode / "render.json"
    return json.loads(path.read_text(encoding="utf-8"))

def clearance_hit_type_counts(log_text):
    counts = {}
    for line in log_text.splitlines():
        if "[CAM-CLEARANCE]" not in line:
            continue
        match = re.search(r"\bhit_prop_type=(-?\d+)\b", line)
        if match is None:
            match = re.search(r"\bprop_type=(-?\d+)\b", line)
        if match is None:
            continue
        hit_type = int(match.group(1))
        counts[hit_type] = counts.get(hit_type, 0) + 1
    return counts

def clearance_hit_obj_type_counts(log_text):
    counts = {}
    for line in log_text.splitlines():
        if "[CAM-CLEARANCE]" not in line:
            continue
        match = re.search(r"\bhit_obj_type=(-?\d+)\b", line)
        if match is None:
            continue
        hit_type = int(match.group(1))
        counts[hit_type] = counts.get(hit_type, 0) + 1
    return counts

def clearance_hit_obj_id_counts(log_text):
    counts = {}
    for line in log_text.splitlines():
        if "[CAM-CLEARANCE]" not in line:
            continue
        match = re.search(r"\bhit_obj=(-?\d+)\b", line)
        if match is None:
            continue
        obj_id = int(match.group(1))
        counts[obj_id] = counts.get(obj_id, 0) + 1
    return counts

def clearance_tile_room_counts(log_text):
    counts = {}
    for line in log_text.splitlines():
        if "[CAM-CLEARANCE]" not in line:
            continue
        match = re.search(r"\btile_room=(-?\d+)\b", line)
        if match is None:
            continue
        room = int(match.group(1))
        counts[room] = counts.get(room, 0) + 1
    return counts

def glass_trace_pad_counts(log_text):
    counts = {}
    for line in log_text.splitlines():
        if "[GLASS_TRACE]" not in line:
            continue
        match = re.search(r"\bpad=(-?\d+)\b", line)
        if match is None:
            continue
        pad = int(match.group(1))
        counts[pad] = counts.get(pad, 0) + 1
    return counts

def clearance_moving_door_hits(log_text):
    count = 0
    for line in log_text.splitlines():
        if "[CAM-CLEARANCE]" not in line:
            continue
        hit_type_match = re.search(r"\bhit_prop_type=(-?\d+)\b", line)
        door_state_match = re.search(r"\bhit_door_state=(-?\d+)\b", line)
        door_open_match = re.search(r"\bhit_door_open=([+-]?(?:\d+(?:\.\d*)?|\.\d+))\b", line)
        if hit_type_match is None or door_state_match is None or door_open_match is None:
            continue
        if (int(hit_type_match.group(1)) == 2
                and int(door_state_match.group(1)) > 0
                and float(door_open_match.group(1)) > 0.0):
            count += 1
    return count

def clearance_door_state_hit_counts(log_text):
    counts = {}
    for line in log_text.splitlines():
        if "[CAM-CLEARANCE]" not in line:
            continue
        hit_type_match = re.search(r"\bhit_prop_type=(-?\d+)\b", line)
        door_state_match = re.search(r"\bhit_door_state=(-?\d+)\b", line)
        door_open_match = re.search(r"\bhit_door_open=([+-]?(?:\d+(?:\.\d*)?|\.\d+))\b", line)
        if hit_type_match is None or door_state_match is None or door_open_match is None:
            continue
        if int(hit_type_match.group(1)) != 2:
            continue
        if float(door_open_match.group(1)) <= 0.0:
            continue
        door_state = int(door_state_match.group(1))
        if door_state <= 0:
            continue
        counts[door_state] = counts.get(door_state, 0) + 1
    return counts

def vehicle_move_counts(log_text):
    counts = {}
    for line in log_text.splitlines():
        if "[VEHICLE_STATE]" not in line or "event=move_accepted" not in line:
            continue
        match = re.search(r"\bobj=(-?\d+)\b", line)
        if match is None:
            continue
        obj_id = int(match.group(1))
        counts[obj_id] = counts.get(obj_id, 0) + 1
    return counts

def required_hit_types(spec):
    requirements = []
    for item in spec.split(","):
        item = item.strip()
        if not item:
            continue
        if ":" in item:
            hit_type, count = item.split(":", 1)
            requirements.append((int(hit_type), int(count)))
        else:
            requirements.append((int(item), 1))
    return requirements

def required_door_state_hits(spec):
    return required_hit_types(spec)

def required_hit_obj_types(spec):
    return required_hit_types(spec)

def required_glass_pads(spec):
    return required_hit_types(spec)

def required_tile_rooms(spec):
    return required_hit_types(spec)

def required_hit_obj_ids(spec):
    return required_hit_types(spec)

def required_vehicle_moves(spec):
    return required_hit_types(spec)

summary = {"status": "pass", "cases": [], "failures": []}

for spec in case_specs:
    parts = spec.split("|")
    if len(parts) < 6 or len(parts) > 14:
        raise SystemExit(f"FAIL: malformed case spec: {spec}")
    parts += [""] * (14 - len(parts))
    (
        label,
        level,
        pad,
        min_cc,
        input_spec,
        hit_type_spec,
        moving_door_hits_spec,
        door_state_hits_spec,
        hit_obj_type_spec,
        glass_pad_spec,
        tile_room_spec,
        frame_spec,
        hit_obj_id_spec,
        vehicle_move_spec,
    ) = parts
    min_cc = int(min_cc)
    frame_target = int(frame_spec) if frame_spec else default_frames
    required_moving_door_hits = int(moving_door_hits_spec) if moving_door_hits_spec else 0
    enabled_dir = root / label / "enabled"
    disabled_dir = root / label / "disabled"
    enabled_log = (enabled_dir / "run.log").read_text(errors="replace")
    disabled_log = (disabled_dir / "run.log").read_text(errors="replace")
    enabled_trace = load_last_trace(enabled_dir / "state.jsonl")
    disabled_trace = load_last_trace(disabled_dir / "state.jsonl")
    enabled_cc = enabled_log.count("[CAM-CLEARANCE]")
    disabled_cc = disabled_log.count("[CAM-CLEARANCE]")
    enabled_hit_types = clearance_hit_type_counts(enabled_log)
    enabled_hit_obj_types = clearance_hit_obj_type_counts(enabled_log)
    enabled_hit_obj_ids = clearance_hit_obj_id_counts(enabled_log)
    enabled_glass_pads = glass_trace_pad_counts(enabled_log)
    enabled_tile_rooms = clearance_tile_room_counts(enabled_log)
    enabled_moving_door_hits = clearance_moving_door_hits(enabled_log)
    enabled_door_state_hits = clearance_door_state_hit_counts(enabled_log)
    enabled_vehicle_moves = vehicle_move_counts(enabled_log)
    pos_delta = vector_delta(enabled_trace, disabled_trace, "pos")
    col_delta = vector_delta(enabled_trace, disabled_trace, "col")
    room_enabled = enabled_trace.get("rooms", {}).get("cur")
    room_disabled = disabled_trace.get("rooms", {}).get("cur")
    render_enabled = read_render_json(label, "enabled")
    render_disabled = read_render_json(label, "disabled")

    case = {
        "label": label,
        "level": int(level),
        "pad": int(pad),
        "input": input_spec,
        "enabled_clearance_lines": enabled_cc,
        "disabled_clearance_lines": disabled_cc,
        "enabled_hit_prop_types": {str(key): value for key, value in sorted(enabled_hit_types.items())},
        "enabled_hit_obj_types": {str(key): value for key, value in sorted(enabled_hit_obj_types.items())},
        "enabled_hit_obj_ids": {str(key): value for key, value in sorted(enabled_hit_obj_ids.items())},
        "enabled_glass_pads": {str(key): value for key, value in sorted(enabled_glass_pads.items())},
        "enabled_tile_rooms": {str(key): value for key, value in sorted(enabled_tile_rooms.items())},
        "enabled_moving_door_hits": enabled_moving_door_hits,
        "enabled_door_state_hits": {str(key): value for key, value in sorted(enabled_door_state_hits.items())},
        "enabled_vehicle_moves": {str(key): value for key, value in sorted(enabled_vehicle_moves.items())},
        "position_delta": pos_delta,
        "collision_delta": col_delta,
        "room_enabled": room_enabled,
        "room_disabled": room_disabled,
        "render_enabled_status": render_enabled.get("status"),
        "render_disabled_status": render_disabled.get("status"),
        "frame_enabled": enabled_trace.get("f"),
        "frame_disabled": disabled_trace.get("f"),
        "frame_target": frame_target,
    }
    summary["cases"].append(case)

    if enabled_cc < min_cc:
        summary["failures"].append(
            f"{label}: enabled clearance lines {enabled_cc} < {min_cc}"
        )
    if disabled_cc != 0:
        summary["failures"].append(
            f"{label}: disabled clearance emitted {disabled_cc} line(s)"
        )
    for hit_type, required_count in required_hit_types(hit_type_spec):
        actual_count = enabled_hit_types.get(hit_type, 0)
        if actual_count < required_count:
            summary["failures"].append(
                f"{label}: hit prop type {hit_type} count {actual_count} < {required_count}"
            )
    for hit_type, required_count in required_hit_obj_types(hit_obj_type_spec):
        actual_count = enabled_hit_obj_types.get(hit_type, 0)
        if actual_count < required_count:
            summary["failures"].append(
                f"{label}: hit obj type {hit_type} count {actual_count} < {required_count}"
            )
    for obj_id, required_count in required_hit_obj_ids(hit_obj_id_spec):
        actual_count = enabled_hit_obj_ids.get(obj_id, 0)
        if actual_count < required_count:
            summary["failures"].append(
                f"{label}: hit obj {obj_id} count {actual_count} < {required_count}"
            )
    for glass_pad, required_count in required_glass_pads(glass_pad_spec):
        actual_count = enabled_glass_pads.get(glass_pad, 0)
        if actual_count < required_count:
            summary["failures"].append(
                f"{label}: glass pad {glass_pad} trace count {actual_count} < {required_count}"
            )
    for tile_room, required_count in required_tile_rooms(tile_room_spec):
        actual_count = enabled_tile_rooms.get(tile_room, 0)
        if actual_count < required_count:
            summary["failures"].append(
                f"{label}: clearance tile room {tile_room} count {actual_count} < {required_count}"
            )
    if enabled_moving_door_hits < required_moving_door_hits:
        summary["failures"].append(
            f"{label}: moving door clearance hits {enabled_moving_door_hits} < {required_moving_door_hits}"
        )
    for door_state, required_count in required_door_state_hits(door_state_hits_spec):
        actual_count = enabled_door_state_hits.get(door_state, 0)
        if actual_count < required_count:
            summary["failures"].append(
                f"{label}: door state {door_state} clearance hits {actual_count} < {required_count}"
            )
    for obj_id, required_count in required_vehicle_moves(vehicle_move_spec):
        actual_count = enabled_vehicle_moves.get(obj_id, 0)
        if actual_count < required_count:
            summary["failures"].append(
                f"{label}: vehicle obj {obj_id} move_accepted count {actual_count} < {required_count}"
            )
    if pos_delta is None or pos_delta > 0.001:
        summary["failures"].append(f"{label}: gameplay pos delta {pos_delta}")
    if col_delta is None or col_delta > 0.001:
        summary["failures"].append(f"{label}: collision pos delta {col_delta}")
    if room_enabled != room_disabled:
        summary["failures"].append(
            f"{label}: current room changed enabled={room_enabled} disabled={room_disabled}"
        )
    if render_enabled.get("status") != "pass":
        summary["failures"].append(f"{label}: enabled render audit failed")
    if render_disabled.get("status") != "pass":
        summary["failures"].append(f"{label}: disabled render audit failed")
    if case["frame_enabled"] is None or int(case["frame_enabled"]) < frame_target - 1:
        summary["failures"].append(
            f"{label}: enabled trace stopped at frame {case['frame_enabled']} before target {frame_target - 1}"
        )
    if case["frame_disabled"] is None or int(case["frame_disabled"]) < frame_target - 1:
        summary["failures"].append(
            f"{label}: disabled trace stopped at frame {case['frame_disabled']} before target {frame_target - 1}"
        )

if summary["failures"]:
    summary["status"] = "fail"

(root / "summary.json").write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n")

if summary["failures"]:
    print("FAIL: Render camera clearance regression")
    for failure in summary["failures"]:
        print(f"  - {failure}")
    raise SystemExit(1)

print("PASS: Render camera clearance regression")
for case in summary["cases"]:
    print(
        "  {label}: cc enabled={enabled_clearance_lines} disabled={disabled_clearance_lines} "
        "hit_types={enabled_hit_prop_types} obj_types={enabled_hit_obj_types} "
        "hit_objs={enabled_hit_obj_ids} "
        "glass_pads={enabled_glass_pads} tile_rooms={enabled_tile_rooms} "
        "moving_door_hits={enabled_moving_door_hits} "
        "door_state_hits={enabled_door_state_hits} vehicle_moves={enabled_vehicle_moves} "
        "pos_delta={position_delta:.3f} "
        "col_delta={collision_delta:.3f} room={room_enabled} "
        "frame={frame_enabled}/{frame_target}".format(
            **case
        )
    )
PY

echo "summary_json: $OUT_DIR/summary.json"
echo "artifacts: $OUT_DIR"
