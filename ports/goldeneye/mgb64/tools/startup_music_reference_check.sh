#!/bin/bash
#
# startup_music_reference_check.sh -- Capture and compare startup music audio.
#
# Captures MGB64's pre-SFX startup music PCM dump and optionally compares it
# against a local emulator/hardware reference capture. All artifacts are local
# ROM-derived validation data; do not commit or redistribute them.
#
set -euo pipefail
cd "$(dirname "$0")/.."

source tools/validation_common.sh

BUILD_DIR="$(validation_default_build_dir)"
BINARY=""
ROM="$(validation_default_rom)"
DO_BUILD=1
TIMEOUT_SECONDS=180
TIMEOUT_BIN="$(validation_resolve_timeout_cmd)"
OUT_DIR="/tmp/mgb64_startup_music_$$"
FRAMES=2400
REFERENCE=""
REFERENCE_FORMAT="auto"
REFERENCE_RAW_RATE=44100
REFERENCE_RAW_CHANNELS=2
REFERENCE_RAW_ENDIAN="little"
TEST_RAW_RATE=22050
TEST_RAW_ENDIAN="little"
TARGET_RATE=22050
REFERENCE_START=0
TEST_START=0
DURATION=""
MAX_OFFSET_SECONDS=20
SEGMENT_SECONDS=10
SEGMENT_HOP_SECONDS=5
MIN_COMPARED_SECONDS=0
CAPTURE_FINAL_MIX=1
REPORT_ONLY=0

usage() {
    cat <<'USAGE'
Usage: tools/startup_music_reference_check.sh [options]

Options:
  --reference PATH              local reference WAV/raw capture to compare
  --reference-format FORMAT     auto, wav, raw (default: auto)
  --reference-raw-rate HZ       raw reference sample rate (default: 44100)
  --reference-raw-channels N    raw reference channel count (default: 2)
  --reference-raw-endian E      raw reference endian: little, big (default: little)
  --test-raw-rate HZ            raw MGB64 dump sample rate (default: 22050)
  --test-raw-endian E           raw MGB64 dump endian: little, big (default: little)
  --target-rate HZ              comparison sample rate (default: 22050)
  --reference-start SECONDS     skip reference prefix before comparing
  --test-start SECONDS          skip MGB64 dump prefix before comparing
  --duration SECONDS            compare only this many seconds
  --max-offset-seconds SECONDS  envelope alignment search window (default: 20)
  --min-compared-seconds N      fail if aligned overlap is shorter than N seconds
  --segment-seconds N           diagnostic segment window (default: 10)
  --segment-hop-seconds N       diagnostic segment hop (default: 5)
  --frames N                    MGB64 music dump frames to capture (default: 2400)
  --no-final-mix-dump           only capture pre-SFX music, not final mixed PCM
  --report-only                 write metrics and return success even on compare failures
  --out-dir DIR                 local artifact directory (default: /tmp/...)
  --rom PATH                    ROM path (default: ./baserom.u.z64)
  --binary PATH                 native binary path (default: build/ge007)
  --build-dir DIR               CMake build directory (default: build)
  --no-build                    reuse an existing binary
  --timeout SECONDS             capture timeout (default: 180)

Artifacts are ROM-derived local validation data. Do not commit, attach, or
redistribute the raw/WAV captures, screenshots, logs, or JSON metrics if they
contain derived game audio.
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --reference) REFERENCE="$2"; shift 2 ;;
        --reference-format) REFERENCE_FORMAT="$2"; shift 2 ;;
        --reference-raw-rate) REFERENCE_RAW_RATE="$2"; shift 2 ;;
        --reference-raw-channels) REFERENCE_RAW_CHANNELS="$2"; shift 2 ;;
        --reference-raw-endian) REFERENCE_RAW_ENDIAN="$2"; shift 2 ;;
        --test-raw-rate) TEST_RAW_RATE="$2"; shift 2 ;;
        --test-raw-endian) TEST_RAW_ENDIAN="$2"; shift 2 ;;
        --target-rate) TARGET_RATE="$2"; shift 2 ;;
        --reference-start) REFERENCE_START="$2"; shift 2 ;;
        --test-start) TEST_START="$2"; shift 2 ;;
        --duration) DURATION="$2"; shift 2 ;;
        --max-offset-seconds) MAX_OFFSET_SECONDS="$2"; shift 2 ;;
        --min-compared-seconds) MIN_COMPARED_SECONDS="$2"; shift 2 ;;
        --segment-seconds) SEGMENT_SECONDS="$2"; shift 2 ;;
        --segment-hop-seconds) SEGMENT_HOP_SECONDS="$2"; shift 2 ;;
        --frames) FRAMES="$2"; shift 2 ;;
        --no-final-mix-dump) CAPTURE_FINAL_MIX=0; shift ;;
        --report-only) REPORT_ONLY=1; shift ;;
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

