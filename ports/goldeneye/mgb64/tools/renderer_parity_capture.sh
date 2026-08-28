#!/bin/bash
#
# renderer_parity_capture.sh -- Capture local renderer parity scenes.
#
# This script creates screenshots and state traces for known renderer
# compatibility defaults. Captures are generated from the user's ROM and must
# stay local; do not commit or attach the resulting BMP/JSONL artifacts.
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
OUT_DIR="/tmp/mgb64_renderer_parity_$$"
SCENE="all"

usage() {
    cat <<'USAGE'
Usage: tools/renderer_parity_capture.sh [options]

Options:
  --scene NAME       all, facility_scissor, surface_sky_fog, world_attrs (default: all)
  --out-dir DIR      output directory for local captures (default: /tmp/...)
  --rom PATH         ROM path (default: ./baserom.u.z64)
  --binary PATH      native binary path (default: build/ge007)
  --build-dir DIR    CMake build directory (default: build)
  --no-build         reuse an existing binary
  --timeout SECONDS  per-capture timeout (default: 45)

Artifacts are ROM-derived local validation data. Do not commit or redistribute
captured screenshots or traces.
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --scene) SCENE="$2"; shift 2 ;;
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

if [[ -z "$BINARY" ]]; then
    BINARY="$(validation_binary_path "$BUILD_DIR")"
fi

if [[ "$DO_BUILD" -eq 1 ]]; then
    validation_configure_build "$BUILD_DIR" >/dev/null
    validation_build "$BUILD_DIR" >/dev/null
fi

validation_require_binary "$BINARY"
validation_require_file "$ROM" "ROM"
validation_acquire_runtime_lock
trap 'validation_release_runtime_lock' EXIT INT TERM

mkdir -p "$OUT_DIR"
OUT_DIR="$(cd "$OUT_DIR" && pwd)"
CAPTURE_INDEX="$OUT_DIR/captures.tsv"
COMPARISON_INDEX="$OUT_DIR/comparisons.tsv"
SUMMARY_JSON="$OUT_DIR/summary.json"
printf 'scene\tvariant\tlevel\tframe\tallow_room_fallback\tscreenshot\ttrace\tlog\tscreenshot_json\trender_json\ttrace_summary_json\n' >"$CAPTURE_INDEX"
printf 'scene\tscreenshot_status\tstate_status\tscreenshot_log\tscreenshot_json\tstate_log\n' >"$COMPARISON_INDEX"

