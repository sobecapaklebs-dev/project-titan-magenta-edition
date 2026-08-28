#!/bin/bash
#
# playability_smoke.sh -- Verify deterministic gameplay input moves Bond.
#
# Captures are generated from the user's ROM and must stay local; do not commit
# traces, screenshots, logs, or audit summaries from /tmp.
#
set -euo pipefail
cd "$(dirname "$0")/.."

source tools/validation_common.sh

BUILD_DIR="$(validation_default_build_dir)"
BINARY=""
ROM="$(validation_default_rom)"
DO_BUILD=1
TIMEOUT_SECONDS=60
OUT_DIR="/tmp/mgb64_playability_smoke_$$"
FRAMES=240
INPUT_WINDOW="80:120"
MIN_MOVING_RECORDS=30
MIN_HORIZONTAL_DELTA="20.0"
LEVELS="33 41"
ALL_LEVELS="33 34 22 26 36 35 9 20 43 27 24 29 30 25 37 23 39 41 28 32"
PATTERNS="forward right back left"
PIN_DEFAULT_CONFIG=1
USER_CONFIG_OVERRIDES=()
CONFIG_OVERRIDES=()
DEFAULT_CONFIG_OVERRIDES=(
    "Video.WindowWidth=640"
    "Video.WindowHeight=480"
    "Video.WindowX=-1"
    "Video.WindowY=-1"
    "Video.WindowMode=windowed"
)

usage() {
    cat <<'USAGE'
Usage: tools/playability_smoke.sh [options]

Options:
  --all                    capture all 20 supported solo stages
  --level LIST             raw LEVELID list, quoted if multiple (default: "33 41")
  --pattern LIST           input pattern list (default: "forward right back left")
                          allowed: forward, back, left, right,
                          forward_left, forward_right, back_left, back_right
  --input-window START:LEN deterministic GE007_AUTO_* window (default: 80:120)
  --frames N               deterministic screenshot/exit frame (default: 240)
  --min-moving-records N   minimum nonzero move.speed records (default: 30)
  --min-horizontal-delta F minimum X/Z player displacement (default: 20.0)
  --out-dir DIR            output directory (default: /tmp/...)
  --rom PATH               ROM path (default: ./baserom.u.z64)
  --binary PATH            native binary path (default: build/ge007)
  --build-dir DIR          CMake build directory (default: build)
  --no-build               reuse an existing native binary
  --timeout SECONDS        per-attempt timeout (default: 60)
  --config-override K=V    pass a deterministic config override to ge007
                          may be repeated; use for RenderScale/MSAA probes
  --no-default-config-overrides
                          do not pin the validation window to 640x480

Artifacts are ROM-derived local validation data. Do not commit captured traces,
screenshots, logs, or generated audit summaries.
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --all) LEVELS="$ALL_LEVELS"; shift ;;
        --level) LEVELS="$2"; shift 2 ;;
        --pattern) PATTERNS="$2"; shift 2 ;;
        --input-window) INPUT_WINDOW="$2"; shift 2 ;;
        --frames) FRAMES="$2"; shift 2 ;;
        --min-moving-records) MIN_MOVING_RECORDS="$2"; shift 2 ;;
        --min-horizontal-delta) MIN_HORIZONTAL_DELTA="$2"; shift 2 ;;
        --out-dir) OUT_DIR="$2"; shift 2 ;;
        --rom) ROM="$2"; shift 2 ;;
        --binary) BINARY="$2"; shift 2 ;;
        --build-dir) BUILD_DIR="$2"; shift 2 ;;
        --no-build) DO_BUILD=0; shift ;;
        --timeout) TIMEOUT_SECONDS="$2"; shift 2 ;;
        --config-override) USER_CONFIG_OVERRIDES+=("$2"); shift 2 ;;
        --no-default-config-overrides) PIN_DEFAULT_CONFIG=0; shift ;;
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
if [[ ! "$MIN_MOVING_RECORDS" =~ ^[1-9][0-9]*$ ]]; then
    echo "FAIL: --min-moving-records must be a positive integer: $MIN_MOVING_RECORDS" >&2
    exit 2
