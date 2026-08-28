#!/bin/bash
#
# startup_visual_parity_capture.sh -- Capture boot/title parity checkpoints.
#
# Captures are generated from the user's ROM and must stay local; do not commit
# traces, screenshots, emulator output, or generated image comparisons.
#
set -euo pipefail
cd "$(dirname "$0")/.."

source tools/validation_common.sh

BUILD_DIR="$(validation_default_build_dir)"
BINARY=""
ROM="$(validation_default_rom)"
DO_BUILD=1
TIMEOUT_SECONDS=90
OUT_DIR="/tmp/mgb64_startup_visual_$$"
FRAMES=1800
NATIVE_SHOT_FRAMES="120 300 600 900 1542 1623 1793"
STOCK_SHOT_FRAMES="300 600 900 1200 1500 1800 2100"
ARES_BIN=""
COMPARE_PAIRS=("legal:120:300")
COMPARE_PAIRS_OVERRIDDEN=0
STRICT_VISUAL=0
PAIR_MAX_CHANGED_PCT=25

usage() {
    cat <<'USAGE'
Usage: tools/startup_visual_parity_capture.sh [options]

Options:
  --ares-bin PATH           instrumented ares binary for stock screenshots
  --native-shot-frames LIST native screenshot frames, comma or space separated
  --stock-shot-frames LIST  stock screenshot frames, comma or space separated
  --compare-pair SPEC       compare label:native_frame:stock_frame; may repeat
  --strict-visual           fail when a screenshot pair exceeds the diff budget
  --pair-max-changed-pct N  pair diff budget passed to compare_screenshots
                            (default: 25)
  --frames N                native title trace frame count (default: 1800)
  --out-dir DIR             output directory (default: /tmp/...)
  --rom PATH                ROM path (default: ./baserom.u.z64)
  --binary PATH             native binary path (default: build/ge007)
  --build-dir DIR           CMake build directory (default: build)
  --no-build                reuse an existing native binary
  --timeout SECONDS         per-process timeout (default: 90)

Artifacts are ROM-derived local validation data. Keep them local.
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --ares-bin) ARES_BIN="$2"; shift 2 ;;
        --native-shot-frames) NATIVE_SHOT_FRAMES="$2"; shift 2 ;;
        --stock-shot-frames) STOCK_SHOT_FRAMES="$2"; shift 2 ;;
        --compare-pair)
            if [[ "$COMPARE_PAIRS_OVERRIDDEN" -eq 0 ]]; then
                COMPARE_PAIRS=()
                COMPARE_PAIRS_OVERRIDDEN=1
            fi
            COMPARE_PAIRS+=("$2")
            shift 2
            ;;
        --strict-visual) STRICT_VISUAL=1; shift ;;
        --pair-max-changed-pct) PAIR_MAX_CHANGED_PCT="$2"; shift 2 ;;
        --frames) FRAMES="$2"; shift 2 ;;
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

for value_name in FRAMES TIMEOUT_SECONDS; do
    value="${!value_name}"
    if [[ ! "$value" =~ ^[1-9][0-9]*$ ]]; then
        echo "FAIL: ${value_name} must be a positive integer: $value" >&2
        exit 2
    fi
done
if ! python3 - "$PAIR_MAX_CHANGED_PCT" <<'PY'
import math
import sys
try:
    value = float(sys.argv[1])
except ValueError:
    raise SystemExit(2)
if not math.isfinite(value) or value < 0.0 or value > 100.0:
    raise SystemExit(2)
PY
then
    echo "FAIL: --pair-max-changed-pct must be between 0 and 100: $PAIR_MAX_CHANGED_PCT" >&2
    exit 2
fi

if [[ -z "$BINARY" ]]; then
    BINARY="$(validation_binary_path "$BUILD_DIR")"
else
    BINARY="$(validation_resolve_path "$BINARY")"
fi
ROM="$(validation_resolve_path "$ROM")"
OUT_DIR="$(validation_resolve_path "$OUT_DIR")"
if [[ -n "$ARES_BIN" ]]; then
    ARES_BIN="$(validation_resolve_path "$ARES_BIN")"
fi

