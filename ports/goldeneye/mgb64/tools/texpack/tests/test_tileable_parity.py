"""W2.E1.T2 — C/Python `is_tileable` parity.

The runtime manifest emit (gfx_pc.c `gfx_texmanifest_is_tileable`) and the
offline pack builder (build_pack.py `is_tileable`) MUST agree on which textures
tile seamlessly, or the two halves of the HD pipeline disagree about seam-safe
upscaling. This test proves they agree by exercising BOTH real implementations:

  * Python: import the real `is_tileable` from tools/texpack/build_pack.py.
  * C: extract the real `gfx_texmanifest_is_tileable` verbatim from gfx_pc.c
       (brace-matched), compile it into a tiny standalone harness, and run it.

Fixtures are synthetic RGBA images generated in-test (never tracked — R2), each
serialized as `int32 w, int32 h, w*h*4 bytes RGBA` (native byte order) for the
harness. The RGBA byte layout — index (y*w + x)*4 — matches both the C indexing
and PIL's RGBA `tobytes()`, so the two implementations see identical pixels.
"""

import importlib.util
import re
import struct
import subprocess
import shutil
from pathlib import Path

import pytest

pytest.importorskip("PIL", reason="Pillow required (tools/texpack/requirements.txt)")
from PIL import Image  # noqa: E402

TESTS_DIR = Path(__file__).resolve().parent
REPO_ROOT = TESTS_DIR.parents[2]
GFX_PC_C = REPO_ROOT / "src" / "platform" / "fast3d" / "gfx_pc.c"
BUILD_PACK = TESTS_DIR.parent / "build_pack.py"


def _load_build_pack():
    spec = importlib.util.spec_from_file_location("texpack_build_pack", BUILD_PACK)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def _extract_c_function(src_path, sig_regex):
    """Return the exact source text of the first function matching sig_regex,
    from its signature through the brace-matched closing '}' (verbatim)."""
    text = src_path.read_text()
    m = re.search(sig_regex, text)
    if not m:
        raise AssertionError(f"could not find function matching {sig_regex!r}")
    start = m.start()
    brace = text.index("{", m.end() - 1)
    depth = 0
    i = brace
    while i < len(text):
        c = text[i]
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                return text[start:i + 1]
        i += 1
    raise AssertionError("unbalanced braces extracting C function")


C_HARNESS_TEMPLATE = """\
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

{func}

int main(int argc, char **argv) {{
    if (argc != 2) {{ fprintf(stderr, "usage: probe <raw>\\n"); return 2; }}
    FILE *f = fopen(argv[1], "rb");
    if (!f) {{ perror("fopen"); return 2; }}
    int32_t w = 0, h = 0;
    if (fread(&w, sizeof(int32_t), 1, f) != 1 ||
        fread(&h, sizeof(int32_t), 1, f) != 1) {{ return 2; }}
    size_t n = (size_t)w * (size_t)h * 4u;
    uint8_t *buf = (uint8_t *)malloc(n ? n : 1);
    if (n && fread(buf, 1, n, f) != n) {{ return 2; }}
    fclose(f);
    printf("%d\\n", gfx_texmanifest_is_tileable(buf, (int)w, (int)h));
    free(buf);
    return 0;
}}
"""


@pytest.fixture(scope="module")
def c_probe(tmp_path_factory):
    cc = shutil.which("cc") or shutil.which("clang") or shutil.which("gcc")
    if cc is None:
        pytest.skip("no C compiler (cc/clang/gcc) available")
    func = _extract_c_function(
        GFX_PC_C,
        r"static\s+int\s+gfx_texmanifest_is_tileable\s*\("
        r"\s*const\s+uint8_t\s*\*\s*rgba\s*,\s*int\s+w\s*,\s*int\s+h\s*\)",
    )
    d = tmp_path_factory.mktemp("tileable_probe")
    cpath = d / "probe.c"
    cpath.write_text(C_HARNESS_TEMPLATE.format(func=func))
    exe = d / "probe"
    subprocess.run([cc, "-O2", "-std=c11", "-Wall", "-Werror",
                    "-o", str(exe), str(cpath)], check=True)
    return exe