fi
if [[ ! "$INPUT_WINDOW" =~ ^[0-9]+:[1-9][0-9]*(,[0-9]+:[1-9][0-9]*)*$ ]]; then
    echo "FAIL: --input-window must use START:LEN windows: $INPUT_WINDOW" >&2
    exit 2
fi
if ! python3 - "$MIN_HORIZONTAL_DELTA" <<'PY'
import math
import sys

try:
    value = float(sys.argv[1])
except ValueError:
    raise SystemExit(1)
raise SystemExit(0 if math.isfinite(value) and value >= 0.0 else 1)
PY
then
    echo "FAIL: --min-horizontal-delta must be a non-negative finite number: $MIN_HORIZONTAL_DELTA" >&2
    exit 2
fi
if [[ -z "${PATTERNS//[[:space:]]/}" ]]; then
    echo "FAIL: --pattern list must not be empty" >&2
    exit 2
fi
for lvl in $LEVELS; do
    if [[ ! "$lvl" =~ ^-?[0-9]+$ ]]; then
        echo "FAIL: --level list contains a non-integer LEVELID: $lvl" >&2
        exit 2
    fi
done
for pattern in $PATTERNS; do
    case "$pattern" in
        forward|back|left|right|forward_left|forward_right|back_left|back_right) ;;
        *)
            echo "FAIL: unknown --pattern entry: $pattern" >&2
            exit 2
            ;;
    esac
done
if [[ "$PIN_DEFAULT_CONFIG" -eq 1 ]]; then
    CONFIG_OVERRIDES+=("${DEFAULT_CONFIG_OVERRIDES[@]}")
fi
if [[ "${#USER_CONFIG_OVERRIDES[@]}" -gt 0 ]]; then
    CONFIG_OVERRIDES+=("${USER_CONFIG_OVERRIDES[@]}")
fi
if [[ "${#CONFIG_OVERRIDES[@]}" -gt 0 ]]; then
    for override in "${CONFIG_OVERRIDES[@]}"; do
        if [[ ! "$override" =~ ^[A-Za-z0-9_.-]+=.+$ ]]; then
            echo "FAIL: --config-override must use Section.Key=value: $override" >&2
            exit 2
        fi
    done
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

mkdir -p "$OUT_DIR"
OUT_DIR="$(cd "$OUT_DIR" && pwd)"

validation_acquire_runtime_lock
trap 'validation_release_runtime_lock' EXIT INT TERM

SUMMARY_FILE="$OUT_DIR/summary.tsv"
SUMMARY_JSON="$OUT_DIR/summary.json"
CONTACT_SHEET="$OUT_DIR/contact_sheet.png"
printf 'level\tpattern\tmoving_records\tmax_horizontal_delta\ttarget_player_records\trecords\n' >"$SUMMARY_FILE"

FAILED=0
PASSED=0
TOTAL=0

pattern_env() {
    local pattern="$1"

    case "$pattern" in
        forward) printf '%s\n' "GE007_AUTO_FORWARD=$INPUT_WINDOW" ;;
        back) printf '%s\n' "GE007_AUTO_BACK=$INPUT_WINDOW" ;;
        left) printf '%s\n' "GE007_AUTO_LEFT=$INPUT_WINDOW" ;;
        right) printf '%s\n' "GE007_AUTO_RIGHT=$INPUT_WINDOW" ;;
        forward_left)
            printf '%s\n' "GE007_AUTO_FORWARD=$INPUT_WINDOW"
            printf '%s\n' "GE007_AUTO_LEFT=$INPUT_WINDOW"
            ;;
        forward_right)
            printf '%s\n' "GE007_AUTO_FORWARD=$INPUT_WINDOW"
            printf '%s\n' "GE007_AUTO_RIGHT=$INPUT_WINDOW"
            ;;
        back_left)
            printf '%s\n' "GE007_AUTO_BACK=$INPUT_WINDOW"
            printf '%s\n' "GE007_AUTO_LEFT=$INPUT_WINDOW"
            ;;
        back_right)
            printf '%s\n' "GE007_AUTO_BACK=$INPUT_WINDOW"
            printf '%s\n' "GE007_AUTO_RIGHT=$INPUT_WINDOW"
            ;;
    esac
}

