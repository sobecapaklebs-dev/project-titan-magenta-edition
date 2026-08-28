#!/usr/bin/env python3
"""gen_treeline.py -- first-party snowy-conifer treeline band generator.

Hand-authored procedural ART (not noise): draws layered spruce silhouettes with
snow-dusted crowns against a fog gradient, for the thin distant treeline bands
that ring outdoor levels (e.g. Surface's 64x17 hillside strips, tok1198-1201).
AI upscalers melt those strips into mush -- there is nothing to recover at 4x
from a 1-2px-per-tree source -- so this draws real trees at the target
resolution instead, at the same trees-per-tile density the stock band implies.

Everything is parameterized by explicit committable values (colors, density,
seed) -- no ROM pixels are read, so the output is Tier A1 (distributable),
routed as source=original. Deterministic: same args => byte-identical PNG.
Horizontally seamless by construction (trees draw modulo the tile width).

Usage:
  gen_treeline.py --token 1200 --size 1024x272 --seed 12 \
      [--trees 22] [--fog d0d3da] [--dark 23272b] [--lit 3f474b] \
      [--snow dde3ea] [--cliff ""] --out-dir <pack>/textures
"""
import argparse
import json
import os
import sys

import numpy as np
from PIL import Image

_HERE = os.path.dirname(os.path.abspath(__file__))
if _HERE not in sys.path:
    sys.path.insert(0, _HERE)
from build_pack import normalize_token_key  # noqa: E402


def _hex_rgb(s):
    s = s.strip().lstrip("#")
    return tuple(int(s[i:i + 2], 16) for i in (0, 2, 4))


def _lerp(a, b, t):
    return tuple(a[i] + (b[i] - a[i]) * t for i in range(3))


