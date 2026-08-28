#!/bin/bash
#
# glass_material_regression.sh -- Guard native-only Dam glass material regressions.
#
# This lane covers three narrow Dam repros:
#   1. a near tinted-glass pane where raw opacity reaches zero
#   2. a prop-attached bullet impact that must render textured by default
#   3. a deterministic gameplay bullet shot that shatters regular Dam glass
#
# The tinted-glass A/B warps near bound pad 10060 and faces pane 10059, proving
# the default low alpha guard is 16 instead of the old cloudy floor of 96. The
# impact A/B proves prop impacts use textured decals by default and that
# GE007_FLAT_PROP_BULLET_IMPACTS remains only a diagnostic fallback. The
# regular-glass shot proves the live bullet path still creates a bounded shatter
# event with active shards and clean render/screenshot health.
#
set -euo pipefail
cd "$(dirname "$0")/.."

source tools/validation_common.sh

BUILD_DIR="$(validation_default_build_dir)"
BINARY=""
ROM="$(validation_default_rom)"
DO_BUILD=1
TIMEOUT_SECONDS=90
OUT_DIR="/tmp/mgb64_glass_material_regression_$$"
FRAMES=135

usage() {
    cat <<'USAGE'
Usage: tools/glass_material_regression.sh [options]

Options:
  --out-dir DIR        output directory (default: /tmp/...)
  --frames N           screenshot/exit frame (default: 135)
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
if (( FRAMES < 135 )); then
    echo "FAIL: --frames must be at least 135 for the full native glass material gate" >&2
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
    local label="$1"
    shift
    local case_dir="$OUT_DIR/$label"
    local trace="$case_dir/state.jsonl"
    local log="$case_dir/run.log"
    local shot="$case_dir/screenshot_${label}.bmp"

    mkdir -p "$case_dir"
    rm -f "$trace" "$log" "$shot" "$case_dir/render.json" "$case_dir/render.txt" \
        "$case_dir/screenshot.json" "$case_dir/screenshot.txt"

    echo "  capture: $label"
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
            --level 33 \
            --deterministic \
            --trace-state "$trace" \
            --screenshot-frame "$FRAMES" \
            --screenshot-label "$label" \
            --screenshot-exit
    ) >"$log" 2>&1; then
        echo "FAIL: capture failed for $label" >&2
        tail -40 "$log" | sed 's/^/  /' >&2
        exit 1
    fi

    if grep -qF "[GEASSERT]" "$log"; then
        echo "FAIL: GEASSERT fired during $label" >&2
        grep -F "[GEASSERT]" "$log" | head -5 | sed 's/^/  /' >&2
        exit 1
    fi
    if grep -qF "[GFX-DL]" "$log"; then
        echo "FAIL: GFX-DL diagnostic rows observed during $label" >&2
        grep -F "[GFX-DL]" "$log" | head -20 | sed 's/^/  /' >&2
        exit 1
    fi
    if [[ ! -s "$trace" ]]; then
        echo "FAIL: missing state trace for $label: $trace" >&2
        exit 1
    fi
    if [[ ! -s "$shot" ]]; then
        echo "FAIL: missing screenshot for $label: $shot" >&2
        tail -40 "$log" | sed 's/^/  /' >&2
        exit 1
    fi

    python3 tools/audit_render_trace.py \
        --label "glass material $label" \
        --json-out "$case_dir/render.json" \
        "$trace" >"$case_dir/render.txt"
    python3 tools/audit_screenshot_health.py \
        --label "glass material $label" \
        --json-out "$case_dir/screenshot.json" \
        "$shot" >"$case_dir/screenshot.txt"
}

auto_aim_dir_script() {
    local start="$1"
    local end="$2"
    local x="$3"
    local y="$4"
    local z="$5"
    local frame
    local sep=""

    for ((frame = start; frame <= end; frame++)); do
        printf "%s%d:%s:%s:%s" "$sep" "$frame" "$x" "$y" "$z"
        sep=","
    done
}

echo "=== Glass Material Regression ==="
echo "  out-dir: $OUT_DIR"
echo "  binary:  $BINARY"
echo "  ROM:     $ROM"
echo "  frames:  $FRAMES"

run_capture tinted_default \
    GE007_TRACE_TINTED_GLASS=1 \
    GE007_TRACE_TINTED_GLASS_BUDGET=6000 \
    GE007_AUTO_WARP_FRAME=40 \
    GE007_AUTO_WARP_PAD=10060 \
    GE007_AUTO_WARP_PAD_FORWARD_OFFSET=400 \
    GE007_AUTO_FACE_COORD_FRAME=45 \
    GE007_AUTO_FACE_COORD_X=15210.81 \
    GE007_AUTO_FACE_COORD_Y=66.34 \
    GE007_AUTO_FACE_COORD_Z=2487.54