audit_effective_config() {
    local config_file="$OUT_DIR/ge007.ini"

    if [[ "${#CONFIG_OVERRIDES[@]}" -eq 0 ]]; then
        return 0
    fi

    python3 - "$config_file" "${CONFIG_OVERRIDES[@]}" <<'PY'
import math
import sys
from pathlib import Path

config_path = Path(sys.argv[1])
overrides = sys.argv[2:]

if not config_path.is_file():
    print(f"FAIL: missing generated config file for override audit: {config_path}")
    raise SystemExit(1)

values = {}
section = None
for raw in config_path.read_text(encoding="utf-8", errors="replace").splitlines():
    line = raw.strip()
    if not line or line.startswith("#") or line.startswith(";"):
        continue
    if line.startswith("[") and line.endswith("]"):
        section = line[1:-1].strip()
        continue
    if section and "=" in line:
        key, value = line.split("=", 1)
        values[f"{section}.{key.strip()}"] = value.strip()


def equal_value(expected: str, actual: str) -> bool:
    if expected == actual:
        return True
    try:
        e = float(expected)
        a = float(actual)
    except ValueError:
        return False
    return math.isfinite(e) and math.isfinite(a) and abs(e - a) <= 1e-6


failures = []
for override in overrides:
    if "=" not in override:
        failures.append(f"malformed override: {override}")
        continue
    full_key, expected = override.split("=", 1)

    # The validation default uses -1 as a center-window request. The runtime
    # persists the resolved screen position, so this one request is not stable
    # evidence that the binary ignored the override path.
    if full_key in ("Video.WindowX", "Video.WindowY") and expected == "-1":
        continue

    actual = values.get(full_key)
    if actual is None:
        failures.append(f"{full_key}: missing from generated config")
    elif not equal_value(expected, actual):
        failures.append(f"{full_key}: expected {expected!r}, got {actual!r}")

if failures:
    print("FAIL: config override audit")
    for failure in failures:
        print(f"  {failure}")
    raise SystemExit(1)

print("PASS: config override audit")
PY
}