def render(w, h, seed, n_trees, fog, dark, lit, snow, snow_left=0.0):
    """Draw the band into an HxWx3 float array. Painter's order: fog gradient,
    then back-to-front tree layers (far = small + fog-washed, near = tall +
    dark), each spruce a stack of jittered triangles, snow-dusted crowns.
    The Surface hillside geometry TILES the strip vertically, so both
    horizontal edges wrap by modulo drawing and the top/bottom ~12%% fades
    into fog -- each repeat reads as another receding forest row instead of
    a hard band. snow_left > 0 reserves that left fraction as a bare snow
    slope (soft diagonal edge), for strips that transition into cliff/snow."""
    rng = np.random.default_rng(seed)
    yy = np.linspace(0.0, 1.0, h)[:, None]
    img = np.zeros((h, w, 3), np.float32)
    mask = np.zeros((h, w), np.float32)      # tree coverage (for alpha-cutout)
    ground = _lerp(fog, snow, 0.55)
    for c in range(3):
        img[..., c] = fog[c] + (ground[c] - fog[c]) * yy

    layers = ((0.55, 0.55, 0.62), (0.80, 0.75, 0.30), (1.00, 1.00, 0.00))
    for height_k, count_k, fog_k in layers:
        base = _lerp(dark, fog, fog_k)
        n = max(2, int(n_trees * count_k))
        xs = (rng.random(n) + np.arange(n)) / n * w      # jittered, spread
        for x0 in xs:
            th = h * height_k * rng.uniform(0.80, 1.00)  # tree height
            tw = th * rng.uniform(0.34, 0.48)            # crown width
            tiers = int(rng.integers(3, 5))
            top = h - th
            tint = rng.uniform(0.85, 1.05)
            for t in range(tiers):
                ty0 = top + th * t / tiers
                ty1 = top + th * (t + 1.15) / tiers
                half = tw * (t + 1.5) / tiers * 0.5
                y0i, y1i = int(max(0, ty0)), int(min(h, ty1))
                for y in range(y0i, y1i):
                    frac = (y - ty0) / max(1.0, ty1 - ty0)
                    hw = max(1.0, half * frac)
                    xa, xb = int(x0 - hw), int(x0 + hw) + 1
                    xr = np.arange(xa, xb) % w           # modulo => seamless
                    col = np.array([base[c] * tint for c in range(3)],
                                   np.float32)
                    # snow load on each tier's shoulders, heavier up top
                    k = 0.85 * max(0.0, 1.0 - frac / 0.45)
                    px = col * (1 - k) + np.array(snow, np.float32) * k
                    img[y, xr] = px
                    mask[y, xr] = 1.0
    # vertical wrap-fade: melt into fog near BOTH edges so vertical tiling
    # reads as receding rows, not a hard seam
    fade = np.minimum(yy / 0.07, (1.0 - yy) / 0.07)
    fade = np.clip(fade, 0.0, 1.0)
    for c in range(3):
        img[..., c] = img[..., c] * fade[:, 0][:, None] + \
            (fog[c] + (ground[c] - fog[c]) * yy[:, 0])[:, None] * \
            (1 - fade[:, 0][:, None])
    mask *= fade[:, 0][:, None]
    if snow_left > 0.0:
        xxn = np.arange(w, dtype=np.float32) / w
        edge = snow_left + 0.06 * np.sin(yy[:, 0] * 6.28318 * 1.5)
        smask = 1.0 / (1.0 + np.exp((xxn[None, :] - edge[:, None]) / 0.02))
        slope = np.empty_like(img)
        for c in range(3):
            slope[..., c] = snow[c] * (0.92 + 0.08 * yy)
        img = slope * smask[..., None] + img * (1 - smask[..., None])
        mask = np.maximum(mask, smask)       # the snow slope is solid
    return img, mask


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--token", required=True)
    ap.add_argument("--size", required=True, help="output WxH, e.g. 1024x272")
    ap.add_argument("--seed", type=int, required=True)
    ap.add_argument("--trees", type=int, default=22,
                    help="foreground trees per tile width (stock density ~1/3 tile-px)")
    ap.add_argument("--fog", default="c9ccd4", help="haze/background RGB hex")
    ap.add_argument("--dark", default="23272b", help="near-silhouette RGB hex")
    ap.add_argument("--lit", default="3f474b", help="lit/trunk accent RGB hex")
    ap.add_argument("--snow", default="dde3ea", help="snow-dust RGB hex")
    ap.add_argument("--snow-left", type=float, default=0.0,
                    help="left fraction reserved as bare snow slope (0..1)")
    ap.add_argument("--flip-v", action="store_true",
                    help="emit V-flipped (Surface hillside geometry maps V inverted)")
    ap.add_argument("--alpha-cutout", action="store_true",
                    help="alpha = tree coverage (for translucent XLU overlay "
                         "strips whose stock texture has see-through gaps)")
    ap.add_argument("--out-dir", required=True)
    args = ap.parse_args(argv)

    tok = normalize_token_key(args.token)
    if tok is None:
        ap.error("--token %r is not a token id" % args.token)
    w, h = (int(v) for v in args.size.lower().split("x"))
    img, mask = render(w, h, args.seed, args.trees, _hex_rgb(args.fog),
                       _hex_rgb(args.dark), _hex_rgb(args.lit),
                       _hex_rgb(args.snow), args.snow_left)
    if args.flip_v:
        img = img[::-1]
        mask = mask[::-1]
    if args.alpha_cutout:
        alpha = np.clip(np.rint(mask * 255), 0, 255).astype(np.uint8)
    else:
        alpha = np.full((h, w), 255, np.uint8)
    rgba = np.dstack([np.clip(np.rint(img), 0, 255).astype(np.uint8), alpha])
    os.makedirs(args.out_dir, exist_ok=True)
    out = os.path.join(args.out_dir, tok + ".png")
    Image.fromarray(rgba, "RGBA").save(out)
    prov = {
        "token": tok, "asset": "first-party gen_treeline.py art",
        "license": "first-party original", "url": "", "tier": "A1",
        "tool": "gen_treeline.py", "size": args.size,
        "args": {"seed": args.seed, "trees": args.trees, "fog": args.fog,
                 "dark": args.dark, "lit": args.lit, "snow": args.snow,
                 "snow_left": args.snow_left, "flip_v": bool(args.flip_v),
                 "alpha_cutout": bool(args.alpha_cutout)},
    }
    with open(os.path.join(args.out_dir, tok + ".provenance.json"), "w") as f:
        json.dump(prov, f, indent=2, sort_keys=True)
        f.write("\n")
    print("wrote %s (%dx%d)" % (out, w, h))


if __name__ == "__main__":
    main()