def _serialize(im):
    """(bytes) native int32 w,h + row-major RGBA — the harness wire format."""
    w, h = im.size
    return struct.pack("=ii", w, h) + im.convert("RGBA").tobytes()


def _c_verdict(probe, im, tmp_path):
    raw = tmp_path / "fixture.bin"
    raw.write_bytes(_serialize(im))
    out = subprocess.run([str(probe), str(raw)], capture_output=True,
                         text=True, check=True)
    return int(out.stdout.strip())


# ---- synthetic fixtures (generated in-test, never tracked) ------------------

def _img(w, h, fn):
    im = Image.new("RGBA", (w, h))
    px = im.load()
    for y in range(h):
        for x in range(w):
            px[x, y] = fn(x, y)
    return im


def _solid(w, h, v=100):
    return _img(w, h, lambda x, y: (v, v, v, 255))


def _hgrad(w, h):
    return _img(w, h, lambda x, y: (int(x * 255 / (w - 1)),) * 3 + (255,))


def _vgrad(w, h):
    return _img(w, h, lambda x, y: (int(y * 255 / (h - 1)),) * 3 + (255,))


def _forced_wrap(w, h):
    """Random-ish interior, but col w-1 := col 0 and row h-1 := row 0 so both
    opposite-edge means are exactly 0 -> unambiguously tileable."""
    im = _img(w, h, lambda x, y: ((x * 37 + y * 91) % 200 + 20,
                                  (x * 53 + y * 17) % 200 + 20,
                                  (x * 11 + y * 73) % 200 + 20, 255))
    px = im.load()
    for y in range(h):
        px[w - 1, y] = px[0, y]
    for x in range(w):
        px[x, h - 1] = px[x, 0]
    return im


def _edge_delta(w, h, delta):
    """Uniform interior=(10,10,10); right column = interior+delta on every
    channel. edge_cols mean = delta, edge_rows mean = 0 -> tileable iff delta<20."""
    base = 10
    return _img(w, h, lambda x, y: ((base + delta,) * 3 + (255,)
                                    if x == w - 1 else (base,) * 3 + (255,)))


# name -> (image factory, intent note). Parity is asserted for ALL; a few carry
# an expected-Python verdict to guard against a broken fixture generator.
FIXTURES = {
    "solid_16":        (lambda: _solid(16, 16),        1),
    "hgrad_16":        (lambda: _hgrad(16, 16),        0),
    "vgrad_16":        (lambda: _vgrad(16, 16),        0),
    "tiny_4x4":        (lambda: _solid(4, 4),          0),   # min(w,h) < 8
    "forced_wrap_32":  (lambda: _forced_wrap(32, 32),  1),
    "edge_delta19_16": (lambda: _edge_delta(16, 16, 19), 1),  # 19 < 20 -> tile
    "edge_delta21_16": (lambda: _edge_delta(16, 16, 21), 0),  # 21 >= 20 -> not
    "min8_wrap":       (lambda: _solid(8, 8),          1),   # boundary of guard
    "h7_nonwrap":      (lambda: _solid(8, 7),          0),   # smaller dim < 8
}


@pytest.mark.parametrize("name", sorted(FIXTURES))
def test_c_python_tileable_parity(name, c_probe, tmp_path):
    factory, expected = FIXTURES[name]
    im = factory()
    bp = _load_build_pack()
    py = int(bp.is_tileable(im))
    c = _c_verdict(c_probe, im, tmp_path)
    assert py == c, f"{name}: python={py} C={c} (implementations disagree)"
    assert py == expected, (
        f"{name}: fixture intent broken — expected python verdict "
        f"{expected}, got {py}")


def test_fixtures_cover_both_verdicts(c_probe, tmp_path):
    """The parity test would be vacuous if every fixture landed on one verdict."""
    bp = _load_build_pack()
    verdicts = set()
    for name, (factory, _) in FIXTURES.items():
        im = factory()
        py = int(bp.is_tileable(im))
        c = _c_verdict(c_probe, im, tmp_path)
        assert py == c, f"{name}: python={py} C={c}"
        verdicts.add(py)
    assert verdicts == {0, 1}, f"fixtures do not exercise both verdicts: {verdicts}"
