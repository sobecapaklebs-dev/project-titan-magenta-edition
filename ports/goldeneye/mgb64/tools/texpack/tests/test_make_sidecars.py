"""W2.E6.T1 -- material sidecars (normal + roughness), docs/remaster-aaa 02 §4.6 / §5.

The four §5 acceptance checks for ``make_sidecars.py``, all with SYNTHETIC in-test
fixtures (a flat PNG, a seamless procedural PNG, a plan carrying a Tier-B entry) so
no image is ever committed (R2 -- sidecar output is local until W1 lands its loader):

  1. Flat input      -> the normal map is EXACTLY (128,128,255) everywhere.
  2. Seamless input  -> the normal map is seamless: the ``build_pack.is_tileable``
                        opposite-edge metric (the exact quantity the Router keys off)
                        stays under its tolerance on the produced ``_n`` map.
  3. Unit length     -> every decoded normal ``(rgb/255*2-1)`` has length within 1%
                        of 1.0 (encoding is a normalized vector + 8-bit quantization).
  4. Tier-B refusal  -> a Tier-B diffuse (per the ``--plan`` entry's SOURCE) + the
                        ``--distributable`` flag is refused; the same diffuse WITHOUT
                        ``--distributable`` still emits (local-only), and a Tier-A
                        diffuse WITH ``--distributable`` is allowed.

The pure normal/roughness functions are also imported directly (the coordination
note in §4.6: W1.E5.T5 imports this same implementation), proving they are
module-level and side-effect-free.
"""
import importlib.util
import json
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
SIDECARS_PY = TEXPACK / "make_sidecars.py"
EDGE_TOL = 20.0   # build_pack.is_tileable default tolerance


def _load(name, fname):
    spec = importlib.util.spec_from_file_location(name, TEXPACK / fname)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


ms = _load("texpack_make_sidecars", "make_sidecars.py")
bp = _load("texpack_build_pack", "build_pack.py")


def _run(*args):
    """Invoke the real CLI (exercises argparse + tier gate + file emit)."""
    return subprocess.run([sys.executable, str(SIDECARS_PY), *map(str, args)],
                          capture_output=True, text=True)


def _write_png(path, arr, mode="RGB"):
    Image.fromarray(np.asarray(arr, dtype=np.uint8), mode).save(path)


def _seamless_rgb(w=96, h=64):
    """A smooth, exactly-periodic (=> tileable) diffuse: integer-period sinusoids,
    so is_tileable holds on the diffuse AND -- once wrapped Sobel keeps it seamless
    -- on the derived normal."""
    xx = np.arange(w)[None, :]
    yy = np.arange(h)[:, None]
    base = (128.0
            + 50.0 * np.sin(2 * np.pi * 3 * xx / w)
            + 40.0 * np.sin(2 * np.pi * 2 * yy / h)
            + 25.0 * np.cos(2 * np.pi * 4 * (xx / w + yy / h)))
    g = np.clip(base, 0, 255)
    return np.stack([g, np.clip(g * 0.9, 0, 255), np.clip(g * 1.1, 0, 255)], axis=-1)


def _edge_metric_pil(path):
    """max opposite-edge L1 mean on a produced PNG -- the exact quantity
    build_pack.is_tileable thresholds against ``tol`` (right vs left, bottom vs top)."""
    a = np.asarray(Image.open(path).convert("RGB"), dtype=np.float64)
    h, w, _ = a.shape
    cols = np.abs(a[:, 0, :] - a[:, w - 1, :]).sum(axis=1).mean() / 3.0
    rows = np.abs(a[0, :, :] - a[h - 1, :, :]).sum(axis=1).mean() / 3.0
    return max(cols, rows)


# --- 1. flat input -> exact (128,128,255) ---------------------------------------

def test_flat_input_gives_exact_flat_normal(tmp_path):
    diffuse = tmp_path / "tok0100.png"
    _write_png(diffuse, np.full((48, 48, 3), 137, dtype=np.uint8))
    out = tmp_path / "textures"
    r = _run(diffuse, "--out-dir", out)
    assert r.returncode == 0, r.stderr
    n = np.asarray(Image.open(out / "tok0100_n.png").convert("RGB"))
    assert n.shape == (48, 48, 3)
    assert np.array_equal(np.unique(n.reshape(-1, 3), axis=0),
                          np.array([[128, 128, 255]], dtype=np.uint8)), \
        "flat diffuse must encode to exactly (128,128,255) at every pixel"


def test_flat_normal_pure_function():
    """The importable pure function agrees (used directly by W1.E5.T5)."""
    n = ms.normal_from_height(np.full((16, 16), 0.5), strength=2.0)
    assert np.array_equal(np.unique(n.reshape(-1, 3), axis=0),
                          np.array([[128, 128, 255]], dtype=np.uint8))


# --- 2. seamless input -> seamless normal ---------------------------------------