if [[ "$DO_BUILD" -eq 1 ]]; then
    validation_configure_build "$BUILD_DIR" >/dev/null
    validation_build "$BUILD_DIR" >/dev/null
fi

validation_require_binary "$BINARY"
validation_require_file "$ROM" "ROM"
if [[ -n "$ARES_BIN" ]]; then
    validation_require_binary "$ARES_BIN"
fi

frame_list() {
    local value="$1"
    tr ',' ' ' <<<"$value"
}

validate_frame_list() {
    local label="$1"
    local list="$2"
    local frame
    for frame in $(frame_list "$list"); do
        if [[ ! "$frame" =~ ^[1-9][0-9]*$ ]]; then
            echo "FAIL: ${label} contains invalid frame: $frame" >&2
            exit 2
        fi
    done
}

validate_frame_list "native shot frames" "$NATIVE_SHOT_FRAMES"
validate_frame_list "stock shot frames" "$STOCK_SHOT_FRAMES"

mkdir -p "$OUT_DIR"
OUT_DIR="$(cd "$OUT_DIR" && pwd)"

NATIVE_SAVE_DIR="$OUT_DIR/native_savedir"
STOCK_SAVES_DIR="$OUT_DIR/ares_saves"
TRACE="$OUT_DIR/native_startup.jsonl"
TRACE_AUDIT_JSON="$OUT_DIR/native_startup_trace_audit.json"
TRACE_LOG="$OUT_DIR/native_startup_trace.log"
PHASE_INDEX="$OUT_DIR/native_phase_samples.tsv"
CAPTURE_INDEX="$OUT_DIR/captures.tsv"
COMPARE_INDEX="$OUT_DIR/comparisons.tsv"
SUMMARY_JSON="$OUT_DIR/summary.json"

printf 'provider\tframe\tphase\tstate\tscreenshot\ttrace\tlog\thealth_json\n' >"$CAPTURE_INDEX"
printf 'label\tnative_frame\tstock_frame\tnative_phase\tnative_state\tstatus\tcompare_json\tcompare_log\n' >"$COMPARE_INDEX"

FAITHFUL_CONFIG=(
    "Video.WindowWidth=640"
    "Video.WindowHeight=480"
    "Video.WindowMode=windowed"
    "Video.HiDPI=0"
    "Video.RenderScale=1"
    "Video.RetroFilter=on"
    "Video.RemasterFX=0"
    "Video.Saturation=1"
    "Video.Contrast=1"
    "Video.Brightness=0"
    "Video.Vignette=0"
    "Video.Bloom=0"
    "Video.Fxaa=0"
    "Video.Sharpen=0"
    "Video.GradePresets=0"
    "Video.Tonemap=0"
)

native_config_args() {
    local item
    for item in "${FAITHFUL_CONFIG[@]}"; do
        printf '%s\n' --config-override
        printf '%s\n' "$item"
    done
}

