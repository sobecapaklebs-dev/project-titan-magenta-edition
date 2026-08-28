#!/usr/bin/env python3
"""make_sidecars.py -- generate tangent-space normal + roughness sidecars from a
diffuse texture (MGB64 AAA remaster, docs/remaster-aaa 02-hd-asset-pipeline §4.6).

W1 owns the loader/shader side (`texture_pack_try_load_sidecars`, doc 01 §4.7); this
tool only writes the files that side reads. File contract (§4.6):

    <pack>/textures/tok####_n.png   tangent-space normal, RGB8, +Z out
                                    (128,128,255) == flat
    <pack>/textures/tok####_r.png   roughness, grayscale, 255 == fully rough

Pure first-party math (numpy only -- already in requirements.txt); no AI weights:

    height_from_luma      -- luma is a cheap height prior for rough surfaces
                             (gravel/rock/concrete); a wrapped Gaussian blur.
    normal_from_height    -- WRAPPED Sobel gradients (mode="wrap" is MANDATORY: a
                             non-wrapped gradient puts a lighting seam on every tile
                             edge of an otherwise seamless texture), normalized,
                             encoded +Z-out. Flat height -> exactly (128,128,255).
    roughness_from_variance -- local luma contrast (wrapped box window): busy reads
                             rough, flat reads smooth (floor 40).

Usage:
    make_sidecars.py <diffuse.png> --out-dir <textures/>
        [--plan plan.json] [--distributable] [--height <h.png>] [--strength 2.0]

The token is inferred from the diffuse filename (tok####.png / settex_####...).

Tier propagation (R2 -- the subtle one): the sidecar math is first-party, but the
tier FOLLOWS THE INPUT. A sidecar of a procedural-generic or CC0 diffuse is A1
(distributable); a sidecar of an upscaled dump is Tier B (local-only). With
--plan + --distributable the tool reads the routed source for the token and REFUSES
to emit when that source resolves (by SOURCE, never a `tier` label) to Tier B --
the same source-based rule the Router/builder enforce (build_pack._entry_is_tier_b),
imported here so the three enforcement points cannot drift. Fail-closed: an unrouted
token (no plan / not in plan) is treated as unknown provenance and also refused.

Single implementation, two entry points (§4.6 coordination with doc 01): W1.E5.T5's
`build_pack.py --emit-material-maps` (a flag W1 adds, not this task) imports the
normal/roughness functions from THIS module rather than reimplementing them, so the
two docs never ship divergent Sobel math. Keep these functions module-level and
side-effect-free.
"""
import argparse
import json
import os
import sys

import numpy as np

# Reuse the landed token-inference + source->tier rule (no fork). Both sibling tools
# run as __main__ from this directory, so make the import work whether THIS file was
# loaded as a script or by pytest's importlib loader.
_HERE = os.path.dirname(os.path.abspath(__file__))
if _HERE not in sys.path:
    sys.path.insert(0, _HERE)
from build_pack import parse_token, _entry_is_tier_b  # noqa: E402


# --------------------------------------------------------------------------- math

def _luma(rgb):
    """Rec.601 luma of an HxWx3 float array (matches synth_texture / the manifest)."""
    rgb = np.asarray(rgb, dtype=np.float64)
    return 0.299 * rgb[..., 0] + 0.587 * rgb[..., 1] + 0.114 * rgb[..., 2]


def _gauss_kernel1d(sigma):
    r = max(1, int(np.ceil(3.0 * sigma)))
    x = np.arange(-r, r + 1, dtype=np.float64)
    k = np.exp(-(x * x) / (2.0 * sigma * sigma))
    return k / k.sum()


def _conv1d_wrap(arr, kernel, axis):
    """1-D correlation with a symmetric kernel and WRAP-around edges (mode='wrap').
    Wrap is what keeps a seamless input seamless through the blur/box stages."""
    r = len(kernel) // 2
    out = np.zeros_like(arr, dtype=np.float64)
    for i, w in enumerate(kernel):
        if w == 0.0:
            continue
        out += w * np.roll(arr, r - i, axis=axis)
    return out


def gaussian_blur(arr, sigma):
    """Separable Gaussian blur with wrap-around edges."""
    a = np.asarray(arr, dtype=np.float64)
    if sigma <= 0.0:
        return a.copy()
    k = _gauss_kernel1d(sigma)
    return _conv1d_wrap(_conv1d_wrap(a, k, axis=0), k, axis=1)


_SOBEL_X = np.array([[-1.0, 0.0, 1.0],
                     [-2.0, 0.0, 2.0],
                     [-1.0, 0.0, 1.0]])
_SOBEL_Y = _SOBEL_X.T.copy()


def _correlate3x3_wrap(a, kernel):
    out = np.zeros_like(a, dtype=np.float64)
    for ky in range(3):
        for kx in range(3):
            w = kernel[ky, kx]
            if w == 0.0:
                continue
            out += w * np.roll(np.roll(a, 1 - ky, axis=0), 1 - kx, axis=1)
    return out


def sobel_x(h, mode="wrap"):
    """d/dx via a wrapped Sobel kernel. Only mode='wrap' is supported -- a
    non-wrapped gradient seams every tile edge (§4.6)."""
    if mode != "wrap":
        raise ValueError("make_sidecars Sobel is wrap-only (seamlessness invariant)")
    return _correlate3x3_wrap(np.asarray(h, dtype=np.float64), _SOBEL_X)


def sobel_y(h, mode="wrap"):
    """d/dy via a wrapped Sobel kernel (see sobel_x)."""
    if mode != "wrap":
        raise ValueError("make_sidecars Sobel is wrap-only (seamlessness invariant)")
    return _correlate3x3_wrap(np.asarray(h, dtype=np.float64), _SOBEL_Y)


