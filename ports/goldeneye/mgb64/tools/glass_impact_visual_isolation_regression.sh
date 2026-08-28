#!/bin/bash
#
# glass_impact_visual_isolation_regression.sh -- Guard the Dam pad-10004
# impact-aligned glass visual checkpoint.
#
# This is the pane/crack/decal companion to the timer-1 active-shard route.
# It proves the screenshot checkpoint has matched health/HUD phase, active shard
# timer, world bullet-impact state, and projection before writing visual metrics.
#
set -euo pipefail
cd "$(dirname "$0")/.."

source tools/validation_common.sh

ROUTE="dam_regular_glass_shatter_rng_impact_visual_probe"
BUILD_DIR="$(validation_default_build_dir)"
BINARY=""
ROM="$(validation_default_rom)"
ARES_BIN=""
DO_BUILD=1
TIMEOUT_SECONDS=240
OUT_DIR="/tmp/mgb64_glass_impact_visual_isolation_$$"

usage() {
    cat <<'USAGE'
Usage: tools/glass_impact_visual_isolation_regression.sh [options]

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
CASE_DIR="$OUT_DIR/impact_visual"
ARES_VIDEO_BLOCKING="${MGB64_ARES_VIDEO_BLOCKING:-true}"
ARES_ROUTE_CONTROL_RETRIES="${MGB64_ARES_STOCK_ROUTE_CONTROL_RETRIES:-8}"

echo "=== Glass Impact Visual Isolation Regression ==="
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
    --timeout "$TIMEOUT_SECONDS"

GFXDL_MATCHES="$CASE_DIR/gfxdl_matches.txt"
if grep -H -F "[GFX-DL]" "$CASE_DIR"/*.log > "$GFXDL_MATCHES"; then
    echo "FAIL: [GFX-DL] warning rows found in impact visual isolation logs" >&2
    head -20 "$GFXDL_MATCHES" | sed 's/^/  /' >&2
    exit 1
fi
rm -f "$GFXDL_MATCHES"

STOCK_TRACE="$CASE_DIR/stock_${ROUTE}.jsonl"
NATIVE_TRACE="$CASE_DIR/native_${ROUTE}.jsonl"
STOCK_SCREENSHOT="$CASE_DIR/stock_${ROUTE}.ppm"
NATIVE_SCREENSHOT="$CASE_DIR/native_${ROUTE}.bmp"
PROJECTION_JSON="$CASE_DIR/projection_${ROUTE}.json"
IMPACT_PIXEL_JSON="$CASE_DIR/impact_pixel_oracle_${ROUTE}.json"
IMPACT_PIXEL_HEATMAP="$CASE_DIR/impact_pixel_oracle_${ROUTE}.png"
PROJECTED_IMPACT_PIXEL_JSON="$CASE_DIR/impact_projected_pixel_oracle_${ROUTE}.json"
PROJECTED_IMPACT_PIXEL_HEATMAP="$CASE_DIR/impact_projected_pixel_oracle_${ROUTE}.png"
CHECKPOINT_CANDIDATES_JSON="$CASE_DIR/impact_checkpoint_candidates_${ROUTE}.json"

python3 tools/compare_glass_projection_trace.py \
    --require-present \
    --baseline-frame 2437 \
    --test-frame 113 \
    --max-active-delta 0 \
    --max-projected-delta 0 \
    --max-onscreen-delta 0 \
    --max-behind-delta 0 \
    --max-max-area-pct-delta 0.10 \
    --max-union-area-pct-delta 1.00 \
    --json-out "$PROJECTION_JSON" \
    "$STOCK_TRACE" \
    "$NATIVE_TRACE"

python3 tools/compare_actor_masked_visual.py \
    --logical-size 320,240 \
    --logical-viewport 0,10,320,220 \
    --baseline-logical-frame active \
    --test-logical-frame full \
    --region impact_focus:220,150,205,130 \
    --region impact_left_unoccluded:220,165,75,90 \
    --region stock_guard_occluder:300,145,120,160 \
    --region hud_viewmodel:300,250,280,170 \
    --exclude-region stock_guard_occluder:300,145,120,160 \
    --exclude-region hud_viewmodel:300,250,280,170 \
    --heatmap "$IMPACT_PIXEL_HEATMAP" \
    --json-out "$IMPACT_PIXEL_JSON" \
    "$STOCK_SCREENSHOT" \
    "$NATIVE_SCREENSHOT"

python3 tools/compare_projected_impact_visual.py \
    --baseline-frame 2437 \
    --test-frame 113 \
    --logical-size 320,240 \
    --logical-viewport 0,10,320,220 \
    --baseline-logical-frame active \
    --test-logical-frame full \
    --padding-logical 8 \
    --exclude-region hud_viewmodel:300,250,280,170 \
    --heatmap "$PROJECTED_IMPACT_PIXEL_HEATMAP" \
    --json-out "$PROJECTED_IMPACT_PIXEL_JSON" \
    "$STOCK_TRACE" \
    "$NATIVE_TRACE" \
    "$STOCK_SCREENSHOT" \
    "$NATIVE_SCREENSHOT" >/dev/null

python3 tools/score_impact_checkpoint_candidates.py \
    --require-active \
    --actor-chrnum 10 \
    --actor-chrnum 12 \
    --top 20 \
    --json-out "$CHECKPOINT_CANDIDATES_JSON" \
    "$STOCK_TRACE" \
    "$NATIVE_TRACE" >/dev/null

python3 - "$CASE_DIR" "$ROUTE" "$PROJECTION_JSON" "$IMPACT_PIXEL_JSON" "$IMPACT_PIXEL_HEATMAP" "$PROJECTED_IMPACT_PIXEL_JSON" "$PROJECTED_IMPACT_PIXEL_HEATMAP" "$CHECKPOINT_CANDIDATES_JSON" <<'PY'
import json
import math
import sys
from pathlib import Path
from typing import Any

root = Path(sys.argv[1])
route = sys.argv[2]
projection_path = Path(sys.argv[3])
impact_pixel_path = Path(sys.argv[4])
impact_pixel_heatmap = Path(sys.argv[5])
projected_impact_pixel_path = Path(sys.argv[6])
projected_impact_pixel_heatmap = Path(sys.argv[7])
checkpoint_candidates_path = Path(sys.argv[8])
summary_path = root / f"summary_{route}.json"
compare_path = root / f"compare_{route}.json"
visual_path = root / f"visual_compare_{route}.json"
health_path = root / f"combat_health_compare_{route}.json"
out_path = root / "glass_impact_visual_isolation_summary.json"
failures: list[str] = []

def load_json(path: Path) -> dict[str, Any]:
    if not path.exists():
        failures.append(f"missing JSON artifact: {path}")
        return {}
    return json.loads(path.read_text(encoding="utf-8"))

def require(condition: bool, message: str) -> None:
    if not condition:
        failures.append(message)

def region_by_name(payload: dict[str, Any], name: str) -> dict[str, Any]:
    for region in payload.get("regions", []) or []:
        if isinstance(region, dict) and region.get("name") == name:
            return region
    return {}

def frame_record(trace_path: Path, frame: int | None) -> dict[str, Any]:
    if frame is None or not trace_path.exists():
        return {}
    with trace_path.open("r", encoding="utf-8") as handle:
        for line in handle:
            if not line.strip():
                continue
            record = json.loads(line)
            if record.get("f") == frame:
                return record
    return {}

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

def world_impact(record: dict[str, Any]) -> dict[str, Any]:
    state = record.get("impact_state")
    if not isinstance(state, dict):
        return {"occupied": 0, "sample": {}}
    sample = state.get("sample")
    if not isinstance(sample, list):
        sample = []
    for item in sample:
        if isinstance(item, dict) and item.get("world"):
            return {"occupied": int(state.get("occupied", 0) or 0), "sample": item}
    return {"occupied": int(state.get("occupied", 0) or 0), "sample": {}}

def impact_brief(item: dict[str, Any]) -> dict[str, Any]:
    return {
        "index": item.get("index"),
        "room": item.get("room"),
        "impact": item.get("impact"),
        "prop": item.get("prop"),
        "prop_pad": item.get("prop_pad"),
        "center": item.get("center"),
        "world_center": item.get("world_center"),
        "projection": item.get("projection"),
    }

def actor_visible(actor: dict[str, Any]) -> bool:
    return bool(actor.get("onscreen") or actor.get("rendered"))

def actor_brief(actor: dict[str, Any]) -> dict[str, Any]:
    return {
        "slot": actor.get("slot"),
        "chrnum": actor.get("chrnum"),
        "action": actor.get("action"),
        "hidden": actor.get("hidden"),
        "hidden_bits": actor.get("hidden_bits"),
        "alive": actor.get("alive"),
        "onscreen": actor.get("onscreen"),
        "rendered": actor.get("rendered"),
        "room": actor.get("room"),
        "dist": actor.get("dist"),
        "pos": actor.get("pos"),
    }

def visible_actor_samples(record: dict[str, Any]) -> list[dict[str, Any]]:
    actors = record.get("actors")
    if not isinstance(actors, dict):
        return []
    sample = actors.get("sample")
    if not isinstance(sample, list):
        return []
    visible = [
        actor_brief(actor)
        for actor in sample
        if isinstance(actor, dict) and actor_visible(actor)
    ]
    return sorted(
        visible,
        key=lambda actor: (
            0 if actor.get("onscreen") else 1,
            actor.get("chrnum") if actor.get("chrnum") is not None else 1_000_000,
            actor.get("slot") if actor.get("slot") is not None else 1_000_000,
        ),
    )

def actor_chr_set(actors: list[dict[str, Any]]) -> list[int]:
    values: list[int] = []
    for actor in actors:
        chrnum = actor.get("chrnum")
        if isinstance(chrnum, int) and chrnum not in values:
            values.append(chrnum)
    return sorted(values)

def actors_by_chrnum(actors: list[dict[str, Any]]) -> dict[int, dict[str, Any]]:
    result: dict[int, dict[str, Any]] = {}
    for actor in actors:
        chrnum = actor.get("chrnum")
        if isinstance(chrnum, int):
            result[chrnum] = actor
    return result

def actor_position_delta(stock_actor: dict[str, Any], native_actor: dict[str, Any]) -> float | None:
    stock_pos = stock_actor.get("pos")
    native_pos = native_actor.get("pos")
    if (
        not isinstance(stock_pos, list)
        or not isinstance(native_pos, list)
        or len(stock_pos) != 3
        or len(native_pos) != 3
        or any(not isinstance(value, (int, float)) for value in stock_pos + native_pos)
    ):
        return None
    return math.sqrt(sum((float(lhs) - float(rhs)) ** 2 for lhs, rhs in zip(stock_pos, native_pos)))

def actor_composition_report(stock_record: dict[str, Any], native_record: dict[str, Any]) -> dict[str, Any]:
    stock_visible = visible_actor_samples(stock_record)
    native_visible = visible_actor_samples(native_record)
    stock_set = actor_chr_set(stock_visible)
    native_set = actor_chr_set(native_visible)
    missing_in_native = [chrnum for chrnum in stock_set if chrnum not in native_set]
    extra_in_native = [chrnum for chrnum in native_set if chrnum not in stock_set]
    stock_by_chr = actors_by_chrnum(stock_visible)
    native_by_chr = actors_by_chrnum(native_visible)
    field_mismatches: list[dict[str, Any]] = []
    position_failures: list[dict[str, Any]] = []
    position_tolerance = 25.0

    for chrnum in sorted(chrnum for chrnum in stock_set if chrnum in native_by_chr):
        stock_actor = stock_by_chr.get(chrnum, {})
        native_actor = native_by_chr.get(chrnum, {})
        for field in ("alive", "hidden", "hidden_bits", "onscreen", "rendered", "action", "room"):
            if stock_actor.get(field) != native_actor.get(field):
                field_mismatches.append({
                    "chrnum": chrnum,
                    "field": field,
                    "stock": stock_actor.get(field),
                    "native": native_actor.get(field),
                })
        delta = actor_position_delta(stock_actor, native_actor)
        if delta is not None and delta > position_tolerance:
            position_failures.append({
                "chrnum": chrnum,
                "delta": delta,
                "stock": stock_actor.get("pos"),
                "native": native_actor.get("pos"),
            })

    status = "clean" if not missing_in_native and not extra_in_native and not field_mismatches and not position_failures else "dirty"
    return {
        "status": status,
        "stock_visible_chrnums": stock_set,
        "native_visible_chrnums": native_set,
        "missing_in_native": missing_in_native,
        "extra_in_native": extra_in_native,
        "field_mismatches": field_mismatches,
        "position_tolerance": position_tolerance,
        "position_failures": position_failures,
        "stock_visible": stock_visible,
        "native_visible": native_visible,
        "note": (
            "Dirty actor composition means broad screenshot changed-pct is not a "
            "production glass pixel oracle. Use route/geometry/projection gates "
            "and add a cleaner actor-free or actor-masked checkpoint before "
            "promoting renderer changes."
            if status == "dirty"
            else "Visible actor composition matches at the screenshot checkpoint."
        ),
    }

def segment_length(a: Any, b: Any) -> float | None:
    return distance3(a, b)

def quad_shape_metrics(vertices: Any) -> dict[str, Any]:
    if (
        not isinstance(vertices, list)
        or len(vertices) != 4
        or any(impact is None for impact in vertices)
    ):
        return {}
    return {
        "edges": [
            segment_length(vertices[0], vertices[1]),
            segment_length(vertices[1], vertices[2]),
            segment_length(vertices[2], vertices[3]),
            segment_length(vertices[3], vertices[0]),
        ],
        "diagonals": [
            segment_length(vertices[0], vertices[2]),
            segment_length(vertices[1], vertices[3]),
        ],
    }

def impact_quad_report(stock_sample: dict[str, Any], native_sample: dict[str, Any]) -> dict[str, Any]:
    stock_center = stock_sample.get("world_center")
    native_center = native_sample.get("world_center")
    stock_vertices = stock_sample.get("world_v")
    native_vertices = native_sample.get("world_v")
    point_deltas: dict[str, float | None] = {
        "center": distance3(stock_center, native_center),
    }
    if isinstance(stock_vertices, list) and isinstance(native_vertices, list):
        for index in range(4):
            stock_vertex = stock_vertices[index] if index < len(stock_vertices) else None
            native_vertex = native_vertices[index] if index < len(native_vertices) else None
            point_deltas[f"v{index}"] = distance3(stock_vertex, native_vertex)

    numeric_deltas = [delta for delta in point_deltas.values() if isinstance(delta, (int, float))]
    max_point_delta = max(numeric_deltas) if numeric_deltas else None
    mean_point_delta = sum(float(delta) for delta in numeric_deltas) / len(numeric_deltas) if numeric_deltas else None

    stock_shape = quad_shape_metrics(stock_vertices)
    native_shape = quad_shape_metrics(native_vertices)
    shape_deltas: dict[str, list[float | None]] = {}
    for key in ("edges", "diagonals"):
        stock_values = stock_shape.get(key)
        native_values = native_shape.get(key)
        if isinstance(stock_values, list) and isinstance(native_values, list):
            shape_deltas[key] = [
                abs(float(lhs) - float(rhs)) if isinstance(lhs, (int, float)) and isinstance(rhs, (int, float)) else None
                for lhs, rhs in zip(stock_values, native_values)
            ]

    return {
        "point_deltas": point_deltas,
        "max_point_delta": max_point_delta,
        "mean_point_delta": mean_point_delta,
        "stock_shape": stock_shape,
        "native_shape": native_shape,
        "shape_deltas": shape_deltas,
    }

def rounding_margin_report(stock_sample: dict[str, Any], native_create: dict[str, Any]) -> dict[str, Any]:
    stock_vertices = stock_sample.get("v")
    native_raw = native_create.get("raw_v")
    native_rounded = native_create.get("rounded_v")
    margins: list[list[Any]] = []
    changed_axis_margins: list[float] = []

    if (
        not isinstance(stock_vertices, list)
        or not isinstance(native_raw, list)
        or not isinstance(native_rounded, list)
    ):
        return {}

    for vertex_index in range(4):
        stock_vertex = stock_vertices[vertex_index] if vertex_index < len(stock_vertices) else None
        raw_vertex = native_raw[vertex_index] if vertex_index < len(native_raw) else None
        rounded_vertex = native_rounded[vertex_index] if vertex_index < len(native_rounded) else None
        axis_margins: list[Any] = []

        if not isinstance(stock_vertex, list) or not isinstance(raw_vertex, list) or not isinstance(rounded_vertex, list):
            margins.append(axis_margins)
            continue

        for axis in range(3):
            if axis >= len(stock_vertex) or axis >= len(raw_vertex) or axis >= len(rounded_vertex):
                axis_margins.append(None)
                continue
            stock_value = stock_vertex[axis]
            raw_value = raw_vertex[axis]
            rounded_value = rounded_vertex[axis]
            if not all(isinstance(value, (int, float)) for value in (stock_value, raw_value, rounded_value)):
                axis_margins.append(None)
                continue

            raw_float = float(raw_value)
            rounded_int = int(rounded_value)
            stock_int = int(stock_value)
            if stock_int > rounded_int:
                margin = (rounded_int + 0.5) - raw_float
                axis_margins.append({"stock_delta": stock_int - rounded_int, "boundary_margin": margin})
                changed_axis_margins.append(abs(margin))
            elif stock_int < rounded_int:
                margin = raw_float - (rounded_int - 0.5)
                axis_margins.append({"stock_delta": stock_int - rounded_int, "boundary_margin": margin})
                changed_axis_margins.append(abs(margin))
            else:
                axis_margins.append({
                    "stock_delta": 0,
                    "low_margin": raw_float - (rounded_int - 0.5),
                    "high_margin": (rounded_int + 0.5) - raw_float,
                })

        margins.append(axis_margins)

    return {
        "stock_v": stock_vertices,
        "native_raw_v": native_raw,
        "native_rounded_v": native_rounded,
        "margins": margins,
        "exact_vertex_match": native_rounded == stock_vertices,
        "changed_axis_count": len(changed_axis_margins),
        "min_changed_axis_margin": min(changed_axis_margins) if changed_axis_margins else None,
        "max_changed_axis_margin": max(changed_axis_margins) if changed_axis_margins else None,
    }

def impact_creation_report(trace_path: Path, stock_sample: dict[str, Any], native_sample: dict[str, Any]) -> dict[str, Any]:
    target_impact = native_sample.get("impact")
    target_room = native_sample.get("room")
    shot_event: dict[str, Any] = {}
    create_event: dict[str, Any] = {}

    if not trace_path.exists() or target_impact is None or target_room is None:
        return {}

    with trace_path.open("r", encoding="utf-8") as handle:
        for line in handle:
            if not line.strip():
                continue
            record = json.loads(line)

            for event in record.get("shot", {}).get("events", []) or []:
                if (
                    isinstance(event, dict)
                    and event.get("phase") == "create_world"
                    and event.get("impact") == target_impact
                    and event.get("best_room") == target_room
                ):
                    shot_event = dict(event)
                    shot_event["trace_frame"] = record.get("f")

            for event in record.get("impact", {}).get("creates", []) or []:
                prop = event.get("prop", {}) if isinstance(event, dict) else {}
                if (
                    isinstance(event, dict)
                    and event.get("impact") == target_impact
                    and event.get("room") == target_room
                    and isinstance(prop, dict)
                    and prop.get("type", -1) == -1
                ):
                    create_event = dict(event)
                    create_event["trace_frame"] = record.get("f")

    return {
        "native_shot": shot_event,
        "native_bullet_create": create_event,
        "rounding_report": rounding_margin_report(stock_sample, create_event),
    }

def impact_identity_matches(sample: Any, target: dict[str, Any]) -> bool:
    if not isinstance(sample, dict) or not isinstance(target, dict):
        return False
    return (
        sample.get("world")
        and sample.get("room") == target.get("room")
        and sample.get("impact") == target.get("impact")
        and sample.get("prop") == target.get("prop")
        and sample.get("prop_pad") == target.get("prop_pad")
    )

def selected_impact_sample(record: dict[str, Any], target: dict[str, Any]) -> dict[str, Any]:
    state = record.get("impact_state")
    if not isinstance(state, dict):
        return {}
    sample = state.get("sample")
    if not isinstance(sample, list):
        return {}
    for item in sample:
        if impact_identity_matches(item, target):
            return item
    return {}

def impact_first_seen_report(trace_path: Path, target: dict[str, Any]) -> dict[str, Any]:
    previous_record: dict[str, Any] = {}
    previous_state: dict[str, Any] = {}

    if not trace_path.exists() or not isinstance(target, dict):
        return {}

    with trace_path.open("r", encoding="utf-8") as handle:
        for line in handle:
            if not line.strip():
                continue
            record = json.loads(line)
            state = record.get("impact_state")
            if not isinstance(state, dict):
                state = {}
            sample = selected_impact_sample(record, target)
            if sample:
                return {
                    "frame": record.get("f"),
                    "global": record.get("move", {}).get("global") if isinstance(record.get("move"), dict) else None,
                    "occupied": state.get("occupied"),
                    "current_slot": state.get("current_slot"),
                    "hash": state.get("hash"),
                    "sample": sample,
                    "previous": {
                        "frame": previous_record.get("f"),
                        "global": previous_record.get("move", {}).get("global") if isinstance(previous_record.get("move"), dict) else None,
                        "occupied": previous_state.get("occupied"),
                        "current_slot": previous_state.get("current_slot"),
                        "hash": previous_state.get("hash"),
                    },
                }
            previous_record = record
            previous_state = state

    return {}

summary = load_json(summary_path)
compare = load_json(compare_path)
visual = load_json(visual_path)
health = load_json(health_path)
projection = load_json(projection_path)
impact_pixel = load_json(impact_pixel_path)
projected_impact_pixel = load_json(projected_impact_pixel_path)
checkpoint_candidates = load_json(checkpoint_candidates_path)
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
require(summary.get("native_render", {}).get("status") == "pass", "native render audit did not pass")
require(summary.get("stock_audit", {}).get("status") == "pass", "stock oracle audit did not pass")
require(compare.get("status") == "pass", "glass/impact pre-pixel comparison did not pass")
require(compare.get("first_sample", {}).get("match") is True, "first active sampled shard does not match")
require(compare.get("first_sample", {}).get("max_numeric_delta") == 0.0, "first sampled shard numeric delta is not zero")
require(compare.get("max_active_delta") == 0, "max active shard delta is not zero")
require(compare.get("first_position_delta") == 0.0, "first active shard position delta is not zero")
require(compare.get("prop_position_delta") == 0.0, "destroyed prop position delta is not zero")
impact_compare = compare.get("impact", {})
require(impact_compare.get("status") == "pass", "impact state comparison did not pass")
impact_center_delta = impact_compare.get("position_deltas", {}).get("center")
require(isinstance(impact_center_delta, (int, float)) and impact_center_delta <= 5.0, "impact center delta exceeds tolerance")

require(health.get("status") == "pass", "health/HUD comparison did not pass")
for side in ("baseline_checkpoint", "test_checkpoint"):
    checkpoint = health.get(side, {})
    state = checkpoint.get("health", {})
    glass = checkpoint.get("glass", {})
    require(state.get("bond") == 1.0, f"{side} Bond health is not full")
    require(state.get("armor") == 0.0, f"{side} armor is not zero")
    require(state.get("damage_show") == -1, f"{side} damage_show is active")
    require(state.get("health_show") == -1, f"{side} health_show is active")
    require(glass.get("active") == 90, f"{side} active shard count is not 90")
    require(glass.get("first_timer") == 6, f"{side} first shard timer is not 6")
    require(glass.get("first_rot_y") == 0.28, f"{side} first shard rot_y is not 0.28")

stock_frame = health.get("baseline_checkpoint", {}).get("frame")
native_frame = health.get("test_checkpoint", {}).get("frame")
stock_trace = Path(artifacts.get("stock_trace", ""))
native_trace = Path(artifacts.get("native_trace", ""))
stock_record = frame_record(stock_trace, stock_frame)
native_record = frame_record(native_trace, native_frame)
actor_composition = actor_composition_report(stock_record, native_record)
stock_impact = world_impact(stock_record)
native_impact = world_impact(native_record)
checkpoint_impact_delta = distance3(
    stock_impact.get("sample", {}).get("world_center"),
    native_impact.get("sample", {}).get("world_center"),
)
quad_report = impact_quad_report(
    stock_impact.get("sample", {}),
    native_impact.get("sample", {}),
)
creation_report = impact_creation_report(
    native_trace,
    stock_impact.get("sample", {}),
    native_impact.get("sample", {}),
)
stock_first_seen = impact_first_seen_report(stock_trace, stock_impact.get("sample", {}))
native_first_seen = impact_first_seen_report(native_trace, native_impact.get("sample", {}))
require(stock_impact.get("occupied") == 1, "stock screenshot checkpoint impact occupancy is not 1")
require(native_impact.get("occupied") == 1, "native screenshot checkpoint impact occupancy is not 1")
require(
    checkpoint_impact_delta is not None and checkpoint_impact_delta <= 5.0,
    "screenshot checkpoint world impact center delta exceeds tolerance",
)
require(bool(creation_report.get("native_shot")), "native create_world shot event is missing from trace")
require(bool(creation_report.get("native_bullet_create", {}).get("raw_v")), "native bullet-impact raw create vertices are missing from trace")
require(
    creation_report.get("native_shot", {}).get("ray", {}).get("valid") == 1,
    "native create_world shot ray is missing from trace",
)
require(bool(stock_first_seen), "stock first-seen world impact report is missing from trace")
require(bool(native_first_seen), "native first-seen world impact report is missing from trace")

require(projection.get("status") == "pass", "glass projection comparison did not pass")
require(projection.get("test", {}).get("scale_mode") == "inv_vis_full", "native projection scale is not inv_vis_full")
for key in ("active", "projected", "onscreen", "behind"):
    require(projection.get("deltas", {}).get(key) == 0, f"projection {key} delta is not zero")

require(impact_pixel.get("status") == "pass", "localized impact pixel oracle did not compute")
require(impact_pixel_heatmap.exists(), f"localized impact pixel heatmap missing: {impact_pixel_heatmap}")
impact_focus = region_by_name(impact_pixel, "impact_focus")
impact_left = region_by_name(impact_pixel, "impact_left_unoccluded")
require(bool(impact_focus), "localized impact pixel oracle missing impact_focus region")
require(bool(impact_left), "localized impact pixel oracle missing impact_left_unoccluded region")
require(projected_impact_pixel.get("status") == "pass", "projected impact pixel oracle did not compute")
require(projected_impact_pixel_heatmap.exists(), f"projected impact pixel heatmap missing: {projected_impact_pixel_heatmap}")
projected_metrics = projected_impact_pixel.get("metrics", {})
projected_region = projected_impact_pixel.get("region", {})
projected_delta = projected_impact_pixel.get("projection", {}).get("delta", {})
projected_selected = projected_impact_pixel.get("selected", {})
require(projected_selected.get("identity_match") is True, "projected impact oracle selected different impact identities")
require(
    isinstance(projected_delta.get("center_pixels"), (int, float)),
    "projected impact oracle missing screen center delta",
)
projected_center_delta = float(projected_delta.get("center_pixels", 1000000.0))
require(
    projected_center_delta <= 1.0,
    f"projected impact screen center delta exceeds tolerance: {projected_center_delta:.3f}px",
)
require(bool(projected_region.get("roi")), "projected impact oracle missing image ROI")
require(
    isinstance(projected_metrics.get("changed_pct"), (int, float)),
    "projected impact oracle missing changed_pct",
)
require(checkpoint_candidates.get("status") == "pass", "impact checkpoint candidate scorer did not compute")
candidate_counts = checkpoint_candidates.get("candidate_counts", {})
require(isinstance(candidate_counts.get("pairs"), int), "impact checkpoint scorer missing pair count")
best_candidates = checkpoint_candidates.get("best") if isinstance(checkpoint_candidates.get("best"), list) else []
best_candidate = best_candidates[0] if best_candidates and isinstance(best_candidates[0], dict) else {}
strict_candidates = checkpoint_candidates.get("best_strict")
if not isinstance(strict_candidates, list):
    strict_candidates = []

require(visual.get("status") == "pass", "visual comparison artifact did not pass")
changed_pct = float(visual.get("changed_pct", 100.0))
masked_pct = float(visual.get("masked", {}).get("changed_pct", 100.0))
regions = {region.get("name"): region for region in visual.get("regions", []) if isinstance(region, dict)}
require(changed_pct <= 92.0, f"whole visual changed_pct is above impact sanity ceiling: {changed_pct:.3f}")
require(masked_pct <= 91.0, f"masked visual changed_pct is above impact sanity ceiling: {masked_pct:.3f}")
for name, limit in (("glass_burst", 90.0), ("damage_arc", 90.0), ("hud_viewmodel", 95.0)):
    require(name in regions, f"missing visual region: {name}")
    if name in regions:
        pct = float(regions[name].get("changed_pct", 100.0))
        require(pct <= limit, f"region {name} changed_pct {pct:.3f} exceeds {limit:.3f}")

focus_masked = impact_focus.get("masked", {}) if isinstance(impact_focus, dict) else {}
focus_full = impact_focus.get("full", {}) if isinstance(impact_focus, dict) else {}
left_masked = impact_left.get("masked", {}) if isinstance(impact_left, dict) else {}
focus_excluded_pct = float(focus_masked.get("excluded_pct", 100.0))
localized_usable = (
    actor_composition.get("status") == "clean"
    and focus_excluded_pct <= 20.0
    and impact_pixel.get("status") == "pass"
)
impact_pixel_status = "candidate" if localized_usable else "masked_dirty"

report = {
    "status": "fail" if failures else "pass",
    "failures": failures,
    "capture_summary": str(summary_path),
    "glass_compare": str(compare_path),
    "health_compare": str(health_path),
    "projection_compare": str(projection_path),
    "visual_compare": str(visual_path),
    "whole_changed_pct": changed_pct,
    "masked_changed_pct": masked_pct,
    "regions": {
        name: regions[name].get("changed_pct")
        for name in ("glass_burst", "damage_arc", "hud_viewmodel")
        if name in regions
    },
    "first_sample": {
        "match": compare.get("first_sample", {}).get("match"),
        "max_numeric_delta": compare.get("first_sample", {}).get("max_numeric_delta"),
        "mismatch_count": compare.get("first_sample", {}).get("mismatch_count"),
    },
    "impact": {
        "compare": impact_compare,
        "checkpoint_center_delta": checkpoint_impact_delta,
        "stock_checkpoint": {
            "frame": stock_frame,
            "impact": impact_brief(stock_impact.get("sample", {})),
        },
        "native_checkpoint": {
            "frame": native_frame,
            "impact": impact_brief(native_impact.get("sample", {})),
        },
        "quad_report": quad_report,
        "creation_report": creation_report,
        "first_seen": {
            "stock": stock_first_seen,
            "native": native_first_seen,
        },
    },
    "visual_oracle": {
        "status": "dirty" if actor_composition.get("status") == "dirty" else "clean",
        "actor_composition": actor_composition,
        "usable_for_production_pixel_fix": actor_composition.get("status") == "clean",
    },
    "impact_pixel_oracle": {
        "status": impact_pixel_status,
        "usable_for_production_pixel_fix": localized_usable,
        "compare": str(impact_pixel_path),
        "heatmap": str(impact_pixel_heatmap),
        "note": (
            "Report-only localized impact/decal pixel oracle. It preserves the "
            "full impact-focus metrics while masking the known stock guard "
            "occluder and HUD/viewmodel. Do not promote renderer changes from "
            "this oracle while actor composition is dirty or the impact-focus "
            "mask exclusion is high."
        ),
        "impact_focus": {
            "roi": impact_focus.get("roi") if isinstance(impact_focus, dict) else None,
            "full_changed_pct": focus_full.get("changed_pct"),
            "masked_changed_pct": focus_masked.get("changed_pct"),
            "masked_excluded_pct": focus_masked.get("excluded_pct"),
            "masked_bright_pixels": {
                "stock": focus_masked.get("features", {}).get("baseline", {}).get("bright_pixels"),
                "native": focus_masked.get("features", {}).get("test", {}).get("bright_pixels"),
            },
        },
        "impact_left_unoccluded": {
            "roi": impact_left.get("roi") if isinstance(impact_left, dict) else None,
            "masked_changed_pct": left_masked.get("changed_pct"),
            "masked_excluded_pct": left_masked.get("excluded_pct"),
            "masked_bright_pixels": {
                "stock": left_masked.get("features", {}).get("baseline", {}).get("bright_pixels"),
                "native": left_masked.get("features", {}).get("test", {}).get("bright_pixels"),
            },
        },
        "excluded_regions": impact_pixel.get("exclude_regions"),
    },
    "projected_impact_pixel_oracle": {
        "status": "geometry_clean_report_only_pixels",
        "compare": str(projected_impact_pixel_path),
        "heatmap": str(projected_impact_pixel_heatmap),
        "usable_for_production_pixel_fix": False,
        "note": (
            "Geometry-derived report-only pixel oracle for the world "
            "bullet-impact decal quad. It uses impact_state.sample[].projection "
            "from stock and native traces to build the screenshot ROI."
        ),
        "selected": projected_selected,
        "region": projected_region,
        "projection_delta": projected_delta,
        "center_delta_tolerance_px": 1.0,
        "changed_pct": projected_metrics.get("changed_pct"),
        "excluded_pct": projected_metrics.get("excluded_pct"),
        "bright_pixels": {
            "stock": projected_metrics.get("features", {}).get("baseline", {}).get("bright_pixels"),
            "native": projected_metrics.get("features", {}).get("test", {}).get("bright_pixels"),
        },
        "near_white_pixels": {
            "stock": projected_metrics.get("features", {}).get("baseline", {}).get("near_white_pixels"),
            "native": projected_metrics.get("features", {}).get("test", {}).get("near_white_pixels"),
        },
    },
    "checkpoint_candidate_search": {
        "status": "strict_candidate_found" if strict_candidates else "no_strict_candidate",
        "compare": str(checkpoint_candidates_path),
        "candidate_counts": candidate_counts,
        "thresholds": checkpoint_candidates.get("thresholds"),
        "best": best_candidate,
        "best_strict_count": len(strict_candidates),
        "note": (
            "Report-only whole-trace search for a cleaner stock/native impact "
            "checkpoint. A strict candidate must satisfy active/timer, health/HUD, "
            "selected world-impact identity, projected impact center, and visible "
            "actor-composition constraints."
        ),
    },
    "projection": {
        "scale_mode": projection.get("test", {}).get("scale_mode"),
        "stock_max_area_pct": projection.get("baseline", {}).get("max_screen_area_pct"),
        "native_max_area_pct": projection.get("test", {}).get("max_screen_area_pct"),
        "stock_union_area_pct": projection.get("baseline", {}).get("union_screen_area_pct"),
        "native_union_area_pct": projection.get("test", {}).get("union_screen_area_pct"),
        "deltas": projection.get("deltas"),
    },
}
out_path.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")

if failures:
    print("FAIL: impact visual isolation audit failed", file=sys.stderr)
    for failure in failures:
        print(f"  - {failure}", file=sys.stderr)
    raise SystemExit(1)

print("PASS: impact visual isolation audit")
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
    "  impact="
    f"center_delta={checkpoint_impact_delta:.3f} "
    f"compare_delta={impact_center_delta:.3f} "
    "quad_max_delta={}".format(
        "unknown"
        if not isinstance(quad_report.get("max_point_delta"), (int, float))
        else f"{quad_report.get('max_point_delta'):.3f}"
    )
)
ray = creation_report.get("native_shot", {}).get("ray", {}).get("world_dir")
min_margin = creation_report.get("rounding_report", {}).get("min_changed_axis_margin")
exact_vertex_match = creation_report.get("rounding_report", {}).get("exact_vertex_match")
shot_frame = creation_report.get("native_shot", {}).get("frame")
create_frame = creation_report.get("native_bullet_create", {}).get("frame")
stock_seen_frame = stock_first_seen.get("frame")
native_seen_frame = native_first_seen.get("frame")
print(
    "  creation="
    "shot_frame={} create_frame={} stock_seen={} native_seen={} ray={} min_changed_margin={}".format(
        shot_frame if shot_frame is not None else "unknown",
        create_frame if create_frame is not None else "unknown",
        stock_seen_frame if stock_seen_frame is not None else "unknown",
        native_seen_frame if native_seen_frame is not None else "unknown",
        "unknown"
        if not isinstance(ray, list)
        else "[" + ",".join(f"{float(value):.6f}" for value in ray[:3]) + "]",
        "matched"
        if exact_vertex_match is True
        else (
            "unknown"
            if not isinstance(min_margin, (int, float))
            else f"{min_margin:.6f}"
        ),
    )
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
    "  visual_oracle="
    "status={status} stock_visible={stock} native_visible={native}".format(
        status=actor_composition.get("status"),
        stock=actor_composition.get("stock_visible_chrnums"),
        native=actor_composition.get("native_visible_chrnums"),
    )
)
print(
    "  impact_pixel_oracle="
    "status={status} focus_masked={focus:.3f}% excluded={excluded:.3f}% "
    "left_unoccluded={left:.3f}% bright={bright0}->{bright1}".format(
        status=impact_pixel_status,
        focus=focus_masked.get("changed_pct", 0.0),
        excluded=focus_masked.get("excluded_pct", 0.0),
        left=left_masked.get("changed_pct", 0.0),
        bright0=left_masked.get("features", {}).get("baseline", {}).get("bright_pixels"),
        bright1=left_masked.get("features", {}).get("test", {}).get("bright_pixels"),
    )
)
print(
    "  projected_impact_pixel_oracle="
    "changed={changed:.3f}% center_delta={center} roi={roi} bright={bright0}->{bright1}".format(
        changed=float(projected_metrics.get("changed_pct", 0.0)),
        center=(
            "unknown"
            if not isinstance(projected_delta.get("center_pixels"), (int, float))
            else f"{float(projected_delta.get('center_pixels')):.3f}px"
        ),
        roi=projected_region.get("roi"),
        bright0=projected_metrics.get("features", {}).get("baseline", {}).get("bright_pixels"),
        bright1=projected_metrics.get("features", {}).get("test", {}).get("bright_pixels"),
    )
)
best_projection_delta = best_candidate.get("projection_center_delta")
print(
    "  checkpoint_candidate_search="
    "status={status} pairs={pairs} strict={strict} best_score={score} "
    "best_projection_delta={projection}".format(
        status="strict_candidate_found" if strict_candidates else "no_strict_candidate",
        pairs=candidate_counts.get("pairs"),
        strict=len(strict_candidates),
        score=(
            "unknown"
            if not isinstance(best_candidate.get("score"), (int, float))
            else f"{float(best_candidate.get('score')):.3f}"
        ),
        projection=(
            "unknown"
            if not isinstance(best_projection_delta, (int, float))
            else f"{float(best_projection_delta):.3f}px"
        ),
    )
)
print(f"  summary={out_path}")
PY

echo "PASS: Dam impact-aligned glass visual route is guarded and repeatable"
echo "artifacts: $OUT_DIR"
