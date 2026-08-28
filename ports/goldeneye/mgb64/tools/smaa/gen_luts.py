#!/usr/bin/env python3
"""Deterministic generator for the SMAA Area/Search lookup-table C headers.

Emits two committed, byte-stable headers consumed by the Metal SMAA passes
(wired up in a later task):

    src/platform/fast3d/smaa_area_tex.h    160x560 RG8,  179200 bytes
    src/platform/fast3d/smaa_search_tex.h   64x16  R8,     1024 bytes

Provenance / license (Tier A2, MGB64 rail R2):
  The lookup-table math is the SMAA reference implementation by Jimenez et al.
  ("SMAA: Enhanced Subpixel Morphological Antialiasing"), from
  https://github.com/iryoku/smaa (commit 71c806a), which is MIT-licensed.
  The two upstream generator scripts are vendored verbatim next to this file
  (tools/smaa/AreaTex.py, tools/smaa/SearchTex.py) with the upstream license in
  tools/smaa/LICENSE. This wrapper reuses their numeric kernels directly:

    * The Area-texture math (areaortho/areadiag and their placement tables) is
      imported unmodified from AreaTex.py and assembled here without Pillow.
    * The Search-texture delta logic (bilinear/edge/deltaLeft/deltaRight) is
      re-expressed from SearchTex.py, whose module body has import-time side
      effects and uses a Pillow API removed in Pillow >= 10, so it cannot be
      imported as a library.

  The output of both paths is byte-identical to the canonical SMAA reference
  textures (Textures/AreaTex.h / Textures/SearchTex.h upstream). This generator
  is fully offline and deterministic: no network, no Pillow requirement, no
  ROM-derived data.

Usage:
    python3 tools/smaa/gen_luts.py           regenerate both headers in place
    python3 tools/smaa/gen_luts.py --check    regenerate in memory; exit 1 if
                                              the committed headers differ
"""

from __future__ import annotations

import argparse
import importlib.util
import os
import sys
import types
from concurrent.futures import ProcessPoolExecutor
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO_ROOT = HERE.parent.parent
AREA_HEADER = REPO_ROOT / "src/platform/fast3d/smaa_area_tex.h"
SEARCH_HEADER = REPO_ROOT / "src/platform/fast3d/smaa_search_tex.h"

UPSTREAM = "github.com/iryoku/smaa"
UPSTREAM_COMMIT = "71c806a"


# ---------------------------------------------------------------------------
# Import the Area-texture kernels from the vendored reference generator.
#
# AreaTex.py does `from PIL import Image` at import time but never touches PIL
# in the kernels we use (only its unused __main__ block and helpers do). Provide
# a stub so this generator has no hard Pillow dependency; if real Pillow is
# installed it is used instead. Either way the numeric output is identical
# because the kernels we call do not use PIL.
_AREATEX = None


def _areatex():
    global _AREATEX
    if _AREATEX is None:
        if "PIL" not in sys.modules:
            try:
                import PIL  # noqa: F401
            except Exception:
                stub = types.ModuleType("PIL")
                stub_image = types.ModuleType("PIL.Image")
                stub.Image = stub_image
                sys.modules["PIL"] = stub
                sys.modules["PIL.Image"] = stub_image
        spec = importlib.util.spec_from_file_location("smaa_areatex_ref", HERE / "AreaTex.py")
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        _AREATEX = module
    return _AREATEX


# ---------------------------------------------------------------------------
# Area texture (160x560, 2 bytes/pixel R,G).
#
# This is a Pillow-free re-assembly of AreaTex.py's assemble()/tex2dortho()/
# tex2ddiag()/cpp() pipeline. The reference builds per-pattern subtextures then
# copies them in via a coordinate compression; each placed pixel therefore
# resolves to the reference area math evaluated at the compressed coordinate,
# which we compute directly.
#
# The diagonal patterns use brute-force area sampling and dominate the runtime,
# so they are distributed over a process pool. Each work unit is one
# (subsample-offset, pattern) pair and is fully independent; results are
# collected in input order, so the output is bit-for-bit deterministic
# regardless of worker count or completion order (a serial fallback is used if
# a pool cannot be created).
def _diag_unit(task):
    """Compute one diagonal (offset, pattern) block. Runs in a worker process.

    Returns a flat list of (x, y, r, g) placements.
    """
    oy, offset, pattern = task
    A = _areatex()
    so, sd = A.SIZE_ORTHO, A.SIZE_DIAG
    ex, ey = A.edgesdiag[pattern]
    out = []
    for left in range(sd):
        for right in range(sd):
            px = 5 * so + left + sd * ex
            py = 4 * sd * oy + right + sd * ey
            area_xy = A.areadiag(pattern, left, right, offset)
            out.append((px, py, int(255.0 * area_xy[0]), int(255.0 * area_xy[1])))
    return out


