# Building MGB64

> **You must supply your own legally-dumped ROM.** This repository contains no
> ROM and no game assets. The native port can compile without ROM media, but
> nothing here will produce a playable game without a ROM you already own.
> Please read [../DISCLAIMER.md](../DISCLAIMER.md).

There are two build targets:

1. **The native port** (`ge007`) — runs the game on desktop platforms using
   SDL2 + OpenGL. This is what most people want.
2. **The N64 ROM matching target** — contributor-facing decompilation work that
   is still being wired up for byte-identical rebuilds.

## 1. Get a ROM

You need a ROM you legally own to run the native port, but you do not need ROM
media or extracted assets to compile it. The most explicit runtime path is to
keep the ROM wherever you store local game data and pass it with `--rom`:

```sh
./build/ge007 --rom "/path/to/baserom.u.z64"
```

For convenience, the port also auto-detects a few common filenames in the
working directory and nearby user folders. The USA build's conventional local
filename is:

```
baserom.u.z64
```

(`GoldenEye 007 (USA).z64`, `ge007.z64`, and `goldeneye.z64` are also
auto-detected by the port at runtime.) Expected SHA-1s are in `ge007.u.sha1`,
`ge007.e.sha1`, `ge007.j.sha1`.

## 2. Build the native port

**Dependencies:**

| Platform | Install |
| --- | --- |
| Linux (Debian/Ubuntu) | `sudo apt install build-essential cmake pkg-config libsdl2-dev libgl1-mesa-dev python3 python3-pil` |
| macOS | `xcode-select --install` then `brew install cmake sdl2` |
| Windows (MSYS2 MINGW64) | Use the MSYS2 commands below. |

> **Pillow (optional).** Two image-diff regression ctests
> (`port_room_glass_source_reconstruction_guard`,
> `port_glass_shard_pixel_target_selection_guard`) use Pillow. It is already in
> the Linux (`python3-pil`) and MSYS2 (`python-pillow`) package sets above; on
> macOS install it with `python3 -m pip install pillow`. Without Pillow those two
> lanes skip cleanly — they do not fail.

On Windows, use the **MSYS2 MinGW 64-bit** shell, not the plain MSYS shell:

```sh
pacman -Syu
# Restart the shell if pacman asks you to, then:
pacman -S --needed base-devel git make mingw-w64-x86_64-{toolchain,cmake,pkgconf,python,python-pillow,SDL2}
python3 --version
```

Linux/GCC configure, build, and ROM-free CTest are covered by the local
preflight/source-archive smoke lane. Windows/MSYS2 setup is
expected to work but is not yet maintainer-verified; please file a platform
validation or build issue with exact text logs if it does not. If you use MSYS2
UCRT64 instead of MINGW64, use the matching `mingw-w64-ucrt-x86_64-*` package
prefix and mention that in your report.

From macOS/Linux, `tools/mingw_cross_check.sh` cross-compiles the Windows
`ge007.exe` locally (`brew install mingw-w64`; vendors the pinned MSYS2 SDL2
into `build-mingw-deps/`) — also available as an opt-in CTest lane via
`-DPORT_MINGW_CROSS_CHECK=ON` (`ctest -R mingw_cross_check`).

**Build:**

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

For cross-platform copy/paste, `cmake --build build --parallel` is equivalent to
the `-j` form above.

CMake may print messages such as `ROM-derived asset data not present`. That is
expected for the public checkout. The native port reads bulk game data from your
ROM at runtime instead of compiling ROM media into the executable.

To capture and summarize compiler/linker warning noise during a local build:

```sh
cmake --build build --parallel 4 2>&1 | tee /tmp/mgb64-build.log
python3 tools/summarize_build_warnings.py \
  /tmp/mgb64-build.log \
  --json-out /tmp/mgb64-build-warnings.json
```

Useful options: `-DSANITIZE=ON` (ASan/UBSan), `-DMACOS_APP_BUNDLE=ON` (build the
engine as a C library for the macOS app shell).

The `.app` shipped on releases is produced by the in-process ImGui bundler
`macos/Scripts/build_gl_app.sh` (see [RELEASING.md](RELEASING.md)). To build a
local unsigned bundle with the legacy Swift/AppKit shell instead, run:

```sh
./macos/Scripts/build_app_bundle.sh --release
./macos/Scripts/verify_asset_free.sh build-macos/MGB64.app
```

The `.app` is for local source builds only: it is unsigned, not notarized, and
links against the SDL2 dylib installed on your machine. If that local SDL2 dylib
requires a newer macOS version than the script's requested deployment target,
the script raises the local bundle target to match and prints both values. See
[`../macos/README.md`](../macos/README.md).