case "$REFERENCE_FORMAT" in
    auto|wav|raw) ;;
    *) echo "FAIL: --reference-format must be auto, wav, or raw" >&2; exit 2 ;;
esac
case "$REFERENCE_RAW_ENDIAN" in
    little|big) ;;
    *) echo "FAIL: --reference-raw-endian must be little or big" >&2; exit 2 ;;
esac
case "$TEST_RAW_ENDIAN" in
    little|big) ;;
    *) echo "FAIL: --test-raw-endian must be little or big" >&2; exit 2 ;;
esac

if [[ ! "$FRAMES" =~ ^[1-9][0-9]*$ ]]; then
    echo "FAIL: --frames must be a positive integer" >&2
    exit 2
fi
for rate_value in "$REFERENCE_RAW_RATE" "$TEST_RAW_RATE" "$TARGET_RATE"; do
    if [[ ! "$rate_value" =~ ^[1-9][0-9]*$ ]]; then
        echo "FAIL: raw and target rates must be positive integers" >&2
        exit 2
    fi
done

if [[ -z "$BINARY" ]]; then
    BINARY="$(validation_binary_path "$BUILD_DIR")"
else
    BINARY="$(validation_resolve_path "$BINARY")"
fi
ROM="$(validation_resolve_path "$ROM")"
OUT_DIR="$(validation_resolve_path "$OUT_DIR")"
if [[ -n "$REFERENCE" ]]; then
    REFERENCE="$(validation_resolve_path "$REFERENCE")"
fi

if [[ "$DO_BUILD" -eq 1 ]]; then
    validation_configure_build "$BUILD_DIR" >/dev/null
    validation_build "$BUILD_DIR" >/dev/null
fi

validation_require_binary "$BINARY"
validation_require_file "$ROM" "ROM"
if [[ -n "$REFERENCE" ]]; then
    validation_require_file "$REFERENCE" "reference capture"
fi
if ! command -v python3 >/dev/null 2>&1; then
    echo "FAIL: python3 is required for audio comparison tooling" >&2
    exit 2
fi

validation_acquire_runtime_lock
trap 'validation_release_runtime_lock' EXIT INT TERM

mkdir -p "$OUT_DIR/savedir"

MGB64_DUMP="$OUT_DIR/mgb64_boot_music.raw"
MGB64_FINAL_DUMP="$OUT_DIR/mgb64_boot_final.raw"
AUDIO_TRACE="$OUT_DIR/mgb64_audio_trace.jsonl"
MUSIC_TRACE="$OUT_DIR/mgb64_music_trace.jsonl"
COMPARE_JSON="$OUT_DIR/boot_audio_compare.json"
SUMMARY_MD="$OUT_DIR/startup_music_reference_summary.md"
CAPTURE_LOG="$OUT_DIR/mgb64_capture.log"
rm -f "$MGB64_DUMP" "$MGB64_FINAL_DUMP" "$AUDIO_TRACE" "$MUSIC_TRACE" \
    "$COMPARE_JSON" "$SUMMARY_MD" "$CAPTURE_LOG"

echo "=== Startup Music Reference Check ==="
echo "  out-dir:   $OUT_DIR"
echo "  binary:    $BINARY"
echo "  ROM:       $ROM"
echo "  frames:    $FRAMES"
echo "  final mix: $([[ "$CAPTURE_FINAL_MIX" -eq 1 ]] && echo yes || echo no)"
if [[ -n "$REFERENCE" ]]; then
    echo "  reference: $REFERENCE"
else
    echo "  reference: (none; capture-only)"
fi

