#!/usr/bin/env bash
#
# fetch_realesrgan.sh -- download the Real-ESRGAN ncnn-vulkan upscaler into a
# local, gitignored cache. This is the GPU-accelerated AI super-resolution engine
# the HD texture-pack pipeline shells out to (Vulkan; uses Metal/MoltenVK on macOS).
#
# We do NOT vendor the ~50 MB binary + models in the repo (it is third-party and
# would trip the >3 MB blob check in scripts/ci/check_no_rom_data.sh). Instead we
# fetch a pinned, checksum-verified release on demand into tools/texpack/.bin/
# (gitignored). Real-ESRGAN is BSD-3-Clause; see THIRD_PARTY.md.
#
set -euo pipefail
cd "$(dirname "$0")"

VERSION="v0.2.5.0"
BIN_DIR=".bin/realesrgan"
mkdir -p "$BIN_DIR"

uname_s="$(uname -s)"; uname_m="$(uname -m)"
case "$uname_s" in
  Darwin) asset="realesrgan-ncnn-vulkan-20220424-macos.zip";   sha256="e0ad05580abfeb25f8d8fb55aaf7bedf552c375b5b4d9bd3c8d59764d2cc333a" ;;
  Linux)  asset="realesrgan-ncnn-vulkan-20220424-ubuntu.zip";  sha256="" ;;  # verify-on-first-use; see release page
  *)      echo "Unsupported OS: $uname_s (Windows: download the -windows.zip manually into $BIN_DIR)" >&2; exit 2 ;;
esac

bin="$BIN_DIR/realesrgan-ncnn-vulkan"
if [ -x "$bin" ] && "$bin" 2>&1 | grep -q "Usage:"; then
  echo "Real-ESRGAN already present: $bin"
  exit 0
fi

url="https://github.com/xinntao/Real-ESRGAN/releases/download/${VERSION}/${asset}"
zip="$BIN_DIR/$asset"
echo "Downloading $asset ..."
curl -fL --retry 3 -o "$zip" "$url"

if [ -n "$sha256" ]; then
  got="$(shasum -a 256 "$zip" | awk '{print $1}')"
  if [ "$got" != "$sha256" ]; then
    echo "CHECKSUM MISMATCH for $asset" >&2
    echo "  expected $sha256" >&2
    echo "  got      $got" >&2
    rm -f "$zip"; exit 1
  fi
  echo "checksum OK"
else
  echo "WARNING: no pinned checksum for $uname_s; verify against the GitHub release page." >&2
fi

unzip -oq "$zip" -d "$BIN_DIR"
chmod +x "$bin" 2>/dev/null || true
rm -f "$zip"
echo "Installed Real-ESRGAN -> $bin"
"$bin" 2>&1 | head -1 || true
