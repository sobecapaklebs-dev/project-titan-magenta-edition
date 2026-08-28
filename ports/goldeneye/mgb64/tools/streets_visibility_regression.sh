#!/bin/bash
#
# streets_visibility_regression.sh -- Guard Streets portal visibility polish.
#
# The main regression captures the late-Streets pad-129 view that used to expose
# fog/sky where foreground buildings should render. The fixed default must match
# the coarse no-portal-BFS diagnostic exactly, while disabling the project-fail
# frustum fallback must still reproduce the old missing-geometry signature.
#
set -euo pipefail
cd "$(dirname "$0")/.."

source tools/validation_common.sh

BUILD_DIR="$(validation_default_build_dir)"
BINARY=""
ROM="$(validation_default_rom)"
DO_BUILD=1
TIMEOUT_SECONDS=90
OUT_DIR="/tmp/mgb64_streets_visibility_$$"
FRAMES=150

usage() {
    cat <<'USAGE'
Usage: tools/streets_visibility_regression.sh [options]

Options:
  --out-dir DIR        output directory (default: /tmp/...)
  --frames N           p129 screenshot/exit frame (default: 150)
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

run_capture() {
    local name="$1"
    local frame="$2"
    shift 2
    local case_dir="$OUT_DIR/$name"
    local trace="$case_dir/state.jsonl"
    local log="$case_dir/run.log"
    local shot="$case_dir/screenshot_${name}.bmp"

    mkdir -p "$case_dir"
    rm -f "$trace" "$log" "$shot" "$case_dir/render.json" "$case_dir/render.txt" \
        "$case_dir/screenshot.json" "$case_dir/screenshot.txt"

    echo "  capture: $name"
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
            "$@" \
            "$BINARY" \
            --savedir "$case_dir" \
            --rom "$ROM" \
            --level 29 \
            --difficulty agent \
            --deterministic \
            --trace-state "$trace" \
            --screenshot-frame "$frame" \
            --screenshot-label "$name" \
            --screenshot-exit
    ) >"$log" 2>&1; then
        echo "FAIL: capture failed for $name" >&2
        tail -40 "$log" | sed 's/^/  /' >&2
        exit 1
    fi

    if grep -qF "[GEASSERT]" "$log"; then
        echo "FAIL: GEASSERT fired during $name" >&2
        grep -F "[GEASSERT]" "$log" | head -5 | sed 's/^/  /' >&2
        exit 1
    fi
    if [[ ! -s "$trace" ]]; then
        echo "FAIL: missing state trace for $name: $trace" >&2
        exit 1
    fi
    if [[ ! -s "$shot" ]]; then
        echo "FAIL: missing screenshot for $name: $shot" >&2
        tail -40 "$log" | sed 's/^/  /' >&2
        exit 1
    fi

    python3 tools/audit_render_trace.py \
        --label "streets visibility $name" \
        --json-out "$case_dir/render.json" \
        "$trace" >"$case_dir/render.txt"
    python3 tools/audit_screenshot_health.py \
        --label "streets visibility $name" \
        --json-out "$case_dir/screenshot.json" \
        "$shot" >"$case_dir/screenshot.txt"
}

p129_env=(
    GE007_AUTO_WARP_FRAME=40
    GE007_AUTO_WARP_PAD=129
    GE007_AUTO_FACE_COORD_FRAME=45
    GE007_AUTO_FACE_COORD_X=4176.90
    GE007_AUTO_FACE_COORD_Y=20.48
    GE007_AUTO_FACE_COORD_Z=22174.50
    GE007_AUTO_FORWARD=70:80
)

static_pad_env() {
    local pad="$1"
    local target_x="$2"
    local target_y="$3"
    local target_z="$4"

    printf '%s\n' \
        GE007_AUTO_WARP_FRAME=40 \
        "GE007_AUTO_WARP_PAD=$pad" \
        GE007_AUTO_FACE_COORD_FRAME=45 \
        "GE007_AUTO_FACE_COORD_X=$target_x" \
        "GE007_AUTO_FACE_COORD_Y=$target_y" \
        "GE007_AUTO_FACE_COORD_Z=$target_z"
}

echo "=== Streets Visibility Regression ==="
echo "  out-dir: $OUT_DIR"
echo "  binary:  $BINARY"
echo "  ROM:     $ROM"
echo "  frames:  $FRAMES"

run_capture p129_default "$FRAMES" "${p129_env[@]}"
run_capture p129_no_portal_bfs "$FRAMES" "${p129_env[@]}" GE007_PORTAL_BFS=0
run_capture p129_project_fallback_off "$FRAMES" "${p129_env[@]}" GE007_PORTAL_PROJECT_FRUSTUM_FALLBACK=0

# Static spot checks from the broader Streets sweep: early road, mid road,
# late road, endpoint, and the elevated interior whose red/blue wall texture can
# otherwise be mistaken for sky leakage by simple color metrics.
run_capture spot_pad040 90 $(static_pad_env 40 -3861.00 20.48 5406.53)
run_capture spot_pad080 90 $(static_pad_env 80 -4405.05 20.48 12754.14)
run_capture spot_pad136 90 $(static_pad_env 136 3366.68 20.48 25893.25)
run_capture spot_pad168 90 $(static_pad_env 168 9453.61 20.48 27022.30)
run_capture spot_pad272 90 $(static_pad_env 272 5209.43 260.33 7924.96)