CAPTURE_ENV=(env -u GE007_DEBUG
    SDL_AUDIODRIVER="${GE007_VALIDATION_SDL_AUDIODRIVER:-dummy}"
    GE007_DETERMINISTIC_STABLE_COUNT=1
    GE007_MUSIC_AUDIO_DUMP="$MGB64_DUMP"
    GE007_MUSIC_AUDIO_DUMP_FRAMES="$FRAMES"
    GE007_AUDIO_TRACE="$AUDIO_TRACE"
    GE007_MUSIC_TRACE="$MUSIC_TRACE"
    GE007_MUSIC_TRACE_SNAPSHOT=1
    GE007_NO_VSYNC=1
    GE007_BACKGROUND=1
    GE007_NO_INPUT_GRAB=1)
if [[ "$CAPTURE_FINAL_MIX" -eq 1 ]]; then
    CAPTURE_ENV+=(
        GE007_AUDIO_DUMP="$MGB64_FINAL_DUMP"
        GE007_AUDIO_DUMP_FRAMES="$FRAMES")
fi
CAPTURE_CMD=("${CAPTURE_ENV[@]}"
    "$BINARY"
    --rom "$ROM"
    --deterministic
    --savedir "$OUT_DIR/savedir"
    --screenshot-frame "$FRAMES"
    --screenshot-label "startup_music_$$"
    --screenshot-exit)

echo ""
echo "== Capturing MGB64 startup music dump =="
if [[ -n "$TIMEOUT_BIN" ]]; then
    (
        cd "$OUT_DIR"
        "$TIMEOUT_BIN" --kill-after=5 "$TIMEOUT_SECONDS" "${CAPTURE_CMD[@]}"
    ) >"$CAPTURE_LOG" 2>&1 || {
        echo "FAIL: MGB64 startup capture failed"
        tail -60 "$CAPTURE_LOG" | sed 's/^/  /'
        exit 1
    }
else
    (
        cd "$OUT_DIR"
        "${CAPTURE_CMD[@]}"
    ) >"$CAPTURE_LOG" 2>&1 || {
        echo "FAIL: MGB64 startup capture failed"
        tail -60 "$CAPTURE_LOG" | sed 's/^/  /'
        exit 1
    }
fi

if [[ ! -s "$MGB64_DUMP" ]]; then
    echo "FAIL: missing MGB64 music dump: $MGB64_DUMP"
    tail -60 "$CAPTURE_LOG" | sed 's/^/  /'
    exit 1
fi

echo "PASS: captured $(wc -c < "$MGB64_DUMP" | tr -d '[:space:]') bytes -> $MGB64_DUMP"
if [[ "$CAPTURE_FINAL_MIX" -eq 1 ]]; then
    if [[ ! -s "$MGB64_FINAL_DUMP" ]]; then
        echo "FAIL: missing final mixed PCM dump: $MGB64_FINAL_DUMP"
        tail -60 "$CAPTURE_LOG" | sed 's/^/  /'
        exit 1
    fi
    echo "PASS: captured $(wc -c < "$MGB64_FINAL_DUMP" | tr -d '[:space:]') bytes -> $MGB64_FINAL_DUMP"
fi
if [[ -s "$AUDIO_TRACE" ]]; then
    echo "PASS: audio trace -> $AUDIO_TRACE"
fi
if [[ -s "$MUSIC_TRACE" ]]; then
    echo "PASS: music trace -> $MUSIC_TRACE"
fi