run_capture() {
    local scene_name="$1"
    local variant="$2"
    local level="$3"
    local frame="$4"
    local env_key="$5"
    local env_value="$6"
    local allow_room_fallback="${7:-0}"
    local scene_dir="$OUT_DIR/$scene_name"
    local label="${scene_name}_${variant}_$$"
    local screenshot_src="screenshot_${label}.bmp"
    local screenshot_dst="$scene_dir/${variant}.bmp"
    local trace="$scene_dir/${variant}.jsonl"
    local log="$scene_dir/${variant}.log"
    local screenshot_json="$scene_dir/${variant}.screenshot.json"
    local render_json="$scene_dir/${variant}.render.json"
    local trace_summary_json="$scene_dir/${variant}.trace-summary.json"
    local env_cmd=()

    mkdir -p "$scene_dir"
    rm -f "$screenshot_src" "$screenshot_dst" "$trace" "$log"

    echo "  capture: ${scene_name}/${variant} level=${level} frame=${frame}${env_key:+ ${env_key}=${env_value}}"

    env_cmd=(env -u GE007_DEBUG \
        SDL_AUDIODRIVER="${GE007_VALIDATION_SDL_AUDIODRIVER:-dummy}" \
        GE007_MUTE=1 \
        GE007_DETERMINISTIC_STABLE_COUNT=1 \
        GE007_NO_VSYNC=1 \
        GE007_BACKGROUND=1 \
        GE007_NO_INPUT_GRAB=1)
    if [[ -n "$env_key" ]]; then
        env_cmd+=("${env_key}=${env_value}")
    fi
    env_cmd+=("$BINARY"
        --rom "$ROM"
        --level "$level"
        --deterministic
        --trace-state "$trace"
        --screenshot-frame "$frame"
        --screenshot-label "$label"
        --screenshot-exit)

    if [[ -n "$TIMEOUT_BIN" ]]; then
        if ! "$TIMEOUT_BIN" --kill-after=5 "$TIMEOUT_SECONDS" "${env_cmd[@]}" >"$log" 2>&1; then
            echo "FAIL: capture failed for ${scene_name}/${variant}"
            tail -40 "$log" | sed 's/^/  /'
            exit 1
        fi
    elif ! "${env_cmd[@]}" >"$log" 2>&1; then
        echo "FAIL: capture failed for ${scene_name}/${variant}"
        tail -40 "$log" | sed 's/^/  /'
        exit 1
    fi

    if [[ ! -s "$screenshot_src" ]]; then
        echo "FAIL: missing screenshot for ${scene_name}/${variant}: $screenshot_src"
        tail -40 "$log" | sed 's/^/  /'
        exit 1
    fi
    mv "$screenshot_src" "$screenshot_dst"
    python3 tools/audit_screenshot_health.py \
        --label "${scene_name}/${variant} screenshot" \
        --expect-size 640x480 \
        --json-out "$screenshot_json" \
        "$screenshot_dst"

    if [[ ! -s "$trace" ]]; then
        echo "FAIL: missing state trace for ${scene_name}/${variant}: $trace"
        exit 1
    fi

    if grep -qF "[GEASSERT]" "$log"; then
        echo "FAIL: GEASSERT fired during ${scene_name}/${variant}"
        grep -F "[GEASSERT]" "$log" | head -5 | sed 's/^/  /'
        exit 1
    fi

    if [[ "$allow_room_fallback" -eq 1 ]]; then
        python3 tools/audit_render_trace.py \
            --label "${scene_name}/${variant}" \
            --allow-room-fallback \
            --json-out "$render_json" \
            "$trace"
    else
        python3 tools/audit_render_trace.py \
            --label "${scene_name}/${variant}" \
            --json-out "$render_json" \
            "$trace"
    fi

    python3 - "$trace" "$trace_summary_json" <<'PY'
import json
import sys

last = None
with open(sys.argv[1], "r", encoding="utf-8", errors="replace") as handle:
    for line in handle:
        if line.startswith("{"):
            last = json.loads(line)
if not last:
    raise SystemExit("FAIL: no JSON state frames")
summary = {
    "trace": sys.argv[1],
    "frame": last.get("f"),
    "tris": last.get("tris"),
    "fog": last.get("fog"),
    "fog_mul": last.get("fog_mul"),
    "fog_off": last.get("fog_off"),
    "rooms": last.get("rooms"),
}
with open(sys.argv[2], "w", encoding="utf-8") as out:
    json.dump(summary, out, indent=2, sort_keys=True)
    out.write("\n")
print("    trace:", json.dumps(summary, sort_keys=True))
PY

    printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
        "$scene_name" \
        "$variant" \
        "$level" \
        "$frame" \
        "$allow_room_fallback" \
        "$screenshot_dst" \
        "$trace" \
        "$log" \
        "$screenshot_json" \
        "$render_json" \
        "$trace_summary_json" >>"$CAPTURE_INDEX"
}

run_scene_comparisons() {
    local scene_name="$1"
    local a="$OUT_DIR/$scene_name/compat"
    local b="$OUT_DIR/$scene_name/diagnostic"
    local screenshot_log="$OUT_DIR/$scene_name/screenshot_compare.txt"
    local screenshot_json="$OUT_DIR/$scene_name/screenshot_compare.json"
    local state_log="$OUT_DIR/$scene_name/state_compare.txt"

    echo ""
    echo "  compare ${scene_name}:"
    local screenshot_status="pass"
    local state_status="pass"
    if python3 tools/compare_screenshots.py "${a}.bmp" "${b}.bmp" \
        --json-out "$screenshot_json" \
        >"$screenshot_log" 2>&1; then
        sed -n '1,8p' "$screenshot_log" | sed 's/^/    /'
    else
        screenshot_status="fail"
        echo "    screenshot compare unavailable; see $screenshot_log"
    fi

    if python3 tools/compare_state.py "${a}.jsonl" "${b}.jsonl" \
        >"$state_log" 2>&1; then
        sed -n '1,8p' "$state_log" | sed 's/^/    /'
    else
        state_status="expected_diagnostic_divergence"
        echo "    state compare reported expected diagnostic divergence:"
        sed -n '1,12p' "$state_log" | sed 's/^/      /'
    fi

    printf '%s\t%s\t%s\t%s\t%s\t%s\n' \
        "$scene_name" \
        "$screenshot_status" \
        "$state_status" \
        "$screenshot_log" \
        "$screenshot_json" \
        "$state_log" >>"$COMPARISON_INDEX"

    echo "    logs: $screenshot_log"
    echo "          $state_log"
}

run_facility_scissor() {
    echo ""
    echo "=== Renderer Scene: facility_scissor ==="
    echo "Expected: compat keeps room scissor disabled; diagnostic enables exact N64 room scissor."
    echo "Use this to inspect interior seam/under-cover regressions."
    run_capture "facility_scissor" "compat" "34" "180" "" "" 0
    run_capture "facility_scissor" "diagnostic" "34" "180" "GE007_EXACT_ROOM_SCISSOR" "1" 0
    run_scene_comparisons "facility_scissor"
}