run_capture tinted_legacy_floor \
    GE007_TRACE_TINTED_GLASS=1 \
    GE007_TRACE_TINTED_GLASS_BUDGET=6000 \
    GE007_TINTED_GLASS_MIN_OPACITY=96 \
    GE007_AUTO_WARP_FRAME=40 \
    GE007_AUTO_WARP_PAD=10060 \
    GE007_AUTO_WARP_PAD_FORWARD_OFFSET=400 \
    GE007_AUTO_FACE_COORD_FRAME=45 \
    GE007_AUTO_FACE_COORD_X=15210.81 \
    GE007_AUTO_FACE_COORD_Y=66.34 \
    GE007_AUTO_FACE_COORD_Z=2487.54

run_capture prop_impact_default \
    GE007_TRACE_BULLET_IMPACTS=1 \
    GE007_AUTO_WARP_FRAME=40 \
    GE007_AUTO_WARP_PAD=100 \
    GE007_AUTO_FIRE=80:40

run_capture prop_impact_legacy_flat \
    GE007_TRACE_BULLET_IMPACTS=1 \
    GE007_FLAT_PROP_BULLET_IMPACTS=1 \
    GE007_AUTO_WARP_FRAME=40 \
    GE007_AUTO_WARP_PAD=100 \
    GE007_AUTO_FIRE=80:40

run_capture regular_glass_bullet_hit \
    GE007_TRACE_GLASS=1 \
    GE007_TRACE_GLASS_BUDGET=400 \
    GE007_TRACE_SHARDS=1 \
    GE007_TRACE_SHARDS_AFTER_FRAME=100 \
    GE007_EFFECT_TRI_TRACE=1 \
    GE007_EFFECT_TRI_TRACE_LABEL=glass_shards \
    GE007_EFFECT_TRI_TRACE_AFTER_FRAME=100 \
    GE007_EFFECT_TRI_TRACE_BUDGET=500 \
    GE007_EFFECT_TRI_TRACE_DRAWCLASS=effect \
    GE007_EFFECT_TRI_TRACE_EMITS_ONLY=1 \
    GE007_TRACE_TEXGEN_MATERIALS=1 \
    GE007_TRACE_TEXGEN_MATERIALS_EFFECT=glass_shards \
    GE007_TRACE_TEXGEN_MATERIALS_AFTER_FRAME=100 \
    GE007_TRACE_TEXGEN_MATERIALS_BUDGET=200 \
    GE007_TRACE_GLASS_SHARD_COVERAGE=1 \
    GE007_TRACE_GLASS_SHARD_COVERAGE_AFTER_FRAME=100 \
    GE007_TRACE_GLASS_SHARD_COVERAGE_BUDGET=60 \
    GE007_OBJECT_TRACE=1 \
    GE007_OBJECT_TRACE_PAD=10003 \
    GE007_OBJECT_TRACE_BUDGET=700 \
    GE007_AUTO_WARP_FRAME=40 \
    GE007_AUTO_WARP_PAD=103 \
    GE007_AUTO_AIM_DIR_SCRIPT="$(auto_aim_dir_script 70 125 0 0 1)" \
    GE007_AUTO_FIRE=70:5

python3 tools/compare_screenshots.py \
    "$OUT_DIR/tinted_default/screenshot_tinted_default.bmp" \
    "$OUT_DIR/tinted_legacy_floor/screenshot_tinted_legacy_floor.bmp" \
    --json-out "$OUT_DIR/tinted_default_vs_legacy_floor.json" \
    >"$OUT_DIR/tinted_default_vs_legacy_floor.txt"

python3 tools/compare_screenshots.py \
    "$OUT_DIR/prop_impact_default/screenshot_prop_impact_default.bmp" \
    "$OUT_DIR/prop_impact_legacy_flat/screenshot_prop_impact_legacy_flat.bmp" \
    --json-out "$OUT_DIR/prop_impact_default_vs_legacy_flat.json" \
    >"$OUT_DIR/prop_impact_default_vs_legacy_flat.txt"

python3 - "$OUT_DIR" <<'PY'
import json
import re
import sys
from pathlib import Path

root = Path(sys.argv[1])
summary = {"status": "pass", "cases": {}, "failures": []}

def read_json(path):
    return json.loads(path.read_text(encoding="utf-8"))

def require_health(case):
    render = read_json(root / case / "render.json")
    screenshot = read_json(root / case / "screenshot.json")
    screenshot_ok = bool(screenshot.get("ok"))
    summary["cases"].setdefault(case, {})["render_status"] = render.get("status")
    summary["cases"].setdefault(case, {})["screenshot_ok"] = screenshot_ok
    if render.get("status") != "pass":
        summary["failures"].append(f"{case}: render health failed")
    if not screenshot_ok:
        summary["failures"].append(f"{case}: screenshot health failed")

