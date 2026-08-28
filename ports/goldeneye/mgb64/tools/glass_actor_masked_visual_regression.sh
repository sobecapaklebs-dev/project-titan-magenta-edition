#!/bin/bash
#
# glass_actor_masked_visual_regression.sh -- Guard the pad10092 actor-light
# active-shard visual fixture.
#
# This is a renderer regression harness, not a strict pixel-parity gate. It
# proves health/glass state at the first-active shard frame, records the known
# actor-composition drift, and compares the tower-pane screenshot through
# explicit actor/viewmodel masks.
#
set -euo pipefail
cd "$(dirname "$0")/.."

source tools/validation_common.sh

ROUTE="dam_regular_glass_shatter_pad10092_actor_masked_visual_probe"
BUILD_DIR="$(validation_default_build_dir)"
BINARY=""
ROM="$(validation_default_rom)"
ARES_BIN=""
DO_BUILD=1
TIMEOUT_SECONDS=240
OUT_DIR="/tmp/mgb64_glass_actor_masked_visual_$$"

usage() {
    cat <<'USAGE'
Usage: tools/glass_actor_masked_visual_regression.sh [options]

Options:
  --out-dir DIR        output directory (default: /tmp/...)
  --rom PATH           ROM path (default: ./baserom.u.z64)
  --binary PATH        native binary path (default: build/ge007)
  --build-dir DIR      CMake build directory (default: build)
  --ares-bin PATH      instrumented ares binary
  --no-build           reuse an existing native binary
  --timeout SECONDS    route timeout (default: 240)

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
CASE_DIR="$OUT_DIR/pad10092_actor_masked"

echo "=== Glass Actor-Masked Visual Regression ==="
echo "  out-dir: $OUT_DIR"
echo "  route:   $ROUTE"
echo "  binary:  $BINARY"
echo "  ROM:     $ROM"
echo "  ares:    $ARES_BIN"

rm -rf "$CASE_DIR"
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
    echo "FAIL: [GFX-DL] warning rows found in actor-masked visual logs" >&2
    head -20 "$GFXDL_MATCHES" | sed 's/^/  /' >&2
    exit 1
fi
rm -f "$GFXDL_MATCHES"

STOCK_TRACE="$CASE_DIR/stock_${ROUTE}.jsonl"
NATIVE_TRACE="$CASE_DIR/native_${ROUTE}.jsonl"
STOCK_SCREENSHOT="$CASE_DIR/stock_${ROUTE}.ppm"
NATIVE_SCREENSHOT="$CASE_DIR/native_${ROUTE}.bmp"
HEALTH_JSON="$CASE_DIR/combat_health_compare_${ROUTE}.json"
GLASS_JSON="$CASE_DIR/compare_${ROUTE}.json"
PROJECTION_JSON="$CASE_DIR/projection_${ROUTE}.json"
ACTOR_SCORE_JSON="$CASE_DIR/actor_composition_score_${ROUTE}.json"
VISUAL_JSON="$CASE_DIR/actor_masked_visual_compare_${ROUTE}.json"
VISUAL_HEATMAP="$CASE_DIR/actor_masked_visual_compare_${ROUTE}.png"

python3 tools/compare_combat_health_trace.py \
    --baseline-label "stock ${ROUTE}" \
    --test-label "native ${ROUTE}" \
    --baseline-frame 2541 \
    --test-frame 126 \
    --health-tolerance 0.001 \
    --damage-show-tolerance 1 \
    --require-match \
    --json-out "$HEALTH_JSON" \
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
    --baseline-frame 2541 \
    --test-frame 126 \
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
python3 tools/score_actor_composition_checkpoints.py \
    --require-active \
    --actor-chrnum 44 \
    --actor-chrnum 7 \
    --fields alive,hidden,onscreen,rendered,action \
    --position-tolerance 25.0 \
    --top 5 \
    --json-out "$ACTOR_SCORE_JSON" \
    "$STOCK_TRACE" \
    "$NATIVE_TRACE"

echo ""
python3 tools/compare_actor_masked_visual.py \
    --logical-size 320,240 \
    --logical-viewport 0,10,320,220 \
    --baseline-logical-frame active \
    --test-logical-frame full \
    --region tower_pane:80,115,320,180 \
    --region impact_side:255,145,120,95 \
    --region lower_actor_cluster:145,160,215,125 \
    --region hud_viewmodel:360,300,255,130 \
    --exclude-region lower_actor_cluster:145,160,215,125 \
    --exclude-region hud_viewmodel:360,300,255,130 \
    --max-masked-excluded-pct 25.0 \
    --max-region-excluded-pct tower_pane=55.0 \
    --heatmap "$VISUAL_HEATMAP" \
    --json-out "$VISUAL_JSON" \
    "$STOCK_SCREENSHOT" \
    "$NATIVE_SCREENSHOT"

python3 - "$CASE_DIR" "$ROUTE" "$HEALTH_JSON" "$GLASS_JSON" "$PROJECTION_JSON" "$ACTOR_SCORE_JSON" "$VISUAL_JSON" <<'PY'
import json
import sys
from pathlib import Path
from typing import Any

case_dir = Path(sys.argv[1])
route = sys.argv[2]
health_path = Path(sys.argv[3])
glass_path = Path(sys.argv[4])
projection_path = Path(sys.argv[5])
actor_path = Path(sys.argv[6])
visual_path = Path(sys.argv[7])
summary_path = case_dir / f"summary_{route}.json"
out_path = case_dir / "glass_actor_masked_visual_summary.json"

failures: list[str] = []

def load(path: Path) -> dict[str, Any]:
    if not path.exists():
        failures.append(f"missing artifact: {path}")
        return {}
    return json.loads(path.read_text(encoding="utf-8"))

def require(condition: bool, message: str) -> None:
    if not condition:
        failures.append(message)

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

capture = load(summary_path)
health = load(health_path)
glass = load(glass_path)
projection = load(projection_path)
actor = load(actor_path)
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
    require(glass_state.get("first_timer", 0) > 0, f"{side} first shard timer is not active")

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

best_actor = (actor.get("best") or [{}])[0]
require(best_actor.get("active_delta") == 0, "best actor pair active_delta is not zero")
require(best_actor.get("health_delta") == 0.0, "best actor pair health_delta is not zero")
require(best_actor.get("timer_delta") == 0.0, "best actor pair timer_delta is not zero")
require(best_actor.get("visible_set_delta") == 0, "best actor pair visible set does not match")

require(visual.get("status") == "pass", "actor-masked visual comparison did not pass")
tower = region_by_name(visual, "tower_pane")
impact = region_by_name(visual, "impact_side")
first_gameplay = capture.get("stock_audit", {}).get("first_gameplay_timeline", {})

report = {
    "status": "fail" if failures else "pass",
    "failures": failures,
    "capture_summary": str(summary_path),
    "health_compare": str(health_path),
    "glass_compare": str(glass_path),
    "projection_compare": str(projection_path),
    "actor_score": str(actor_path),
    "visual_compare": str(visual_path),
    "visual_heatmap": str(case_dir / f"actor_masked_visual_compare_{route}.png"),
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
    "actor_composition": {
        "strict_matches": len(actor.get("best_strict") or []),
        "best_score": best_actor.get("score"),
        "visible_set_delta": best_actor.get("visible_set_delta"),
        "field_mismatches": best_actor.get("field_mismatches"),
        "position_failures": best_actor.get("position_failures"),
        "note": (
            "This fixture is actor-light, not actor-clean. The visual comparator "
            "masks lower_actor_cluster and hud_viewmodel while preserving "
            "unmasked tower_pane/impact_side metrics."
        ),
    },
    "visual": {
        "full_changed_pct": visual.get("full", {}).get("changed_pct"),
        "masked_changed_pct": visual.get("masked", {}).get("changed_pct"),
        "masked_excluded_pct": visual.get("masked", {}).get("excluded_pct"),
        "tower_pane_full_changed_pct": tower.get("full", {}).get("changed_pct"),
        "tower_pane_masked_changed_pct": tower.get("masked", {}).get("changed_pct"),
        "tower_pane_excluded_pct": tower.get("masked", {}).get("excluded_pct"),
        "impact_side_full_changed_pct": impact.get("full", {}).get("changed_pct"),
        "impact_side_masked_changed_pct": impact.get("masked", {}).get("changed_pct"),
    },
    "stock_first_gameplay_global": first_gameplay.get("global"),
}
out_path.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")

if failures:
    print("FAIL: actor-masked glass visual summary audit failed", file=sys.stderr)
    for failure in failures:
        print(f"  - {failure}", file=sys.stderr)
    raise SystemExit(1)

print("PASS: actor-masked glass visual summary audit")
print(
    "  visual: full={:.3f}% masked={:.3f}% tower_masked={:.3f}% impact_masked={:.3f}%".format(
        report["visual"]["full_changed_pct"],
        report["visual"]["masked_changed_pct"],
        report["visual"]["tower_pane_masked_changed_pct"],
        report["visual"]["impact_side_masked_changed_pct"],
    )
)
print(
    "  actor: strict_matches={} best_score={}".format(
        report["actor_composition"]["strict_matches"],
        report["actor_composition"]["best_score"],
    )
)
print(
    "  projection: scale={} max_area={:.3f}%->{:.3f}% union={:.3f}%->{:.3f}%".format(
        report["projection"]["scale_mode"],
        report["projection"]["stock_max_area_pct"],
        report["projection"]["native_max_area_pct"],
        report["projection"]["stock_union_area_pct"],
        report["projection"]["native_union_area_pct"],
    )
)
print(f"  summary={out_path}")
PY

echo "PASS: Dam pad10092 actor-masked active-shard visual route is guarded"
echo "artifacts: $OUT_DIR"