python3 tools/compare_screenshots.py \
    "$OUT_DIR/p129_default/screenshot_p129_default.bmp" \
    "$OUT_DIR/p129_no_portal_bfs/screenshot_p129_no_portal_bfs.bmp" \
    --max-changed-pct 0.10 \
    --json-out "$OUT_DIR/p129_default_vs_no_portal_bfs.json" \
    >"$OUT_DIR/p129_default_vs_no_portal_bfs.txt"

python3 tools/compare_screenshots.py \
    "$OUT_DIR/p129_default/screenshot_p129_default.bmp" \
    "$OUT_DIR/p129_project_fallback_off/screenshot_p129_project_fallback_off.bmp" \
    --json-out "$OUT_DIR/p129_default_vs_project_fallback_off.json" \
    >"$OUT_DIR/p129_default_vs_project_fallback_off.txt"

python3 - "$OUT_DIR" <<'PY'
import json
import sys
from pathlib import Path

root = Path(sys.argv[1])
required_p129_rooms = {26, 27, 28, 36, 43, 44, 45, 46, 53}

def load_last_trace(case):
    last = None
    trace = root / case / "state.jsonl"
    with trace.open("r", encoding="utf-8") as handle:
        for line in handle:
            line = line.strip()
            if line:
                last = json.loads(line)
    if last is None:
        raise SystemExit(f"FAIL: no trace records for {case}")
    return last

def room_vis(record):
    return record.get("rooms", {}).get("vis", {})

def rendered_count(record):
    value = room_vis(record).get("rendered")
    if value is None:
        raise SystemExit("FAIL: trace missing rendered room count")
    return int(value)

def draw_rooms(record):
    return list(room_vis(record).get("draw_sample") or [])

records = {
    case: load_last_trace(case)
    for case in (
        "p129_default",
        "p129_no_portal_bfs",
        "p129_project_fallback_off",
        "spot_pad040",
        "spot_pad080",
        "spot_pad136",
        "spot_pad168",
        "spot_pad272",
    )
}

default_vs_no = json.loads((root / "p129_default_vs_no_portal_bfs.json").read_text())
default_vs_off = json.loads((root / "p129_default_vs_project_fallback_off.json").read_text())

failures = []

default_draw = set(draw_rooms(records["p129_default"]))
no_bfs_draw = set(draw_rooms(records["p129_no_portal_bfs"]))
off_draw = set(draw_rooms(records["p129_project_fallback_off"]))

if not required_p129_rooms.issubset(default_draw):
    failures.append(f"p129 default missing rooms {sorted(required_p129_rooms - default_draw)}")
if not required_p129_rooms.issubset(no_bfs_draw):
    failures.append(f"p129 no_portal_bfs missing rooms {sorted(required_p129_rooms - no_bfs_draw)}")
if required_p129_rooms.issubset(off_draw):
    failures.append("p129 project-fallback-off did not reproduce missing-room control")
if rendered_count(records["p129_default"]) > 12:
    failures.append(f"p129 default over-admitted rooms: {rendered_count(records['p129_default'])} > 12")

changed_no = float(default_vs_no.get("changed_pct", 100.0))
changed_off = float(default_vs_off.get("changed_pct", 0.0))

if changed_no > 0.10:
    failures.append(f"p129 default vs no_portal_bfs changed {changed_no:.3f}% > 0.10%")
if changed_off < 5.0:
    failures.append(f"p129 project-fallback-off control too weak: {changed_off:.3f}% < 5.0%")

for case, record in records.items():
    if int(record.get("bad_cmds") or 0) != 0:
        failures.append(f"{case} bad_cmds={record.get('bad_cmds')}")
    if int(record.get("crashes") or 0) != 0:
        failures.append(f"{case} crashes={record.get('crashes')}")
    if int(record.get("nan") or 0) != 0:
        failures.append(f"{case} nan={record.get('nan')}")

summary = {
    "status": "fail" if failures else "pass",
    "p129": {
        "default_rooms": rendered_count(records["p129_default"]),
        "default_draw": draw_rooms(records["p129_default"]),
        "no_portal_bfs_rooms": rendered_count(records["p129_no_portal_bfs"]),
        "no_portal_bfs_draw": draw_rooms(records["p129_no_portal_bfs"]),
        "project_fallback_off_rooms": rendered_count(records["p129_project_fallback_off"]),
        "project_fallback_off_draw": draw_rooms(records["p129_project_fallback_off"]),
        "default_vs_no_portal_bfs_changed_pct": changed_no,
        "default_vs_project_fallback_off_changed_pct": changed_off,
    },
    "spots": {
        case: {
            "current_room": records[case].get("rooms", {}).get("cur"),
            "rendered_rooms": rendered_count(records[case]),
            "draw_sample": draw_rooms(records[case]),
        }
        for case in records
        if case.startswith("spot_")
    },
    "failures": failures,
}

(root / "summary.json").write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n")

if failures:
    print("FAIL: Streets visibility regression")
    for failure in failures:
        print(f"  - {failure}")
    raise SystemExit(1)

print("PASS: Streets visibility regression")
print(
    "  p129 rooms: default=%d no_portal_bfs=%d project_fallback_off=%d"
    % (
        summary["p129"]["default_rooms"],
        summary["p129"]["no_portal_bfs_rooms"],
        summary["p129"]["project_fallback_off_rooms"],
    )
)
print(
    "  p129 changed: default_vs_no_portal_bfs=%.3f%% "
    "default_vs_project_fallback_off=%.3f%%"
    % (changed_no, changed_off)
)
PY

echo "summary_json: $OUT_DIR/summary.json"
echo "artifacts: $OUT_DIR"