def test_seamless_input_gives_seamless_normal(tmp_path):
    rgb = _seamless_rgb()
    diffuse = tmp_path / "tok0200.png"
    _write_png(diffuse, rgb)
    # sanity: the fixture itself is tileable by the same metric the router uses
    assert bp.is_tileable(Image.open(diffuse).convert("RGBA"), tol=EDGE_TOL)
    out = tmp_path / "textures"
    r = _run(diffuse, "--out-dir", out)
    assert r.returncode == 0, r.stderr
    n_path = out / "tok0200_n.png"
    assert bp.is_tileable(Image.open(n_path).convert("RGBA"), tol=EDGE_TOL), \
        f"normal map must stay seamless (edge metric {_edge_metric_pil(n_path):.2f} < {EDGE_TOL})"
    assert _edge_metric_pil(n_path) < EDGE_TOL


# --- 3. decoded normals are unit length within 1% -------------------------------

def test_normals_are_unit_length(tmp_path):
    # A varied (seeded) diffuse so normals actually tilt -- stresses quantization,
    # including steep gradients, not just the flat degenerate case.
    rng = np.random.default_rng(20640622)
    diffuse = tmp_path / "tok0300.png"
    _write_png(diffuse, rng.integers(0, 256, size=(64, 64, 3), dtype=np.uint16).astype(np.uint8))
    out = tmp_path / "textures"
    # a large strength exaggerates the tilt -> harder unit-length test
    r = _run(diffuse, "--out-dir", out, "--strength", "6.0")
    assert r.returncode == 0, r.stderr
    n = np.asarray(Image.open(out / "tok0300_n.png").convert("RGB"), dtype=np.float64)
    dec = n / 255.0 * 2.0 - 1.0
    length = np.sqrt((dec ** 2).sum(axis=-1))
    assert np.all(np.abs(length - 1.0) <= 0.01), \
        f"decoded normals must be unit length +/-1% (worst {np.abs(length - 1.0).max():.4f})"


# --- 4. Tier-B diffuse + --distributable -> refused -----------------------------

def _plan(entries):
    return {"pack_kind": "full", "entries": entries}


def test_tier_b_plus_distributable_refused(tmp_path):
    diffuse = tmp_path / "tok0007.png"
    _write_png(diffuse, _seamless_rgb())
    plan = tmp_path / "plan.json"
    # ai_upscale source == Tier B by source (build_pack._entry_is_tier_b).
    plan.write_text(json.dumps(_plan({
        "tok0007": {"source": "ai_upscale", "mode": "whole_image", "tier": "A1"}})))
    out = tmp_path / "textures"
    r = _run(diffuse, "--out-dir", out, "--plan", plan, "--distributable")
    assert r.returncode != 0, "Tier-B diffuse must be refused in a --distributable build"
    assert "REFUSED" in r.stderr and "tok0007" in r.stderr, r.stderr
    # the label lie must not smuggle it in, and nothing may be written on refusal
    assert not (out / "tok0007_n.png").exists()


def test_tier_b_without_distributable_still_emits(tmp_path):
    """Tier B == local-only, not forbidden: a non-distributable build still emits."""
    diffuse = tmp_path / "tok0007.png"
    _write_png(diffuse, _seamless_rgb())
    plan = tmp_path / "plan.json"
    plan.write_text(json.dumps(_plan({"tok0007": {"source": "ai_upscale"}})))
    out = tmp_path / "textures"
    r = _run(diffuse, "--out-dir", out, "--plan", plan)
    assert r.returncode == 0, r.stderr
    assert (out / "tok0007_n.png").exists() and (out / "tok0007_r.png").exists()


def test_tier_a_distributable_allowed(tmp_path):
    diffuse = tmp_path / "tok0042.png"
    _write_png(diffuse, _seamless_rgb())
    plan = tmp_path / "plan.json"
    # procedural-generic == A1 == distributable.
    plan.write_text(json.dumps(_plan({
        "tok0042": {"source": "procedural", "tone": {"mode": "generic"}}})))
    out = tmp_path / "textures"
    r = _run(diffuse, "--out-dir", out, "--plan", plan, "--distributable")
    assert r.returncode == 0, r.stderr
    assert (out / "tok0042_n.png").exists()


def test_distributable_without_plan_fails_closed(tmp_path):
    """No plan == unknown provenance == refused in a distributable build (R2)."""
    diffuse = tmp_path / "tok0500.png"
    _write_png(diffuse, _seamless_rgb())
    out = tmp_path / "textures"
    r = _run(diffuse, "--out-dir", out, "--distributable")
    assert r.returncode != 0 and "REFUSED" in r.stderr, r.stderr


# --- coordination: single, importable implementation (§4.6) ---------------------

def test_functions_are_importable_and_side_effect_free():
    for fn in ("height_from_luma", "normal_from_height", "roughness_from_variance",
               "sobel_x", "sobel_y", "gaussian_blur"):
        assert callable(getattr(ms, fn)), fn
    # roughness is grayscale uint8 with the documented 40..255 range / polarity
    rough = ms.roughness_from_variance(np.zeros((16, 16)), win=8)
    assert rough.dtype == np.uint8 and rough.min() == 40   # flat -> floor 40
    # wrapped-only Sobel invariant is enforced (a non-wrap call must raise)
    with pytest.raises(ValueError):
        ms.sobel_x(np.zeros((8, 8)), mode="reflect")