run_attempt() {
    local lvl="$1"
    local pattern="$2"
    local label="playability_${lvl}_${pattern}_$$"
    local trace="$OUT_DIR/level_${lvl}_${pattern}.jsonl"
    local log="$OUT_DIR/level_${lvl}_${pattern}.log"
    local render_log="$OUT_DIR/level_${lvl}_${pattern}.render.txt"
    local render_json="$OUT_DIR/level_${lvl}_${pattern}.render.json"
    local movement_log="$OUT_DIR/level_${lvl}_${pattern}.movement.txt"
    local movement_json="$OUT_DIR/level_${lvl}_${pattern}.movement.json"
    local screenshot_log="$OUT_DIR/level_${lvl}_${pattern}.screenshot.txt"
    local screenshot_json="$OUT_DIR/level_${lvl}_${pattern}.screenshot.json"
    local screenshot_src="$OUT_DIR/screenshot_${label}.bmp"
    local screenshot_dst="$OUT_DIR/level_${lvl}_${pattern}.bmp"
    local env_vars=()
    local config_args=()
    local binary_args=()
    local entry
    local assert_count

    rm -f "$trace" "$log" "$render_log" "$render_json" "$movement_log" "$movement_json" "$screenshot_log" "$screenshot_json" "$screenshot_src" "$screenshot_dst"

    while IFS= read -r entry; do
        env_vars+=("$entry")
    done < <(pattern_env "$pattern")
    if [[ "${#CONFIG_OVERRIDES[@]}" -gt 0 ]]; then
        for entry in "${CONFIG_OVERRIDES[@]}"; do
            config_args+=("--config-override" "$entry")
        done
    fi
    binary_args=("--savedir" "$OUT_DIR")
    if [[ "${#config_args[@]}" -gt 0 ]]; then
        binary_args+=("${config_args[@]}")
    fi

    if ! (
        cd "$OUT_DIR"
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
            "${env_vars[@]}" \
            "$BINARY" \
            "${binary_args[@]}" \
            --rom "$ROM" \
            --level "$lvl" \
            --deterministic \
            --trace-state "$trace" \
            --screenshot-frame "$FRAMES" \
            --screenshot-label "$label" \
            --screenshot-exit
    ) >"$log" 2>&1; then
        echo "    process: FAIL"
        tail -20 "$log" | sed 's/^/      /'
        return 1
    fi
    echo "    process: PASS"

    if audit_effective_config >"$OUT_DIR/level_${lvl}_${pattern}.config.txt" 2>&1; then
        echo "    config: PASS"
    else
        echo "    config: FAIL"
        sed -n '1,16p' "$OUT_DIR/level_${lvl}_${pattern}.config.txt" | sed 's/^/      /'
        return 1
    fi

    assert_count="$(grep -cF "[GEASSERT]" "$log" 2>/dev/null || true)"
    assert_count="${assert_count:-0}"
    if [[ "$assert_count" -ne 0 ]]; then
        echo "    assertions: FAIL ($assert_count)"
        grep -F "[GEASSERT]" "$log" | head -5 | sed 's/^/      /'
        return 1
    fi
    echo "    assertions: PASS"

    if [[ ! -s "$trace" ]]; then
        echo "    trace: FAIL (missing or empty)"
        tail -20 "$log" | sed 's/^/      /'
        return 1
    fi
    echo "    trace: PASS"

    if [[ -f "$screenshot_src" ]]; then
        mv "$screenshot_src" "$screenshot_dst"
        echo "    screenshot: PASS"
    else
        echo "    screenshot: FAIL (missing)"
        return 1
    fi

    if python3 tools/audit_screenshot_health.py \
        --label "playability level $lvl pattern $pattern screenshot" \
        --expect-size 640x480 \
        --json-out "$screenshot_json" \
        "$screenshot_dst" >"$screenshot_log" 2>&1; then
        echo "    screenshot_health: PASS"
    else
        echo "    screenshot_health: FAIL"
        sed -n '1,12p' "$screenshot_log" | sed 's/^/      /'
        return 1
    fi

    if python3 tools/audit_render_trace.py \
        --label "playability level $lvl pattern $pattern" \
        --json-out "$render_json" \
        "$trace" >"$render_log" 2>&1; then
        echo "    render_health: PASS"
    else
        echo "    render_health: FAIL"
        sed -n '1,12p' "$render_log" | sed 's/^/      /'
        return 1
    fi

    if python3 tools/audit_oracle_trace.py \
        --kind movement \
        --label "playability level $lvl pattern $pattern" \
        --require-stage "$lvl" \
        --require-target-player \
        --min-moving-records "$MIN_MOVING_RECORDS" \
        --min-horizontal-delta "$MIN_HORIZONTAL_DELTA" \
        --max-suppressed-menu-records 0 \
        --json-out "$movement_json" \
        "$trace" >"$movement_log" 2>&1; then
        echo "    movement: PASS"
    else
        echo "    movement: FAIL"
        sed -n '1,16p' "$movement_log" | sed 's/^/      /'
        return 1
    fi

    python3 - "$movement_json" "$lvl" "$pattern" "$SUMMARY_FILE" <<'PY'
import json
import sys

metrics_path, level, pattern, summary_path = sys.argv[1:5]
with open(metrics_path, "r", encoding="utf-8") as handle:
    metrics = json.load(handle)
with open(summary_path, "a", encoding="utf-8") as handle:
    handle.write(
        f"{level}\t{pattern}\t"
        f"{int(metrics.get('moving_records', 0))}\t"
        f"{float(metrics.get('max_horizontal_delta', 0.0)):.6f}\t"
        f"{int(metrics.get('target_player_records', 0))}\t"
        f"{int(metrics.get('records', 0))}\n"
    )
PY

    return 0
}

