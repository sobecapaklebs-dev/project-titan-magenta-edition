#!/bin/bash
#
# glass_active_visual_isolation_regression.sh -- Guard active-shard visual isolation.
#
# This is an active-shard renderer-isolation gate, not a final pixel-parity gate.
# It proves the Dam first-active shatter visual route reaches clean stock/native
# control state, exact first sampled shard parity, full-health/no-HUD phase, and
# writes a scoped screenshot comparison artifact.
#
set -euo pipefail
cd "$(dirname "$0")/.."

source tools/validation_common.sh

BUILD_DIR="$(validation_default_build_dir)"
BINARY=""
ROM="$(validation_default_rom)"
ARES_BIN=""
DO_BUILD=1
TIMEOUT_SECONDS=240
OUT_DIR="/tmp/mgb64_glass_active_visual_isolation_$$"
NATIVE_CONFIG_OVERRIDES=()

usage() {
    cat <<'USAGE'
Usage: tools/glass_active_visual_isolation_regression.sh [options]

Options:
  --out-dir DIR        output directory (default: /tmp/...)
  --rom PATH           ROM path (default: ./baserom.u.z64)
  --binary PATH        native binary path (default: build/ge007)
  --build-dir DIR      CMake build directory (default: build)
  --ares-bin PATH      instrumented ares binary
  --no-build           reuse an existing native binary
  --native-config-override KEY=VALUE
                       append a native config override for renderer A/B probes
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
        --native-config-override) NATIVE_CONFIG_OVERRIDES+=("$2"); shift 2 ;;
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
CASE_DIR="$OUT_DIR/active_rng_visual"

echo "=== Glass Active Visual Isolation Regression ==="
echo "  out-dir: $OUT_DIR"
echo "  binary:  $BINARY"
echo "  ROM:     $ROM"
echo "  ares:    $ARES_BIN"

rm -rf "$CASE_DIR"
CAPTURE_ARGS=(
    --route dam_regular_glass_shatter_rng_visual_probe
    --out-dir "$CASE_DIR"
    --rom "$ROM"
    --binary "$BINARY"
    --ares-bin "$ARES_BIN"
    --no-build
)
if [[ "${#NATIVE_CONFIG_OVERRIDES[@]}" -gt 0 ]]; then
    for override in "${NATIVE_CONFIG_OVERRIDES[@]}"; do
        CAPTURE_ARGS+=(--native-config-override "$override")
    done
fi
CAPTURE_ARGS+=(--timeout "$TIMEOUT_SECONDS")
tools/movement_oracle_capture.sh "${CAPTURE_ARGS[@]}"