write_summary() {
    local compare_status="${1:-}"
    python3 - "$SUMMARY_MD" "$OUT_DIR" "$MGB64_DUMP" "$MGB64_FINAL_DUMP" \
        "$AUDIO_TRACE" "$MUSIC_TRACE" "$COMPARE_JSON" "$FRAMES" "$REFERENCE" "$compare_status" <<'PY'
import json
import os
import sys
from pathlib import Path

summary, out_dir, music_raw, final_raw, audio_trace, music_trace, compare_json, frames, reference, compare_status = sys.argv[1:]

def size_line(path):
    if not path or not os.path.exists(path):
        return "missing"
    return f"{os.path.getsize(path)} bytes"

lines = [
    "# Startup Music Reference Summary",
    "",
    "Local ROM-derived audio artifacts are not redistributable.",
    "",
    f"- Out dir: `{out_dir}`",
    f"- Frames: `{frames}`",
    f"- MGB64 music raw: `{music_raw}` ({size_line(music_raw)})",
    f"- MGB64 final raw: `{final_raw}` ({size_line(final_raw)})",
    f"- Audio trace: `{audio_trace}` ({size_line(audio_trace)})",
    f"- Music trace: `{music_trace}` ({size_line(music_trace)})",
]
if reference:
    lines.append(f"- Reference: `{reference}` ({size_line(reference)})")
if compare_status:
    lines.append(f"- Compare exit status: `{compare_status}`")

if os.path.exists(audio_trace):
    trace_frames = 0
    max_peak = 0
    rail_hits = 0
    rail_frames = 0
    sample_total = 0
    sample_counts = {}
    clamp_delta_totals = {
        "adpcm": 0,
        "resample": 0,
        "env_mixer": 0,
        "mix": 0,
        "pole_filter": 0,
    }
    with open(audio_trace, "r", encoding="utf-8") as f:
        for line in f:
            if not line.strip():
                continue
            row = json.loads(line)
            trace_frames += 1
            samples = int(row.get("samples") or 0)
            sample_total += samples
            sample_counts[samples] = sample_counts.get(samples, 0) + 1
            max_peak = max(max_peak, int(row.get("output_peak") or 0))
            frame_rails = int(row.get("output_rail_hits") or 0)
            rail_hits += frame_rails
            if frame_rails:
                rail_frames += 1
            clamp_delta_totals["adpcm"] += int(row.get("adpcm_clamp_delta") or 0)
            clamp_delta_totals["resample"] += int(row.get("resample_clamp_delta") or 0)
            clamp_delta_totals["env_mixer"] += int(row.get("env_mixer_clamp_delta") or 0)
            clamp_delta_totals["mix"] += int(row.get("mix_clamp_delta") or 0)
            clamp_delta_totals["pole_filter"] += int(row.get("pole_filter_clamp_delta") or 0)
    lines.extend([
        f"- Audio trace frames: `{trace_frames}`",
        f"- Audio sample frames: `{sample_total}`",
        f"- Audio average samples/frame: `{(sample_total / trace_frames) if trace_frames else 0:.3f}`",
        "- Audio frame sizes: "
        + ", ".join(
            f"`{samples}={count}`" for samples, count in sorted(sample_counts.items())
        ),
        f"- Audio max peak: `{max_peak}`",
        f"- Audio rail hits: `{rail_hits}` across `{rail_frames}` frame(s)",
        "- Mixer clamp deltas: "
        + ", ".join(f"`{name}={value}`" for name, value in clamp_delta_totals.items()),
    ])

if os.path.exists(music_trace):
    events = []
    last_snapshot = {}
    with open(music_trace, "r", encoding="utf-8") as f:
        for line in f:
            if not line.strip():
                continue
            row = json.loads(line)
            event = row.get("event")
            if event == "snapshot":
                last_snapshot[int(row.get("slot") or 0)] = row
            elif event:
                events.append(row)
    lines.extend([
        f"- Music events: `{len(events)}`",
    ])
    if events:
        lines.append("- Music event timeline:")
        for row in events[:24]:
            frame = int(row.get("frame") or 0)
            seconds = frame / 30.0
            lines.append(
                f"  - frame `{frame}` (`{seconds:.2f}s`): "
                f"`{row.get('event')}` slot `{row.get('slot')}` "
                f"track `{row.get('track')}` previous `{row.get('previous')}` "
                f"state `{row.get('state')}`"
            )
        if len(events) > 24:
            lines.append(f"  - ... `{len(events) - 24}` more event(s)")
    if last_snapshot:
        lines.append("- Final music slots:")
        for slot in sorted(last_snapshot):
            row = last_snapshot[slot]
            lines.append(
                f"  - slot `{slot}` track `{row.get('track')}` "
                f"state `{row.get('state')}` volume `{row.get('volume')}` "
                f"seqp `{row.get('seqp_volume')}` fade `{row.get('fade')}` "
                f"remaining `{row.get('fade_remaining')}` "
                f"seq_ticks `{row.get('seq_ticks')}` "
                f"cur_time `{row.get('cur_time')}` "
                f"next_delta `{row.get('next_delta')}` "
                f"uspt `{row.get('uspt')}` "
                f"voices `{row.get('active_voices')}` "
                f"queued `{row.get('queued_events')}`"
            )

if os.path.exists(compare_json):
    with open(compare_json, "r", encoding="utf-8") as f:
        report = json.load(f)
    lines.extend([
        "",
        "## Metrics",
        "",
        f"- Compared seconds: `{report.get('compared_seconds', 0):.2f}`",
        f"- Lag seconds: `{report.get('lag_seconds', 0):+.3f}`",
        f"- Envelope correlation: `{report.get('envelope_corr', 0):.3f}`",
        f"- Spectral cosine: `{report.get('spectral_cosine', 0):.3f}`",
        f"- RMS delta dB: `{report.get('rms_delta_db', 0):+.2f}`",
        f"- Relative band MAE dB: `{report.get('relative_band_mae_db', 0):.2f}`",
        f"- Relative high-band delta dB: `{report.get('high_relative_band_delta_db', 0):+.2f}`",
    ])
    stereo = report.get("stereo")
    if stereo:
        lines.extend([
            f"- Stereo balance delta dB: `{stereo.get('balance_delta_db', 0):+.2f}`",
            f"- Stereo width delta dB: `{stereo.get('width_delta_db', 0):+.2f}`",
            f"- Possible channel swap: `{bool(stereo.get('possible_channel_swap'))}`",
        ])
    failures = report.get("failures") or []
    lines.append(f"- Failures: `{len(failures)}`")
    for failure in failures:
        lines.append(f"  - `{failure}`")
    diagnosis = report.get("diagnosis", {}).get("overall", [])
    if diagnosis:
        lines.extend(["", "## Diagnosis", ""])
        for note in diagnosis:
            lines.append(f"- {note}")

Path(summary).write_text("\n".join(lines) + "\n", encoding="utf-8")
PY
    echo "Summary: $SUMMARY_MD"
}

