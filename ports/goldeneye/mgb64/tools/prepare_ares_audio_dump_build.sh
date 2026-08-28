#!/bin/bash
#
# prepare_ares_audio_dump_build.sh -- Build a local instrumented ares binary.
#
# This clones ares into ignored local build space, applies a tiny PCM dump hook,
# and builds the desktop frontend. It does not vendor ares source and does not
# create redistributable game audio. Use the resulting binary with
# tools/ares_startup_audio_reference.sh.
#
set -euo pipefail
cd "$(dirname "$0")/.."

ARES_REPO_URL="${ARES_REPO_URL:-https://github.com/ares-emulator/ares.git}"
ARES_REF="${ARES_REF:-91b112279ab2ce89c5fc9bff5dbb81e29af51a68}"
WORK_DIR="${MGB64_ARES_AUDIO_REF_DIR:-$PWD/build/ares-audio-dump}"
JOBS="${JOBS:-}"
FORCE=0
NO_BUILD=0

usage() {
    cat <<'USAGE'
Usage: tools/prepare_ares_audio_dump_build.sh [options]

Options:
  --work-dir DIR       ignored local workspace (default: build/ares-audio-dump)
  --ares-ref REF       ares commit/tag/branch (default: pinned known-good commit)
  --jobs N             parallel build jobs (default: host CPU count)
  --no-build           clone/patch only
  --force              delete and recreate the local ares checkout

The checkout and build are local-only. Do not commit ares source, ROM-derived
audio dumps, or generated captures.
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --work-dir) WORK_DIR="$2"; shift 2 ;;
        --ares-ref) ARES_REF="$2"; shift 2 ;;
        --jobs) JOBS="$2"; shift 2 ;;
        --no-build) NO_BUILD=1; shift ;;
        --force) FORCE=1; shift ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown arg: $1" >&2; usage >&2; exit 2 ;;
    esac
done

if [[ -z "$JOBS" ]]; then
    if command -v sysctl >/dev/null 2>&1; then
        JOBS="$(sysctl -n hw.ncpu 2>/dev/null || true)"
    fi
    if [[ -z "$JOBS" ]] && command -v getconf >/dev/null 2>&1; then
        JOBS="$(getconf _NPROCESSORS_ONLN 2>/dev/null || true)"
    fi
    JOBS="${JOBS:-4}"
fi
if [[ ! "$JOBS" =~ ^[1-9][0-9]*$ ]]; then
    echo "FAIL: --jobs must be a positive integer" >&2
    exit 2
fi

if ! command -v git >/dev/null 2>&1; then
    echo "FAIL: git is required" >&2
    exit 2
fi
if ! command -v cmake >/dev/null 2>&1; then
    echo "FAIL: cmake is required" >&2
    exit 2
fi
if ! command -v python3 >/dev/null 2>&1; then
    echo "FAIL: python3 is required" >&2
    exit 2
fi

WORK_DIR="$(python3 - "$WORK_DIR" <<'PY'
import os
import sys
print(os.path.abspath(sys.argv[1]))
PY
)"
SRC_DIR="$WORK_DIR/ares"
BUILD_DIR="$SRC_DIR/build-audio-dump"

if [[ "$FORCE" -eq 1 && -e "$SRC_DIR" ]]; then
    rm -rf "$SRC_DIR"
fi

mkdir -p "$WORK_DIR"
if [[ ! -d "$SRC_DIR/.git" ]]; then
    git clone --depth 1 "$ARES_REPO_URL" "$SRC_DIR"
fi

if ! git -C "$SRC_DIR" diff --quiet || ! git -C "$SRC_DIR" diff --cached --quiet; then
    echo "FAIL: local ares checkout has uncommitted changes: $SRC_DIR" >&2
    echo "Use --force to recreate it, or clean that checkout manually." >&2
    exit 1
fi

git -C "$SRC_DIR" fetch origin "$ARES_REF" --depth 1 2>/dev/null || git -C "$SRC_DIR" fetch origin "$ARES_REF"
git -C "$SRC_DIR" checkout --detach "$ARES_REF"

python3 - "$SRC_DIR" <<'PY'
from pathlib import Path
import sys

src = Path(sys.argv[1])

