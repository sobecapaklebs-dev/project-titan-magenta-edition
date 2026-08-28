#!/bin/bash
#
# ares_startup_audio_reference.sh -- Capture a local ares startup audio reference.
#
# This requires an instrumented ares build that honors ARES_AUDIO_DUMP and
# writes signed 16-bit stereo PCM. Stock ares builds normally do not expose this
# hook; in that case the script fails without creating a pretend reference.
#
set -euo pipefail
cd "$(dirname "$0")/.."

source tools/validation_common.sh

ARES_BIN="/Applications/ares.app/Contents/MacOS/ares"
ROM="$(validation_default_rom)"
OUT_DIR="/tmp/mgb64_ares_audio_ref_$$"
RUN_SECONDS=85
RATE=44100
CHANNELS=2
DUMP_FRAMES=""
BOOT_FAST=1

usage() {
    cat <<'USAGE'
Usage: tools/ares_startup_audio_reference.sh [options]

Options:
  --ares-bin PATH       ares binary (default: /Applications/ares.app/Contents/MacOS/ares)
  --rom PATH            ROM path (default: ./baserom.u.z64)
  --out-dir DIR         local artifact directory (default: /tmp/...)
  --seconds N           wall-clock seconds before terminating ares (default: 85)
  --rate HZ             expected raw reference sample rate (default: 44100)
  --channels N          expected raw reference channels (default: 2)
  --dump-frames N       ARES_AUDIO_DUMP_FRAMES value (default: seconds * rate)
  --no-fast-boot        set Boot/Fast=false for the reference run

The raw output is ROM-derived game audio. Keep it local; do not commit, attach,
or redistribute it.
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --ares-bin) ARES_BIN="$2"; shift 2 ;;
        --rom) ROM="$2"; shift 2 ;;
        --out-dir) OUT_DIR="$2"; shift 2 ;;
        --seconds) RUN_SECONDS="$2"; shift 2 ;;
        --rate) RATE="$2"; shift 2 ;;
        --channels) CHANNELS="$2"; shift 2 ;;
        --dump-frames) DUMP_FRAMES="$2"; shift 2 ;;
        --no-fast-boot) BOOT_FAST=0; shift ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown arg: $1" >&2; usage >&2; exit 2 ;;
    esac
done

for value_name in RUN_SECONDS RATE CHANNELS; do
    value="${!value_name}"
    if [[ ! "$value" =~ ^[1-9][0-9]*$ ]]; then
        echo "FAIL: ${value_name} must be a positive integer, got: $value" >&2
        exit 2
    fi
done
if [[ -z "$DUMP_FRAMES" ]]; then
    DUMP_FRAMES=$((RUN_SECONDS * RATE))
elif [[ ! "$DUMP_FRAMES" =~ ^[1-9][0-9]*$ ]]; then
    echo "FAIL: --dump-frames must be a positive integer" >&2
    exit 2
fi
EXPECTED_BYTES=$((DUMP_FRAMES * CHANNELS * 2))

ARES_BIN="$(validation_resolve_path "$ARES_BIN")"
ROM="$(validation_resolve_path "$ROM")"
OUT_DIR="$(validation_resolve_path "$OUT_DIR")"

validation_require_file "$ARES_BIN" "ares binary"
validation_require_file "$ROM" "ROM"
if ! command -v python3 >/dev/null 2>&1; then
    echo "FAIL: python3 is required for manifest generation" >&2
    exit 2
fi

if pgrep -f "$ARES_BIN" >/dev/null 2>&1; then
    echo "FAIL: ares is already running; close it before capturing a reference" >&2
    exit 2
fi

validation_acquire_runtime_lock
ARES_PID=""
cleanup() {
    if [[ -n "$ARES_PID" ]] && kill -0 "$ARES_PID" 2>/dev/null; then
        kill "$ARES_PID" 2>/dev/null || true
        for _ in 1 2 3 4 5; do
            if ! kill -0 "$ARES_PID" 2>/dev/null; then
                break
            fi
            sleep 0.2
        done
        if kill -0 "$ARES_PID" 2>/dev/null; then
            kill -9 "$ARES_PID" 2>/dev/null || true
        fi
        wait "$ARES_PID" 2>/dev/null || true
    fi
    validation_release_runtime_lock
}
trap cleanup EXIT INT TERM

mkdir -p "$OUT_DIR"
REFERENCE_RAW="$OUT_DIR/ares_boot_${RATE}.raw"
LOG="$OUT_DIR/ares.log"
MANIFEST="$OUT_DIR/ares_reference_manifest.json"
SETTINGS_FILE="$OUT_DIR/ares_settings.bml"
SAVES_DIR="$OUT_DIR/ares_saves"
rm -f "$REFERENCE_RAW" "$LOG" "$MANIFEST" "$SETTINGS_FILE"
rm -rf "$SAVES_DIR"
mkdir -p "$SAVES_DIR"