if [[ -z "$REFERENCE" ]]; then
    write_summary ""
    echo ""
    echo "Capture-only mode complete. To compare later:"
    echo "  tools/startup_music_reference_check.sh \\"
    echo "    --no-build --rom \"$ROM\" --binary \"$BINARY\" \\"
    echo "    --out-dir \"$OUT_DIR\" --reference /tmp/mgb64_audio_ref/reference_boot.wav"
    exit 0
fi

COMPARE_CMD=(python3 tools/compare_audio_reference.py
    "$REFERENCE"
    "$MGB64_DUMP"
    --reference-format "$REFERENCE_FORMAT"
    --reference-raw-rate "$REFERENCE_RAW_RATE"
    --reference-raw-channels "$REFERENCE_RAW_CHANNELS"
    --reference-raw-endian "$REFERENCE_RAW_ENDIAN"
    --test-format raw
    --test-raw-rate "$TEST_RAW_RATE"
    --target-rate "$TARGET_RATE"
    --test-raw-endian "$TEST_RAW_ENDIAN"
    --reference-start "$REFERENCE_START"
    --test-start "$TEST_START"
    --max-offset-seconds "$MAX_OFFSET_SECONDS"
    --min-compared-seconds "$MIN_COMPARED_SECONDS"
    --segment-seconds "$SEGMENT_SECONDS"
    --segment-hop-seconds "$SEGMENT_HOP_SECONDS"
    --print-bands
    --print-segments
    --json-out "$COMPARE_JSON")
if [[ -n "$DURATION" ]]; then
    COMPARE_CMD+=(--duration "$DURATION")
fi

echo ""
echo "== Comparing against reference =="
COMPARE_STATUS=0
"${COMPARE_CMD[@]}" || COMPARE_STATUS="$?"
write_summary "$COMPARE_STATUS"
if [[ "$COMPARE_STATUS" -ne 0 && "$REPORT_ONLY" -ne 1 ]]; then
    echo ""
    echo "FAIL: comparison reported differences; use --report-only for exploratory captures"
    exit "$COMPARE_STATUS"
fi
echo ""
echo "Metrics JSON: $COMPARE_JSON"
