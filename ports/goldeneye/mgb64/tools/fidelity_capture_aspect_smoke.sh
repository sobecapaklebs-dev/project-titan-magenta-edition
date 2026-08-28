#!/bin/bash
#
# fidelity_capture_aspect_smoke.sh -- FID-0135 fail-closed screenshot contract.
#
# A normal screenshot remains allowed at a widescreen source and emits the
# existing disclosure. Hardware-reference automation opts into
# GE007_FIDELITY_CAPTURE=1: the same widescreen source must then fail before
# readback (auto-screenshot exit 4, no BMP), while an exact 640x480 source
# succeeds. RenderScale=1 makes the expected source dimensions explicit and
# catches accidental use of a Retina CAMetalLayer size in place of WebGPU's
# readable scene target. The default backend is intentionally used.
#
# Artifacts are ROM-derived local validation data. Do not commit them.
set -euo pipefail
cd "$(dirname "$0")/.."

source tools/validation_common.sh

BUILD_DIR="$(validation_default_build_dir)"
BINARY=""
ROM="$(validation_default_rom)"
DO_BUILD=1
TIMEOUT_SECONDS=90
OUT_DIR="/tmp/mgb64_fidelity_capture_aspect_$$"
LEVEL=33
FRAME=30

usage() {
    cat <<'USAGE'
Usage: tools/fidelity_capture_aspect_smoke.sh [options]

Options:
  --out-dir DIR      output directory (default: /tmp/...)
  --level N          raw LEVELID (default: 33 = Dam)
  --frame N          screenshot frame (default: 30)
  --rom PATH         ROM path (default: ./baserom.u.z64)
  --binary PATH      native binary path (default: build/ge007)
  --build-dir DIR    CMake build directory (default: build)
  --no-build         reuse an existing native binary
  --timeout SECONDS  per-capture timeout (default: 90)

Artifacts are ROM-derived local validation data; do not commit them.
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --out-dir) OUT_DIR="$2"; shift 2 ;;
        --level) LEVEL="$2"; shift 2 ;;
        --frame) FRAME="$2"; shift 2 ;;
        --rom) ROM="$2"; shift 2 ;;
        --binary) BINARY="$2"; shift 2 ;;
        --build-dir) BUILD_DIR="$2"; shift 2 ;;
        --no-build) DO_BUILD=0; shift ;;
        --timeout) TIMEOUT_SECONDS="$2"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown arg: $1" >&2; usage >&2; exit 2 ;;
    esac
done

for pair in "level:$LEVEL" "frame:$FRAME" "timeout:$TIMEOUT_SECONDS"; do
    name="${pair%%:*}"
    value="${pair#*:}"
    if [[ ! "$value" =~ ^[1-9][0-9]*$ ]]; then
        echo "FAIL: --$name must be a positive integer: $value" >&2
        exit 2
    fi
done

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
trap 'validation_release_runtime_lock' EXIT INT TERM

mkdir -p "$OUT_DIR"
OUT_DIR="$(cd "$OUT_DIR" && pwd)"

run_case() {
    local label="$1"
    local width="$2"
    local height="$3"
    local strict="$4"
    local expected_rc="$5"
    local case_dir="$OUT_DIR/$label"
    local save_dir="$case_dir/save"
    local log="$case_dir/run.log"
    local shot="$case_dir/screenshot_${label}.bmp"
    local strict_env=()
    local rc=0

    mkdir -p "$save_dir"
    rm -f "$shot" "$log"
    if [[ "$strict" -eq 1 ]]; then
        strict_env+=(GE007_FIDELITY_CAPTURE=1)
    fi

    echo "  case $label: window=${width}x${height} strict=$strict expect_rc=$expected_rc"
    if (
        cd "$case_dir"
        validation_run_with_timeout "$TIMEOUT_SECONDS" \
            env -u GE007_DEBUG \
                SDL_AUDIODRIVER="$(validation_silent_audio_driver)" \
                GE007_MUTE=1 \
                GE007_DETERMINISTIC_STABLE_COUNT=1 \
                GE007_NO_VSYNC=1 \
                GE007_BACKGROUND=1 \
                GE007_NO_INPUT_GRAB=1 \
                GE007_DISABLE_LEVEL_INTRO=1 \
                ${strict_env[@]+"${strict_env[@]}"} \
                "$BINARY" \
                --savedir "$save_dir" \
                --rom "$ROM" \
                --level "$LEVEL" \
                --deterministic \
                --config-override Video.WindowWidth="$width" \
                --config-override Video.WindowHeight="$height" \
                --config-override Video.WindowMode=windowed \
                --config-override Video.HiDPI=0 \
                --config-override Video.RenderScale=1 \
                --screenshot-frame "$FRAME" \
                --screenshot-label "$label" \
                --screenshot-exit
    ) >"$log" 2>&1; then
        rc=0
    else
        rc=$?
    fi

    if [[ "$rc" -ne "$expected_rc" ]]; then
        echo "FAIL: $label exited $rc, expected $expected_rc" >&2
        tail -60 "$log" | sed 's/^/  /' >&2
        exit 1
    fi

    if [[ "$expected_rc" -eq 0 ]]; then
        if [[ ! -s "$shot" ]]; then
            echo "FAIL: $label succeeded without a screenshot: $shot" >&2
            tail -60 "$log" | sed 's/^/  /' >&2
            exit 1
        fi
        if grep -qF "fidelity capture refused" "$log"; then
            echo "FAIL: $label logged a strict refusal despite succeeding" >&2
            exit 1
        fi
    else
        if [[ -e "$shot" ]]; then
            echo "FAIL: $label wrote a screenshot despite strict rejection" >&2
            exit 1
        fi
        if ! grep -qF "fidelity capture refused: source framebuffer ${width}x${height} is not 4:3" "$log"; then
            echo "FAIL: $label missing the strict aspect-refusal diagnostic" >&2
            tail -60 "$log" | sed 's/^/  /' >&2
            exit 1
        fi
        if ! grep -qF "Auto-screenshot did NOT persist a valid file" "$log"; then
            echo "FAIL: $label missing the auto-capture failure marker" >&2
            tail -60 "$log" | sed 's/^/  /' >&2
            exit 1
        fi
    fi
}

echo "== fidelity capture aspect smoke (FID-0135) =="
run_case legacy_wide 640 360 0 0
if ! grep -qF "the capture ADDS letterbox/pillarbox bars" "$OUT_DIR/legacy_wide/run.log"; then
    echo "FAIL: legacy widescreen capture did not disclose capture-added bars" >&2
    exit 1
fi
run_case strict_wide 640 360 1 4
run_case strict_4x3 640 480 1 0

echo "PASS: legacy wide capture preserved; strict wide rejected; strict 4:3 captured"
echo "artifacts: $OUT_DIR"
