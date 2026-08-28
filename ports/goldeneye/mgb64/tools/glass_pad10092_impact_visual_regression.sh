#!/bin/bash
#
# glass_pad10092_impact_visual_regression.sh -- Guard the Dam pad-10092
# actor-light impact/decal route seed.
#
# This is not a final pixel-parity gate. It proves that the pad-10092
# stock-backed route reaches matching health, glass state, shard projection, and
# selected world-impact/decal geometry. Actor composition and screenshot pixels
# remain report-only until a cleaner route/view/mask is found.
#
set -euo pipefail
cd "$(dirname "$0")/.."

source tools/validation_common.sh

ROUTE="dam_regular_glass_shatter_pad10092_impact_visual_probe"
STOCK_FRAME=2541
NATIVE_FRAME=124
IMPACT_CENTER_TOLERANCE=5.0
PROJECTION_CENTER_TOLERANCE=1.0
BUILD_DIR="$(validation_default_build_dir)"
BINARY=""
ROM="$(validation_default_rom)"
ARES_BIN=""
DO_BUILD=1
TIMEOUT_SECONDS=300
OUT_DIR="/tmp/mgb64_glass_pad10092_impact_visual_$$"

usage() {
    cat <<'USAGE'
Usage: tools/glass_pad10092_impact_visual_regression.sh [options]

Options:
  --out-dir DIR        output directory (default: /tmp/...)
  --rom PATH           ROM path (default: ./baserom.u.z64)
  --binary PATH        native binary path (default: build/ge007)
  --build-dir DIR      CMake build directory (default: build)
  --ares-bin PATH      instrumented ares binary
  --no-build           reuse an existing native binary
  --timeout SECONDS    route timeout (default: 300)

Artifacts are ROM-derived local validation data. Do not commit captured traces,
screenshots, logs, emulator output, or generated summaries.
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --out-dir) OUT_DIR="$2"; shift 2 ;;
        --rom) ROM="$2"; shift 2 ;;
        --binary) BINARY="$2"; shift 2 ;;
        --build-dir) BUILD_DIR="$2"; shift 2 ;;
        --ares-bin) ARES_BIN="$2"; shift 2 ;;
        --no-build) DO_BUILD=0; shift ;;
        --timeout) TIMEOUT_SECONDS="$2"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown arg: $1" >&2; usage >&2; exit 2 ;;
    esac
done

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

if [[ -z "$ARES_BIN" ]]; then
    if [[ -x "build/ares-movement-oracle/ares/build-movement-oracle/desktop-ui/ares.app/Contents/MacOS/ares" ]]; then
        ARES_BIN="build/ares-movement-oracle/ares/build-movement-oracle/desktop-ui/ares.app/Contents/MacOS/ares"
    else
        ARES_BIN="build/ares-movement-oracle/ares/build-movement-oracle/desktop-ui/ares"
    fi
fi
ARES_BIN="$(validation_resolve_path "$ARES_BIN")"

if [[ "$DO_BUILD" -eq 1 ]]; then
    validation_configure_build "$BUILD_DIR" >/dev/null
    validation_build "$BUILD_DIR" >/dev/null
fi

validation_require_binary "$BINARY"
validation_require_file "$ROM" "ROM"
validation_require_binary "$ARES_BIN"

mkdir -p "$OUT_DIR"
OUT_DIR="$(cd "$OUT_DIR" && pwd)"
CASE_DIR="$OUT_DIR/pad10092_impact"
ARES_VIDEO_BLOCKING="${MGB64_ARES_VIDEO_BLOCKING:-true}"
ARES_ROUTE_CONTROL_RETRIES="${MGB64_ARES_STOCK_ROUTE_CONTROL_RETRIES:-8}"

echo "=== Glass Pad10092 Impact Visual Regression ==="
echo "  out-dir: $OUT_DIR"
echo "  route:   $ROUTE"
echo "  binary:  $BINARY"
echo "  ROM:     $ROM"
echo "  ares:    $ARES_BIN"
echo "  ares video blocking: $ARES_VIDEO_BLOCKING"
echo "  stock route retries: $ARES_ROUTE_CONTROL_RETRIES"