run_native() {
    local frame="$1"
    local label="$2"
    local screenshot_src="$OUT_DIR/screenshot_${label}.bmp"
    local screenshot_dst="$OUT_DIR/native_${frame}.bmp"
    local health_json="$OUT_DIR/native_${frame}.screenshot.json"
    local log="$OUT_DIR/native_${frame}.log"
    local phase=""
    local state=""
    local cmd=()
    local config_args=()

    while IFS= read -r item; do
        config_args+=("$item")
    done < <(native_config_args)
    rm -f "$screenshot_src" "$screenshot_dst" "$health_json" "$log"

    cmd=(env -u GE007_DEBUG
        SDL_AUDIODRIVER="${GE007_VALIDATION_SDL_AUDIODRIVER:-dummy}"
        GE007_MUTE=1
        GE007_DETERMINISTIC_STABLE_COUNT=1
        GE007_NO_VSYNC=1
        GE007_BACKGROUND=1
        GE007_NO_INPUT_GRAB=1
        GE007_FIDELITY_CAPTURE=1
        "$BINARY"
        "${config_args[@]}"
        --savedir "$NATIVE_SAVE_DIR"
        --rom "$ROM"
        --deterministic
        --screenshot-frame "$frame"
        --screenshot-label "$label"
        --screenshot-exit)

    echo "  native screenshot frame=$frame"
    if ! (cd "$OUT_DIR" && validation_run_with_timeout "$TIMEOUT_SECONDS" "${cmd[@]}") >"$log" 2>&1; then
        echo "FAIL: native startup screenshot failed at frame $frame" >&2
        tail -60 "$log" | sed 's/^/  /' >&2
        exit 1
    fi
    if [[ ! -s "$screenshot_src" ]]; then
        echo "FAIL: missing native startup screenshot: $screenshot_src" >&2
        tail -60 "$log" | sed 's/^/  /' >&2
        exit 1
    fi
    mv "$screenshot_src" "$screenshot_dst"
    python3 tools/audit_screenshot_health.py \
        --label "native startup frame $frame" \
        --expect-size 640x480 \
        --min-unique-colors 1 \
        --max-dominant-pct 100 \
        --max-black-pct 100 \
        --max-white-pct 100 \
        --min-mean-luma 0 \
        --json-out "$health_json" \
        "$screenshot_dst"
    IFS=$'\t' read -r phase state < <(lookup_native_phase "$frame")
    printf 'native\t%s\t%s\t%s\t%s\t\t%s\t%s\n' "$frame" "$phase" "$state" "$screenshot_dst" "$log" "$health_json" >>"$CAPTURE_INDEX"
}

run_native_trace() {
    local label="startup_trace_$$"
    local screenshot_src="$OUT_DIR/screenshot_${label}.bmp"
    local screenshot_dst="$OUT_DIR/native_trace_final.bmp"
    local config_args=()
    local cmd=()

    while IFS= read -r item; do
        config_args+=("$item")
    done < <(native_config_args)
    rm -rf "$NATIVE_SAVE_DIR"
    mkdir -p "$NATIVE_SAVE_DIR"
    rm -f "$TRACE" "$TRACE_AUDIT_JSON" "$TRACE_LOG" "$screenshot_src" "$screenshot_dst"

    cmd=(env -u GE007_DEBUG
        SDL_AUDIODRIVER="${GE007_VALIDATION_SDL_AUDIODRIVER:-dummy}"
        GE007_MUTE=1
        GE007_DETERMINISTIC_STABLE_COUNT=1
        GE007_NO_VSYNC=1
        GE007_BACKGROUND=1
        GE007_NO_INPUT_GRAB=1
        GE007_FIDELITY_CAPTURE=1
        "$BINARY"
        "${config_args[@]}"
        --savedir "$NATIVE_SAVE_DIR"
        --rom "$ROM"
        --deterministic
        --trace-state "$TRACE"
        --screenshot-frame "$FRAMES"
        --screenshot-label "$label"
        --screenshot-exit)

    echo "  native title trace frames=$FRAMES"
    if ! (cd "$OUT_DIR" && validation_run_with_timeout "$TIMEOUT_SECONDS" "${cmd[@]}") >"$TRACE_LOG" 2>&1; then
        echo "FAIL: native startup trace capture failed" >&2
        tail -80 "$TRACE_LOG" | sed 's/^/  /' >&2
        exit 1
    fi
    if [[ ! -s "$TRACE" ]]; then
        echo "FAIL: native startup trace was not written: $TRACE" >&2
        tail -80 "$TRACE_LOG" | sed 's/^/  /' >&2
        exit 1
    fi
    if [[ -s "$screenshot_src" ]]; then
        mv "$screenshot_src" "$screenshot_dst"
    fi
    python3 tools/audit_startup_trace.py \
        --label "native startup title trace" \
        --json-out "$TRACE_AUDIT_JSON" \
        "$TRACE"
}

native_phase_frame_args() {
    local frame
    local pair
    local label
    local native_frame
    local stock_frame

    {
        for frame in $(frame_list "$NATIVE_SHOT_FRAMES"); do
            printf '%s\n' "$frame"
        done
        for pair in "${COMPARE_PAIRS[@]}"; do
            IFS=: read -r label native_frame stock_frame <<<"$pair"
            if [[ -n "${native_frame:-}" ]]; then
                printf '%s\n' "$native_frame"
            fi
        done
    } | sort -n -u
}