GFXDL_MATCHES="$CASE_DIR/gfxdl_matches.txt"
if grep -H -F "[GFX-DL]" "$CASE_DIR"/*.log > "$GFXDL_MATCHES"; then
    echo "FAIL: [GFX-DL] warning rows found in active visual isolation logs" >&2
    head -20 "$GFXDL_MATCHES" | sed 's/^/  /' >&2
    exit 1
fi
rm -f "$GFXDL_MATCHES"

ROUTE="dam_regular_glass_shatter_rng_visual_probe"
STOCK_TRACE="$CASE_DIR/stock_${ROUTE}.jsonl"
NATIVE_TRACE="$CASE_DIR/native_${ROUTE}.jsonl"
PROJECTION_JSON="$CASE_DIR/projection_${ROUTE}.json"
PROJECTED_VISUAL_JSON="$CASE_DIR/projected_visual_${ROUTE}.json"
SHARD_PIXEL_ORACLE_JSON="$CASE_DIR/projected_shard_pixel_oracle_${ROUTE}.json"

python3 tools/compare_glass_projection_trace.py \
    --require-present \
    --max-active-delta 0 \
    --max-projected-delta 0 \
    --max-onscreen-delta 0 \
    --max-behind-delta 0 \
    --max-max-area-pct-delta 0.10 \
    --max-union-area-pct-delta 1.00 \
    --json-out "$PROJECTION_JSON" \
    "$STOCK_TRACE" \
    "$NATIVE_TRACE"

python3 tools/compare_glass_projected_visual.py \
    --baseline-trace "$STOCK_TRACE" \
    --test-trace "$NATIVE_TRACE" \
    --baseline-image "$CASE_DIR/stock_${ROUTE}.ppm" \
    --test-image "$CASE_DIR/native_${ROUTE}.bmp" \
    --logical-size 320,240 \
    --logical-viewport 0,10,320,220 \
    --baseline-logical-frame active \
    --test-logical-frame full \
    --projection-padding 4 \
    --max-changed-pct 95.0 \
    --json-out "$PROJECTED_VISUAL_JSON"

python3 tools/compare_glass_shard_pixel_oracle.py \
    --baseline-trace "$STOCK_TRACE" \
    --test-trace "$NATIVE_TRACE" \
    --baseline-image "$CASE_DIR/stock_${ROUTE}.ppm" \
    --test-image "$CASE_DIR/native_${ROUTE}.bmp" \
    --logical-size 320,240 \
    --logical-viewport 0,10,320,220 \
    --baseline-logical-frame active \
    --test-logical-frame full \
    --json-out "$SHARD_PIXEL_ORACLE_JSON"

python3 - "$CASE_DIR" <<'PY'
import json
import math
import sys
from pathlib import Path
from typing import Any

root = Path(sys.argv[1])
route = "dam_regular_glass_shatter_rng_visual_probe"
summary_path = root / f"summary_{route}.json"
compare_path = root / f"compare_{route}.json"
projection_path = root / f"projection_{route}.json"
projected_visual_path = root / f"projected_visual_{route}.json"
shard_pixel_oracle_path = root / f"projected_shard_pixel_oracle_{route}.json"
visual_path = root / f"visual_compare_{route}.json"
health_path = root / f"combat_health_compare_{route}.json"

failures: list[str] = []

def load_json(path: Path) -> dict[str, Any]:
    if not path.exists():
        failures.append(f"missing JSON artifact: {path}")
        return {}
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)

def require(condition: bool, message: str) -> None:
    if not condition:
        failures.append(message)

def first_destroyed_pad(compare: dict[str, Any], side: str) -> Any:
    props = compare.get(f"{side}_props", {})
    destroyed = props.get("first_destroyed", {})
    prop = destroyed.get("prop", {})
    return prop.get("pad")

def frame_record(trace_path: Path, frame: int | None) -> dict[str, Any]:
    if frame is None:
        return {}
    with trace_path.open("r", encoding="utf-8") as handle:
        for line in handle:
            if not line.strip():
                continue
            record = json.loads(line)
            if record.get("f") == frame:
                return record
    return {}

def actor_summary(record: dict[str, Any]) -> dict[str, Any]:
    actors = record.get("actors", {})
    sample = []
    for actor in actors.get("sample", []):
        if not isinstance(actor, dict):
            continue
        if actor.get("onscreen") or actor.get("rendered"):
            sample.append({
                "chrnum": actor.get("chrnum"),
                "action": actor.get("action"),
                "hidden": actor.get("hidden"),
                "onscreen": actor.get("onscreen"),
                "rendered": actor.get("rendered"),
                "pos": actor.get("pos"),
            })
    return {
        "onscreen": actors.get("onscreen"),
        "rendered": actors.get("rendered"),
        "hidden": actors.get("hidden"),
        "visible_sample": sample,
    }

def impact_item_summary(item: Any) -> dict[str, Any]:
    if not isinstance(item, dict):
        return {}
    return {
        "index": item.get("index"),
        "room": item.get("room"),
        "impact": item.get("impact"),
        "world": item.get("world"),
        "prop": item.get("prop"),
        "prop_pad": item.get("prop_pad"),
        "center": item.get("center"),
        "world_center": item.get("world_center"),
    }

def distance3(a: Any, b: Any) -> float | None:
    if (
        not isinstance(a, list)
        or not isinstance(b, list)
        or len(a) != 3
        or len(b) != 3
        or any(not isinstance(value, (int, float)) for value in a + b)
    ):
        return None
    return math.sqrt(sum((float(lhs) - float(rhs)) ** 2 for lhs, rhs in zip(a, b)))

def impact_checkpoint_summary(
    record: dict[str, Any],
    impact: dict[str, Any],
    first_glass_frame: int | None,
) -> dict[str, Any]:
    state = record.get("impact_state", {}) if isinstance(record, dict) else {}
    if not isinstance(state, dict):
        state = {}
    frame = record.get("f") if isinstance(record, dict) else None
    occupied = int(state.get("occupied", 0) or 0)
    sample = state.get("sample")
    if not isinstance(sample, list):
        sample = []
    first_impact_frame = impact.get("first_active")
    first_world_frame = impact.get("first_world_active")
    return {
        "frame": frame,
        "occupied": occupied,
        "sample_count": len(sample),
        "first": impact_item_summary(state.get("first")),
        "sample": [impact_item_summary(item) for item in sample[:2]],
        "first_active_frame": first_impact_frame,
        "first_world_active_frame": first_world_frame,
        "first_active_delta_from_glass": (
            int(first_impact_frame) - int(first_glass_frame)
            if first_impact_frame is not None and first_glass_frame is not None
            else None
        ),
        "first_world_delta_from_glass": (
            int(first_world_frame) - int(first_glass_frame)
            if first_world_frame is not None and first_glass_frame is not None
            else None
        ),
        "checkpoint_delta_from_glass": (
            int(frame) - int(first_glass_frame)
            if frame is not None and first_glass_frame is not None
            else None
        ),
        "max_occupied": impact.get("max_occupied"),
        "max_buffer_len": impact.get("max_buffer_len"),
        "first_sample": impact_item_summary(impact.get("first_sample")),
        "first_world_sample": impact_item_summary(impact.get("first_world_sample")),
    }

summary = load_json(summary_path)
compare = load_json(compare_path)
projection = load_json(projection_path)
projected_visual = load_json(projected_visual_path)
shard_pixel_oracle = load_json(shard_pixel_oracle_path)
visual = load_json(visual_path)
health = load_json(health_path)
artifacts = summary.get("artifacts", {})

for key in (
    "compare_json",
    "health_compare_json",
    "native_render_json",
    "native_screenshot",
    "native_screenshot_health_json",
    "native_trace",
    "stock_audit_json",
    "stock_screenshot",
    "stock_screenshot_health_json",
    "stock_trace",
    "visual_compare_heatmap",
    "visual_compare_json",
    "visual_compare_txt",
):
    value = artifacts.get(key)
    require(bool(value), f"summary missing artifact path: {key}")
    if value:
        require(Path(value).exists(), f"summary artifact path does not exist: {key}={value}")

require(summary.get("status") == "pass", "capture summary did not pass")
require(summary.get("exit_status") == 0, "capture exit status is not zero")
require(summary.get("compare_kind") == "visual", "route did not run as visual compare")
require(summary.get("native_render", {}).get("status") == "pass", "native render audit did not pass")
require(summary.get("stock_audit", {}).get("status") == "pass", "stock oracle audit did not pass")
require(summary.get("native_screenshot_health", {}).get("ok") is True, "native screenshot health failed")
require(summary.get("stock_screenshot_health", {}).get("ok") is True, "stock screenshot health failed")

stock_audit = summary.get("stock_audit", {})
first_gameplay = stock_audit.get("first_gameplay_timeline", {})
require(first_gameplay.get("global") == 1146, "stock first gameplay global was not 1146")
require(stock_audit.get("max_force_player_applies", 0) >= 300, "stock force-player applications below route minimum")
require(stock_audit.get("max_force_player_stan_applies", 0) >= 300, "stock stan resolutions below route minimum")

require(health.get("status") == "pass", "health/HUD comparison did not pass")
comparison = health.get("comparison", {})
require(comparison.get("health_delta") == 0.0, "Bond health delta is not zero")
require(comparison.get("damage_show_delta") == 0, "damage_show delta is not zero")
require(comparison.get("health_show_delta") == 0, "health_show delta is not zero")
for side in ("baseline_checkpoint", "test_checkpoint"):
    checkpoint = health.get(side, {})
    state = checkpoint.get("health", {})
    glass = checkpoint.get("glass", {})
    require(state.get("bond") == 1.0, f"{side} Bond health is not full")
    require(state.get("armor") == 0.0, f"{side} armor is not zero")
    require(state.get("damage_show") == -1, f"{side} damage_show is active")
    require(state.get("health_show") == -1, f"{side} health_show is active")
    require(glass.get("active") == 90, f"{side} active shard count is not 90")
    require(glass.get("first_timer") == 1, f"{side} first shard timer is not 1")
    require(glass.get("first_rot_y") == 0.0, f"{side} first shard rot_y is not 0")

require(compare.get("status") == "pass", "glass state pre-pixel comparison did not pass")
require(compare.get("baseline", {}).get("max_active") == 90, "stock max_active is not 90")
require(compare.get("test", {}).get("max_active") == 90, "native max_active is not 90")
require(compare.get("max_active_delta") == 0, "max_active delta is not zero")
require(compare.get("baseline", {}).get("first_timer") == 1, "stock first active timer is not 1")
require(compare.get("test", {}).get("first_timer") == 1, "native first active timer is not 1")
require(compare.get("first_position_delta") == 0.0, "first active shard position delta is not zero")
require(compare.get("prop_position_delta") == 0.0, "destroyed prop position delta is not zero")
require(first_destroyed_pad(compare, "baseline") == 10004, "stock destroyed pad is not 10004")
require(first_destroyed_pad(compare, "test") == 10004, "native destroyed pad is not 10004")
first_sample = compare.get("first_sample", {})
require(first_sample.get("match") is True, "first active sampled shard does not match")
require(first_sample.get("max_numeric_delta") == 0.0, "first sampled shard numeric delta is not zero")
require(first_sample.get("mismatch_count") == 0, "first sampled shard mismatch count is not zero")

require(projection.get("status") == "pass", "glass projection comparison did not pass")
require(projection.get("test", {}).get("scale_mode") == "inv_vis_full", "native glass projection scale is not inv_vis_full")
for key in ("active", "projected", "onscreen", "behind"):
    require(projection.get("deltas", {}).get(key) == 0, f"projection {key} delta is not zero")

require(projected_visual.get("status") == "pass", "projected glass visual comparison did not pass")
require(shard_pixel_oracle.get("status") == "pass", "projected shard pixel oracle did not pass")

require(visual.get("status") == "pass", "visual comparison artifact did not pass")
changed_pct = float(visual.get("changed_pct", 100.0))
require(changed_pct <= 95.0, f"whole visual changed_pct is above isolation sanity ceiling: {changed_pct:.3f}")
masked = visual.get("masked", {})
masked_pct = float(masked.get("changed_pct", 100.0))
require(masked_pct <= 95.0, f"masked visual changed_pct is above isolation sanity ceiling: {masked_pct:.3f}")
regions = {region.get("name"): region for region in visual.get("regions", []) if isinstance(region, dict)}
for name, limit in (("glass_burst", 99.5), ("damage_arc", 95.0), ("hud_viewmodel", 99.0)):
    require(name in regions, f"missing visual region: {name}")
    if name in regions:
        pct = float(regions[name].get("changed_pct", 100.0))
        require(pct <= limit, f"region {name} changed_pct {pct:.3f} exceeds {limit:.3f}")

stock_trace = Path(artifacts.get("stock_trace", ""))
native_trace = Path(artifacts.get("native_trace", ""))
stock_frame = health.get("baseline_checkpoint", {}).get("frame")
native_frame = health.get("test_checkpoint", {}).get("frame")
stock_record = frame_record(stock_trace, stock_frame) if stock_trace.exists() else {}
native_record = frame_record(native_trace, native_frame) if native_trace.exists() else {}
stock_actors = actor_summary(stock_record)
native_actors = actor_summary(native_record)
stock_impact = impact_checkpoint_summary(
    stock_record,
    compare.get("baseline_impact", {}),
    compare.get("baseline", {}).get("first_active"),
)
native_impact = impact_checkpoint_summary(
    native_record,
    compare.get("test_impact", {}),
    compare.get("test", {}).get("first_active"),
)
stock_world = stock_impact.get("first_world_sample", {}).get("world_center")
native_world = native_impact.get("first_world_sample", {}).get("world_center")
impact_world_center_delta = distance3(stock_world, native_world)
impact_phase_dirty = (
    stock_impact.get("occupied") != native_impact.get("occupied")
    or stock_impact.get("first_active_delta_from_glass")
    != native_impact.get("first_active_delta_from_glass")
    or (
        impact_world_center_delta is not None
        and impact_world_center_delta > 5.0
    )
)
actor_warning = {
    "stock": stock_actors,
    "native": native_actors,
    "note": (
        "Actor composition is intentionally reported, not gated, for this "
        "renderer-isolation route. The pad-10004 pixel-parity route remains "
        "blocked until actor composition is isolated or a cleaner pane/view is selected."
    ),
}

report = {
    "status": "fail" if failures else "pass",
    "failures": failures,
    "capture_summary": str(summary_path),
    "glass_compare": str(compare_path),
    "projection_compare": str(projection_path),
    "projected_visual_compare": str(projected_visual_path),
        "shard_pixel_oracle": str(shard_pixel_oracle_path),
        "visual_compare": str(visual_path),
        "health_compare": str(health_path),
        "impact_lifecycle": {
            "status": "dirty" if impact_phase_dirty else "clean",
            "stock": stock_impact,
            "native": native_impact,
            "checkpoint_occupied_match": stock_impact.get("occupied") == native_impact.get("occupied"),
            "first_active_delta_from_glass_match": (
                stock_impact.get("first_active_delta_from_glass")
                == native_impact.get("first_active_delta_from_glass")
            ),
            "first_world_center_delta": impact_world_center_delta,
            "note": (
                "Reported, not gated. Dirty impact lifecycle means the route is "
                "still valid for shard projection/material containment, but not "
                "for final pane/crack/decal pixel parity."
            ),
        },
        "whole_changed_pct": changed_pct,
        "masked_changed_pct": masked_pct,
        "regions": {
        name: regions[name].get("changed_pct")
        for name in ("glass_burst", "damage_arc", "hud_viewmodel")
        if name in regions
    },
    "first_sample": {
        "match": first_sample.get("match"),
        "max_numeric_delta": first_sample.get("max_numeric_delta"),
        "mismatch_count": first_sample.get("mismatch_count"),
    },
    "projection": {
        "scale_mode": projection.get("test", {}).get("scale_mode"),
        "stock_max_area_pct": projection.get("baseline", {}).get("max_screen_area_pct"),
        "native_max_area_pct": projection.get("test", {}).get("max_screen_area_pct"),
        "stock_union_area_pct": projection.get("baseline", {}).get("union_screen_area_pct"),
        "native_union_area_pct": projection.get("test", {}).get("union_screen_area_pct"),
        "deltas": projection.get("deltas"),
    },
    "projected_visual": {
        "changed_pct": projected_visual.get("projected_roi", {}).get("changed_pct"),
        "roi": projected_visual.get("projected_roi", {}).get("roi"),
        "baseline_bright": projected_visual.get("projected_roi", {}).get("features", {}).get("baseline", {}).get("bright_pixels"),
        "native_bright": projected_visual.get("projected_roi", {}).get("features", {}).get("test", {}).get("bright_pixels"),
        "baseline_mean_rgb": projected_visual.get("projected_roi", {}).get("features", {}).get("baseline", {}).get("mean_rgb"),
        "native_mean_rgb": projected_visual.get("projected_roi", {}).get("features", {}).get("test", {}).get("mean_rgb"),
    },
    "shard_pixel_oracle_metrics": {
        "sample": shard_pixel_oracle.get("sample"),
        "warnings": shard_pixel_oracle.get("warnings"),
        "coverage": shard_pixel_oracle.get("coverage"),
        "union_changed_pct": shard_pixel_oracle.get("union_metrics", {}).get("changed_pct"),
        "union_luma_delta_mean": shard_pixel_oracle.get("union_metrics", {}).get("luma_delta", {}).get("mean"),
        "union_saturation_delta_mean": shard_pixel_oracle.get("union_metrics", {}).get("saturation_delta", {}).get("mean"),
        "union_abs_rgb_delta_mean": shard_pixel_oracle.get("union_metrics", {}).get("abs_rgb_delta", {}).get("mean"),
        "baseline_buckets": shard_pixel_oracle.get("union_metrics", {}).get("baseline_buckets"),
        "native_buckets": shard_pixel_oracle.get("union_metrics", {}).get("test_buckets"),
        "strongest_piece": (shard_pixel_oracle.get("top_abs_rgb_delta") or [None])[0],
    },
    "stock_first_gameplay_global": first_gameplay.get("global"),
    "actor_composition": actor_warning,
}
(root / "glass_active_visual_isolation_summary.json").write_text(
    json.dumps(report, indent=2, sort_keys=True) + "\n",
    encoding="utf-8",
)

if failures:
    print("FAIL: active-shard visual isolation audit failed", file=sys.stderr)
    for failure in failures:
        print(f"  - {failure}", file=sys.stderr)
    raise SystemExit(1)

print("PASS: active-shard visual isolation audit")
print(f"  whole_changed_pct={changed_pct:.3f}")
print(f"  masked_changed_pct={masked_pct:.3f}")
print(
    "  regions="
    + ", ".join(
        f"{name}={regions[name].get('changed_pct'):.3f}"
        for name in ("glass_burst", "damage_arc", "hud_viewmodel")
        if name in regions
    )
)
print(
    "  first_sample="
    f"match={first_sample.get('match')} max_delta={first_sample.get('max_numeric_delta')}"
)
print(
    "  projection="
    "scale={scale} max_area={stock:.3f}%->{native:.3f}% union={stock_union:.3f}%->{native_union:.3f}%".format(
        scale=projection.get("test", {}).get("scale_mode"),
        stock=projection.get("baseline", {}).get("max_screen_area_pct"),
        native=projection.get("test", {}).get("max_screen_area_pct"),
        stock_union=projection.get("baseline", {}).get("union_screen_area_pct"),
        native_union=projection.get("test", {}).get("union_screen_area_pct"),
    )
)
print(
    "  projected_visual="
    "changed={changed:.3f}% bright={bright0}->{bright1}".format(
        changed=projected_visual.get("projected_roi", {}).get("changed_pct"),
        bright0=projected_visual.get("projected_roi", {}).get("features", {}).get("baseline", {}).get("bright_pixels"),
        bright1=projected_visual.get("projected_roi", {}).get("features", {}).get("test", {}).get("bright_pixels"),
    )
)
print(
    "  shard_pixel_oracle="
    "sample={common}/{active} coverage={coverage} overlap={overlap:.1f}% changed={changed:.3f}%".format(
        common=shard_pixel_oracle.get("sample", {}).get("common_indices"),
        active=shard_pixel_oracle.get("baseline", {}).get("active"),
        coverage=shard_pixel_oracle.get("coverage", {}).get("pixels"),
        overlap=shard_pixel_oracle.get("coverage", {}).get("overlap_pct"),
        changed=shard_pixel_oracle.get("union_metrics", {}).get("changed_pct"),
    )
)
print(
    "  impact_lifecycle="
    "status={status} checkpoint_occ={stock_occ}->{native_occ} "
    "first_delta_from_glass={stock_delta}->{native_delta} world_center_delta={world_delta}".format(
        status="dirty" if impact_phase_dirty else "clean",
        stock_occ=stock_impact.get("occupied"),
        native_occ=native_impact.get("occupied"),
        stock_delta=stock_impact.get("first_active_delta_from_glass"),
        native_delta=native_impact.get("first_active_delta_from_glass"),
        world_delta=(
            f"{impact_world_center_delta:.3f}"
            if impact_world_center_delta is not None
            else "n/a"
        ),
    )
)
print(f"  summary={root / 'glass_active_visual_isolation_summary.json'}")
PY

echo "PASS: Dam active-shard visual isolation route is guarded and repeatable"
echo "artifacts: $OUT_DIR"