for case in (
    "tinted_default",
    "tinted_legacy_floor",
    "prop_impact_default",
    "prop_impact_legacy_flat",
    "regular_glass_bullet_hit",
):
    require_health(case)

tinted_re = re.compile(
    r"render obj=(?P<obj>-?\d+) pad=(?P<pad>-?\d+) .* "
    r"opacity=(?P<opacity>\d+) renderOpacity=(?P<render>\d+) "
    r"minOpacity=(?P<min>\d+)"
)

def find_tinted(case, expected_min, expected_render):
    log = (root / case / "run.log").read_text(errors="replace")
    matches = []
    for line in log.splitlines():
        if "TINTED_GLASS_TRACE" not in line or " render " not in line:
            continue
        match = tinted_re.search(line)
        if not match:
            continue
        record = {key: int(value) for key, value in match.groupdict().items()}
        if record["pad"] == 10059:
            matches.append(record)

    summary["cases"].setdefault(case, {})["tinted_matches"] = matches[-5:]
    good = [
        record for record in matches
        if record["opacity"] == 0
        and record["render"] == expected_render
        and record["min"] == expected_min
    ]
    if not good:
        summary["failures"].append(
            f"{case}: missing pad 10059 render opacity=0 "
            f"renderOpacity={expected_render} minOpacity={expected_min}"
        )

find_tinted("tinted_default", 16, 16)
find_tinted("tinted_legacy_floor", 96, 96)

def iter_trace_records(case):
    trace = root / case / "state.jsonl"
    with trace.open("r", encoding="utf-8") as handle:
        for line in handle:
            line = line.strip()
            if line:
                yield json.loads(line)

def prop_summary(event):
    prop = event.get("prop")
    return prop if isinstance(prop, dict) else {}

def find_impact(case, expected_flat):
    creates = []
    renders = []

    for record in iter_trace_records(case):
        impact = record.get("impact", {})
        if not isinstance(impact, dict):
            continue
        for event in impact.get("creates", []):
            prop = prop_summary(event)
            if prop.get("obj_type") == 3 and prop.get("pad") == 100:
                creates.append(event)
        for event in impact.get("renders", []):
            prop = prop_summary(event)
            if (
                prop.get("obj_type") == 3
                and prop.get("pad") == 100
                and event.get("flat") == expected_flat
                and event.get("rendered", 0) > 0
            ):
                renders.append(event)

    summary["cases"].setdefault(case, {})["impact_create_count"] = len(creates)
    summary["cases"].setdefault(case, {})["impact_render_count"] = len(renders)
    summary["cases"].setdefault(case, {})["impact_last_render"] = renders[-1] if renders else None

    if not creates:
        summary["failures"].append(f"{case}: no prop pad 100 bullet-impact create event")
    if not renders:
        summary["failures"].append(
            f"{case}: no prop pad 100 bullet-impact render event with flat={expected_flat}"
        )

find_impact("prop_impact_default", 0)
find_impact("prop_impact_legacy_flat", 1)