write_native_phase_index() {
    local frames=()
    local frame

    while IFS= read -r frame; do
        if [[ -n "$frame" ]]; then
            frames+=("$frame")
        fi
    done < <(native_phase_frame_args)

    python3 - "$TRACE" "$PHASE_INDEX" "${frames[@]}" <<'PY'
import json
import sys
from pathlib import Path

trace_path = Path(sys.argv[1])
phase_path = Path(sys.argv[2])
sample_frames = [int(value) for value in sys.argv[3:]]


def title_phase(menu, mode, blood_state):
    if menu == 0:
        return "legal"
    if menu == 1:
        return "nintendo"
    if menu == 2:
        return "rare"
    if menu == 3:
        if blood_state == 1:
            return "gunbarrel_blood"
        if mode is not None:
            return f"gunbarrel_m{mode}"
        return "gunbarrel"
    if menu is not None:
        return f"front_menu_{menu}"
    return "unknown"


def number_text(value, precision=4):
    if isinstance(value, float):
        return f"{value:.{precision}f}".rstrip("0").rstrip(".")
    if value is None:
        return "-"
    return str(value)


records = {}
with trace_path.open("r", encoding="utf-8", errors="replace") as handle:
    for line in handle:
        if not line.strip():
            continue
        try:
            record = json.loads(line)
        except json.JSONDecodeError:
            continue
        frame = record.get("f")
        if isinstance(frame, int):
            records[frame] = record

with phase_path.open("w", encoding="utf-8") as handle:
    handle.write("frame\trecord_frame\tphase\tstate\n")
    for frame in sample_frames:
        record = records.get(frame)
        if not isinstance(record, dict):
            handle.write(f"{frame}\t\tmissing\ttrace_record=missing\n")
            continue
        front = record.get("front") if isinstance(record.get("front"), dict) else {}
        title = record.get("title") if isinstance(record.get("title"), dict) else {}
        menu = front.get("menu")
        timer = front.get("menu_timer")
        mode = title.get("gunbarrel_mode")
        blood = title.get("blood_state")
        phase = title_phase(menu, mode, blood)
        state = (
            f"menu={number_text(menu)};"
            f"timer={number_text(timer)};"
            f"gun={number_text(mode)};"
            f"eye={number_text(title.get('eye_counter'))};"
            f"blood={number_text(blood)};"
            f"wave={number_text(title.get('wave'))};"
            f"rare={number_text(title.get('rare_rotation'), 2)};"
            f"nintendo={number_text(title.get('nintendo_rotation'), 4)};"
            f"scale={number_text(title.get('nintendo_scale'), 6)}"
        )
        handle.write(f"{frame}\t{record.get('f')}\t{phase}\t{state}\n")
PY
}

lookup_native_phase() {
    local frame="$1"

    if [[ ! -s "$PHASE_INDEX" ]]; then
        printf '\t\n'
        return
    fi
    awk -F '\t' -v frame="$frame" '
        NR > 1 && $1 == frame {
            print $3 "\t" $4
            found = 1
            exit
        }
        END {
            if (!found) {
                print "\t"
            }
        }
    ' "$PHASE_INDEX"
}

