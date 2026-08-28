# Third-Party Components & Provenance

This document inventories code in this repository that the MGB64 contributors did
**not** author, so that its origin and licensing are transparent. It complements
[LICENSE](LICENSE) (which covers MGB64's own first-party code) and
[DISCLAIMER.md](DISCLAIMER.md) (which covers the decompiled game and its assets).

If you believe anything here is mis-attributed or should not be included, please
open an issue — we will address it promptly.

## The game (decompiled code) and its assets

The decompiled game code is a transformative work; the game, its code, and all
of its assets remain the property of their respective rights holders (see
[DISCLAIMER.md](DISCLAIMER.md)). **No ROM or ROM-derived media is distributed in
this repository** — see that file and the CI contamination guard
(`scripts/ci/check_no_rom_data.sh`).

The decompiled game code in this repository originates from the upstream
[**n64decomp/007**](https://github.com/n64decomp/007) decompilation, which MGB64
started from and continues to work off of. That project carries its own license
and contributor history; MGB64's native port layer and project tooling are
layered on top of it. Refer to the upstream repository for the provenance and
licensing of the decompiled sources it provides.

## Nintendo 64 SDK compatibility material (libultra / SGI)

| Path | Origin | Notes |
| --- | --- | --- |
| `include/PR/*.h`, except the clean-room compatibility headers listed below | Nintendo 64 SDK (libultra), © Silicon Graphics, Inc. 1994–1999 | Platform/ABI interface headers (RCP/GBI, OS, audio, etc.). |
| `src/libultra/**`, except the clean-room compatibility sources listed below | Nintendo 64 SDK (libultra), © Silicon Graphics, Inc. | SDK-lineage implementation source used primarily by the matching target and non-native compatibility paths. |
| `src/libultrare/**`, except the clean-room/unused audio paths listed below | Rare-modified libultra routines, derived from the Nintendo 64 SDK | SDK-lineage implementation source used primarily by the matching target and non-native compatibility paths. |
| `src/bootcode.s` | Public placeholder for N64 IPL3 boot material | The public file intentionally contains no IPL3 boot ROM code or boot-font bytes. Matching-target contributors must provide any required boot material locally from a legally obtained source and must not commit or redistribute it. |

These files are the standard Nintendo 64 / SGI development interface and
implementation material used by the decompiled code's original platform. They
are **not** original to this project and remain the property of Silicon Graphics,
Inc. / Nintendo. MGB64 claims no ownership of them and they are not offered under
MGB64's MIT license.

The public `src/bootcode.s` file is kept only so the matching-target linker has a
stable source path. It is asset-free and non-bootable by itself; the native port
does not compile or link it.

This is the largest remaining conservative provenance area in the tree. The
native port already re-implements platform behavior in `src/platform/`, and the
release guard currently finds no proprietary SDK notice text in tracked files.
Remaining cleanup work is to keep replacing or isolating matching-target-only
SDK-lineage compatibility source and to audit native-compiled support code with
historical SDK/demo structure.

Unused legacy headers `include/PR/gs2dex.h`, `include/PR/primage.h`,
`include/PR/rdb.h`, `include/PR/ram_ORIGINAL_rom.h`,
`include/PR/rm_ORIGINAL_on.h`, `include/PR/ultraerror.h`, and
`include/PR/libultra.h` have been removed from the public tree. The native
support-code audit is recorded in `docs/PROVENANCE_AUDIT.md`.

As of the current native CMake build, the native executable no longer compiles
any `src/libultra/audio/*.c` or `src/libultrare/audio/*.c` source files. The
top-level compatibility headers
`include/{assert,bstring,limits,math,sgidefs,stddef,stdlib,svr4_math}.h` and
`include/PR/{R4300,abi,gbi,gu,libaudio,mbi,os,os_internal,rcp,region,ultratypes}.h`
and internal libultra helper headers
`src/libultra/{audio/synthInternals.h,gu/guint.h,io/viint.h}` are now clean-room
declarations/constants. The
`NATIVE_PORT` branch of `include/PR/os.h` is a clean-room forwarding shim to the
native platform surface. The native `include/PR/R4300.h` address macros are
guarded as fallback definitions so they do not override the native platform
address-conversion macros.
The native audio source set is explicit in `CMakeLists.txt` and guarded by
`tools/check_native_sdk_surface.py` so
SDK-derived libultra audio files cannot enter the native target through a
wildcard; the same guard also checks that the cleaned `PR` compatibility
headers, top-level compatibility headers, and the native `PR/os.h` surface
remain free of proprietary notice text and avoid native macro regressions, and
that `R4300.h` cannot clobber platform address macros. The
native build no longer compiles SDK GU source files or SDK/Rare libaudio
implementation files. Former split libaudio helper/wrapper files under
`src/libultra/audio/` that were no longer direct Makefile objects have been
removed from the public tree. The shared clean-room audio compatibility
implementation now lives in `src/platform/audio_compat.c`, with
`src/libultra/audio/clean_compat.c` serving as the matching-target wrapper.
The matching-target linker scripts now place only
`src/libultra/audio/clean_compat.o` for libaudio sections; historical
SDK/Rare libaudio object references are rejected by the public inventory guard.
The native CMake targets also avoid broad `src/libultra/`, `src/libultra/gu/`,
and `src/libultrare/audio/` include directories; only the narrow
`src/libultra/audio/` include path remains for the clean-room audio
compatibility helper headers, and this boundary is release-guarded.
The native build uses the clean-room replacements in `src/platform/gu_trig.c`
and `src/platform/audio_compat.c`. Most matching-target GU C helpers
`src/libultra/gu/{align,cosf,coss,lookat,lookatref,mtxutil,normalize,ortho,perspective,rotate,scale,sinf,sins,translate}.c`
and `src/libultra/io/viblack.c` are now clean-room compatibility sources. Other
SDK/libultra files remain in-tree primarily for the N64 matching target and
compatibility headers. The exact tracked compatibility-path inventory is guarded
by `tools/check_sdk_inventory.py`; adding or removing files under `include/PR/`,
`src/libultra/`, or `src/libultrare/`, or changing matching-target libaudio
linker placement, must update that inventory and this provenance documentation.
See `ROADMAP.md` for the staged cleanup plan.

## Vendored tools and libraries

| Path | Component | License | Upstream |
| --- | --- | --- | --- |
| `src/platform/fast3d/` | Fast3D display-list interpreter + GL/Metal backends — the core N64-GBI-to-GPU renderer, compiled into every native binary. `gfx_cc.c`'s 2-cycle combiner additionally derives from the Perfect Dark port; the native Metal backend and smooth-normal builder are first-party. Full breakdown and the reproduced license notice are in `src/platform/fast3d/PROVENANCE.md`. | Modified BSD-2-Clause (n64-fast3d-engine; source BSD-style, binary redistribution restricted to asset-free binaries) — see `src/platform/fast3d/PROVENANCE.md` | https://github.com/Emill/n64-fast3d-engine |
| wgpu-native (fetched, **not** committed) | gfx-rs's C implementation of the standard `webgpu.h` API. Backs the **default** cross-platform render backend (`MGB64_WEBGPU_BACKEND`, ON by default; `-DMGB64_WEBGPU_BACKEND=OFF` for a GL/Metal-only binary) that replaces the GL+Metal fork — dispatches to Metal (macOS), D3D12/Vulkan (Windows), Vulkan (Linux/handheld). `cmake/webgpu.cmake` downloads + SHA-256-verifies the per-platform PREBUILT release, pinned to `v29.0.1.1`, at configure time (no source or binary committed here; matches the Real-ESRGAN fetched-dependency pattern). See `docs/design/adr/0001-webgpu-render-backend.md`. | MIT OR Apache-2.0 (dual) | https://github.com/gfx-rs/wgpu-native |
| `lib/glad/` | glad OpenGL loader (generated, v0.1.36) + Khronos `khrplatform.h` | MIT and Khronos notices — see `lib/glad/LICENSE` | https://github.com/Dav1dde/glad |
| `lib/cgltf/cgltf.h` | cgltf v1.14 glTF 2.0 parser, used by the optional scene-decoration loader (`src/platform/decor_assets.c`) | MIT — full notice in the file header | https://github.com/jkuhlmann/cgltf |
| `lib/stb/stb_image.h` | stb_image v2.30 PNG decoder, used by the optional HD texture-pack loader (`src/platform/texture_pack.c`) | Public domain (Unlicense) / MIT dual — full notice in the file trailer | https://github.com/nothings/stb |
| `lib/imgui/` | Dear ImGui (immediate-mode GUI) + `imgui_impl_sdl2` / `imgui_impl_opengl3` backends, used by the in-process app shell (`src/app/`); pinned @ `776bf2ab` | MIT — see `lib/imgui/LICENSE.txt` | https://github.com/ocornut/imgui |
| `lib/nfd/` | nativefiledialog-extended (portable native "open file" dialogs) for the app shell's ROM picker; pinned @ `3cd252a8` | zlib — see `lib/nfd/LICENSE` | https://github.com/btzy/nativefiledialog-extended |
| `lib/sdl_gamecontrollerdb/gamecontrollerdb.txt` | Community SDL controller-mapping database, loaded at controller init via `SDL_GameControllerAddMappingsFromFile` (`src/platform/platform_sdl.c`) so exotic/hybrid/handheld pads map correctly. Plain-text mapping data (GUID→button layout), no ROM/asset content; packaging drops a copy beside the binary / into the .app `Resources`. | zlib — see `lib/sdl_gamecontrollerdb/LICENSE.txt` | https://github.com/mdqinc/SDL_GameControllerDB |
| `src/app/app_font.h` | Embedded UI font for the app shell — Roboto Medium, re-encoded as a compressed-base85 C header. The `.ttf` byte source is **not tracked** (no binary blobs in the repo); fetch it upstream to regenerate. | Apache-2.0 — see `lib/fonts/LICENSE.txt` | https://fonts.google.com/specimen/Roboto |
| `tools/asm-processor/` | asm-processor (MIPS asm pre/post-processor) | The Unlicense (public domain) — see `tools/asm-processor/LICENSE` | https://github.com/simonlindholm/asm-processor |
| `tools/ido5.3_recomp/` | Local-only placeholder for an external static recompilation of SGI's IDO 5.3 C compiler | No recompilation source or IDO/IRIX compiler input files are redistributed here; this ignored directory must be populated locally for matching-target work | https://github.com/decompals/ido-static-recomp |
| `tools/gzipsrc/` | gzip source (DEFLATE compression used by the asset pipeline) | GNU GPL v2 or later — see `tools/gzipsrc/COPYING` and `tools/gzipsrc/README.md` | https://www.gnu.org/software/gzip/ |
| `tools/extractor/puff.c`, `tools/extractor/puff.h` | Mark Adler's `puff` inflate implementation, locally modified as noted in-file | zlib-style license retained in `puff.h`; see `tools/extractor/README.md` | https://zlib.net/puff/ |
| `tools/mktex/src/libpdtex/reader.c` | Texture decompression routines copied/adapted from Perfect Dark decompilation `texdecompress.c` | MIT, via upstream Perfect Dark decompilation; see `tools/mktex/LICENSE.perfect_dark` and `tools/mktex/PROVENANCE.md` | https://github.com/n64decomp/perfect_dark |
| `tools/armips/` (`tools/armips.cpp`) | armips assembler, including embedded tinyformat formatting helper | armips MIT and tinyformat Boost Software License; notices retained in `tools/armips.cpp` | https://github.com/Kingcom/armips |
| `tools/texpack/.bin/` (fetched, **not** committed) | Real-ESRGAN ncnn-vulkan upscaler + models, used by the optional HD texture-pack pipeline | BSD-3-Clause (Real-ESRGAN); model weights per upstream. Fetched on demand by `tools/texpack/fetch_realesrgan.sh` into a gitignored cache — never tracked in this repo. | https://github.com/xinntao/Real-ESRGAN |
| `tools/smaa/AreaTex.py`, `tools/smaa/SearchTex.py` | SMAA reference lookup-table generator scripts (Jimenez et al.), vendored **verbatim**. The first-party wrapper `tools/smaa/gen_luts.py` reuses their kernels to emit the committed SMAA LUT headers `src/platform/fast3d/smaa_area_tex.h` / `smaa_search_tex.h` (no ROM-derived data). | MIT — full text in `tools/smaa/LICENSE`; provenance in `tools/smaa/README.md`; MIT text also in `NOTICE.md` | https://github.com/iryoku/smaa |

See each component's directory for its own license/README where present. The
`tools/texpack/` scripts are first-party (this project); they fetch the third-party
upscaler at runtime rather than vendoring it.

## Attribution & thanks

This project is built directly on the
[**n64decomp/007**](https://github.com/n64decomp/007) decompilation — the
foundation MGB64 started from and continues to extend — and follows the
conventions established by the wider N64 decompilation community, the
[Super Mario 64](https://github.com/n64decomp/sm64),
[Ocarina of Time](https://github.com/zeldaret/oot), and
[Perfect Dark](https://github.com/fgsfdsfgs/perfect_dark) projects in particular.
Their tooling and approach made this work possible.