shatter_re = re.compile(
    r"\[GLASS-SHATTER\] frame=(?P<frame>\d+) .* "
    r"grid=(?P<w>\d+)x(?P<h>\d+)=(?P<pieces>\d+) pieces"
)
shard_re = re.compile(
    r"\[GLASS-SHARD\] frame=(?P<frame>\d+) active=(?P<active>\d+)/(?P<max>\d+)"
)
object_visible_re = re.compile(
    r"\[OBJECT_TRACE\] frame=(?P<frame>\d+) event=visibility .* "
    r"pad=10003 .* room_visible=1"
)
gfx_effect_tri_re = re.compile(
    r"\[(?P<kind>GFX-EMIT-BIG|GFX-SHARD-CANDIDATE)\] "
    r"frame=(?P<frame>\d+) .* effect=(?P<effect>\S+) .* "
    r"ndc_bbox=\[(?P<min_x>-?\d+(?:\.\d+)?),(?P<min_y>-?\d+(?:\.\d+)?)\]-"
    r"\[(?P<max_x>-?\d+(?:\.\d+)?),(?P<max_y>-?\d+(?:\.\d+)?)\] "
    r"area2=(?P<area2>-?\d+(?:\.\d+)?)"
)
effect_tri_re = re.compile(
    r"\[EFFECT-TRI\] frame=(?P<frame>\d+) event=emit label=glass_shards .* "
    r"texwh=\((?P<texwh>[^)]*)\) .* "
    r"raw=0x(?P<raw>[0-9A-Fa-f]+) eff=0x(?P<eff>[0-9A-Fa-f]+) "
    r"omh=0x(?P<omh>[0-9A-Fa-f]+) cc=0x(?P<cc>[0-9A-Fa-f]+) "
    r"geom=0x(?P<geom>[0-9A-Fa-f]+) depth=\((?P<depth>[^)]*)\) .* "
    r"bbox=\[(?P<min_x>-?\d+(?:\.\d+)?),(?P<min_y>-?\d+(?:\.\d+)?)\]-"
    r"\[(?P<max_x>-?\d+(?:\.\d+)?),(?P<max_y>-?\d+(?:\.\d+)?)\] "
    r"area2=(?P<area2>-?\d+(?:\.\d+)?)"
)
shard_coverage_re = re.compile(
    r"\[SHARD-COVERAGE\] frame=(?P<frame>\d+) tris=(?P<tris>\d+) "
    r"raw=0x(?P<raw>[0-9A-Fa-f]+) raw_mismatch=(?P<raw_mismatch>\d+) "
    r"eff=0x(?P<eff>[0-9A-Fa-f]+) eff_mismatch=(?P<eff_mismatch>\d+) "
    r"omh=0x(?P<omh>[0-9A-Fa-f]+) omh_mismatch=(?P<omh_mismatch>\d+) "
    r"cc=0x(?P<cc>[0-9A-Fa-f]+) cc_mismatch=(?P<cc_mismatch>\d+) "
    r"opts=0x(?P<opts>[0-9A-Fa-f]+) opts_mismatch=(?P<opts_mismatch>\d+) "
    r"geom=0x(?P<geom>[0-9A-Fa-f]+) geom_mismatch=(?P<geom_mismatch>\d+) "
    r"mode_decode=\{z=(?P<z>\w+) cvg=(?P<cvg>\w+) aa=(?P<aa>\d+) "
    r"imrd=(?P<imrd>\d+) clr_on_cvg=(?P<clr_on_cvg>\d+) "
    r"cvg_x_alpha=(?P<cvg_x_alpha>\d+) alpha_cvg=(?P<alpha_cvg>\d+) "
    r"force_bl=(?P<force_bl>\d+) .*?\} "
    r"blend=(?P<blend>\w+) blend_mismatch=(?P<blend_mismatch>\d+) "
    r"api_blend=(?P<api_blend>\w+) api_blend_mismatch=(?P<api_blend_mismatch>\d+) "
    r".*? grid=(?P<grid>\d+x\d+) cells=(?P<cells>\d+) "
    r"cell_hits=(?P<cell_hits>\d+) overlap_cells=(?P<overlap_cells>\d+) "
    r"max_cell=(?P<max_cell>\d+) avg_hits=(?P<avg_hits>-?\d+(?:\.\d+)?)"
)