run_surface_sky_fog() {
    echo ""
    echo "=== Renderer Scene: surface_sky_fog ==="
    echo "Expected: compat uses portal-BFS room admission; diagnostic disables it as a broad fallback."
    echo "Use this to inspect outdoor sky/fog state and room-admission side effects."
    run_capture "surface_sky_fog" "compat" "36" "180" "" "" 0
    run_capture "surface_sky_fog" "diagnostic" "36" "180" "GE007_PORTAL_BFS" "0" 1
    run_scene_comparisons "surface_sky_fog"
}

run_world_attrs() {
    echo ""
    echo "=== Renderer Scene: world_attrs (W1.E2) ==="
    echo "Expected: GE007_FORCE_WORLD_ATTRS packs + declares the world-position vertex"
    echo "attribute but leaves it unconsumed, so the rendered pixels are IDENTICAL to the"
    echo "off variant — the plumbing is transparent. compat = FORCE off, diagnostic ="
    echo "FORCE on. The pair should compare byte-identical (screenshot diff ~0%)."
    run_capture "world_attrs" "compat" "dam" "120" "" "" 1
    run_capture "world_attrs" "diagnostic" "dam" "120" "GE007_FORCE_WORLD_ATTRS" "1" 1
    run_scene_comparisons "world_attrs"
}

echo "=== Renderer Parity Capture ==="
echo "  out-dir: $OUT_DIR"
echo "  binary:  $BINARY"
echo "  ROM:     $ROM"

case "$SCENE" in
    all)
        run_facility_scissor
        run_surface_sky_fog
        run_world_attrs
        ;;
    facility_scissor)
        run_facility_scissor
        ;;
    surface_sky_fog)
        run_surface_sky_fog
        ;;
    world_attrs)
        run_world_attrs
        ;;
    *)
        echo "Unknown scene: $SCENE" >&2
        usage >&2
        exit 2
        ;;
esac

python3 - "$CAPTURE_INDEX" "$COMPARISON_INDEX" "$SUMMARY_JSON" <<'PY'
import csv
import json
import sys
from pathlib import Path

capture_index = Path(sys.argv[1])
comparison_index = Path(sys.argv[2])
summary_path = Path(sys.argv[3])


def load_json(path):
    with open(path, "r", encoding="utf-8") as handle:
        return json.load(handle)


captures = []
with capture_index.open("r", encoding="utf-8", newline="") as handle:
    for row in csv.DictReader(handle, delimiter="\t"):
        captures.append(
            {
                "scene": row["scene"],
                "variant": row["variant"],
                # --level accepts numeric LEVELIDs or stage slugs (world_attrs
                # captures use "dam"); keep slugs as strings.
                "level": int(row["level"]) if row["level"].isdigit() else row["level"],
                "frame": int(row["frame"]),
                "allow_room_fallback": row["allow_room_fallback"] == "1",
                "screenshot": row["screenshot"],
                "trace": row["trace"],
                "log": row["log"],
                "screenshot_health": load_json(row["screenshot_json"]),
                "render_health": load_json(row["render_json"]),
                "trace_summary": load_json(row["trace_summary_json"]),
            }
        )

comparisons = []
with comparison_index.open("r", encoding="utf-8", newline="") as handle:
    for row in csv.DictReader(handle, delimiter="\t"):
        row["screenshot_compare"] = load_json(row["screenshot_json"])
        comparisons.append(row)

failures = []
for capture in captures:
    if not capture["screenshot_health"].get("ok"):
        failures.append(f"{capture['scene']}/{capture['variant']}: screenshot health failed")
    if capture["render_health"].get("status") != "pass":
        failures.append(f"{capture['scene']}/{capture['variant']}: render health failed")
for comparison in comparisons:
    if comparison["screenshot_status"] != "pass":
        failures.append(f"{comparison['scene']}: screenshot comparison failed")

summary = {
    "status": "fail" if failures else "pass",
    "capture_count": len(captures),
    "comparison_count": len(comparisons),
    "failures": failures,
    "captures": captures,
    "comparisons": comparisons,
}
with summary_path.open("w", encoding="utf-8") as handle:
    json.dump(summary, handle, indent=2, sort_keys=True)
    handle.write("\n")

print(f"summary_json: {summary_path}")
print(
    "summary:"
    f" status={summary['status']}"
    f" captures={summary['capture_count']}"
    f" comparisons={summary['comparison_count']}"
    f" failures={len(failures)}"
)
if failures:
    for failure in failures[:8]:
        print(f"  {failure}")
    raise SystemExit(1)
PY

echo ""
echo "=== Renderer Parity Capture: PASS ==="
echo "Artifacts are local ROM-derived validation data. Keep them out of git."
