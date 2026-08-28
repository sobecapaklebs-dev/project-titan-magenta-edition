#!/usr/bin/env bash
#
# package_windows_zip.sh -- package the built Windows `ge007.exe` app into a
# portable .zip (ge007.exe + SDL2.dll + docs).
#
# Runs in the release CI under MSYS2/MinGW64, or locally on Windows. Produces:
#   dist/mgb64-windows-<version>.zip
#
# The app ships NO game data (bring-your-own-ROM); nothing here embeds ROM bytes.
set -euo pipefail
cd "$(git rev-parse --show-toplevel 2>/dev/null || pwd)"

binary="build/ge007.exe"
version="dev"
while [[ $# -gt 0 ]]; do
  case "$1" in
    --binary) binary="$2"; shift 2 ;;
    --version) version="$2"; shift 2 ;;
    -h|--help) echo "Usage: $0 [--binary PATH] [--version VER]"; exit 0 ;;
    *) echo "Unknown arg: $1" >&2; exit 1 ;;
  esac
done
[[ -f "$binary" ]] || { echo "ERROR: binary not found: $binary" >&2; exit 1; }

dist="dist"; mkdir -p "$dist"
stage="$(mktemp -d)/MGB64"
mkdir -p "$stage"
cp "$binary" "$stage/ge007.exe"

# Bundle the runtime DLLs the exe links (SDL2 + the MinGW runtime libs), resolved
# via ldd against the MSYS2 MinGW prefix (skip Windows system DLLs).
mingw_prefix="${MINGW_PREFIX:-/mingw64}"
ldd "$binary" | awk '{print $3}' | while read -r dll; do
  case "$dll" in
    "$mingw_prefix"/*) cp -u "$dll" "$stage/" && echo "bundled $(basename "$dll")" ;;
  esac
done
# Ensure SDL2.dll made it (belt and suspenders).
[[ -f "$stage/SDL2.dll" ]] || { [[ -f "$mingw_prefix/bin/SDL2.dll" ]] && cp "$mingw_prefix/bin/SDL2.dll" "$stage/"; }

cp LICENSE README.md "$stage/" 2>/dev/null || true
# Community controller-mapping DB (MC.2), next to the exe where SDL_GetBasePath()
# resolves it at controller init.
cp lib/sdl_gamecontrollerdb/gamecontrollerdb.txt "$stage/" 2>/dev/null || true
# Windows install + setup guide (writable install dir, ROM placement, adding to a
# game library, save/log locations). Staged into the zip so it ships with the exe.
cp docs/WINDOWS_SETUP.md "$stage/" 2>/dev/null || true
cat > "$stage/RUN_ME.txt" <<'EOF'
MGB64 (Windows portable build)

1. Unzip to a writable folder you own (e.g. C:\Games\MGB64), NOT Program Files.
2. Double-click ge007.exe to open the launcher.
3. Click "Choose ROM..." and pick your own legally-dumped GoldenEye 007 ROM.
4. This app ships NO game data. See WINDOWS_SETUP.md, README.md / DISCLAIMER.md.
EOF

( cd "$(dirname "$stage")" && zip -r -q "$OLDPWD/$dist/mgb64-windows-$version.zip" MGB64 )
echo "wrote $dist/mgb64-windows-$version.zip"
rm -rf "$(dirname "$stage")"