def find_regular_glass_shatter(case):
    log = (root / case / "run.log").read_text(errors="replace")
    shatters = []
    shard_frames = []
    visible_frames = []
    shard_large_tris = []
    shard_pathological_candidates = []
    shard_effect_rows = []
    shard_texgen_rows = []
    shard_coverage_rows = []
    glass_trace_records = 0
    glass_trace_first_active = None
    glass_trace_last_active = None
    glass_trace_max_active = 0
    glass_trace_max_buffer_len = 0

    for line in log.splitlines():
        match = shatter_re.search(line)
        if match:
            shatters.append({key: int(value) for key, value in match.groupdict().items()})
            continue

        match = shard_re.search(line)
        if match:
            shard_frames.append({key: int(value) for key, value in match.groupdict().items()})
            continue

        match = object_visible_re.search(line)
        if match:
            visible_frames.append(int(match.group("frame")))
            continue

        match = gfx_effect_tri_re.search(line)
        if match and match.group("effect") == "glass_shards":
            record = {
                "frame": int(match.group("frame")),
                "min_x": float(match.group("min_x")),
                "min_y": float(match.group("min_y")),
                "max_x": float(match.group("max_x")),
                "max_y": float(match.group("max_y")),
                "area2": float(match.group("area2")),
            }
            record["width"] = record["max_x"] - record["min_x"]
            record["height"] = record["max_y"] - record["min_y"]
            if match.group("kind") == "GFX-EMIT-BIG":
                shard_large_tris.append(record)
            else:
                shard_pathological_candidates.append(record)
            continue

        match = effect_tri_re.search(line)
        if match:
            record = {
                "frame": int(match.group("frame")),
                "raw": match.group("raw").upper(),
                "eff": match.group("eff").upper(),
                "omh": match.group("omh").upper(),
                "cc": match.group("cc").upper(),
                "geom": match.group("geom").upper(),
                "depth": match.group("depth"),
                "texwh": match.group("texwh"),
                "min_x": float(match.group("min_x")),
                "min_y": float(match.group("min_y")),
                "max_x": float(match.group("max_x")),
                "max_y": float(match.group("max_y")),
                "area2": float(match.group("area2")),
            }
            record["width"] = record["max_x"] - record["min_x"]
            record["height"] = record["max_y"] - record["min_y"]
            shard_effect_rows.append(record)
            continue

        match = shard_coverage_re.search(line)
        if match:
            record = {
                "frame": int(match.group("frame")),
                "tris": int(match.group("tris")),
                "raw": match.group("raw").upper(),
                "raw_mismatch": int(match.group("raw_mismatch")),
                "eff": match.group("eff").upper(),
                "eff_mismatch": int(match.group("eff_mismatch")),
                "omh": match.group("omh").upper(),
                "omh_mismatch": int(match.group("omh_mismatch")),
                "cc": match.group("cc").upper(),
                "cc_mismatch": int(match.group("cc_mismatch")),
                "opts": match.group("opts").upper(),
                "opts_mismatch": int(match.group("opts_mismatch")),
                "geom": match.group("geom").upper(),
                "geom_mismatch": int(match.group("geom_mismatch")),
                "z": match.group("z"),
                "cvg": match.group("cvg"),
                "aa": int(match.group("aa")),
                "imrd": int(match.group("imrd")),
                "clr_on_cvg": int(match.group("clr_on_cvg")),
                "cvg_x_alpha": int(match.group("cvg_x_alpha")),
                "alpha_cvg": int(match.group("alpha_cvg")),
                "force_bl": int(match.group("force_bl")),
                "blend": match.group("blend"),
                "blend_mismatch": int(match.group("blend_mismatch")),
                "api_blend": match.group("api_blend"),
                "api_blend_mismatch": int(match.group("api_blend_mismatch")),
                "grid": match.group("grid"),
                "cells": int(match.group("cells")),
                "cell_hits": int(match.group("cell_hits")),
                "overlap_cells": int(match.group("overlap_cells")),
                "max_cell": int(match.group("max_cell")),
                "avg_hits": float(match.group("avg_hits")),
            }
            shard_coverage_rows.append(record)
            continue

        if "[TEXGEN-MATERIAL]" in line and "effect=glass_shards" in line:
            shard_texgen_rows.append(line)

    for record in iter_trace_records(case):
        glass = record.get("glass", {})
        if not isinstance(glass, dict):
            continue
        glass_trace_records += 1
        active = int(glass.get("active", 0) or 0)
        buffer_len = int(glass.get("buffer_len", 0) or 0)
        if buffer_len > glass_trace_max_buffer_len:
            glass_trace_max_buffer_len = buffer_len
        if active > glass_trace_max_active:
            glass_trace_max_active = active
        if active > 0:
            frame = int(record.get("f", 0) or 0)
            if glass_trace_first_active is None:
                glass_trace_first_active = {
                    "frame": frame,
                    "active": active,
                    "next": int(glass.get("next", 0) or 0),
                    "hash": glass.get("hash"),
                    "first": glass.get("first"),
                }
            glass_trace_last_active = {
                "frame": frame,
                "active": active,
                "next": int(glass.get("next", 0) or 0),
                "hash": glass.get("hash"),
                "first": glass.get("first"),
            }

    first = shatters[0] if shatters else None
    max_active = max((record["active"] for record in shard_frames), default=0)
    full_active_frames = 0
    if first is not None:
        full_active_frames = sum(
            1
            for record in shard_frames
            if record["frame"] >= first["frame"] and record["active"] >= first["pieces"]
        )
    max_large_tri = max(shard_large_tris, key=lambda record: record["area2"], default=None)
    max_candidate = max(
        shard_pathological_candidates, key=lambda record: record["area2"], default=None
    )
    max_effect_tri = max(shard_effect_rows, key=lambda record: record["area2"], default=None)
    material_modes = sorted({record["raw"] for record in shard_effect_rows})
    material_effective_modes = sorted({record["eff"] for record in shard_effect_rows})
    material_other_modes_h = sorted({record["omh"] for record in shard_effect_rows})
    material_combiners = sorted({record["cc"] for record in shard_effect_rows})
    material_geometry_modes = sorted({record["geom"] for record in shard_effect_rows})
    material_depth = sorted({record["depth"] for record in shard_effect_rows})
    material_texwh = sorted({record["texwh"] for record in shard_effect_rows})
    material_frames = sorted({record["frame"] for record in shard_effect_rows})
    coverage_raw_modes = sorted({record["raw"] for record in shard_coverage_rows})
    coverage_effective_modes = sorted({record["eff"] for record in shard_coverage_rows})
    coverage_other_modes_h = sorted({record["omh"] for record in shard_coverage_rows})
    coverage_combiners = sorted({record["cc"] for record in shard_coverage_rows})
    coverage_geometry_modes = sorted({record["geom"] for record in shard_coverage_rows})
    coverage_options = sorted({record["opts"] for record in shard_coverage_rows})
    coverage_z_modes = sorted({record["z"] for record in shard_coverage_rows})
    coverage_cvg_modes = sorted({record["cvg"] for record in shard_coverage_rows})
    coverage_blends = sorted({record["blend"] for record in shard_coverage_rows})
    coverage_api_blends = sorted({record["api_blend"] for record in shard_coverage_rows})
    coverage_max_cell = max((record["max_cell"] for record in shard_coverage_rows), default=0)
    coverage_max_overlap_cells = max((record["overlap_cells"] for record in shard_coverage_rows), default=0)
    coverage_max_cells = max((record["cells"] for record in shard_coverage_rows), default=0)
    coverage_max_avg_hits = max((record["avg_hits"] for record in shard_coverage_rows), default=0.0)
    coverage_mismatch_totals = {
        "raw": sum(record["raw_mismatch"] for record in shard_coverage_rows),
        "eff": sum(record["eff_mismatch"] for record in shard_coverage_rows),
        "omh": sum(record["omh_mismatch"] for record in shard_coverage_rows),
        "cc": sum(record["cc_mismatch"] for record in shard_coverage_rows),
        "opts": sum(record["opts_mismatch"] for record in shard_coverage_rows),
        "geom": sum(record["geom_mismatch"] for record in shard_coverage_rows),
        "blend": sum(record["blend_mismatch"] for record in shard_coverage_rows),
        "api_blend": sum(record["api_blend_mismatch"] for record in shard_coverage_rows),
    }

    summary["cases"].setdefault(case, {})["shatter"] = first
    summary["cases"].setdefault(case, {})["shatter_count"] = len(shatters)
    summary["cases"].setdefault(case, {})["shard_frame_count"] = len(shard_frames)
    summary["cases"].setdefault(case, {})["shard_max_active"] = max_active
    summary["cases"].setdefault(case, {})["shard_full_active_frames"] = full_active_frames
    summary["cases"].setdefault(case, {})["pad_10003_visible_frames"] = visible_frames[:5]
    summary["cases"].setdefault(case, {})["glass_shard_large_tri_count"] = len(shard_large_tris)
    summary["cases"].setdefault(case, {})["glass_shard_large_tri_max"] = max_large_tri
    summary["cases"].setdefault(case, {})["glass_shard_candidate_count"] = len(shard_pathological_candidates)
    summary["cases"].setdefault(case, {})["glass_shard_candidate_max"] = max_candidate
    summary["cases"].setdefault(case, {})["glass_shard_effect_material"] = {
        "rows": len(shard_effect_rows),
        "texgen_rows": len(shard_texgen_rows),
        "frames": material_frames[:3] + (["..."] if len(material_frames) > 6 else []) + material_frames[-3:],
        "raw_modes": material_modes,
        "effective_modes": material_effective_modes,
        "other_mode_h": material_other_modes_h,
        "combiners": material_combiners,
        "geometry_modes": material_geometry_modes,
        "depth": material_depth,
        "texwh": material_texwh,
        "max_tri": max_effect_tri,
    }
    summary["cases"].setdefault(case, {})["glass_shard_coverage"] = {
        "rows": len(shard_coverage_rows),
        "raw_modes": coverage_raw_modes,
        "effective_modes": coverage_effective_modes,
        "other_mode_h": coverage_other_modes_h,
        "combiners": coverage_combiners,
        "geometry_modes": coverage_geometry_modes,
        "options": coverage_options,
        "z_modes": coverage_z_modes,
        "cvg_modes": coverage_cvg_modes,
        "blends": coverage_blends,
        "api_blends": coverage_api_blends,
        "max_cells": coverage_max_cells,
        "max_overlap_cells": coverage_max_overlap_cells,
        "max_cell": coverage_max_cell,
        "max_avg_hits": coverage_max_avg_hits,
        "mismatch_totals": coverage_mismatch_totals,
        "sample": shard_coverage_rows[:3],
    }
    summary["cases"].setdefault(case, {})["trace_glass"] = {
        "records": glass_trace_records,
        "first_active": glass_trace_first_active,
        "last_active": glass_trace_last_active,
        "max_active": glass_trace_max_active,
        "max_buffer_len": glass_trace_max_buffer_len,
    }

    if first is None:
        summary["failures"].append(f"{case}: no GLASS-SHATTER event")
        return
    if not (90 <= first["frame"] <= 125):
        summary["failures"].append(
            f"{case}: shatter frame {first['frame']} outside expected 90..125 window"
        )
    if first["pieces"] != 90 or first["w"] != 10 or first["h"] != 9:
        summary["failures"].append(
            f"{case}: expected 10x9=90 shard grid, got "
            f"{first['w']}x{first['h']}={first['pieces']}"
        )
    if full_active_frames < 10:
        summary["failures"].append(
            f"{case}: expected at least 10 full-active shard frames, got {full_active_frames}"
        )
    if glass_trace_records == 0:
        summary["failures"].append(f"{case}: no structured glass trace records")
    if glass_trace_first_active is None:
        summary["failures"].append(f"{case}: structured glass trace never reported active shards")
    elif glass_trace_first_active["frame"] < first["frame"]:
        summary["failures"].append(
            f"{case}: structured glass active frame {glass_trace_first_active['frame']} "
            f"precedes shatter frame {first['frame']}"
        )
    if glass_trace_max_active < first["pieces"]:
        summary["failures"].append(
            f"{case}: structured glass max_active {glass_trace_max_active} "
            f"< shatter pieces {first['pieces']}"
        )
    if not visible_frames:
        summary["failures"].append(f"{case}: pad 10003 was never visible in object trace")
    if shard_pathological_candidates:
        summary["failures"].append(
            f"{case}: saw {len(shard_pathological_candidates)} preclip pathological "
            "glass_shards candidates"
        )
    if max_large_tri and (
        max_large_tri["area2"] > 3.0
        or (max_large_tri["width"] >= 1.95 and max_large_tri["height"] >= 1.95)
    ):
        summary["failures"].append(
            f"{case}: glass_shards emitted screen-spanning triangle "
            f"area2={max_large_tri['area2']:.2f} "
            f"bbox=[{max_large_tri['min_x']:.2f},{max_large_tri['min_y']:.2f}]-"
            f"[{max_large_tri['max_x']:.2f},{max_large_tri['max_y']:.2f}]"
        )
    if len(shard_effect_rows) < 100:
        summary["failures"].append(
            f"{case}: expected at least 100 emitted glass_shards material rows, "
            f"got {len(shard_effect_rows)}"
        )
    if len(shard_texgen_rows) < 20:
        summary["failures"].append(
            f"{case}: expected at least 20 glass_shards TEXGEN-MATERIAL rows, "
            f"got {len(shard_texgen_rows)}"
        )
    if len(shard_coverage_rows) < 10:
        summary["failures"].append(
            f"{case}: expected at least 10 SHARD-COVERAGE rows, got {len(shard_coverage_rows)}"
        )
    if material_modes != ["0C1849D8"] or material_effective_modes != ["0C1849D8"]:
        summary["failures"].append(
            f"{case}: unexpected glass_shards render modes raw={material_modes} "
            f"effective={material_effective_modes}"
        )
    if material_other_modes_h != ["00992C60"]:
        summary["failures"].append(
            f"{case}: unexpected glass_shards other_mode_h values {material_other_modes_h}"
        )
    if material_combiners != ["00F38E4F020A2D12"]:
        summary["failures"].append(
            f"{case}: unexpected glass_shards combiner ids {material_combiners}"
        )
    if material_geometry_modes != ["00060205"]:
        summary["failures"].append(
            f"{case}: unexpected glass_shards geometry modes {material_geometry_modes}"
        )
    if material_depth != ["1,0,1,0,0x800"]:
        summary["failures"].append(
            f"{case}: unexpected glass_shards depth tuple(s) {material_depth}"
        )
    if material_texwh != ["56x54,32x27"]:
        summary["failures"].append(
            f"{case}: unexpected glass_shards texture dimensions {material_texwh}"
        )
    if coverage_raw_modes != ["0C1849D8"] or coverage_effective_modes != ["0C1849D8"]:
        summary["failures"].append(
            f"{case}: unexpected SHARD-COVERAGE render modes raw={coverage_raw_modes} "
            f"effective={coverage_effective_modes}"
        )
    if coverage_other_modes_h != ["00992C60"]:
        summary["failures"].append(
            f"{case}: unexpected SHARD-COVERAGE other_mode_h values {coverage_other_modes_h}"
        )
    if coverage_combiners != ["00F38E4F020A2D12"]:
        summary["failures"].append(
            f"{case}: unexpected SHARD-COVERAGE combiner ids {coverage_combiners}"
        )
    if coverage_geometry_modes != ["00060205"]:
        summary["failures"].append(
            f"{case}: unexpected SHARD-COVERAGE geometry modes {coverage_geometry_modes}"
        )
    if coverage_z_modes != ["xlu"] or coverage_cvg_modes != ["wrap"]:
        summary["failures"].append(
            f"{case}: unexpected SHARD-COVERAGE z/cvg modes z={coverage_z_modes} "
            f"cvg={coverage_cvg_modes}"
        )
    if coverage_blends != ["alpha"]:
        summary["failures"].append(
            f"{case}: unexpected SHARD-COVERAGE frontend blend modes {coverage_blends}"
        )
    if any(
        mode not in {"alpha", "alpha_coverage", "alpha_cvg_wrap_stencil", "alpha_rdp_memory", "alpha_rdp_cvg_memory"}
        for mode in coverage_api_blends
    ):
        summary["failures"].append(
            f"{case}: unexpected SHARD-COVERAGE backend api blend modes {coverage_api_blends}"
        )
    bad_flags = [
        record for record in shard_coverage_rows
        if record["aa"] != 1
        or record["imrd"] != 1
        or record["clr_on_cvg"] != 1
        or record["cvg_x_alpha"] != 0
        or record["alpha_cvg"] != 0
        or record["force_bl"] != 1
    ]
    if bad_flags:
        summary["failures"].append(
            f"{case}: SHARD-COVERAGE rows have unexpected coverage flags; "
            f"first bad row={bad_flags[0]}"
        )
    if any(value != 0 for value in coverage_mismatch_totals.values()):
        summary["failures"].append(
            f"{case}: SHARD-COVERAGE material/blend changed within frame(s): "
            f"{coverage_mismatch_totals}"
        )
    if coverage_max_cell < 2 or coverage_max_overlap_cells <= 0:
        summary["failures"].append(
            f"{case}: SHARD-COVERAGE did not observe overlapping bbox cells "
            f"(max_cell={coverage_max_cell}, max_overlap_cells={coverage_max_overlap_cells})"
        )
    # Stock-matched regular shards can occupy a visible portion of the 320x220
    # viewport. Keep this guard aimed at pathological/full-screen emission,
    # not the old under-scaled native shard envelope.
    if max_effect_tri and (
        max_effect_tri["area2"] > 0.50
        or max_effect_tri["width"] > 1.00
        or max_effect_tri["height"] > 1.00
    ):
        summary["failures"].append(
            f"{case}: emitted glass_shards material triangle unexpectedly large "
            f"area2={max_effect_tri['area2']:.5f} width={max_effect_tri['width']:.3f} "
            f"height={max_effect_tri['height']:.3f}"
        )

