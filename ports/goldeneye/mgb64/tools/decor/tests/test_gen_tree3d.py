"""gen_tree3d.py -- first-party 3D spruce generator, synthetic checks only.

Contract properties:

  1. determinism -- same args => byte-identical .glb + textures.
  2. engine budgets -- every POSITION accessor carries min/max (the loader's
     s16 quantization reads them); extents stay inside |1.6| model units;
     textures default to <= 4096 texels (the LOADBLOCK 12-bit lrs ceiling);
     triangle count stays in the low hundreds (frame-DL budget).
  3. crossed quads -- the cards primitive is double-sided MASK with paired
     perpendicular quads (card tri count is a multiple of 4).
  4. provenance -- tier A1, bark source recorded.
"""
import importlib.util
import json
import struct
import subprocess
import sys
from pathlib import Path

import numpy as np
from PIL import Image

TESTS_DIR = Path(__file__).resolve().parent
GEN = TESTS_DIR.parent / "gen_tree3d.py"


def _load(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


gt = _load(GEN, "gen_tree3d")


def _run(out, extra=()):
    subprocess.run(
        [sys.executable, str(GEN), "--name", "t", "--seed", "5",
         "--whorls", "6", "--cards", "4", "--out", str(out), *extra],
        check=True, capture_output=True)
    return (out / "t.glb").read_bytes()


def _parse_glb(raw):
    jlen, jtype = struct.unpack("<II", raw[12:20])
    assert jtype == 0x4E4F534A
    return json.loads(raw[20:20 + jlen])


def test_deterministic(tmp_path):
    a = _run(tmp_path / "a")
    b = _run(tmp_path / "b")
    assert a == b
    na = (tmp_path / "a" / "t_needles.png").read_bytes()
    nb = (tmp_path / "b" / "t_needles.png").read_bytes()
    assert na == nb


def test_budgets_and_structure(tmp_path):
    doc = _parse_glb(_run(tmp_path / "m"))
    prims = doc["meshes"][0]["primitives"]
    assert len(prims) == 2
    tri_total = 0
    for pr in prims:
        pa = doc["accessors"][pr["attributes"]["POSITION"]]
        assert "min" in pa and "max" in pa          # loader quantization needs these
        assert max(map(abs, pa["min"] + pa["max"])) < 1.6
        tri_total += doc["accessors"][pr["indices"]]["count"] // 3
    assert tri_total < 800                           # frame-DL budget per tree
    mats = doc["materials"]
    cards = [m for m in mats if m.get("alphaMode") == "MASK"]
    assert len(cards) == 1 and cards[0]["doubleSided"] is True
    # crossed quads: cards indices count = whorls*cards*2 quads * 2 tris * 3
    card_prim = prims[1]
    n_idx = doc["accessors"][card_prim["indices"]]["count"]
    assert n_idx == 6 * 4 * 2 * 2 * 3
    im = Image.open(tmp_path / "m" / "t_needles.png")
    assert im.size[0] * im.size[1] <= 4096           # LOADBLOCK texel ceiling
    a = np.asarray(im)
    assert (a[..., 3] == 0).any() and (a[..., 3] == 255).any()


def test_provenance(tmp_path):
    _run(tmp_path / "p")
    prov = json.loads((tmp_path / "p" / "t.provenance.json").read_text())
    assert prov["tier"] == "A1"
    assert prov["tool"] == "gen_tree3d.py"
    assert prov["bark"]["license"] == "first-party original"
    assert prov["triangles"] < 800