run_stock() {
    local frame="$1"
    local settings_file="$OUT_DIR/ares_settings_${frame}.bml"
    local trace="$OUT_DIR/stock_${frame}.jsonl"
    local screenshot="$OUT_DIR/stock_${frame}.ppm"
    local health_json="$OUT_DIR/stock_${frame}.screenshot.json"
    local log="$OUT_DIR/stock_${frame}.log"
    local ares_pid=""
    local elapsed=0
    local trace_lines=0
    local cmd=()

    rm -f "$settings_file" "$trace" "$screenshot" "$health_json" "$log"
    mkdir -p "$STOCK_SAVES_DIR"

    echo "  stock screenshot frame=$frame"
    cmd=(env
        MGB64_ARES_ORACLE_TRACE="$trace"
        MGB64_ARES_MOVEMENT_TRACE="$trace"
        MGB64_ARES_FRAME_LIMIT="$frame"
        MGB64_ARES_SCREENSHOT_PATH="$screenshot"
        MGB64_ARES_SCREENSHOT_FRAME="$frame"
        MGB64_ARES_TARGET_STAGE=90
        "$ARES_BIN"
        --kiosk
        --settings-file "$settings_file"
        --setting Audio/Mute=true
        --setting Audio/Blocking=false
        --setting Audio/Dynamic=false
        --setting Input/Defocus=Allow
        --setting General/AutoSaveMemory=false
        --setting Paths/Saves="$STOCK_SAVES_DIR"
        --setting Boot/Fast=true
        --setting Video/Blocking=false
        --system "Nintendo 64"
        --no-file-prompt
        "$ROM")

    "${cmd[@]}" >"$log" 2>&1 &
    ares_pid="$!"
    while kill -0 "$ares_pid" 2>/dev/null; do
        if [[ -f "$trace" ]]; then
            trace_lines="$(wc -l < "$trace" | tr -d '[:space:]')"
            if [[ "$trace_lines" =~ ^[0-9]+$ && "$trace_lines" -ge "$frame" && -s "$screenshot" ]]; then
                break
            fi
        fi
        if [[ "$elapsed" -ge "$TIMEOUT_SECONDS" ]]; then
            break
        fi
        sleep 1
        elapsed=$((elapsed + 1))
    done

    if kill -0 "$ares_pid" 2>/dev/null; then
        kill "$ares_pid" 2>/dev/null || true
        sleep 1
        if kill -0 "$ares_pid" 2>/dev/null; then
            kill -9 "$ares_pid" 2>/dev/null || true
        fi
        wait "$ares_pid" 2>/dev/null || true
    else
        wait "$ares_pid" 2>/dev/null || true
    fi

    if [[ ! -s "$trace" ]]; then
        echo "FAIL: stock startup trace was not written: $trace" >&2
        tail -80 "$log" | sed 's/^/  /' >&2
        exit 1
    fi
    trace_lines="$(wc -l < "$trace" | tr -d '[:space:]')"
    if [[ ! "$trace_lines" =~ ^[0-9]+$ || "$trace_lines" -lt "$frame" ]]; then
        echo "FAIL: stock startup trace stopped at ${trace_lines:-0}/${frame} frame(s)" >&2
        tail -80 "$log" | sed 's/^/  /' >&2
        exit 1
    fi
    if [[ ! -s "$screenshot" ]]; then
        echo "FAIL: stock startup screenshot was not written: $screenshot" >&2
        tail -80 "$log" | sed 's/^/  /' >&2
        exit 1
    fi
    python3 tools/audit_screenshot_health.py \
        --label "stock startup frame $frame" \
        --expect-size 640x480 \
        --min-unique-colors 1 \
        --max-dominant-pct 100 \
        --max-black-pct 100 \
        --max-white-pct 100 \
        --min-mean-luma 0 \
        --json-out "$health_json" \
        "$screenshot"
    printf 'stock\t%s\t\t\t%s\t%s\t%s\t%s\n' "$frame" "$screenshot" "$trace" "$log" "$health_json" >>"$CAPTURE_INDEX"
}

run_compare_pair() {
    local spec="$1"
    local label native_frame stock_frame
    local native_shot stock_shot json_out log_out status
    local native_phase=""
    local native_state=""

    IFS=: read -r label native_frame stock_frame <<<"$spec"
    if [[ -z "${label:-}" || -z "${native_frame:-}" || -z "${stock_frame:-}" ]]; then
        echo "FAIL: compare pair must be label:native_frame:stock_frame: $spec" >&2
        exit 2
    fi
    native_shot="$OUT_DIR/native_${native_frame}.bmp"
    stock_shot="$OUT_DIR/stock_${stock_frame}.ppm"
    json_out="$OUT_DIR/compare_${label}.json"
    log_out="$OUT_DIR/compare_${label}.txt"
    status="pass"
    IFS=$'\t' read -r native_phase native_state < <(lookup_native_phase "$native_frame")
    echo "  compare ${label}: native frame=${native_frame} phase=${native_phase:-unknown} state=${native_state:-unknown} stock frame=${stock_frame}"

    if [[ ! -s "$native_shot" || ! -s "$stock_shot" ]]; then
        echo "  compare ${label}: skipped; missing native or stock screenshot"
        status="skipped"
    elif python3 tools/compare_screenshots.py \
        "$native_shot" \
        "$stock_shot" \
        --active-threshold 5 \
        --max-changed-pct "$PAIR_MAX_CHANGED_PCT" \
        --json-out "$json_out" >"$log_out" 2>&1; then
        sed -n '1,14p' "$log_out" | sed 's/^/    /'
    else
        status="fail"
        echo "  compare ${label}: diff budget exceeded; see $log_out"
        sed -n '1,18p' "$log_out" | sed 's/^/    /'
        if [[ "$STRICT_VISUAL" -eq 1 ]]; then
            printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$label" "$native_frame" "$stock_frame" "$native_phase" "$native_state" "$status" "$json_out" "$log_out" >>"$COMPARE_INDEX"
            exit 1
        fi
    fi

    printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$label" "$native_frame" "$stock_frame" "$native_phase" "$native_state" "$status" "$json_out" "$log_out" >>"$COMPARE_INDEX"
}