find_regular_glass_shatter("regular_glass_bullet_hit")

summary["comparisons"] = {
    "tinted_default_vs_legacy_floor": read_json(root / "tinted_default_vs_legacy_floor.json"),
    "prop_impact_default_vs_legacy_flat": read_json(root / "prop_impact_default_vs_legacy_flat.json"),
}

if summary["failures"]:
    summary["status"] = "fail"

(root / "summary.json").write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n")

if summary["failures"]:
    print("FAIL: glass material regression failed")
    for failure in summary["failures"]:
        print(f"  - {failure}")
    raise SystemExit(1)

print("PASS: glass material regression")
print(
    "  tinted default: opacity=0 renderOpacity=16; "
    "legacy floor: opacity=0 renderOpacity=96"
)
default_impact = summary["cases"]["prop_impact_default"]["impact_last_render"]
legacy_impact = summary["cases"]["prop_impact_legacy_flat"]["impact_last_render"]
print(
    "  prop impacts: default flat=%d rendered=%d; legacy flat=%d rendered=%d"
    % (
        default_impact["flat"],
        default_impact["rendered"],
        legacy_impact["flat"],
        legacy_impact["rendered"],
    )
)
shatter = summary["cases"]["regular_glass_bullet_hit"]["shatter"]
glass_case = summary["cases"]["regular_glass_bullet_hit"]
max_tri = glass_case["glass_shard_large_tri_max"]
material = glass_case["glass_shard_effect_material"]
coverage = glass_case["glass_shard_coverage"]
trace_glass = glass_case["trace_glass"]
print(
    "  regular glass bullet hit: first_shatter_frame=%d pieces=%d "
    "shatters=%d shard_frames=%d max_active=%d shard_large_tris=%d max_area2=%.2f"
    % (
        shatter["frame"],
        shatter["pieces"],
        glass_case["shatter_count"],
        glass_case["shard_frame_count"],
        glass_case["shard_max_active"],
        glass_case["glass_shard_large_tri_count"],
        max_tri["area2"] if max_tri else 0.0,
    )
)
print(
    "  shard material: rows=%d texgen_rows=%d mode=%s cc=%s max_area2=%.5f"
    % (
        material["rows"],
        material["texgen_rows"],
        ",".join(material["raw_modes"]),
        ",".join(material["combiners"]),
        material["max_tri"]["area2"] if material["max_tri"] else 0.0,
    )
)
print(
    "  shard coverage: rows=%d opts=%s api_blend=%s max_cell=%d max_overlap_cells=%d max_avg_hits=%.2f"
    % (
        coverage["rows"],
        ",".join(coverage["options"]),
        ",".join(coverage["api_blends"]),
        coverage["max_cell"],
        coverage["max_overlap_cells"],
        coverage["max_avg_hits"],
    )
)
print(
    "  structured glass trace: first_active_frame=%s max_active=%d buffer_len=%d"
    % (
        trace_glass["first_active"]["frame"] if trace_glass["first_active"] else "none",
        trace_glass["max_active"],
        trace_glass["max_buffer_len"],
    )
)
PY

echo "  summary: $OUT_DIR/summary.json"