echo "=== Playability Smoke ==="
echo "  out-dir:              $OUT_DIR"
echo "  binary:               $BINARY"
echo "  ROM:                  $ROM"
echo "  levels:               $LEVELS"
echo "  patterns:             $PATTERNS"
echo "  input-window:         $INPUT_WINDOW"
echo "  frames:               $FRAMES"
echo "  min-moving-records:   $MIN_MOVING_RECORDS"
echo "  min-horizontal-delta: $MIN_HORIZONTAL_DELTA"
if [[ "${#CONFIG_OVERRIDES[@]}" -gt 0 ]]; then
    echo "  config-overrides:"
    for override in "${CONFIG_OVERRIDES[@]}"; do
        echo "    $override"
    done
fi

for lvl in $LEVELS; do
    level_pass=0
    TOTAL=$((TOTAL + 1))

    echo ""
    echo "=== Playability: Level $lvl ==="
    for pattern in $PATTERNS; do
        echo "  attempt: $pattern"
        if run_attempt "$lvl" "$pattern"; then
            echo "  level: PASS ($pattern)"
            level_pass=1
            break
        fi
    done

    if [[ "$level_pass" -eq 1 ]]; then
        PASSED=$((PASSED + 1))
    else
        FAILED=$((FAILED + 1))
        echo "  level: FAIL (no pattern satisfied process, render, and movement audits)"
    fi
done

SUMMARY_ARGS=(
    "$SUMMARY_FILE"
    "$SUMMARY_JSON"
    "$CONTACT_SHEET"
    "$OUT_DIR"
    "$LEVELS"
    "$PATTERNS"
    "$TOTAL"
    "$PASSED"
    "$FAILED"
    "$INPUT_WINDOW"
    "$FRAMES"
    "$MIN_MOVING_RECORDS"
    "$MIN_HORIZONTAL_DELTA"
)
if [[ "${#CONFIG_OVERRIDES[@]}" -gt 0 ]]; then
    SUMMARY_ARGS+=("${CONFIG_OVERRIDES[@]}")
fi

python3 - "${SUMMARY_ARGS[@]}" <<'PY'
import csv
import json
import sys
from pathlib import Path

summary_file = Path(sys.argv[1])
summary_json = Path(sys.argv[2])
contact_sheet = Path(sys.argv[3])
out_dir = Path(sys.argv[4])
levels = sys.argv[5].split()
patterns = sys.argv[6].split()
total = int(sys.argv[7])
passed = int(sys.argv[8])
failed = int(sys.argv[9])
input_window = sys.argv[10]
frames = int(sys.argv[11])
min_moving_records = int(sys.argv[12])
min_horizontal_delta = float(sys.argv[13])
config_overrides = sys.argv[14:]

LEVEL_NAMES = {
    33: "Dam",
    34: "Facility",
    22: "Statue",
    26: "Frigate",
    36: "Surface 1",
    35: "Runway",
    9: "Bunker 1",
    20: "Silo",
    43: "Surface 2",
    27: "Bunker 2",
    24: "Archives",
    29: "Streets",
    30: "Depot",
    25: "Train",
    37: "Jungle",
    23: "Control",
    39: "Caverns",
    41: "Cradle",
    28: "Aztec",
    32: "Egyptian",
}


def load_json(path: Path) -> dict:
    with path.open("r", encoding="utf-8") as handle:
        data = json.load(handle)
    return data if isinstance(data, dict) else {"value": data}


def load_audit(path: Path) -> dict:
    try:
        return load_json(path)
    except FileNotFoundError:
        return {"status": "missing", "path": str(path), "failures": ["missing audit JSON"]}
    except json.JSONDecodeError as exc:
        return {"status": "invalid", "path": str(path), "failures": [f"invalid JSON: {exc}"]}