def _map_units(fn, tasks):
    workers = min(len(tasks), os.cpu_count() or 1)
    env = os.environ.get("SMAA_GEN_JOBS")
    if env:
        try:
            workers = max(1, min(len(tasks), int(env)))
        except ValueError:
            pass
    if workers <= 1:
        return [fn(t) for t in tasks]
    try:
        with ProcessPoolExecutor(max_workers=workers) as pool:
            return list(pool.map(fn, tasks))
    except Exception:
        return [fn(t) for t in tasks]


def build_area_tex() -> bytes:
    A = _areatex()
    so, sd = A.SIZE_ORTHO, A.SIZE_DIAG
    width = 2 * 5 * so                                  # 160
    height = len(A.SUBSAMPLE_OFFSETS_ORTHO) * 5 * so    # 560
    buf = bytearray(width * height * 2)

    def place(x: int, y: int, r: int, g: int) -> None:
        off = (y * width + x) * 2
        buf[off] = r
        buf[off + 1] = g

    # Orthogonal patterns: coordinates are compressed quadratically
    # (compress = (left**2, right**2)) so short subtextures reach long distances.
    # These are cheap and computed serially.
    for oy, offset in enumerate(A.SUBSAMPLE_OFFSETS_ORTHO):
        for pattern in range(16):
            ex, ey = A.edgesortho[pattern]
            for left in range(so):
                for right in range(so):
                    px = left + so * ex
                    py = 5 * so * oy + right + so * ey
                    area_xy = A.areaortho(pattern, left * left, right * right, offset)
                    place(px, py, int(255.0 * area_xy[0]), int(255.0 * area_xy[1]))

    # Diagonal patterns: placed to the right of the orthogonal block, identity
    # coordinate mapping; computed in parallel (see note above).
    diag_tasks = [
        (oy, offset, pattern)
        for oy, offset in enumerate(A.SUBSAMPLE_OFFSETS_DIAG)
        for pattern in range(16)
    ]
    for placements in _map_units(_diag_unit, diag_tasks):
        for px, py, r, g in placements:
            place(px, py, r, g)

    return bytes(buf)


# ---------------------------------------------------------------------------
# Search texture (64x16, 1 byte/pixel).
#
# Re-expressed from SearchTex.py (bilinear / edge reverse-lookup / deltaLeft /
# deltaRight), then the same 66x33 -> crop[0,17,64,33] -> vertical-flip pipeline.
def build_search_tex() -> bytes:
    def lerp(v0, v1, p):
        return v0 + (v1 - v0) * p

    def bilinear(e):
        a = lerp(e[0], e[1], 1.0 - 0.25)
        b = lerp(e[2], e[3], 1.0 - 0.25)
        return lerp(a, b, 1.0 - 0.125)

    # Reverse lookup: bilinear fetch value -> which of the 4 edges are active.
    edge = {}
    for e0 in (0, 1):
        for e1 in (0, 1):
            for e2 in (0, 1):
                for e3 in (0, 1):
                    edge[bilinear([e0, e1, e2, e3])] = [e0, e1, e2, e3]

    def delta_left(left, top):
        d = 0
        if top[3] == 1:
            d += 1
        if d == 1 and top[2] == 1 and left[1] != 1 and left[3] != 1:
            d += 1
        return d

    def delta_right(left, top):
        d = 0
        if top[3] == 1 and left[1] != 1 and left[3] != 1:
            d += 1
        if d == 1 and top[2] == 1 and left[0] != 1 and left[2] != 1:
            d += 1
        return d

    img = [[0] * 66 for _ in range(33)]  # img[y][x]

    for x in range(33):
        for y in range(33):
            tc = 0.03125 * x, 0.03125 * y
            if tc[0] in edge and tc[1] in edge:
                img[y][x] = 127 * delta_left(edge[tc[0]], edge[tc[1]])
    for x in range(33):
        for y in range(33):
            tc = 0.03125 * x, 0.03125 * y
            if tc[0] in edge and tc[1] in edge:
                img[y][33 + x] = 127 * delta_right(edge[tc[0]], edge[tc[1]])

    # Crop to [0,17,64,33] (64x16) then FLIP_TOP_BOTTOM.
    cropped = [row[0:64] for row in img[17:33]][::-1]

    out = bytearray()
    for row in cropped:
        for value in row:
            out.append(value & 0xFF)
    return bytes(out)