rm -rf "$CASE_DIR"
env \
    MGB64_ARES_VIDEO_BLOCKING="$ARES_VIDEO_BLOCKING" \
    MGB64_ARES_STOCK_ROUTE_CONTROL_RETRIES="$ARES_ROUTE_CONTROL_RETRIES" \
    tools/movement_oracle_capture.sh \
    --route "$ROUTE" \
    --out-dir "$CASE_DIR" \
    --rom "$ROM" \
    --binary "$BINARY" \
    --ares-bin "$ARES_BIN" \
    --no-build \
    --no-compare \
    --timeout "$TIMEOUT_SECONDS"

GFXDL_MATCHES="$CASE_DIR/gfxdl_matches.txt"
if grep -H -F "[GFX-DL]" "$CASE_DIR"/*.log > "$GFXDL_MATCHES"; then
    echo "FAIL: [GFX-DL] warning rows found in pad10092 impact logs" >&2
    head -20 "$GFXDL_MATCHES" | sed 's/^/  /' >&2
    exit 1
fi
rm -f "$GFXDL_MATCHES"

STOCK_TRACE="$CASE_DIR/stock_${ROUTE}.jsonl"
NATIVE_TRACE="$CASE_DIR/native_${ROUTE}.jsonl"
STOCK_SCREENSHOT="$CASE_DIR/stock_${ROUTE}.ppm"
NATIVE_SCREENSHOT="$CASE_DIR/native_${ROUTE}.bmp"
HEALTH_JSON="$CASE_DIR/combat_health_compare_${ROUTE}.json"
FRAMING_JSON="$CASE_DIR/visual_framing_compare_${ROUTE}.json"
GLASS_JSON="$CASE_DIR/compare_${ROUTE}.json"
PROJECTION_JSON="$CASE_DIR/projection_${ROUTE}.json"
IMPACT_CANDIDATES_JSON="$CASE_DIR/impact_checkpoint_candidates_${ROUTE}.json"
PROJECTED_IMPACT_JSON="$CASE_DIR/impact_projected_pixel_oracle_${ROUTE}.json"
PROJECTED_IMPACT_HEATMAP="$CASE_DIR/impact_projected_pixel_oracle_${ROUTE}.png"
IMPACT_SEQUENCE_JSON="$CASE_DIR/impact_sequence_compare_${ROUTE}.json"
VISUAL_JSON="$CASE_DIR/actor_masked_visual_compare_${ROUTE}.json"
VISUAL_HEATMAP="$CASE_DIR/actor_masked_visual_compare_${ROUTE}.png"

python3 tools/compare_combat_health_trace.py \
    --baseline-label "stock ${ROUTE}" \
    --test-label "native ${ROUTE}" \
    --baseline-frame "$STOCK_FRAME" \
    --test-frame "$NATIVE_FRAME" \
    --health-tolerance 0.001 \
    --damage-show-tolerance 1 \
    --require-match \
    --json-out "$HEALTH_JSON" \
    "$STOCK_TRACE" \
    "$NATIVE_TRACE"

echo ""
python3 tools/compare_visual_framing_trace.py \
    --baseline-label "stock ${ROUTE}" \
    --test-label "native ${ROUTE}" \
    --baseline-frame "$STOCK_FRAME" \
    --test-frame "$NATIVE_FRAME" \
    --max-camera-pos-delta 0.25 \
    --max-camera-target-delta 0.25 \
    --max-render-camera-pos-delta 0.25 \
    --max-render-camera-target-delta 0.25 \
    --max-cam-up-delta 0.01 \
    --max-facing-delta 0.01 \
    --max-room-basis-delta 0.01 \
    --max-view-delta 0.0 \
    --max-vv-verta-delta 0.001 \
    --json-out "$FRAMING_JSON" \
    "$STOCK_TRACE" \
    "$NATIVE_TRACE"

echo ""
python3 tools/compare_glass_trace.py \
    --require-active \
    --max-active-tolerance 0 \
    --first-position-tolerance 1.0 \
    --require-prop-destroyed \
    --prop-position-tolerance 1.0 \
    --max-buffer-len 200 \
    --json-out "$GLASS_JSON" \
    "$STOCK_TRACE" \
    "$NATIVE_TRACE"

echo ""
python3 tools/compare_glass_projection_trace.py \
    --require-present \
    --baseline-frame "$STOCK_FRAME" \
    --test-frame "$NATIVE_FRAME" \
    --max-active-delta 0 \
    --max-projected-delta 0 \
    --max-onscreen-delta 0 \
    --max-behind-delta 0 \
    --max-max-area-pct-delta 0.10 \
    --max-union-area-pct-delta 1.00 \
    --json-out "$PROJECTION_JSON" \
    "$STOCK_TRACE" \
    "$NATIVE_TRACE"

echo ""
python3 tools/score_impact_checkpoint_candidates.py \
    --require-active \
    --actor-chrnum 7 \
    --actor-chrnum 44 \
    --top 20 \
    --json-out "$IMPACT_CANDIDATES_JSON" \
    "$STOCK_TRACE" \
    "$NATIVE_TRACE"

echo ""
python3 tools/compare_projected_impact_visual.py \
    --baseline-frame "$STOCK_FRAME" \
    --test-frame "$NATIVE_FRAME" \
    --logical-size 320,240 \
    --logical-viewport 0,10,320,220 \
    --baseline-logical-frame active \
    --test-logical-frame full \
    --padding-logical 8 \
    --exclude-region hud_viewmodel:360,300,255,130 \
    --heatmap "$PROJECTED_IMPACT_HEATMAP" \
    --json-out "$PROJECTED_IMPACT_JSON" \
    "$STOCK_TRACE" \
    "$NATIVE_TRACE" \
    "$STOCK_SCREENSHOT" \
    "$NATIVE_SCREENSHOT" >/dev/null

echo ""
python3 tools/compare_bullet_impact_sequence.py \
    --baseline-label "stock ${ROUTE}" \
    --test-label "native ${ROUTE}" \
    --baseline-frame "$STOCK_FRAME" \
    --test-frame "$NATIVE_FRAME" \
    --json-out "$IMPACT_SEQUENCE_JSON" \
    "$STOCK_TRACE" \
    "$NATIVE_TRACE" >/dev/null

echo ""
python3 tools/compare_actor_masked_visual.py \
    --logical-size 320,240 \
    --logical-viewport 0,10,320,220 \
    --baseline-logical-frame active \
    --test-logical-frame full \
    --region tower_pane:80,115,320,180 \
    --region impact_side:255,145,120,95 \
    --region projected_impact:173,156,20,19 \
    --region lower_actor_cluster:145,160,215,125 \
    --region hud_viewmodel:360,300,255,130 \
    --exclude-region lower_actor_cluster:145,160,215,125 \
    --exclude-region hud_viewmodel:360,300,255,130 \
    --heatmap "$VISUAL_HEATMAP" \
    --json-out "$VISUAL_JSON" \
    "$STOCK_SCREENSHOT" \
    "$NATIVE_SCREENSHOT"

python3 - "$CASE_DIR" "$ROUTE" "$STOCK_FRAME" "$NATIVE_FRAME" "$IMPACT_CENTER_TOLERANCE" "$PROJECTION_CENTER_TOLERANCE" "$HEALTH_JSON" "$FRAMING_JSON" "$GLASS_JSON" "$PROJECTION_JSON" "$IMPACT_CANDIDATES_JSON" "$PROJECTED_IMPACT_JSON" "$IMPACT_SEQUENCE_JSON" "$VISUAL_JSON" "$PROJECTED_IMPACT_HEATMAP" "$VISUAL_HEATMAP" <<'PY'
import json
import sys
from pathlib import Path
from typing import Any

case_dir = Path(sys.argv[1])
route = sys.argv[2]
stock_frame = int(sys.argv[3])
native_frame = int(sys.argv[4])
impact_center_tolerance = float(sys.argv[5])
projection_center_tolerance = float(sys.argv[6])
health_path = Path(sys.argv[7])
framing_path = Path(sys.argv[8])
glass_path = Path(sys.argv[9])
projection_path = Path(sys.argv[10])
impact_candidates_path = Path(sys.argv[11])
projected_impact_path = Path(sys.argv[12])
impact_sequence_path = Path(sys.argv[13])
visual_path = Path(sys.argv[14])
projected_impact_heatmap = Path(sys.argv[15])
visual_heatmap = Path(sys.argv[16])
capture_summary_path = case_dir / f"summary_{route}.json"
out_path = case_dir / "glass_pad10092_impact_visual_summary.json"

failures: list[str] = []

def load(path: Path) -> dict[str, Any]:
    if not path.exists():
        failures.append(f"missing artifact: {path}")
        return {}
    return json.loads(path.read_text(encoding="utf-8"))

def require(condition: bool, message: str) -> None:
    if not condition:
        failures.append(message)

def first_item(value: Any) -> dict[str, Any]:
    return value[0] if isinstance(value, list) and value and isinstance(value[0], dict) else {}

def region_by_name(visual: dict[str, Any], name: str) -> dict[str, Any]:
    for region in visual.get("regions", []):
        if isinstance(region, dict) and region.get("name") == name:
            return region
    failures.append(f"missing visual region: {name}")
    return {}

def destroyed_pad(glass: dict[str, Any], side: str) -> Any:
    props = glass.get(f"{side}_props", {})
    destroyed = props.get("first_destroyed", {})
    prop = destroyed.get("prop", {})
    return prop.get("pad")

capture = load(capture_summary_path)
health = load(health_path)
framing = load(framing_path)
glass = load(glass_path)
projection = load(projection_path)
impact_candidates = load(impact_candidates_path)
projected_impact = load(projected_impact_path)
impact_sequence = load(impact_sequence_path)
visual = load(visual_path)

artifacts = capture.get("artifacts", {})
for key in (
    "native_render_json",
    "native_screenshot",
    "native_screenshot_health_json",
    "native_trace",
    "stock_audit_json",
    "stock_screenshot",
    "stock_screenshot_health_json",
    "stock_trace",
):
    value = artifacts.get(key)
    require(bool(value), f"capture summary missing artifact path: {key}")
    if value:
        require(Path(value).exists(), f"capture artifact does not exist: {key}={value}")

require(capture.get("status") == "pass", "capture summary did not pass")
require(capture.get("exit_status") == 0, "capture exit status is not zero")
require(capture.get("native_render", {}).get("status") == "pass", "native render audit did not pass")
require(capture.get("stock_audit", {}).get("status") == "pass", "stock audit did not pass")
require(capture.get("native_screenshot_health", {}).get("ok") is True, "native screenshot health failed")
require(capture.get("stock_screenshot_health", {}).get("ok") is True, "stock screenshot health failed")

require(health.get("status") == "pass", "health comparison did not pass")
for side in ("baseline_checkpoint", "test_checkpoint"):
    checkpoint = health.get(side, {})
    state = checkpoint.get("health", {})
    glass_state = checkpoint.get("glass", {})
    require(state.get("bond") == 1.0, f"{side} Bond health is not full")
    require(state.get("armor") == 0.0, f"{side} armor is not zero")
    require(state.get("damage_show") == -1, f"{side} damage_show is active")
    require(state.get("health_show") == -1, f"{side} health_show is active")
    require(glass_state.get("active") == 88, f"{side} active shard count is not 88")
    require(glass_state.get("first_timer") == 1, f"{side} first shard timer is not 1")

require(framing.get("status") == "pass", "visual framing comparison did not pass")
framing_deltas = framing.get("deltas", {})
require(
    isinstance(framing_deltas.get("cam_pos"), (int, float))
    and framing_deltas["cam_pos"] <= 0.25,
    "visual framing camera position delta exceeds 0.25",
)
require(
    isinstance(framing_deltas.get("cam_target"), (int, float))
    and framing_deltas["cam_target"] <= 0.25,
    "visual framing camera target delta exceeds 0.25",
)
require(
    isinstance(framing_deltas.get("view"), (int, float))
    and framing_deltas["view"] == 0.0,
    "visual framing viewport differs",
)

require(glass.get("status") == "pass", "glass state comparison did not pass")
require(glass.get("baseline", {}).get("max_active") == 88, "stock max_active is not 88")
require(glass.get("test", {}).get("max_active") == 88, "native max_active is not 88")
require(glass.get("max_active_delta") == 0, "max_active delta is not zero")
require(glass.get("first_position_delta") == 0.0, "first active shard position delta is not zero")
require(glass.get("prop_position_delta") == 0.0, "destroyed prop position delta is not zero")
require(destroyed_pad(glass, "baseline") == 10092, "stock destroyed pad is not 10092")
require(destroyed_pad(glass, "test") == 10092, "native destroyed pad is not 10092")

require(projection.get("status") == "pass", "glass projection comparison did not pass")
require(projection.get("test", {}).get("scale_mode") == "inv_vis_full", "native glass projection scale is not inv_vis_full")
for key in ("active", "projected", "onscreen", "behind"):
    require(projection.get("deltas", {}).get(key) == 0, f"projection {key} delta is not zero")

require(impact_candidates.get("status") == "pass", "impact checkpoint scorer did not compute")
candidate_counts = impact_candidates.get("candidate_counts", {})
require(isinstance(candidate_counts.get("pairs"), int) and candidate_counts["pairs"] > 0, "impact candidate scorer found no pairs")
best_impact = first_item(impact_candidates.get("best"))
require(best_impact.get("impact_identity_match") is True, "best impact candidate identity does not match")
require(best_impact.get("active_delta") == 0.0, "best impact candidate active_delta is not zero")
require(best_impact.get("timer_delta") == 0.0, "best impact candidate timer_delta is not zero")
require(best_impact.get("health_delta") == 0.0, "best impact candidate health_delta is not zero")
require(best_impact.get("hud_delta") == 0.0, "best impact candidate hud_delta is not zero")
require(best_impact.get("visible_delta") == 0, "best impact candidate visible actor set differs")
require(
    isinstance(best_impact.get("impact_center_delta"), (int, float))
    and best_impact["impact_center_delta"] <= impact_center_tolerance,
    f"best impact center delta exceeds {impact_center_tolerance}",
)
require(
    isinstance(best_impact.get("projection_center_delta"), (int, float))
    and best_impact["projection_center_delta"] <= projection_center_tolerance,
    f"best projected impact center delta exceeds {projection_center_tolerance}px",
)

require(projected_impact.get("status") == "pass", "projected impact visual comparator did not pass")
projected_delta = projected_impact.get("projection", {}).get("delta", {})
require(projected_impact.get("selected", {}).get("identity_match") is True, "projected impact selected identity did not match")
require(
    isinstance(projected_delta.get("center_pixels"), (int, float))
    and projected_delta["center_pixels"] <= projection_center_tolerance,
    f"projected impact visual center delta exceeds {projection_center_tolerance}px",
)

require(visual.get("status") == "pass", "actor-masked visual comparison did not pass")
tower = region_by_name(visual, "tower_pane")
impact_side = region_by_name(visual, "impact_side")
projected_region = region_by_name(visual, "projected_impact")

strict_candidates = impact_candidates.get("best_strict")
if not isinstance(strict_candidates, list):
    strict_candidates = []

report = {
    "status": "fail" if failures else "pass",
    "failures": failures,
        "capture_summary": str(capture_summary_path),
        "health_compare": str(health_path),
        "framing_compare": str(framing_path),
        "glass_compare": str(glass_path),
    "projection_compare": str(projection_path),
    "impact_checkpoint_candidates": str(impact_candidates_path),
    "projected_impact_compare": str(projected_impact_path),
    "projected_impact_heatmap": str(projected_impact_heatmap),
    "impact_sequence_compare": str(impact_sequence_path),
    "visual_compare": str(visual_path),
    "visual_heatmap": str(visual_heatmap),
    "geometry_gate": {
        "status": "pass" if not failures else "fail",
        "stock_frame": stock_frame,
        "native_frame": native_frame,
        "impact_center_delta": best_impact.get("impact_center_delta"),
        "impact_center_tolerance": impact_center_tolerance,
        "projection_center_delta": best_impact.get("projection_center_delta"),
        "projection_center_tolerance_px": projection_center_tolerance,
            "projected_visual_center_delta": projected_delta.get("center_pixels"),
        },
        "framing_gate": {
            "status": framing.get("status"),
            "cam_pos_delta": framing_deltas.get("cam_pos"),
            "cam_target_delta": framing_deltas.get("cam_target"),
            "render_cam_pos_delta": framing_deltas.get("render_cam_pos"),
            "render_cam_target_delta": framing_deltas.get("render_cam_target"),
            "cam_up_delta": framing_deltas.get("cam_up"),
            "facing_delta": framing_deltas.get("facing"),
            "room_basis_delta": framing_deltas.get("room_basis"),
            "view_delta": framing_deltas.get("view"),
            "room_set_delta": framing_deltas.get("room_set"),
            "note": (
                "Camera/view/room basis are gated here. Room-set differences are "
                "reported but not gated because stock/native room visibility "
                "telemetry is not yet normalized for this forced-pose route."
            ),
        },
    "checkpoint_candidate_search": {
        "status": "strict_candidate_found" if strict_candidates else "no_strict_candidate",
        "candidate_counts": candidate_counts,
        "best": best_impact,
        "best_strict_count": len(strict_candidates),
        "note": (
            "This route is impact-geometry clean but actor-composition dirty. "
            "The strict candidate count is expected to stay zero until the "
            "route/view/mask removes chr7/chr44 drift."
        ),
    },
    "impact_sequence": {
        "status": impact_sequence.get("status"),
        "match": impact_sequence.get("match"),
        "first_pair_identity_match": impact_sequence.get("first_pair_identity_match"),
        "impact_type_sequence": (impact_sequence.get("sequence") or {}).get("impact"),
        "mismatches": impact_sequence.get("mismatches"),
        "interpretation": impact_sequence.get("interpretation"),
        "note": (
            "Report-only for this seed route. The route gates selected impact "
            "geometry, but screenshot pixel work must account for the full "
            "sampled impact sequence."
        ),
    },
    "glass": {
        "max_active": glass.get("baseline", {}).get("max_active"),
        "first_position_delta": glass.get("first_position_delta"),
        "prop_position_delta": glass.get("prop_position_delta"),
        "destroyed_pad": destroyed_pad(glass, "baseline"),
    },
    "projection": {
        "scale_mode": projection.get("test", {}).get("scale_mode"),
        "stock_max_area_pct": projection.get("baseline", {}).get("max_screen_area_pct"),
        "native_max_area_pct": projection.get("test", {}).get("max_screen_area_pct"),
        "stock_union_area_pct": projection.get("baseline", {}).get("union_screen_area_pct"),
        "native_union_area_pct": projection.get("test", {}).get("union_screen_area_pct"),
        "deltas": projection.get("deltas"),
    },
    "visual": {
        "full_changed_pct": visual.get("full", {}).get("changed_pct"),
        "masked_changed_pct": visual.get("masked", {}).get("changed_pct"),
        "masked_excluded_pct": visual.get("masked", {}).get("excluded_pct"),
        "tower_pane_masked_changed_pct": tower.get("masked", {}).get("changed_pct"),
        "impact_side_masked_changed_pct": impact_side.get("masked", {}).get("changed_pct"),
        "projected_impact_masked_changed_pct": projected_region.get("masked", {}).get("changed_pct"),
        "projected_impact_excluded_pct": projected_region.get("masked", {}).get("excluded_pct"),
    },
}
out_path.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")

if failures:
    print("FAIL: pad10092 impact visual summary audit failed", file=sys.stderr)
    for failure in failures:
        print(f"  - {failure}", file=sys.stderr)
    raise SystemExit(1)

print("PASS: pad10092 impact visual summary audit")
print(
    "  geometry: impact_delta={impact:.3f} projection_delta={projection:.3f}px projected_visual_delta={visual:.3f}px".format(
        impact=float(report["geometry_gate"]["impact_center_delta"]),
        projection=float(report["geometry_gate"]["projection_center_delta"]),
        visual=float(report["geometry_gate"]["projected_visual_center_delta"]),
    )
)
print(
    "  checkpoint_candidate_search=status={status} pairs={pairs} strict={strict}".format(
        status=report["checkpoint_candidate_search"]["status"],
        pairs=candidate_counts.get("pairs"),
        strict=len(strict_candidates),
    )
)
impact_type_sequence = report["impact_sequence"]["impact_type_sequence"] or {}
print(
    "  impact_sequence: match={match} first_pair={first_pair} stock={stock} native={native}".format(
        match=report["impact_sequence"]["match"],
        first_pair=report["impact_sequence"]["first_pair_identity_match"],
        stock=impact_type_sequence.get("baseline"),
        native=impact_type_sequence.get("test"),
    )
)
print(
    "  visual_report: masked={masked:.3f}% projected_impact={projected:.3f}% excluded={excluded:.3f}%".format(
        masked=float(report["visual"]["masked_changed_pct"]),
        projected=float(report["visual"]["projected_impact_masked_changed_pct"]),
        excluded=float(report["visual"]["projected_impact_excluded_pct"]),
    )
)
print(f"  summary={out_path}")
PY

echo "PASS: Dam pad10092 impact visual route is guarded"
echo "artifacts: $OUT_DIR"