write_summary() {
    python3 - "$CAPTURE_INDEX" "$COMPARE_INDEX" "$TRACE_AUDIT_JSON" "$PHASE_INDEX" "$SUMMARY_JSON" <<'PY'
import csv
import json
import sys
from pathlib import Path

capture_index = Path(sys.argv[1])
compare_index = Path(sys.argv[2])
trace_audit = Path(sys.argv[3])
phase_index = Path(sys.argv[4])
summary_path = Path(sys.argv[5])

captures = []
if capture_index.is_file():
    with capture_index.open("r", encoding="utf-8", newline="") as handle:
        captures = list(csv.DictReader(handle, delimiter="\t"))

comparisons = []
if compare_index.is_file():
    with compare_index.open("r", encoding="utf-8", newline="") as handle:
        comparisons = list(csv.DictReader(handle, delimiter="\t"))

trace_payload = {}
if trace_audit.is_file():
    trace_payload = json.loads(trace_audit.read_text(encoding="utf-8"))

native_phase_samples = []
if phase_index.is_file():
    with phase_index.open("r", encoding="utf-8", newline="") as handle:
        native_phase_samples = list(csv.DictReader(handle, delimiter="\t"))

failures = []
if trace_payload.get("status") not in ("", None, "pass"):
    failures.append("native startup title trace audit failed")

summary = {
    "status": "fail" if failures else "pass",
    "failures": failures,
    "capture_count": len(captures),
    "comparison_count": len(comparisons),
    "trace_audit": str(trace_audit),
    "native_phase_index": str(phase_index),
    "native_phase_samples": native_phase_samples,
    "captures": captures,
    "comparisons": comparisons,
}
summary_path.write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8")
print(f"summary: {summary_path}")
PY
}

validation_acquire_runtime_lock
trap 'validation_release_runtime_lock' EXIT INT TERM

echo "=== Startup Visual Parity Capture ==="
echo "  out-dir: $OUT_DIR"
echo "  native:  $BINARY"
echo "  ROM:     $ROM"
if [[ -n "$ARES_BIN" ]]; then
    echo "  ares:    $ARES_BIN"
else
    echo "  ares:    (none; native trace/screenshots only)"
fi

echo ""
echo "== Native title trace =="
run_native_trace
write_native_phase_index

echo ""
echo "== Native screenshots =="
for frame in $(frame_list "$NATIVE_SHOT_FRAMES"); do
    run_native "$frame" "startup_native_${frame}_$$"
done

if [[ -n "$ARES_BIN" ]]; then
    echo ""
    echo "== Stock screenshots =="
    for frame in $(frame_list "$STOCK_SHOT_FRAMES"); do
        run_stock "$frame"
    done

    echo ""
    echo "== Stock/native screenshot comparisons =="
    for pair in "${COMPARE_PAIRS[@]}"; do
        run_compare_pair "$pair"
    done
fi

echo ""
write_summary
echo "=== Startup Visual Parity Capture: PASS ==="
echo "  captures: $CAPTURE_INDEX"
echo "  summary:  $SUMMARY_JSON"
