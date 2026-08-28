"""cc0_import.py -- open-licensed import processing, on synthetic fixtures only.

No real asset touches this test: sources are numpy-synthesized PNGs. Covers the
contract properties:

  1. tone-lock   -- output mean lands on --mean regardless of source tone.
  2. high-pass   -- a strong left->right gradient source becomes tile-uniform
                    (macro variation collapses) while the mean holds; and a
                    SEAMLESS source stays seamless (the blur is wrap-padded, so
                    the wrap-edge step stays comparable to interior steps).
  3. i-alpha     -- alpha equals the Rec.601 luma of the emitted RGB (the
                    I-format intensity-as-alpha coupling); without the flag
                    alpha is 255.
  4. license     -- tier is a prefix match (CC0/"CC0 1.0"/PD/Public Domain ->
                    A1, CC-BY* -> A2) so version suffixes don't downgrade.
  5. provenance  -- CLI writes tok####.png + tok####.provenance.json carrying
                    the same `args` shape the overrides JSON uses, and the run
                    is deterministic (same args => byte-identical PNG).
"""
import importlib.util
import json
import subprocess
import sys
from pathlib import Path

import numpy as np
from PIL import Image

TESTS_DIR = Path(__file__).resolve().parent
CC0_IMPORT = TESTS_DIR.parent / "cc0_import.py"


def _load(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


ci = _load(CC0_IMPORT, "cc0_import")


def _noise_src(mean=210.0, seed=7, size=(256, 256)):
    rng = np.random.default_rng(seed)
    a = np.clip(rng.normal(mean, 20, size + (3,)), 0, 255).astype(np.uint8)
    return Image.fromarray(a, "RGB")


def _seamless_src(size=256):
    """Periodic (toroidal) pattern -- seamless by construction."""
    x = np.linspace(0, 2 * np.pi, size, endpoint=False)
    field = 40 * np.sin(3 * x)[None, :] + 40 * np.cos(5 * x)[:, None] + 150
    a = np.repeat(np.clip(field, 0, 255)[..., None], 3, axis=2).astype(np.uint8)
    return Image.fromarray(a, "RGB")


def test_tone_lock_mean():
    rgba = ci.process(_noise_src(mean=230.0), (128, 64), mean_target=188.0)
    assert rgba.shape == (64, 128, 4)
    assert abs(float(rgba[..., :3].mean()) - 188.0) < 2.0


def test_highpass_flattens_gradient_keeps_mean():
    grad = np.tile(np.linspace(60, 220, 256, dtype=np.float64)[None, :, None],
                   (256, 1, 3)).astype(np.uint8)
    src = Image.fromarray(grad, "RGB")
    flat = ci.process(src, (256, 256), 140.0, highpass_sigma=8)
    raw = ci.process(src, (256, 256), 140.0)
    # Judge flattening on INTERIOR columns: the fixture is deliberately
    # non-seamless, so the wrap-padded blur (correct for the tileable-texture
    # use case) leaves bands inside ~3*sigma of the wrap edges by design.
    lo, hi = 3 * 8, 256 - 3 * 8
    col_means_flat = flat[:, lo:hi, :3].mean(axis=(0, 2))
    col_means_raw = raw[:, lo:hi, :3].mean(axis=(0, 2))
    assert col_means_flat.std() < 0.25 * col_means_raw.std()
    assert abs(float(flat[..., :3].mean()) - 140.0) < 2.0


def test_highpass_preserves_seamlessness():
    out = ci.process(_seamless_src(), (128, 128), 150.0, highpass_sigma=16)
    rgb = out[..., :3].astype(np.float64)
    wrap_step_x = np.abs(rgb[:, 0] - rgb[:, -1]).mean()
    wrap_step_y = np.abs(rgb[0, :] - rgb[-1, :]).mean()
    interior_step = np.abs(np.diff(rgb, axis=1)).mean()
    # Wrap edges must step no harder than ~an interior texel step (seam intact);
    # an edge-extended blur fails this by design.
    assert wrap_step_x < 3.0 * interior_step + 1.0
    assert wrap_step_y < 3.0 * interior_step + 1.0


def test_desat_scales_chroma_about_luma():
    rng = np.random.default_rng(3)
    a = np.clip(rng.normal(150, 15, (64, 64, 3)), 0, 255)
    a[..., 2] += 40                                       # blue-cast source
    src = Image.fromarray(np.clip(a, 0, 255).astype(np.uint8), "RGB")

    def chroma(rgba):
        rgb = rgba[..., :3].astype(np.float64)
        luma = 0.299 * rgb[..., 0] + 0.587 * rgb[..., 1] + 0.114 * rgb[..., 2]
        return np.abs(rgb - luma[..., None]).mean()

    keep = ci.process(src, (64, 64), 150.0)
    half = ci.process(src, (64, 64), 150.0, desat=0.5)
    gray = ci.process(src, (64, 64), 150.0, desat=0.0)
    assert chroma(half) < 0.7 * chroma(keep)
    assert chroma(gray) < 1.0                              # rounding only
    assert abs(float(half[..., :3].mean()) - 150.0) < 2.0  # mean still locked


def test_i_alpha_is_output_luma():
    rgba = ci.process(_noise_src(), (64, 64), 188.0, i_alpha=True)
    rgb = rgba[..., :3].astype(np.float64)
    luma = 0.299 * rgb[..., 0] + 0.587 * rgb[..., 1] + 0.114 * rgb[..., 2]
    assert np.abs(rgba[..., 3].astype(np.float64) - luma).max() <= 1.0
    opaque = ci.process(_noise_src(), (64, 64), 188.0, i_alpha=False)
    assert (opaque[..., 3] == 255).all()


def test_license_tier_prefix_match():
    assert ci.license_tier("CC0") == "A1"
    assert ci.license_tier("CC0 1.0") == "A1"
    assert ci.license_tier("pd") == "A1"
    assert ci.license_tier("Public Domain") == "A1"
    assert ci.license_tier("CC-BY-4.0") == "A2"
    assert ci.license_tier("CC-BY-SA-4.0") == "A2"


def test_cli_provenance_and_determinism(tmp_path):
    src = tmp_path / "src.png"
    _noise_src().save(src)

    def run(out_dir, license_str):
        subprocess.run(
            [sys.executable, str(CC0_IMPORT), str(src), "--token", "1267",
             "--size", "64x32", "--mean", "188", "--contrast", "0.9",
             "--highpass", "8", "--i-alpha", "--asset", "Fixture Snow",
             "--license", license_str, "--url", "https://example.test/snow",
             "--out-dir", str(out_dir)],
            check=True, capture_output=True)
        return (out_dir / "tok1267.png").read_bytes(), \
               json.loads((out_dir / "tok1267.provenance.json").read_text())

    png_a, prov = run(tmp_path / "a", "CC0")
    png_b, _ = run(tmp_path / "b", "CC0")
    assert png_a == png_b                       # deterministic
    assert prov["tier"] == "A1" and prov["license"] == "CC0"
    # provenance carries the same `args` shape the overrides JSON uses
    assert prov["args"] == {"mean": 188.0, "contrast": 0.9,
                            "highpass": 8.0, "i_alpha": True, "desat": 1.0}
    _, prov_by = run(tmp_path / "c", "CC-BY-4.0")
    assert prov_by["tier"] == "A2"
    im = Image.open(tmp_path / "a" / "tok1267.png")
    assert im.size == (64, 32) and im.mode == "RGBA"