accepted = []
accepted_levels = set()
with summary_file.open("r", encoding="utf-8", newline="") as handle:
    for row in csv.DictReader(handle, delimiter="\t"):
        level = row["level"]
        pattern = row["pattern"]
        accepted_levels.add(level)
        screenshot_json = out_dir / f"level_{level}_{pattern}.screenshot.json"
        render_json = out_dir / f"level_{level}_{pattern}.render.json"
        movement_json = out_dir / f"level_{level}_{pattern}.movement.json"
        screenshot_path = out_dir / f"level_{level}_{pattern}.bmp"
        accepted.append(
            {
                "level": int(level),
                "level_name": LEVEL_NAMES.get(int(level), ""),
                "pattern": pattern,
                "screenshot": str(screenshot_path),
                "records": int(row["records"]),
                "target_player_records": int(row["target_player_records"]),
                "moving_records": int(row["moving_records"]),
                "max_horizontal_delta": float(row["max_horizontal_delta"]),
                "screenshot_health": load_audit(screenshot_json),
                "render_health": load_audit(render_json),
                "movement": load_audit(movement_json),
            }
        )

failures = []
for level in levels:
    if level not in accepted_levels:
        failures.append(f"level {level}: no accepted movement pattern")

for entry in accepted:
    if not entry["screenshot_health"].get("ok", False):
        failures.append(f"level {entry['level']} {entry['pattern']}: screenshot health failed")
    if entry["render_health"].get("status") != "pass":
        failures.append(f"level {entry['level']} {entry['pattern']}: render health failed")
    if entry["movement"].get("status") != "pass":
        failures.append(f"level {entry['level']} {entry['pattern']}: movement audit failed")

contact_sheet_path = None
if accepted:
    try:
        from PIL import Image, ImageDraw
    except ImportError:
        failures.append("contact sheet: Pillow is unavailable")
    else:
        thumb_w = 320
        thumb_h = 240
        label_h = 22
        cols = 5 if len(accepted) >= 5 else max(1, len(accepted))
        rows = (len(accepted) + cols - 1) // cols
        resample = getattr(getattr(Image, "Resampling", Image), "LANCZOS")
        sheet = Image.new("RGB", (cols * thumb_w, rows * (thumb_h + label_h)), (18, 18, 18))
        draw = ImageDraw.Draw(sheet)

        for index, entry in enumerate(accepted):
            shot = Path(entry["screenshot"])
            with Image.open(shot) as image:
                thumb = image.convert("RGB").resize((thumb_w, thumb_h), resample)
            x = (index % cols) * thumb_w
            y = (index // cols) * (thumb_h + label_h)
            sheet.paste(thumb, (x, y + label_h))
            name = entry.get("level_name") or f"LEVELID {entry['level']}"
            draw.text((x + 6, y + 4), f"{entry['level']} {name} [{entry['pattern']}]", fill=(235, 235, 235))

        sheet.save(contact_sheet)
        contact_sheet_path = str(contact_sheet)

summary = {
    "status": "fail" if failed or failures else "pass",
    "summary_tsv": str(summary_file),
    "contact_sheet": contact_sheet_path,
    "counts": {
        "requested": total,
        "passed": passed,
        "failed": failed,
        "accepted": len(accepted),
    },
    "config": {
        "levels": [int(level) for level in levels],
        "patterns": patterns,
        "input_window": input_window,
        "frames": frames,
        "min_moving_records": min_moving_records,
        "min_horizontal_delta": min_horizontal_delta,
        "config_overrides": config_overrides,
    },
    "failures": failures,
    "accepted": accepted,
}
with summary_json.open("w", encoding="utf-8") as handle:
    json.dump(summary, handle, indent=2, sort_keys=True)
    handle.write("\n")

print(f"summary_json: {summary_json}")
if contact_sheet_path:
    print(f"contact_sheet: {contact_sheet_path}")
PY

echo ""
echo "=== Playability Smoke: $PASSED/$TOTAL passed, $FAILED failed ==="
echo "  artifacts: $OUT_DIR"
echo "  summary:   $SUMMARY_FILE"
echo "  json:      $SUMMARY_JSON"
if [[ -s "$CONTACT_SHEET" ]]; then
    echo "  contact:   $CONTACT_SHEET"
fi
exit "$FAILED"