audio_cpp = src / "ruby/audio/audio.cpp"
text = audio_cpp.read_text(encoding="utf-8")
if "ARES_AUDIO_DUMP" not in text:
    text = text.replace(
        "#include <SDL3/SDL.h>\n",
        "#include <SDL3/SDL.h>\n"
        "#include <cmath>\n"
        "#include <cstdio>\n"
        "#include <cstdlib>\n",
        1,
    )
    helper = r'''
namespace {

struct AudioDump {
  FILE* file = nullptr;
  u64 framesWritten = 0;
  u64 frameLimit = 0;
  bool configured = false;
  bool complete = false;

  auto configure() -> void {
    if(configured) return;
    configured = true;

    auto path = getenv("ARES_AUDIO_DUMP");
    if(!path || !*path) {
      complete = true;
      return;
    }

    if(auto frames = getenv("ARES_AUDIO_DUMP_FRAMES")) {
      char* end = nullptr;
      auto parsed = strtoull(frames, &end, 10);
      if(end && *end == 0) frameLimit = parsed;
    }

    file = fopen(path, "wb");
    if(!file) {
      fprintf(stderr, "ares audio dump: failed to open %s\n", path);
      complete = true;
      return;
    }

    setvbuf(file, nullptr, _IONBF, 0);
    fprintf(stderr, "ares audio dump: writing s16le stereo PCM to %s", path);
    if(frameLimit) fprintf(stderr, " (%llu frames)", (unsigned long long)frameLimit);
    fprintf(stderr, "\n");
  }

  auto clampS16(f64 sample) -> s16 {
    if(!std::isfinite(sample)) sample = 0.0;
    if(sample > 1.0) sample = 1.0;
    if(sample < -1.0) sample = -1.0;

    auto scale = sample < 0.0 ? 32768.0 : 32767.0;
    auto value = (s32)std::lround(sample * scale);
    if(value > 32767) value = 32767;
    if(value < -32768) value = -32768;
    return (s16)value;
  }

  auto output(const f64 samples[]) -> void {
    configure();
    if(complete || !file) return;

    auto left = clampS16(samples[0]);
    auto right = clampS16(samples[1]);
    u8 bytes[4] = {
      (u8)((u16)left & 0xff),
      (u8)(((u16)left >> 8) & 0xff),
      (u8)((u16)right & 0xff),
      (u8)(((u16)right >> 8) & 0xff),
    };
    fwrite(bytes, 1, sizeof(bytes), file);

    framesWritten++;
    if(frameLimit && framesWritten >= frameLimit) {
      fclose(file);
      file = nullptr;
      complete = true;
      fprintf(stderr, "ares audio dump: captured %llu frames\n", (unsigned long long)framesWritten);
    }
  }

  ~AudioDump() {
    if(file) fclose(file);
  }
};

auto audioDump() -> AudioDump& {
  static AudioDump dump;
  return dump;
}

}

'''
    text = text.replace("\nnamespace ruby {\n", "\n" + helper + "namespace ruby {\n", 1)
    text = text.replace(
        "  output[0] = samples[0];\n"
        "  output[1] = samples[1];\n"
        "  SDL_PutAudioStreamData",
        "  output[0] = samples[0];\n"
        "  output[1] = samples[1];\n"
        "  audioDump().output(samples);\n"
        "  SDL_PutAudioStreamData",
        1,
    )
    audio_cpp.write_text(text, encoding="utf-8")

compilerconfig = src / "cmake/macos/compilerconfig.cmake"
if compilerconfig.exists():
    text = compilerconfig.read_text(encoding="utf-8")
    if "--show-sdk-version" not in text:
        needle = """  execute_process(
    COMMAND xcrun --sdk macosx --show-sdk-platform-version
    OUTPUT_VARIABLE ares_macos_current_sdk
    RESULT_VARIABLE result
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
"""
        replacement = needle + """  if(NOT result EQUAL 0)
    execute_process(
      COMMAND xcrun --sdk macosx --show-sdk-version
      OUTPUT_VARIABLE ares_macos_current_sdk
      RESULT_VARIABLE result
      OUTPUT_STRIP_TRAILING_WHITESPACE
    )
  endif()
"""
        text = text.replace(needle, replacement, 1)
        compilerconfig.write_text(text, encoding="utf-8")

macos_ui = src / "desktop-ui/cmake/os-macos.cmake"
if macos_ui.exists():
    text = macos_ui.read_text(encoding="utf-8")
    text = text.replace(
        "if(ACTOOL_PROGRAM)\n",
        'if(ACTOOL_PROGRAM AND NOT "$ENV{ARES_CLT_BUILD}" STREQUAL "1")\n',
        1,
    )
    macos_ui.write_text(text, encoding="utf-8")
PY

if [[ "$NO_BUILD" -eq 1 ]]; then
    echo "Prepared instrumented ares checkout: $SRC_DIR"
    exit 0
fi

ARES_CLT_BUILD=1 cmake -S "$SRC_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD_DIR" --target desktop-ui --parallel "$JOBS"

if [[ -x "$BUILD_DIR/desktop-ui/ares.app/Contents/MacOS/ares" ]]; then
    ARES_BIN="$BUILD_DIR/desktop-ui/ares.app/Contents/MacOS/ares"
elif [[ -x "$BUILD_DIR/desktop-ui/ares" ]]; then
    ARES_BIN="$BUILD_DIR/desktop-ui/ares"
else
    echo "FAIL: built desktop-ui target, but could not find the ares executable" >&2
    exit 1
fi

echo ""
echo "PASS: instrumented ares binary:"
echo "  $ARES_BIN"
echo ""
echo "Capture a local startup reference with:"
echo "  tools/ares_startup_audio_reference.sh --ares-bin \"$ARES_BIN\" --rom baserom.u.z64"