# ---------------------------------------------------------------------------
# Header emission.
def _emit_header(guard: str, symbol: str, macro_prefix: str, dims: dict, data: bytes) -> str:
    lines = []
    lines.append(f"/* {symbol} - SMAA lookup table, {dims['w']}x{dims['h']}, {len(data)} bytes. */")
    lines.append("/*")
    lines.append(" * GENERATED FILE - do not edit by hand.")
    lines.append(" *   regenerate: python3 tools/smaa/gen_luts.py")
    lines.append(" *   verify:     python3 tools/smaa/gen_luts.py --check")
    lines.append(" *")
    lines.append(f" * Data is the SMAA reference lookup table (Jimenez et al.,")
    lines.append(f" * {UPSTREAM} @ {UPSTREAM_COMMIT}, MIT-licensed). See tools/smaa/LICENSE,")
    lines.append(" * tools/smaa/README.md, NOTICE.md and THIRD_PARTY.md. No ROM-derived data.")
    lines.append(" */")
    lines.append(f"#ifndef {guard}")
    lines.append(f"#define {guard}")
    lines.append("")
    lines.append("#include <stdint.h>")
    lines.append("")
    lines.append(f"#define {macro_prefix}_WIDTH {dims['w']}")
    lines.append(f"#define {macro_prefix}_HEIGHT {dims['h']}")
    lines.append(f"#define {macro_prefix}_PITCH {dims['pitch']}")
    lines.append(f"#define {macro_prefix}_SIZE ({macro_prefix}_HEIGHT * {macro_prefix}_PITCH)")
    lines.append("")
    lines.append(f"static const uint8_t {symbol}[{macro_prefix}_SIZE] = {{")

    per_line = 12
    for i in range(0, len(data), per_line):
        chunk = data[i:i + per_line]
        lines.append("    " + " ".join("0x%02x," % b for b in chunk))
    lines.append("};")
    lines.append("")
    lines.append(f"#endif /* {guard} */")
    lines.append("")
    return "\n".join(lines)


def area_header_text() -> str:
    data = build_area_tex()
    return _emit_header(
        guard="MGB64_SMAA_AREA_TEX_H",
        symbol="g_smaa_area_tex",
        macro_prefix="SMAA_AREATEX",
        dims={"w": 160, "h": 560, "pitch": "(SMAA_AREATEX_WIDTH * 2)"},
        data=data,
    )


def search_header_text() -> str:
    data = build_search_tex()
    return _emit_header(
        guard="MGB64_SMAA_SEARCH_TEX_H",
        symbol="g_smaa_search_tex",
        macro_prefix="SMAA_SEARCHTEX",
        dims={"w": 64, "h": 16, "pitch": "SMAA_SEARCHTEX_WIDTH"},
        data=data,
    )


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate the SMAA Area/Search LUT headers.")
    parser.add_argument(
        "--check",
        action="store_true",
        help="regenerate in memory and fail if the committed headers differ",
    )
    args = parser.parse_args()

    targets = [
        (AREA_HEADER, area_header_text()),
        (SEARCH_HEADER, search_header_text()),
    ]

    if args.check:
        stale = []
        for path, text in targets:
            current = path.read_text(encoding="utf-8") if path.is_file() else None
            if current != text:
                stale.append(path)
        if stale:
            print("SMAA LUT headers are OUT OF DATE; run: python3 tools/smaa/gen_luts.py",
                  file=sys.stderr)
            for path in stale:
                print(f"  - stale: {path.relative_to(REPO_ROOT)}", file=sys.stderr)
            return 1
        print("PASS: SMAA LUT headers regenerate byte-identical")
        return 0

    for path, text in targets:
        path.write_text(text, encoding="utf-8")
        print(f"wrote {path.relative_to(REPO_ROOT)} ({len(text.encode('utf-8'))} bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
