#!/usr/bin/env python3
"""cc0_import.py -- process an open-licensed source image into a pack texture.

The manual half of the roadmap's open-licensed import route (REMASTER_ROADMAP §3,
02-hd-asset-pipeline §6 P2.4): take a PD/CC0/CC-BY source image (e.g. an ambientCG
material's color map), and emit a runtime pack texture `tok####.png` that drops in
next to AI-upscaled and procedural entries:

  1. resize to the curated pack size (LANCZOS; pick a size with the same aspect as
     the native tile so in-world texel aspect is preserved),
  2. optional HIGH-PASS (`--highpass SIGMA`): remove macro brightness variation so a
     photo texture reads tile-uniform across large tiled planes (same property
     synth_texture.py enforces). The blur runs WRAP-PADDED (3x3 tile -> blur ->
     center crop) so a seamless source stays seamless -- an edge-extended blur
     would perturb the wrap edges of a tileable texture.
  3. tone-lock: scale contrast (`--contrast`) about the mean, then shift the SCALAR
     mean to `--mean` -- the curated avgRGB luma from the texmanifest/overrides
     (committable metadata, NOT ROM pixel statistics, so the output stays Tier
     A1/A2). Deliberately scalar: the source's own channel cast (e.g. cool snow
     shadows) is preserved, not forced to a per-channel target.
  4. `--i-alpha`: re-attach the N64 I-format intensity-as-alpha coupling
     (alpha := luma of the emitted RGB). The runtime loader reads PNGs via stbi at
     4 channels, so a plain-RGB replacement for an fmt=4 token would render
     alpha=255 everywhere, diverging from the native decode (gfx_pc.c replicates
     intensity into alpha). Off for CI/RGBA tokens.

Sources should be plain color maps: any source alpha channel is DISCARDED without
compositing (a transparent-edge source would pollute the tone/high-pass stats --
flatten it first if you must use one).

Every output gets a sibling provenance record `tok####.provenance.json` (asset
name, source URL, license, and the same `args` shape the overrides JSON carries) --
the substrate for pack NOTICE generation. License tier is a provenance fact:
CC0/PD -> A1, anything else (CC-BY*, share-alike -- review before distributing)
-> A2.

Deterministic: same input + args => same PNG (LANCZOS + numpy, no RNG).

Usage:
  cc0_import.py <source_image> --token 1267 --size 1024x512 --mean 188 \
      [--contrast 0.9] [--highpass 48] [--i-alpha] \
      --asset "ambientCG Snow010A" --license CC0 --url https://ambientcg.com/... \
      --out-dir ~/ge007_hd/textures
"""
import argparse
import json
import os
import sys

import numpy as np
from PIL import Image, ImageFilter

# Reuse the landed shared helpers (no fork): token naming + the one Rec.601 luma.
# Both sibling tools run as __main__ from this directory, so make the import work
# whether THIS file was loaded as a script or by pytest's importlib loader.
_HERE = os.path.dirname(os.path.abspath(__file__))
if _HERE not in sys.path:
    sys.path.insert(0, _HERE)
from build_pack import normalize_token_key  # noqa: E402
from make_sidecars import _luma  # noqa: E402


def _wrap_blur(im, sigma):
    """Gaussian blur with toroidal (wrap) padding: 3x3 tile, blur, center crop.
    Keeps a seamless source seamless where PIL's default edge-extend would not."""
    w, h = im.size
    tiled = Image.new(im.mode, (3 * w, 3 * h))
    for gy in range(3):
        for gx in range(3):
            tiled.paste(im, (gx * w, gy * h))
    return tiled.filter(ImageFilter.GaussianBlur(sigma)).crop((w, h, 2 * w, 2 * h))


