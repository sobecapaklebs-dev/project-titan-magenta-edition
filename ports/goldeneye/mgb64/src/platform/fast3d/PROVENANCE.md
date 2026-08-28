# Provenance — `src/platform/fast3d/`

The Fast3D display-list interpreter and its GPU backends in this directory are a
**derivative of third-party code**, not original MGB64 work. This file records
that origin and reproduces the applicable license notice, as required by the
upstream license (see the source-redistribution condition below).

## Origin

| File(s) | Derived from | License |
| --- | --- | --- |
| `gfx_pc.c`, `gfx_opengl.c`, `gfx_rendering_api.h` | Emill/n64-fast3d-engine — `https://github.com/Emill/n64-fast3d-engine` (attributed in each file header) | n64-fast3d-engine license — modified BSD-2-Clause (see below) |
| `gfx_cc.c`, `gfx_cc.h` | Emill/n64-fast3d-engine, **rewritten for 2-cycle support** based on the [fgsfdsfgs/perfect_dark](https://github.com/fgsfdsfgs/perfect_dark) port | n64-fast3d-engine license — modified BSD-2-Clause (see below) |
| `gfx_backend.c`, `gfx_backend.h`, `gfx_metal.mm`, `gfx_room_normals.c`, `gfx_uniforms.h`, `gfx_screen_config.h` | MGB64-authored code written against the upstream `GfxRenderingAPI` seam: the render-backend selector, the native Metal backend, the smooth-normal builder, and shared declarations. `gfx_metal.mm` used the libultraship Metal backend (Kenix3, MIT) as a structural reference only. | MIT (first-party, per repo [LICENSE](../../../LICENSE)) |
| `smaa_area_tex.h`, `smaa_search_tex.h` | Generated LUTs — see [`../../../THIRD_PARTY.md`](../../../THIRD_PARTY.md) (SMAA) | MIT |

The MGB64-authored files above build on the upstream's `GfxRenderingAPI` seam and
data structures; they are covered by MGB64's own MIT license but are noted here so
the boundary with the upstream code is explicit.

## Upstream license (n64-fast3d-engine)

The n64-fast3d-engine upstream is **not** MIT-licensed (an earlier revision of
this file incorrectly reproduced an MIT notice). It is distributed under a
custom, BSD-2-Clause–style license: source redistribution is permitted on
BSD-style terms, but **binary redistribution is restricted** to binaries that
contain no assets the distributor lacks the right to distribute. The notice
below was reconciled verbatim against the authoritative upstream `LICENSE.txt`
(`https://github.com/Emill/n64-fast3d-engine/blob/master/LICENSE.txt`):

```
Copyright (c) 2020 Emill, MaikelChan

Redistribution and use in source forms, with or without modification,
are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.
2. Redistributions in binary form are not allowed except in cases where the binary contains no assets you do not have the right to distribute.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

**Binary-redistribution note (condition 2).** MGB64's prebuilt binaries contain
no ROM or ROM-derived assets: they load a user-supplied ROM at runtime and are
verified asset-free by `macos/Scripts/verify_asset_free.sh` (built binary/app)
and the source-tree guard `scripts/ci/check_no_rom_data.sh`. They therefore
satisfy condition 2, which is what permits distributing them at all.

The 2-cycle combiner work in `gfx_cc.c` additionally derives from the Perfect Dark
decompilation/port (MIT); see this repository's `THIRD_PARTY.md` entry for
`tools/mktex` for that project's licensing.
