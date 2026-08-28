#!/bin/bash
#
# glass_route_parity_regression.sh -- Guard stock/native Dam glass route parity.
#
# This is the milestone-2 gate after native-only glass material correctness. It
# does not compare screenshots. It proves the stock ares oracle and native route
# hit the same Dam regular-glass pane, create the same active shard count, remove
# the same prop, and preserve the stricter first-sample shard parity on the RNG
# isolation route.
#
set -euo pipefail
cd "$(dirname "$0")/.."

source tools/validation_common.sh

BUILD_DIR="$(validation_default_build_dir)"
BINARY=""
ROM="$(validation_default_rom)"
ARES_BIN=""
DO_BUILD=1
TIMEOUT_SECONDS=180
OUT_DIR="/tmp/mgb64_glass_route_parity_$$"

usage() {
    cat <<'USAGE'
Usage: tools/glass_route_parity_regression.sh [options]

Options:
  --out-dir DIR        output directory (default: /tmp/...)
  --rom PATH           ROM path (default: ./baserom.u.z64)
  --binary PATH        native binary path (default: build/ge007)
  --build-dir DIR      CMake build directory (default: build)
  --ares-bin PATH      instrumented ares binary
  --no-build           reuse an existing native binary
  --timeout SECONDS    per-route timeout (default: 180)

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
ROUTE_WORK_DIR="$OUT_DIR/routes"
mkdir -p "$ROUTE_WORK_DIR"

python3 - "$ROUTE_WORK_DIR" <<'PY'
import json
import sys
from pathlib import Path

route_dir = Path(sys.argv[1])
source_dir = Path("tools/rom_oracle_routes")
for name in (
    "dam_regular_glass_shatter_probe",
    "dam_regular_glass_shatter_rng_isolation_probe",
):
    source = source_dir / f"{name}.json"
    route = json.loads(source.read_text(encoding="utf-8"))
    route["stock_require_first_gameplay_global"] = ""
    (route_dir / f"{name}.json").write_text(
        json.dumps(route, indent=2) + "\n",
        encoding="utf-8",
    )
PY

SHATTER_ROUTE="$ROUTE_WORK_DIR/dam_regular_glass_shatter_probe.json"
RNG_ROUTE="$ROUTE_WORK_DIR/dam_regular_glass_shatter_rng_isolation_probe.json"

echo "=== Glass Route Parity Regression ==="
echo "  out-dir: $OUT_DIR"
echo "  binary:  $BINARY"
echo "  ROM:     $ROM"
echo "  ares:    $ARES_BIN"

run_route() {
    local route="$1"
    local label="$2"
    local case_dir="$OUT_DIR/$label"

    echo "  route: $route"
    rm -rf "$case_dir"
    tools/movement_oracle_capture.sh \
        --route "$route" \
        --out-dir "$case_dir" \
        --rom "$ROM" \
        --binary "$BINARY" \
        --ares-bin "$ARES_BIN" \
        --no-build \
        --timeout "$TIMEOUT_SECONDS"
}

run_route "$SHATTER_ROUTE" shatter_state
run_route "$RNG_ROUTE" rng_isolation

python3 - "$OUT_DIR" <<'PY'
import json
import math
import sys
from pathlib import Path

root = Path(sys.argv[1])
cases = {
    "shatter_state": "dam_regular_glass_shatter_probe",
    "rng_isolation": "dam_regular_glass_shatter_rng_isolation_probe",
}
failures = []
case_summary = {}

def load_json(path):
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)

def require(condition, message):
    if not condition:
        failures.append(message)

def get_prop_pad(compare, side):
    props = compare.get(f"{side}_props", {})
    destroyed = props.get("first_destroyed", {})
    prop = destroyed.get("prop", {})
    return prop.get("pad")

