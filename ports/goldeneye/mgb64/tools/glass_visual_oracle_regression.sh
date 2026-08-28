#!/bin/bash
#
# glass_visual_oracle_regression.sh -- Guard the clean Dam static-glass visual oracle.
#
# This is a route/fixture gate, not a renderer-parity gate. It proves the
# stock/native screenshot comparison lane can produce a visual artifact for the
# Dam glass corridor only after the stock origin, health/HUD phase, rendered
# actor composition, render health, and scene sanity checks are clean.
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
OUT_DIR="/tmp/mgb64_glass_visual_oracle_$$"

usage() {
    cat <<'USAGE'
Usage: tools/glass_visual_oracle_regression.sh [options]

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
CASE_DIR="$OUT_DIR/static_glass_visual"

echo "=== Glass Visual Oracle Regression ==="
echo "  out-dir: $OUT_DIR"
echo "  binary:  $BINARY"
echo "  ROM:     $ROM"
echo "  ares:    $ARES_BIN"

rm -rf "$CASE_DIR"
tools/movement_oracle_capture.sh \
    --route dam_glass_visual_probe \
    --out-dir "$CASE_DIR" \
    --rom "$ROM" \
    --binary "$BINARY" \
    --ares-bin "$ARES_BIN" \
    --no-build \
    --timeout "$TIMEOUT_SECONDS"

GFXDL_MATCHES="$CASE_DIR/gfxdl_matches.txt"
if grep -H -F "[GFX-DL]" "$CASE_DIR"/*.log > "$GFXDL_MATCHES"; then
    echo "FAIL: [GFX-DL] warning rows found in visual oracle logs" >&2
    head -20 "$GFXDL_MATCHES" | sed 's/^/  /' >&2
    exit 1
fi
rm -f "$GFXDL_MATCHES"

python3 - "$CASE_DIR" <<'PY'
import json
import sys
from pathlib import Path

root = Path(sys.argv[1])
route = "dam_glass_visual_probe"
summary_path = root / f"summary_{route}.json"
visual_path = root / f"visual_compare_{route}.json"
health_path = root / f"combat_health_compare_{route}.json"
actor_path = root / f"actor_compare_{route}.json"

failures: list[str] = []

def load_json(path: Path) -> dict:
    if not path.exists():
        failures.append(f"missing JSON artifact: {path}")
        return {}
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)

def require(condition: bool, message: str) -> None:
    if not condition:
        failures.append(message)

summary = load_json(summary_path)
visual = load_json(visual_path)
health = load_json(health_path)
actor = load_json(actor_path)

artifacts = summary.get("artifacts", {})
for key in (
    "native_screenshot",
    "native_screenshot_health_json",
    "native_render_json",
    "stock_screenshot",
    "stock_screenshot_health_json",
    "stock_audit_json",
    "health_compare_json",
    "actor_compare_json",
    "visual_compare_json",
    "visual_compare_txt",
    "visual_compare_heatmap",
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

first_gameplay = summary.get("stock_audit", {}).get("first_gameplay_timeline", {})
require(first_gameplay.get("global") == 1146, "stock first gameplay global was not 1146")
require(summary.get("stock_audit", {}).get("max_force_player_applies", 0) >= 5, "stock force-player applications below route minimum")
require(summary.get("stock_audit", {}).get("max_force_player_stan_applies", 0) >= 5, "stock stan resolutions below route minimum")

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
    require(glass.get("active") == 0, f"{side} has active glass shards")

require(actor.get("status") == "pass", "rendered actor composition comparison did not pass")
require(actor.get("baseline", {}).get("max_active") == 0, "stock static route has active glass")
require(actor.get("test", {}).get("max_active") == 0, "native static route has active glass")
require(actor.get("baseline_props", {}).get("max_destroyed") == 0, "stock static route destroyed a glass prop")
require(actor.get("test_props", {}).get("max_destroyed") == 0, "native static route destroyed a glass prop")
expected_chrnums = {10, 11, 12}
seen_chrnums: set[int] = set()
max_actor_delta = 0.0
for item in actor.get("actors", []):
    chrnum = item.get("chrnum")
    seen_chrnums.add(chrnum)
    require(item.get("status") == "pass", f"actor chr {chrnum} comparison did not pass")
    require(item.get("frame_mode") == "screenshot", f"actor chr {chrnum} was not checked at screenshot frame")
    fields = item.get("fields", {})
    for field in ("alive", "rendered"):
        result = fields.get(field, {})
        require(result.get("match") is True, f"actor chr {chrnum} field {field} did not match")
        require(result.get("baseline") == 1 and result.get("test") == 1, f"actor chr {chrnum} field {field} was not active on both sides")
    delta = item.get("position_delta")
    require(isinstance(delta, (int, float)), f"actor chr {chrnum} missing position delta")
    if isinstance(delta, (int, float)):
        max_actor_delta = max(max_actor_delta, float(delta))
        require(delta <= 12.0, f"actor chr {chrnum} position delta exceeds 12.0: {delta}")
require(seen_chrnums == expected_chrnums, f"actor guard chr set mismatch: {sorted(seen_chrnums)}")

require(visual.get("status") == "pass", "visual comparison artifact did not pass")
require(visual.get("pixels", 0) > 250000, "visual comparison did not cover the expected viewport")
changed_pct = float(visual.get("changed_pct", 100.0))
require(changed_pct <= 90.0, f"whole visual changed_pct is above scene-sanity ceiling: {changed_pct:.3f}")

region_limits = {
    "center_glass": 85.0,
    "left_room": 75.0,
    "pp7_hud": 98.0,
}
regions = {region.get("name"): region for region in visual.get("regions", []) if isinstance(region, dict)}
for name, limit in region_limits.items():
    require(name in regions, f"missing visual region: {name}")
    if name in regions:
        pct = float(regions[name].get("changed_pct", 100.0))
        require(pct <= limit, f"region {name} changed_pct {pct:.3f} exceeds {limit:.3f}")

center = regions.get("center_glass", {})
center_features = center.get("features", {})
for side in ("baseline", "test"):
    features = center_features.get(side, {})
    require(features.get("warm_pixels") == 0, f"center_glass {side} has warm pixels")
    require(features.get("bright_pixels") == 0, f"center_glass {side} has bright pixels")

report = {
    "status": "fail" if failures else "pass",
    "failures": failures,
    "capture_summary": str(summary_path),
    "visual_compare": str(visual_path),
    "health_compare": str(health_path),
    "actor_compare": str(actor_path),
    "whole_changed_pct": changed_pct,
    "regions": {
        name: regions[name].get("changed_pct")
        for name in region_limits
        if name in regions
    },
    "actor_max_position_delta": max_actor_delta,
    "stock_first_gameplay_global": first_gameplay.get("global"),
}
(root / "glass_visual_oracle_regression_summary.json").write_text(
    json.dumps(report, indent=2, sort_keys=True) + "\n",
    encoding="utf-8",
)

if failures:
    print("FAIL: clean Dam glass visual oracle audit failed", file=sys.stderr)
    for failure in failures:
        print(f"  - {failure}", file=sys.stderr)
    raise SystemExit(1)

print("PASS: clean Dam glass visual oracle audit")
print(f"  whole_changed_pct={changed_pct:.3f}")
print(
    "  regions="
    + ", ".join(
        f"{name}={regions[name].get('changed_pct'):.3f}"
        for name in region_limits
        if name in regions
    )
)
print(f"  actor_max_position_delta={max_actor_delta:.3f}")
print(f"  summary={root / 'glass_visual_oracle_regression_summary.json'}")
PY

echo "PASS: Dam static-glass visual oracle is clean and repeatable"
echo "artifacts: $OUT_DIR"