echo "=== ares Startup Audio Reference ==="
echo "  out-dir:     $OUT_DIR"
echo "  ares:        $ARES_BIN"
echo "  ROM:         $ROM"
echo "  seconds:     $RUN_SECONDS"
echo "  raw format:  s16le ${CHANNELS}ch @ ${RATE} Hz"
echo "  dump frames: $DUMP_FRAMES"
echo "  max bytes:   $EXPECTED_BYTES"

BOOT_FAST_VALUE="true"
if [[ "$BOOT_FAST" -eq 0 ]]; then
    BOOT_FAST_VALUE="false"
fi

env \
    ARES_AUDIO_DUMP="$REFERENCE_RAW" \
    ARES_AUDIO_DUMP_FRAMES="$DUMP_FRAMES" \
    "$ARES_BIN" \
    --kiosk \
    --settings-file "$SETTINGS_FILE" \
    --setting Audio/Mute=false \
    --setting Audio/Frequency="$RATE" \
    --setting Audio/Blocking=false \
    --setting Audio/Dynamic=false \
    --setting Input/Defocus=Allow \
    --setting General/AutoSaveMemory=false \
    --setting Paths/Saves="$SAVES_DIR" \
    --setting Boot/Fast="$BOOT_FAST_VALUE" \
    --setting Video/Blocking=false \
    --system "Nintendo 64" \
    --no-file-prompt \
    "$ROM" >"$LOG" 2>&1 &
ARES_PID="$!"

elapsed=0
while kill -0 "$ARES_PID" 2>/dev/null; do
    if [[ -f "$REFERENCE_RAW" ]]; then
        current_bytes="$(wc -c < "$REFERENCE_RAW" | tr -d '[:space:]')"
        if [[ "$current_bytes" =~ ^[0-9]+$ && "$current_bytes" -ge "$EXPECTED_BYTES" ]]; then
            break
        fi
    fi
    if [[ "$elapsed" -ge "$RUN_SECONDS" ]]; then
        break
    fi
    sleep 1
    elapsed=$((elapsed + 1))
done

cleanup
trap - EXIT INT TERM

if [[ ! -s "$REFERENCE_RAW" ]]; then
    echo "FAIL: no ares audio dump was written: $REFERENCE_RAW" >&2
    echo "This usually means the selected ares binary is a stock build without" >&2
    echo "ARES_AUDIO_DUMP instrumentation. Use a hardware/movie WAV reference," >&2
    echo "or run this script with --ares-bin pointing at an instrumented build." >&2
    echo "Log: $LOG" >&2
    exit 1
fi

python3 - "$MANIFEST" "$REFERENCE_RAW" "$LOG" "$SETTINGS_FILE" "$SAVES_DIR" "$ARES_BIN" "$ROM" \
    "$RATE" "$CHANNELS" "$DUMP_FRAMES" "$EXPECTED_BYTES" "$RUN_SECONDS" "$BOOT_FAST_VALUE" <<'PY'
import json
import os
import sys
from pathlib import Path

manifest, raw_path, log_path, settings_path, saves_dir, ares_bin, rom_path, rate, channels, dump_frames, expected_bytes, seconds, boot_fast = sys.argv[1:]
byte_count = os.path.getsize(raw_path)
frame_size = int(channels) * 2
payload = {
    "reference_raw": os.path.abspath(raw_path),
    "log": os.path.abspath(log_path),
    "settings_file": os.path.abspath(settings_path),
    "saves_dir": os.path.abspath(saves_dir),
    "ares_bin": os.path.abspath(ares_bin),
    "rom": os.path.abspath(rom_path),
    "format": "raw",
    "sample_format": "s16le",
    "sample_rate": int(rate),
    "channels": int(channels),
    "dump_frames": int(dump_frames),
    "captured_frames": byte_count // frame_size,
    "expected_bytes": int(expected_bytes),
    "complete": byte_count >= int(expected_bytes),
    "wall_seconds": int(seconds),
    "boot_fast": boot_fast == "true",
    "bytes": byte_count,
    "asset_rule": "ROM-derived audio; keep local and do not redistribute",
}
Path(manifest).write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")
PY

echo "PASS: captured $(wc -c < "$REFERENCE_RAW" | tr -d '[:space:]') bytes -> $REFERENCE_RAW"
if [[ "$(wc -c < "$REFERENCE_RAW" | tr -d '[:space:]')" -lt "$EXPECTED_BYTES" ]]; then
    echo "WARN: capture stopped before requested frame count; increase --seconds for a complete reference"
fi
echo "Manifest: $MANIFEST"
echo ""
echo "Compare with:"
echo "  tools/startup_music_reference_check.sh \\"
echo "    --no-build --rom \"$ROM\" --out-dir \"$OUT_DIR/mgb64\" \\"
echo "    --reference \"$REFERENCE_RAW\" --reference-format raw \\"
echo "    --reference-raw-rate \"$RATE\" --reference-raw-channels \"$CHANNELS\" \\"
echo "    --min-compared-seconds 60 --report-only"