def process(src_rgb, size_wh, mean_target, contrast=1.0, highpass_sigma=None,
            i_alpha=False, desat=1.0):
    """size_wh=(w,h). Returns an HxWx4 uint8 RGBA array. Pure function (testable).
    desat scales chroma about the per-pixel luma (1.0 = keep the source's cast,
    0.0 = grayscale) -- for sources whose hue is right in spirit but too loud
    against the level's palette (e.g. teal ice on a snowfield)."""
    im = src_rgb.convert("RGB").resize(size_wh, Image.LANCZOS)
    a = np.asarray(im).astype(np.float32)
    if highpass_sigma:
        blur = np.asarray(_wrap_blur(im, highpass_sigma), dtype=np.float32)
        a = a - blur + a.mean(axis=(0, 1))
    if desat != 1.0:
        luma = _luma(a).astype(np.float32)[..., None]
        a = luma + (a - luma) * desat
    a = (a - a.mean()) * contrast + mean_target
    a = np.clip(np.rint(a), 0, 255)
    if i_alpha:
        alpha = np.clip(np.rint(_luma(a)), 0, 255).astype(np.uint8)
    else:
        alpha = np.full((size_wh[1], size_wh[0]), 255, np.uint8)
    return np.dstack([a.astype(np.uint8), alpha])


def license_tier(license_str):
    """CC0/PD -> A1; anything else -> A2 (attribution/share-alike: review first).
    Prefix match so version suffixes ("CC0 1.0") don't silently downgrade."""
    up = license_str.strip().upper()
    return "A1" if up.startswith(("CC0", "PD", "PUBLIC DOMAIN")) else "A2"


def _positive_float(v):
    f = float(v)
    if f <= 0:
        raise argparse.ArgumentTypeError("must be > 0")
    return f


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("source", help="open-licensed source image (color map, no alpha)")
    ap.add_argument("--token", required=True,
                    help="settex token (1267 / tok1267 / settex_1267)")
    ap.add_argument("--size", required=True, help="output WxH, e.g. 1024x512")
    ap.add_argument("--mean", required=True, type=float,
                    help="target scalar mean (curated avgRGB luma; committable)")
    ap.add_argument("--contrast", type=float, default=1.0,
                    help="contrast scale about the mean (default 1.0)")
    ap.add_argument("--highpass", type=_positive_float, default=None, metavar="SIGMA",
                    help="tile-uniform wrap-padded high-pass sigma (tiled surfaces)")
    ap.add_argument("--i-alpha", action="store_true",
                    help="attach I-format intensity-as-alpha (fmt=4 tokens)")
    ap.add_argument("--desat", type=float, default=1.0,
                    help="chroma scale about luma (1.0 keep, 0.0 grayscale)")
    ap.add_argument("--asset", required=True, help='asset name, e.g. "ambientCG Snow010A"')
    ap.add_argument("--license", required=True, help="CC0 / PD / CC-BY-4.0 ...")
    ap.add_argument("--url", default="", help="source URL for the provenance record")
    ap.add_argument("--out-dir", required=True, help="pack textures/ dir")
    args = ap.parse_args(argv)

    tok = normalize_token_key(args.token)
    if tok is None:
        ap.error("--token %r is not a token id" % args.token)
    w, h = (int(v) for v in args.size.lower().split("x"))
    rgba = process(Image.open(args.source), (w, h), args.mean, args.contrast,
                   args.highpass, args.i_alpha, args.desat)
    os.makedirs(args.out_dir, exist_ok=True)
    out = os.path.join(args.out_dir, tok + ".png")
    Image.fromarray(rgba, "RGBA").save(out)

    # Same `args` shape the overrides JSON carries, so the committed curation
    # record and the build artifact can be diffed field-for-field.
    prov = {
        "token": tok,
        "asset": args.asset,
        "license": args.license,
        "url": args.url,
        "tier": license_tier(args.license),
        "tool": "cc0_import.py",
        "size": args.size,
        "args": {
            "mean": args.mean,
            "contrast": args.contrast,
            "highpass": args.highpass,
            "i_alpha": bool(args.i_alpha),
            "desat": args.desat,
        },
    }
    prov_path = os.path.join(args.out_dir, tok + ".provenance.json")
    with open(prov_path, "w") as f:
        json.dump(prov, f, indent=2, sort_keys=True)
        f.write("\n")
    print("wrote %s (%dx%d rgb_mean=%.1f) + provenance" %
          (out, w, h, float(rgba[..., :3].mean())))


if __name__ == "__main__":
    main()
