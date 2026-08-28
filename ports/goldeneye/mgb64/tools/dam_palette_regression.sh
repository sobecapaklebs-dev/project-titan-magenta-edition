#!/bin/bash
#
# dam_palette_regression.sh -- Guard Dam paletted material color regressions.
#
# This lane targets the gray guard-face/uniform and missing red/tan color class.
# It captures Dam near live guards, dumps the CI settex textures used by those
# character materials, audits both the texture pipeline log and decoded RGB, and
# captures a close view of a Dam alarm lens to guard the missing red-circle case.
#
set -euo pipefail
cd "$(dirname "$0")/.."

source tools/validation_common.sh

BUILD_DIR="$(validation_default_build_dir)"
BINARY=""
ROM="$(validation_default_rom)"
DO_BUILD=1
TIMEOUT_SECONDS=90
OUT_DIR="/tmp/mgb64_dam_palette_regression_$$"
FRAMES=80
COLOR_TEX_IDS="1167,1370,1371,1959,1961,1828,1965,1631,1784,1783"

usage() {
    cat <<'USAGE'
Usage: tools/dam_palette_regression.sh [options]

Options:
  --out-dir DIR        output directory (default: /tmp/...)
  --frames N           screenshot/exit frame (default: 80)
  --rom PATH           ROM path (default: ./baserom.u.z64)
  --binary PATH        native binary path (default: build/ge007)
  --build-dir DIR      CMake build directory (default: build)
  --no-build           reuse an existing native binary
  --timeout SECONDS    capture timeout (default: 90)

Artifacts are ROM-derived local validation data. Do not commit captured traces,
screenshots, logs, or texture dumps.
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
if (( FRAMES < 80 )); then
    echo "FAIL: --frames must be at least 80 for the guard and alarm color probes" >&2
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

CASE_DIR="$OUT_DIR/guard_ci"
SAVE_DIR="$CASE_DIR/save"
DUMP_DIR="$CASE_DIR/settex_dumps"
TRACE="$CASE_DIR/state.jsonl"
LOG="$CASE_DIR/run.log"
SHOT="$CASE_DIR/screenshot_guard_ci.bmp"
ALARM_CASE_DIR="$OUT_DIR/alarm_red"
ALARM_SAVE_DIR="$ALARM_CASE_DIR/save"
ALARM_TRACE="$ALARM_CASE_DIR/state.jsonl"
ALARM_LOG="$ALARM_CASE_DIR/run.log"
ALARM_SHOT="$ALARM_CASE_DIR/screenshot_alarm_red.bmp"
SUMMARY="$OUT_DIR/summary.json"

rm -rf "$CASE_DIR" "$ALARM_CASE_DIR"
mkdir -p "$SAVE_DIR" "$DUMP_DIR" "$ALARM_SAVE_DIR"

echo "=== Dam Palette Regression ==="
echo "  out-dir: $OUT_DIR"
echo "  binary:  $BINARY"
echo "  ROM:     $ROM"
echo "  frames:  $FRAMES"

if ! (
    cd "$CASE_DIR"
    validation_run_with_timeout "$TIMEOUT_SECONDS" \
        env -u GE007_DEBUG \
            SDL_AUDIODRIVER="${GE007_VALIDATION_SDL_AUDIODRIVER:-dummy}" \
            GE007_MUTE=1 \
            GE007_DETERMINISTIC_STABLE_COUNT=1 \
            GE007_NO_VSYNC=1 \
            GE007_BACKGROUND=1 \
            GE007_NO_INPUT_GRAB=1 \
            GE007_VERBOSE=1 \
            GE007_DISABLE_LEVEL_INTRO=1 \
            GE007_TRACE_TEX_PIPELINE=1 \
            GE007_TRACE_TEXSELECT=1 \
            GE007_DUMP_SETTEX_TEXTURES="$COLOR_TEX_IDS" \
            GE007_DUMP_SETTEX_DIR="$DUMP_DIR" \
            GE007_AUTO_WARP_CHR_FRAME=20 \
            GE007_AUTO_WARP_CHRNUM=0 \
            GE007_AUTO_WARP_CHR_DISTANCE=250 \
            GE007_AUTO_WARP_CHR_ANGLE=0 \
            GE007_TRACE_CHRNUM=0 \
            "$BINARY" \
            --savedir "$SAVE_DIR" \
            --rom "$ROM" \
            --level 33 \
            --deterministic \
            --trace-state "$TRACE" \
            --screenshot-frame "$FRAMES" \
            --screenshot-label guard_ci \
            --screenshot-exit
) >"$LOG" 2>&1; then
    echo "FAIL: Dam palette capture failed" >&2
    tail -80 "$LOG" | sed 's/^/  /' >&2
    exit 1
fi

if grep -qF "[GEASSERT]" "$LOG"; then
    echo "FAIL: GEASSERT fired during Dam palette capture" >&2
    grep -F "[GEASSERT]" "$LOG" | head -5 | sed 's/^/  /' >&2
    exit 1
fi
if grep -qF "[GFX-DL]" "$LOG"; then
    echo "FAIL: GFX-DL diagnostic rows observed" >&2
    grep -F "[GFX-DL]" "$LOG" | head -20 | sed 's/^/  /' >&2
    exit 1
fi
# The CI grayscale fallback (gfx_pc.c) fires SILENTLY on a paletted texture whose
# TLUT was never registered — the FID-0122 guard-face/alarm/door class. The
# capture runs with GE007_VERBOSE=1, which arms the [CI-GRAYSCALE-FALLBACK] log,
# so any occurrence is a real regression. (The older "No palette" strings were
# code comments only and never matched — kept here for belt-and-suspenders.)
if grep -Eq "CI-GRAYSCALE-FALLBACK|No palette|fallback to grayscale" "$LOG"; then
    echo "FAIL: a paletted (CI) texture fell back to grayscale (unregistered TLUT)" >&2
    grep -En "CI-GRAYSCALE-FALLBACK|No palette|fallback to grayscale" "$LOG" | head -20 | sed 's/^/  /' >&2
    exit 1
fi
if [[ ! -s "$TRACE" ]]; then
    echo "FAIL: missing state trace: $TRACE" >&2
    exit 1
fi
if [[ ! -s "$SHOT" ]]; then
    echo "FAIL: missing screenshot: $SHOT" >&2
    tail -80 "$LOG" | sed 's/^/  /' >&2
    exit 1
fi

python3 tools/audit_screenshot_health.py \
    --label "Dam palette guard CI" \
    --json-out "$CASE_DIR/screenshot.json" \
    "$SHOT" >"$CASE_DIR/screenshot.txt"

if ! (
    cd "$ALARM_CASE_DIR"
    validation_run_with_timeout "$TIMEOUT_SECONDS" \
        env -u GE007_DEBUG \
            SDL_AUDIODRIVER="${GE007_VALIDATION_SDL_AUDIODRIVER:-dummy}" \
            GE007_MUTE=1 \
            GE007_DETERMINISTIC_STABLE_COUNT=1 \
            GE007_NO_VSYNC=1 \
            GE007_BACKGROUND=1 \
            GE007_NO_INPUT_GRAB=1 \
            GE007_DISABLE_LEVEL_INTRO=1 \
            GE007_AUTO_FORCE_PLAYER_SCRIPT="40-${FRAMES}:5729.95:81.32:-3321.35:90:0:167.31:10070" \
            "$BINARY" \
            --savedir "$ALARM_SAVE_DIR" \
            --rom "$ROM" \
            --level 33 \
            --deterministic \
            --trace-state "$ALARM_TRACE" \
            --screenshot-frame "$FRAMES" \
            --screenshot-label alarm_red \
            --screenshot-exit
) >"$ALARM_LOG" 2>&1; then
    echo "FAIL: Dam alarm color capture failed" >&2
    tail -80 "$ALARM_LOG" | sed 's/^/  /' >&2
    exit 1
fi

if grep -qF "[GEASSERT]" "$ALARM_LOG"; then
    echo "FAIL: GEASSERT fired during Dam alarm color capture" >&2
    grep -F "[GEASSERT]" "$ALARM_LOG" | head -5 | sed 's/^/  /' >&2
    exit 1
fi
if grep -qF "[GFX-DL]" "$ALARM_LOG"; then
    echo "FAIL: GFX-DL diagnostic rows observed during Dam alarm color capture" >&2
    grep -F "[GFX-DL]" "$ALARM_LOG" | head -20 | sed 's/^/  /' >&2
    exit 1
fi
if [[ ! -s "$ALARM_TRACE" ]]; then
    echo "FAIL: missing alarm state trace: $ALARM_TRACE" >&2
    exit 1
fi
if [[ ! -s "$ALARM_SHOT" ]]; then
    echo "FAIL: missing alarm screenshot: $ALARM_SHOT" >&2
    tail -80 "$ALARM_LOG" | sed 's/^/  /' >&2
    exit 1
fi

python3 tools/audit_screenshot_health.py \
    --label "Dam alarm red lens" \
    --json-out "$ALARM_CASE_DIR/screenshot.json" \
    "$ALARM_SHOT" >"$ALARM_CASE_DIR/screenshot.txt"

python3 - "$LOG" "$DUMP_DIR" "$ALARM_SHOT" "$SUMMARY" <<'PY'
import json
import re
import sys
from pathlib import Path
from PIL import Image

log_path = Path(sys.argv[1])
dump_dir = Path(sys.argv[2])
alarm_shot_path = Path(sys.argv[3])
summary_path = Path(sys.argv[4])
log = log_path.read_text(encoding="utf-8", errors="replace")

expected = {
    1167: {"non_gray": 300, "redish": 100, "tanish": 50},
    1783: {"non_gray": 800, "redish": 300, "tanish": 100},
    1784: {"non_gray": 800, "redish": 300, "tanish": 50},
    1828: {"non_gray": 1000, "redish": 300, "tanish": 200},
    1965: {"non_gray": 1000, "tanish": 300},
}
all_ids = [1167, 1370, 1371, 1959, 1961, 1828, 1965, 1631, 1784, 1783]

failures = []

if not re.search(r"\[TEX-SETTIMG\].*fmt=2.*cache=0x80000000000007bc static=1 lods=1", log):
    failures.append("missing static CI G_SETTIMG identity for cache 0x80000000000007bc")
if not re.search(r"\[TEX-LOADBLOCK\].*cache=0x80000000000007bc static=1 lods=1", log):
    failures.append("missing static CI LOADBLOCK identity for cache 0x80000000000007bc")
if not re.search(r"\[TLUT_\d+\].*count=", log):
    failures.append("missing TLUT load evidence for CI material path")

def read_info(path):
    data = {}
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        if "=" in line:
            key, value = line.split("=", 1)
            data[key] = value
    return data

def read_p6(path):
    raw = path.read_bytes()
    idx = 0
    tokens = []
    while len(tokens) < 4:
        while idx < len(raw) and raw[idx] in b" \t\r\n":
            idx += 1
        if idx < len(raw) and raw[idx:idx + 1] == b"#":
            while idx < len(raw) and raw[idx] not in b"\r\n":
                idx += 1
            continue
        start = idx
        while idx < len(raw) and raw[idx] not in b" \t\r\n":
            idx += 1
        tokens.append(raw[start:idx].decode("ascii"))
    if tokens[0] != "P6":
        raise ValueError(f"{path}: expected P6, got {tokens[0]}")
    width = int(tokens[1])
    height = int(tokens[2])
    maxval = int(tokens[3])
    if maxval != 255:
        raise ValueError(f"{path}: expected maxval 255, got {maxval}")
    if idx < len(raw) and raw[idx] in b" \t\r\n":
        idx += 1
    data = raw[idx:]
    expected_len = width * height * 3
    if len(data) != expected_len:
        raise ValueError(f"{path}: expected {expected_len} RGB bytes, got {len(data)}")
    return width, height, data

def color_stats(path):
    width, height, data = read_p6(path)
    pixels = width * height
    unique = set()
    non_gray = 0
    strong_non_gray = 0
    redish = 0
    tanish = 0
    for offset in range(0, len(data), 3):
        r, g, b = data[offset], data[offset + 1], data[offset + 2]
        unique.add((r, g, b))
        span = max(r, g, b) - min(r, g, b)
        if span >= 16:
            non_gray += 1
        if span >= 32:
            strong_non_gray += 1
        if r > g + 16 and r > b + 16 and r > 48:
            redish += 1
        if r > 55 and g > 38 and b < 90 and r >= g >= b and r - b >= 18:
            tanish += 1
    return {
        "width": width,
        "height": height,
        "pixels": pixels,
        "unique": len(unique),
        "non_gray": non_gray,
        "strong_non_gray": strong_non_gray,
        "redish": redish,
        "tanish": tanish,
    }

def alarm_red_stats(path):
    image = Image.open(path).convert("RGB")
    red_pixels = 0
    warm_pixels = 0
    min_x = image.width
    min_y = image.height
    max_x = -1
    max_y = -1

    for y in range(image.height):
        for x in range(image.width):
            r, g, b = image.getpixel((x, y))
            if r > 80 and r > g * 1.2 and r > b * 1.2:
                warm_pixels += 1
            if r > 120 and g < 80 and b < 80 and r > g * 1.4 and r > b * 1.4:
                red_pixels += 1
                min_x = min(min_x, x)
                min_y = min(min_y, y)
                max_x = max(max_x, x)
                max_y = max(max_y, y)

    bbox = None
    if red_pixels:
        bbox = [min_x, min_y, max_x, max_y]

    return {
        "width": image.width,
        "height": image.height,
        "red_pixels": red_pixels,
        "warm_pixels": warm_pixels,
        "red_bbox": bbox,
    }

stats = {}
for tex_id in all_ids:
    info_path = dump_dir / f"ge007_settex_{tex_id}.info.txt"
    if not info_path.exists():
        failures.append(f"missing settex dump info for texture {tex_id}")
        continue
    info = read_info(info_path)
    if info.get("fmt") != "2" or info.get("lutmode") != "2":
        failures.append(f"texture {tex_id} expected CI/RGBA16 palette fmt=2 lutmode=2, got fmt={info.get('fmt')} lutmode={info.get('lutmode')}")
    palette_colours = int(info.get("palette_colours", "0"))
    if palette_colours <= 0:
        failures.append(f"texture {tex_id} has no palette colours")
    rgba_path = Path(info.get("rgba_path", ""))
    if not rgba_path.exists():
        failures.append(f"missing RGBA dump for texture {tex_id}: {rgba_path}")
        continue
    tex_stats = color_stats(rgba_path)
    stats[str(tex_id)] = {
        "palette_colours": palette_colours,
        **tex_stats,
    }
    required = expected.get(tex_id)
    if required:
        for key, minimum in required.items():
            if tex_stats[key] < minimum:
                failures.append(
                    f"texture {tex_id} {key}={tex_stats[key]} < {minimum}"
                )

ci_palette_logs = re.findall(r"\[SETTEX_CI_\d+\] texnum=(\d+) ncolours=(\d+) pal=0x[0-9a-fA-F]+", log)
if len(ci_palette_logs) < 8:
    failures.append(f"too few SETTEX_CI palette logs: {len(ci_palette_logs)} < 8")

alarm_stats = alarm_red_stats(alarm_shot_path)
if alarm_stats["red_pixels"] < 150:
    failures.append(f"alarm red lens pixels={alarm_stats['red_pixels']} < 150")
red_bbox = alarm_stats.get("red_bbox")
if red_bbox is None:
    failures.append("alarm red lens bbox missing")
else:
    min_x, min_y, max_x, max_y = red_bbox
    if not (250 <= min_x <= 380 and 160 <= min_y <= 280 and max_x > min_x and max_y > min_y):
        failures.append(f"alarm red lens bbox out of expected view: {red_bbox}")

summary = {
    "status": "fail" if failures else "pass",
    "stats": stats,
    "alarm_red": alarm_stats,
    "ci_palette_log_count": len(ci_palette_logs),
    "failures": failures,
}
summary_path.write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n")

if failures:
    print("FAIL: Dam palette regression")
    for failure in failures:
        print(f"  - {failure}")
    raise SystemExit(1)

print("PASS: Dam palette regression")
for tex_id in sorted(expected):
    row = stats[str(tex_id)]
    print(
        "  tex %d: palette=%d non_gray=%d redish=%d tanish=%d unique=%d"
        % (
            tex_id,
            row["palette_colours"],
            row["non_gray"],
            row["redish"],
            row["tanish"],
            row["unique"],
        )
    )
print(
    "  alarm red: pixels=%d warm=%d bbox=%s"
    % (alarm_stats["red_pixels"], alarm_stats["warm_pixels"], alarm_stats["red_bbox"])
)
PY

echo "summary_json: $SUMMARY"
echo "artifacts: $OUT_DIR"