def height_from_luma(lum, blur_sigma=1.5):
    """Luma-as-height prior for rough surfaces -- a wrapped Gaussian blur. `lum` is
    expected normalized to roughly [0,1] so the default --strength reads sensibly."""
    return gaussian_blur(lum, blur_sigma)


def normal_from_height(h, strength=2.0):
    """Tangent-space normal map (+Z out) from a height field via WRAPPED Sobel
    gradients. Returns an HxWx3 uint8 array; a flat height encodes to exactly
    (128,128,255). Every decoded normal is unit length (up to 8-bit quantization)."""
    h = np.asarray(h, dtype=np.float64)
    dx = sobel_x(h, mode="wrap")
    dy = sobel_y(h, mode="wrap")
    nx = -strength * dx
    ny = -strength * dy
    nz = np.ones_like(h)
    inv = 1.0 / np.sqrt(nx * nx + ny * ny + nz * nz)
    nx *= inv
    ny *= inv
    nz *= inv
    enc = (np.stack([nx, ny, nz], axis=-1) * 0.5 + 0.5) * 255.0
    return np.rint(np.clip(enc, 0.0, 255.0)).astype(np.uint8)


def roughness_from_variance(lum, win=8):
    """Roughness (grayscale uint8, 255 == fully rough) from local luma variance with
    a WRAPPED box window: local contrast reads rough, a flat region reads smooth
    (floored at 40). `lum` is expected in 0..255 (the /40 threshold is tuned there)."""
    lum = np.asarray(lum, dtype=np.float64)
    k = np.ones(int(win), dtype=np.float64) / float(win)
    mean = _conv1d_wrap(_conv1d_wrap(lum, k, axis=0), k, axis=1)
    meansq = _conv1d_wrap(_conv1d_wrap(lum * lum, k, axis=0), k, axis=1)
    var = np.clip(meansq - mean * mean, 0.0, None)
    r = np.clip(255.0 * np.sqrt(var) / 40.0, 40.0, 255.0)
    return np.rint(r).astype(np.uint8)


# --------------------------------------------------------------- tier (R2) gate

def distributable_allowed(plan, token):
    """(allowed, entry) for emitting `token`'s sidecar into a --distributable pack.

    Fail-closed and SOURCE-based (never a `tier` label): only a plan entry whose
    source is A-tier (procedural-generic / CC0 / original / stock) is allowed. An
    unrouted token -- no plan, or a token the plan doesn't carry -- is unknown
    provenance and refused, matching build_pack._entry_is_tier_b's treatment of an
    unknown source."""
    entries = plan.get("entries", {}) if isinstance(plan, dict) else {}
    entry = entries.get(token)
    if isinstance(entry, dict) and not _entry_is_tier_b(entry):
        return True, entry
    return False, entry


# ------------------------------------------------------------------------- CLI

def _pil_image():
    try:
        from PIL import Image
        return Image
    except ImportError:
        sys.exit("Pillow required: pip install -r tools/texpack/requirements.txt")


def main(argv=None):
    ap = argparse.ArgumentParser(
        description="Generate tangent-space normal + roughness sidecars (§4.6).")
    ap.add_argument("diffuse", help="diffuse texture (tok####.png); token is inferred")
    ap.add_argument("--out-dir", required=True,
                    help="pack textures/ dir; writes tok####_n.png + tok####_r.png")
    ap.add_argument("--plan", help="route plan.json supplying the token's source (tier)")
    ap.add_argument("--distributable", action="store_true",
                    help="refuse to emit when the diffuse source is Tier B (R2)")
    ap.add_argument("--height", help="true height field PNG (E5.T3) in place of luma")
    ap.add_argument("--strength", type=float, default=2.0,
                    help="normal bump strength (default 2.0)")
    args = ap.parse_args(argv)

    token = parse_token(os.path.basename(args.diffuse))
    if token is None:
        sys.exit(f"cannot infer token from {args.diffuse!r} "
                 f"(need a tok####.png / settex_#### name)")

    plan = {}
    if args.plan:
        with open(args.plan) as fp:
            plan = json.load(fp)

    if args.distributable:
        allowed, entry = distributable_allowed(plan, token)
        if not allowed:
            src = entry.get("source", "?") if isinstance(entry, dict) \
                else "unrouted (no plan entry)"
            sys.exit(
                f"REFUSED: --distributable sidecars for {token} -- diffuse source "
                f"'{src}' is Tier B (non-redistributable). The sidecar tier follows "
                f"the diffuse (§4.6): route the diffuse to a procedural-generic / CC0 "
                f"/ original source, or drop --distributable (emit local-only).")

    Image = _pil_image()
    rgb = np.asarray(Image.open(args.diffuse).convert("RGB"), dtype=np.float64)
    lum255 = _luma(rgb)

    if args.height:
        height = np.asarray(Image.open(args.height).convert("L"), dtype=np.float64) / 255.0
    else:
        height = height_from_luma(lum255 / 255.0)

    normal = normal_from_height(height, strength=args.strength)
    rough = roughness_from_variance(lum255, win=8)

    os.makedirs(args.out_dir, exist_ok=True)
    n_path = os.path.join(args.out_dir, f"{token}_n.png")
    r_path = os.path.join(args.out_dir, f"{token}_r.png")
    Image.fromarray(normal, "RGB").save(n_path)
    Image.fromarray(rough, "L").save(r_path)
    print(f"wrote {n_path}")
    print(f"wrote {r_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