def audit_case(label, route_name, *, require_impact, require_first_sample):
    case_dir = root / label
    summary_path = case_dir / f"summary_{route_name}.json"
    compare_path = case_dir / f"compare_{route_name}.json"
    require(summary_path.exists(), f"{label}: missing summary JSON {summary_path}")
    require(compare_path.exists(), f"{label}: missing compare JSON {compare_path}")
    if not summary_path.exists() or not compare_path.exists():
        return

    capture = load_json(summary_path)
    compare = load_json(compare_path)
    artifacts = capture.get("artifacts", {})
    native_render_path = Path(artifacts.get("native_render_json", ""))
    native_render = load_json(native_render_path) if native_render_path.exists() else {}

    baseline = compare.get("baseline", {})
    test = compare.get("test", {})
    first_sample = compare.get("first_sample", {})
    impact = compare.get("impact", {})
    baseline_props = compare.get("baseline_props", {})
    test_props = compare.get("test_props", {})

    require(capture.get("status") == "pass", f"{label}: capture summary did not pass")
    require(compare.get("status") == "pass", f"{label}: glass comparison did not pass")
    require(native_render.get("status") == "pass", f"{label}: native render audit did not pass")
    require(baseline.get("max_active") == 90, f"{label}: stock max_active != 90")
    require(test.get("max_active") == 90, f"{label}: native max_active != 90")
    require(compare.get("max_active_delta") == 0, f"{label}: max_active_delta != 0")
    require(baseline.get("first_timer") == 1, f"{label}: stock first shard timer != 1")
    require(test.get("first_timer") == 1, f"{label}: native first shard timer != 1")
    require(compare.get("first_position_delta") == 0.0, f"{label}: first shard position delta != 0")
    require(compare.get("prop_position_delta") == 0.0, f"{label}: prop position delta != 0")
    require(baseline_props.get("max_destroyed") == 1, f"{label}: stock destroyed count != 1")
    require(test_props.get("max_destroyed") == 1, f"{label}: native destroyed count != 1")
    require(baseline_props.get("max_remove") == 1, f"{label}: stock remove count != 1")
    require(test_props.get("max_remove") == 1, f"{label}: native remove count != 1")
    require(get_prop_pad(compare, "baseline") == 10004, f"{label}: stock destroyed pad is not 10004")
    require(get_prop_pad(compare, "test") == 10004, f"{label}: native destroyed pad is not 10004")

    if require_impact:
        center_delta = impact.get("position_deltas", {}).get("center")
        require(impact.get("status") == "pass", f"{label}: impact comparison did not pass")
        require(center_delta is not None and center_delta <= 5.0, f"{label}: impact center delta > 5.0")
        fields = impact.get("fields", {})
        require(fields.get("room", {}).get("match") is True, f"{label}: impact room did not match")
        require(fields.get("impact", {}).get("match") is True, f"{label}: impact id did not match")

    if require_first_sample:
        require(first_sample.get("match") is True, f"{label}: first shard sample did not match")
        require(first_sample.get("max_numeric_delta") == 0.0, f"{label}: first shard sample numeric delta != 0")
        require(first_sample.get("mismatch_count") == 0, f"{label}: first shard sample mismatch count != 0")

    case_summary[label] = {
        "route": route_name,
        "summary_json": str(summary_path),
        "compare_json": str(compare_path),
        "stock_first_active": baseline.get("first_active"),
        "native_first_active": test.get("first_active"),
        "stock_max_active": baseline.get("max_active"),
        "native_max_active": test.get("max_active"),
        "first_position_delta": compare.get("first_position_delta"),
        "prop_position_delta": compare.get("prop_position_delta"),
        "impact_center_delta": impact.get("position_deltas", {}).get("center"),
        "first_sample_match": first_sample.get("match"),
        "first_sample_max_numeric_delta": first_sample.get("max_numeric_delta"),
        "stock_destroyed_pad": get_prop_pad(compare, "baseline"),
        "native_destroyed_pad": get_prop_pad(compare, "test"),
        "native_render_status": native_render.get("status"),
    }

audit_case(
    "shatter_state",
    cases["shatter_state"],
    require_impact=True,
    require_first_sample=False,
)
audit_case(
    "rng_isolation",
    cases["rng_isolation"],
    require_impact=False,
    require_first_sample=True,
)

summary = {
    "status": "fail" if failures else "pass",
    "cases": case_summary,
    "failures": failures,
}
(root / "summary.json").write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n")

if failures:
    print("FAIL: glass route parity regression")
    for failure in failures:
        print(f"  - {failure}")
    raise SystemExit(1)

print("PASS: glass route parity regression")
state = case_summary["shatter_state"]
rng = case_summary["rng_isolation"]
print(
    "  shatter_state: active=%d->%d impact_center_delta=%.3f destroyed_pad=%s->%s"
    % (
        state["stock_max_active"],
        state["native_max_active"],
        state["impact_center_delta"],
        state["stock_destroyed_pad"],
        state["native_destroyed_pad"],
    )
)
print(
    "  rng_isolation: active=%d->%d first_sample_match=%s max_delta=%.3f"
    % (
        rng["stock_max_active"],
        rng["native_max_active"],
        rng["first_sample_match"],
        rng["first_sample_max_numeric_delta"],
    )
)
PY

echo "summary_json: $OUT_DIR/summary.json"
echo "artifacts: $OUT_DIR"
