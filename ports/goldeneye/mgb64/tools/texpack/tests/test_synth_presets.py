"""W2.E5.T1 — procedural synth presets: concrete + sand + snow.

Each new Tier-A1 generic-tone preset in ``synth_texture.py`` must satisfy the
three §5 acceptance checks (roadmap 02-hd-asset-pipeline §5 W2.E5.T1 row),
all computed IN-TEST so no image is ever committed (R2 — synth output is local):

  1. Determinism   — a fixed ``--seed`` yields a byte-stable sha256 across two
                     independent CLI runs (exercises the real command).
  2. Self-tileable — the ``build_pack.is_tileable`` edge metric (opposite-edge L1
                     means — the exact quantity the router keys off) is < 20.
  3. Tile-uniform  — an FFT of the output luma carries < 5% of its |F| amplitude
                     (DC excluded) in radial frequencies below 8 cycles/width;
                     macro blotches at that scale would repeat across a tiled
                     plane, so this proves the ``_highpass`` did its job.

Resolution note: the 8-cycle bound is an ABSOLUTE spatial frequency, so the
tile-uniformity metric is defined relative to the shipping resolution. These
presets target the >=1024px hero surfaces the HD-terrain chain routes to
(§4.4(a): Dam ground 64x32 -> 1024x512); the seam/uniformity checks run there.
"""
import hashlib
import importlib.util
import subprocess
import sys
from pathlib import Path

import pytest

pytest.importorskip("numpy", reason="numpy required (tools/texpack/requirements.txt)")
pytest.importorskip("PIL", reason="Pillow required (tools/texpack/requirements.txt)")
import numpy as np  # noqa: E402
from PIL import Image  # noqa: E402

TESTS_DIR = Path(__file__).resolve().parent
TEXPACK = TESTS_DIR.parent
SYNTH_PY = TEXPACK / "synth_texture.py"


def _load(name, fname):
    spec = importlib.util.spec_from_file_location(name, TEXPACK / fname)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


st = _load("texpack_synth_texture", "synth_texture.py")
bp = _load("texpack_build_pack", "build_pack.py")

PRESETS = ["concrete", "sand", "snow"]
W, H = 1024, 512          # §4.4(a) canonical production size (Dam ground -> 1024x512)
SEED = 20640622           # synth_texture.py default --seed
EDGE_TOL = 20.0           # is_tileable tolerance (build_pack.py:94)
LOWFREQ_CYCLES = 8        # §5: radial frequencies below this are "macro"
LOWFREQ_MAX = 0.05        # §5: macro band must be < 5% of total spectral energy

# One production-resolution synthesis per preset, reused across the in-process
# checks (generation is the expensive part; the tone/seed are fixed so this is
# exactly what every check would otherwise regenerate).
_CACHE = {}


def _synth(kind):
    if kind not in _CACHE:
        mean, sd = st.DEFAULT_TONE[kind]
        _CACHE[kind] = st.synthesize(kind, W, H, SEED, mean, sd)
    return _CACHE[kind]


def _edge_metric(rgb):
    """max opposite-edge L1 mean — the same quantity build_pack.is_tileable
    thresholds against `tol` (right edge vs left, bottom vs top)."""
    a = rgb.astype(np.float64)
    cols = np.abs(a[:, 0, :] - a[:, W - 1, :]).sum(axis=1).mean() / 3.0
    rows = np.abs(a[0, :, :] - a[H - 1, :, :]).sum(axis=1).mean() / 3.0
    return max(cols, rows)


def _lowfreq_fraction(rgb):
    """Fraction of total |F| amplitude (DC excluded) that sits in radial
    frequencies below LOWFREQ_CYCLES cycles across the width — computed exactly
    like synth_texture._highpass measures radial frequency."""
    lum = 0.299 * rgb[..., 0] + 0.587 * rgb[..., 1] + 0.114 * rgb[..., 2]
    F = np.abs(np.fft.fft2(lum.astype(np.float64)))
    cx = np.fft.fftfreq(W)[None, :] * W
    cy = np.fft.fftfreq(H)[:, None] * H
    rc = np.sqrt(cx ** 2 + (cy * (W / float(H))) ** 2)   # cycles across width
    nondc = rc > 0
    low = nondc & (rc < LOWFREQ_CYCLES)
    return float(F[low].sum() / F[nondc].sum())


# --- 0. registration -------------------------------------------------------

@pytest.mark.parametrize("kind", PRESETS)
def test_preset_registered(kind):
    assert kind in st.GEN, f"{kind} not registered in GEN"
    assert kind in st.DEFAULT_TONE, f"{kind} has no default generic tone"


# --- 1. determinism (two real CLI runs -> identical bytes) -----------------

@pytest.mark.parametrize("kind", PRESETS)
def test_cli_determinism(kind, tmp_path):
    """A fixed --seed must produce byte-identical output across two independent
    invocations of the actual command. Written to pytest's tmp dir (outside the
    repo — never committed, R2). Determinism is size-independent, so this runs
    at a small size for speed."""
    def run(tag):
        out = tmp_path / f"{kind}_{tag}.png"
        subprocess.run(
            [sys.executable, str(SYNTH_PY), kind, "--out", str(out),
             "--size", "256x256", "--seed", str(SEED)],
            check=True, capture_output=True, text=True)
        return hashlib.sha256(out.read_bytes()).hexdigest()

    assert run("a") == run("b"), f"{kind}: sha256 differs across identical runs"


@pytest.mark.parametrize("kind", PRESETS)
def test_core_determinism(kind):
    """The in-process synthesis core is byte-stable at the production size too."""
    a = _synth(kind)
    b = st.synthesize(kind, W, H, SEED, *st.DEFAULT_TONE[kind])
    assert hashlib.sha256(a.tobytes()).hexdigest() == \
        hashlib.sha256(b.tobytes()).hexdigest()


# --- 2. self-tileable (edge metric < 20, the router's own math) ------------

@pytest.mark.parametrize("kind", PRESETS)
def test_self_tileable(kind):
    rgb = _synth(kind)
    im = Image.fromarray(rgb, "RGB")
    assert bp.is_tileable(im), f"{kind}: build_pack.is_tileable rejected it"
    metric = _edge_metric(rgb)
    assert metric < EDGE_TOL, f"{kind}: edge metric {metric:.2f} >= {EDGE_TOL}"


# --- 3. _highpass verified (macro band < 5% of spectral energy) ------------

@pytest.mark.parametrize("kind", PRESETS)
def test_highpass_tile_uniform(kind):
    frac = _lowfreq_fraction(_synth(kind))
    assert frac < LOWFREQ_MAX, (
        f"{kind}: {frac * 100:.2f}% of spectral energy below "
        f"{LOWFREQ_CYCLES} cycles/width (>= {LOWFREQ_MAX * 100:.0f}% — not "
        f"tile-uniform; _highpass too weak for this preset)")


# --- hygiene: synthesis is pure, writes no image into the repo -------------

def test_synthesis_writes_no_repo_image():
    before = set(TEXPACK.rglob("*.png"))
    for kind in PRESETS:
        _synth(kind)
    assert set(TEXPACK.rglob("*.png")) == before, \
        "synthesis must not write PNGs into the repo tree (R2)"
