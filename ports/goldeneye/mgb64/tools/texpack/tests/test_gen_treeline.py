"""gen_treeline.py -- first-party treeline band art, synthetic checks only.

Contract properties:

  1. determinism   -- same args => byte-identical PNG (route/rebuild safe).
  2. horizontal wrap -- trees draw modulo the tile width, so the wrap-edge
                       step stays comparable to an interior step (no seam).
  3. vertical fade -- both vertical edges melt into the fog gradient (the
                      Surface hillsides tile the strip vertically), so the
                      top and bottom rows must be tree-free (near-fog).
  4. snow_left     -- the reserved left fraction reads as bright snow slope.
  5. provenance    -- tier A1 / first-party original, args recorded.
"""
import importlib.util
import json
import subprocess
import sys
from pathlib import Path

import numpy as np
from PIL import Image

TESTS_DIR = Path(__file__).resolve().parent
GEN = TESTS_DIR.parent / "gen_treeline.py"


def _load(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


gt = _load(GEN, "gen_treeline")

FOG = (201, 204, 212)
DARK = (42, 48, 52)
LIT = (63, 71, 75)
SNOW = (221, 227, 234)


def test_horizontal_wrap_seamless():
    img, _ = gt.render(256, 96, seed=7, n_trees=12, fog=FOG, dark=DARK,
                       lit=LIT, snow=SNOW)
    wrap = np.abs(img[:, 0] - img[:, -1]).mean()
    interior = np.abs(np.diff(img, axis=1)).mean()
    assert wrap < 4.0 * interior + 2.0


def test_vertical_edges_fade_to_fog():
    img, mask = gt.render(256, 96, seed=7, n_trees=12, fog=FOG, dark=DARK,
                          lit=LIT, snow=SNOW)
    assert mask[0].max() == 0.0 and mask[-1].max() == 0.0
    for edge_row in (img[0], img[-1]):
        # edge rows sit on the fog gradient: no dark tree pixels survive
        assert edge_row.min() > min(FOG) - 30


def test_snow_left_region_is_bright():
    img, mask = gt.render(256, 96, seed=7, n_trees=12, fog=FOG, dark=DARK,
                          lit=LIT, snow=SNOW, snow_left=0.5)
    assert mask[:, : int(256 * 0.35)].mean() > 0.95   # slope is solid coverage
    left = img[:, : int(256 * 0.35)]
    assert left.mean() > 200          # bare snow slope
    # and the right half still contains dark trees
    assert img[:, int(256 * 0.6):].min() < 120


def test_cli_deterministic_and_provenance(tmp_path):
    def run(out):
        subprocess.run(
            [sys.executable, str(GEN), "--token", "1200", "--size", "128x48",
             "--seed", "12", "--trees", "10", "--flip-v",
             "--out-dir", str(out)],
            check=True, capture_output=True)
        return (out / "tok1200.png").read_bytes(), \
            json.loads((out / "tok1200.provenance.json").read_text())

    a, prov = run(tmp_path / "a")
    b, _ = run(tmp_path / "b")
    assert a == b
    assert prov["tier"] == "A1" and prov["tool"] == "gen_treeline.py"
    assert prov["args"]["flip_v"] is True and prov["args"]["seed"] == 12
    im = Image.open(tmp_path / "a" / "tok1200.png")
    assert im.size == (128, 48) and im.mode == "RGBA"


def test_alpha_cutout_mode(tmp_path):
    subprocess.run(
        [sys.executable, str(GEN), "--token", "1198", "--size", "128x48",
         "--seed", "5", "--trees", "10", "--alpha-cutout",
         "--out-dir", str(tmp_path)],
        check=True, capture_output=True)
    a = np.asarray(Image.open(tmp_path / "tok1198.png"))
    assert (a[..., 3] < 250).mean() > 0.05    # real transparency between trees
    assert (a[..., 3] > 250).mean() > 0.10    # and solid trees