For maintainer packaging work, `build_app_bundle.sh` also supports
`--strict-deployment-target` and `--bundle-sdl2`. Use those with a controlled
SDL2 build before signing or notarizing a distributable app; ordinary source
builds do not need them.

A GPU supporting OpenGL 3.0 / ES 3.0 or above is required at runtime.

### Optional Docker dev shell

The repository includes a Dockerfile with the native-port build dependencies
plus the matching-target toolchain basics. Build it from the repository root:

```sh
docker build -t mgb64-dev .
```

Then run it with the checkout bind-mounted:

```sh
docker run --rm -it -v "$PWD":/home/dev/mgb64 -w /home/dev/mgb64 mgb64-dev
```

The `.dockerignore` file excludes ROMs, extracted assets, build outputs, and
local captures from Docker build context. Keep your ROM on the host and pass it
at runtime; do not bake ROMs or extracted assets into a Docker image.

## 3. Optional: extract assets for matching-target work

The native port does not require this step. It is useful for contributors
working on the N64 matching target or generated-data/linker tasks. Extraction
writes ROM-derived data into git-ignored paths; none of it may be committed.

```sh
# Extract everything from baserom.u.z64:
scripts/extract_baserom.u.sh
```

(The N64 `make` target also runs extraction automatically via `extractassets`.)

## 4. N64 ROM matching target (decompilation / matching)

This is for contributors working on byte-matching the original ROM, not the
supported way to play the native port today. It needs a MIPS toolchain
(`binutils-mips-*`), `python3`, the in-tree matching tools, and local
user-supplied SGI IDO/IRIX compiler files. Those proprietary compiler files are
not redistributed here. The public checkout also does not vendor IDO
static-recompilation source; `tools/ido5.3_recomp` is an ignored local
placeholder that matching-target contributors can populate from external tooling
they are legally comfortable using. Generating that recompiled compiler locally
also expects an ignored IRIX root under `tools/irix/root` (for example
`tools/irix/root/usr/bin/cc` and `tools/irix/root/usr/lib/err.english.cc`). The
native CMake port does not need any of this.

```sh
make            # matching target; see docs/STATUS.md for current gaps
```

The current public-checkout gaps are the local IDO static-recompilation tooling,
the proprietary IDO/IRIX toolchain input, and wiring the extracted
animation/image-table data into the N64 link. Set `COMPARE=1` when working on
that path to verify output SHA-1s.

## Running

Once the port is built and your ROM is in place:

```sh
./build/ge007 --rom "/path/to/baserom.u.z64"
```

If you omit `--rom`, the port looks for a ROM in the working directory and its
parents (see the auto-detected names above). Saves and config are written to a
per-user data directory.

## Reporting platform validation

If you are validating a platform, open the **Platform validation report** issue
template and include text output only. Do not attach ROMs, extracted assets,
save files, screenshots, video, or audio.

Please include:

- OS/version and, on Windows, the MSYS2 environment (`echo $MSYSTEM`);
- GPU/driver, if you ran an interactive boot;
- compiler, CMake, Python, and SDL2/pkg-config versions;
- exact commit SHA;
- whether each command below passed or failed.

```sh
git rev-parse HEAD
cmake --version
cc --version
python3 --version
pkg-config --modversion sdl2 || pkgconf --modversion sdl2

cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./tools/validate_quick.sh
./build/ge007 --rom /path/to/baserom.u.z64
```

## Troubleshooting

- **`SDL2 not found`** — install the SDL2 dev package for your platform (above).
- **`pkg-config` / `pkgconf` not found** — install `pkg-config` on Linux, or
  `mingw-w64-x86_64-pkgconf` / `mingw-w64-ucrt-x86_64-pkgconf` on MSYS2.
- **MSYS2 finds the wrong compiler or cannot find SDL2** — make sure you are in
  the **MSYS2 MinGW 64-bit** shell, not the plain MSYS shell. `echo $MSYSTEM`
  should print `MINGW64`, and `which cc cmake pkg-config python3` should point
  under `/mingw64/bin` where applicable.
- **`python3` is missing on MSYS2** — install the MinGW Python package above and
  reopen the MinGW shell. The build and validation scripts invoke `python3`
  directly; `python --version` alone is not enough.
- **CMake prints `ROM-derived asset data not present`** — this is expected and
  harmless. Those bulk tables (animation frames, Rareware logo) are not
  distributed; the port loads them from *your* ROM at runtime.
- **A window opens then closes immediately / no ROM** — make sure your ROM is in
  place (step 1) or pass `--rom /path/to/baserom.u.z64`.
