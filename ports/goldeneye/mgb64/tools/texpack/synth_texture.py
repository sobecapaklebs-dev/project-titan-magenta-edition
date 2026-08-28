#!/usr/bin/env python3
"""
synth_texture.py -- generate seamless, tone-matched PROCEDURAL surface textures.

Why this exists
---------------
The Real-ESRGAN upscale path (build_pack.py) is great for detailed art (signs,
crates, characters) but it degrades the big *tiled* hero surfaces: the ground
gravel gets smoothed into a flat smear and the rock walls melt into oily blobs,
because the N64 source is tiny (64x32 / 64x64) and the AI has no real detail to
recover -- it hallucinates instead.

For those surfaces a better answer is to *synthesize* a high-resolution, truly
seamless texture from scratch. With explicit generic tone (--mean/--sd), the
output is first-party math (no AI, no scraped assets) and distributable. With
--match, the output inherits ROM-derived tonal statistics and is local-only,
just like an upscale pack.

It is gameplay-neutral by the same contract as the rest of the pack: the loader
keeps the native tile dims, so UVs are unchanged; only the GL upload differs.

Usage
-----
  # one hero surface, tone-matched to its dump original (local-only Tier B):
  python3 tools/texpack/synth_texture.py gravel --match dump/ge007_settex_0022.rgba.ppm \
        --size 1024x512 --out ~/pack/textures/tok0022.png

  # or set tone explicitly (neutral gray, mean-luma / contrast; distributable Tier A):
  python3 tools/texpack/synth_texture.py rock --mean 87 --sd 60 \
        --size 1024x1024 --out ~/pack/textures/tok0949.png

Deterministic: same args + seed => same texture.
"""
import argparse, os, sys

import numpy as np


def _load_match_luma(path):
    """Flattened luma of a dump original (for histogram matching) plus mean/sd."""
    from PIL import Image
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    from build_pack import load_rgba
    im = load_rgba(path).convert("RGB")
    a = np.asarray(im, dtype=np.float64)
    lum = 0.299 * a[..., 0] + 0.587 * a[..., 1] + 0.114 * a[..., 2]
    return lum.ravel(), float(lum.mean()), float(lum.std())


def _hist_match(src, target_vals):
    """Remap src (HxW float) so its luma histogram equals the target's. This
    reproduces the original's EXACT tonal distribution -- dark matrix plus the
    occasional bright pebble fleck -- not just its mean/sd, so the substituted
    surface reads at the same brightness/contrast in-game as the stock texture."""
    s = src.ravel()
    order = np.argsort(s, kind="stable")
    ranks = np.empty(len(s), dtype=np.float64)
    ranks[order] = np.arange(len(s))
    q = ranks / max(1, len(s) - 1)                     # per-pixel quantile [0,1]
    tgt = np.sort(target_vals)
    matched = np.interp(q, np.linspace(0, 1, len(tgt)), tgt)
    return matched.reshape(src.shape)


def _worley(h, w, gx, gy, rng):
    """Toroidal jittered-grid Worley noise -> (F1, F2) distance fields, seamless.

    A random per-layer phase offset shifts every feature point by the same
    fraction of a cell, so cell centers do NOT line up on a visible grid between
    layers. Each pixel searches its 3x3 cell neighborhood with wrap-around, so
    opposite edges match exactly and the result tiles. Distances are in cell
    units (so a value of 1 ~ one cell width)."""
    ox0, oy0 = rng.random(), rng.random()                 # break grid alignment
    px = (np.arange(gx)[None, :] + 0.15 + 0.7 * rng.random((gy, gx))) / gx
    py = (np.arange(gy)[:, None] + 0.15 + 0.7 * rng.random((gy, gx))) / gy
    px = (px + ox0 / gx) % 1.0
    py = (py + oy0 / gy) % 1.0

    ys = (np.arange(h) + 0.5) / h
    xs = (np.arange(w) + 0.5) / w
    gxs = np.floor(((xs - ox0 / gx) % 1.0) * gx).astype(int)
    gys = np.floor(((ys - oy0 / gy) % 1.0) * gy).astype(int)

    F1 = np.full((h, w), 1e9)
    F2 = np.full((h, w), 1e9)
    Xs = xs[None, :]
    Ys = ys[:, None]
    for oy in (-1, 0, 1):
        for ox in (-1, 0, 1):
            cx = (gxs[None, :] + ox) % gx
            cy = (gys[:, None] + oy) % gy
            fx = px[0, cx]
            fy = py[cy, 0]
            dx = np.abs(Xs - fx); dx = np.minimum(dx, 1.0 - dx)
            dy = np.abs(Ys - fy); dy = np.minimum(dy, 1.0 - dy)
            d = np.sqrt(dx * dx + dy * dy)
            closer = d < F1
            F2 = np.where(closer, F1, np.minimum(F2, d))
            F1 = np.where(closer, d, F1)
    norm = max(gx, gy)
    return F1 * norm, F2 * norm


def _fft_noise(h, w, beta, rng):
    """Seamless isotropic fractal noise via spectral synthesis: filter white
    noise by 1/f**beta in the frequency domain and inverse-FFT. The DFT is
    inherently periodic, so the result tiles with NO directional banding.
    Returned zero-mean, unit-std."""
    fy = np.fft.fftfreq(h)[:, None]
    fx = np.fft.fftfreq(w)[None, :]
    f = np.sqrt(fx * fx + fy * fy)
    f[0, 0] = 1.0                       # avoid div-by-zero at DC
    amp = 1.0 / (f ** beta)
    amp[0, 0] = 0.0                     # zero mean
    phase = rng.random((h, w)) * 2 * np.pi
    spec = amp * np.exp(1j * phase)
    img = np.fft.ifft2(spec).real
    return (img - img.mean()) / (img.std() + 1e-6)


def _fbm(h, w, rng, octaves=5, persist=0.55):
    """Isotropic fractal noise (spectral). beta tuned so persist maps to a
    plausible spectral slope; octaves kept for API compatibility."""
    beta = 1.6 + 1.2 * persist
    return _fft_noise(h, w, beta, rng)


def _highpass(g, min_cells, soft=0.5):
    """Remove large-scale (low-frequency) brightness variation so the texture is
    TILE-UNIFORM: its local average is the same everywhere. Critical for tiled
    ground/wall surfaces -- any structure coarser than the tile (clouds, blotches)
    repeats visibly across the plane. Keeps everything finer than `min_cells`
    cycles across the width. Seamless (operates in the periodic DFT domain)."""
    H, W = g.shape
    cx = np.fft.fftfreq(W)[None, :] * W
    cy = np.fft.fftfreq(H)[:, None] * H
    rc = np.sqrt(cx ** 2 + (cy * (W / float(H))) ** 2)     # cycles across width
    keep = 1.0 / (1.0 + np.exp(-(rc - min_cells) / (min_cells * soft)))
    out = np.fft.ifft2(np.fft.fft2(g) * keep).real
    return out


def _bandpass(h, w, cells, rng, bw=0.6, aniso_y=1.0):
    """Seamless isotropic band-pass noise: spectral energy concentrated around a
    target spatial frequency (`cells` features across the width). This yields
    randomly-placed blobs ~one cell wide with NO grid -- the natural building
    block for pebbles/clasts. Zero-mean, unit-std.

    `aniso_y` stretches features along y by scaling the y spatial frequency (a
    value of 3 makes blobs 3x longer in y -> wind ripples). It multiplies a
    frequency-domain term, so the result stays exactly periodic and tileable.
    aniso_y==1.0 leaves the isotropic path bit-identical."""
    fy = np.fft.fftfreq(h)[:, None] * h
    fx = np.fft.fftfreq(w)[None, :] * w
    fyt = fy if aniso_y == 1.0 else fy * aniso_y
    r = np.sqrt((fx * (cells / w)) ** 2 + (fyt * (cells / h)) ** 2)
    # log-normal-ish bell centered on r==1 (one feature per cell-width)
    band = np.exp(-((np.log(r + 1e-6)) ** 2) / (2 * bw * bw))
    band[0, 0] = 0.0
    phase = rng.random((h, w)) * 2 * np.pi
    img = np.fft.ifft2(band * np.exp(1j * phase)).real
    return (img - img.mean()) / (img.std() + 1e-6)


def _pebbles(h, w, cell, rng, sharp=2.6):
    """A packed field of rounded pebbles at the given cell size. Each Worley cell
    is a stone, bright at its center (small F1) and dropping into a dark crevice
    at the cell boundary. Returns height in [0,1]."""
    f1, f2 = _worley(h, w, max(2, w // cell), max(2, h // cell), rng)
    body = np.clip(1.0 - f1 * sharp, 0.0, 1.0)        # round bump per stone
    body = body ** 0.7                                 # flatten tops a touch
    crev = np.clip((f2 - f1) * 2.0, 0.0, 1.0)          # darken inter-stone gaps
    return body * (0.5 + 0.5 * crev)


def gen_gravel(h, w, rng):
    """Packed gravel from band-pass noise at three clast scales (randomly placed,
    no grid) plus a broad tone wash and fine grit. The clast layers are combined
    so brighter stones sit over a darker matrix; a mild S-curve rounds the stones.
    Neutral gray in [0,1]."""
    big = _bandpass(h, w, w / 26.0, rng, bw=0.55)
    mid = _bandpass(h, w, w / 14.0, rng, bw=0.55)
    sml = _bandpass(h, w, w / 7.0,  rng, bw=0.50)
    # raised-cosine each layer to [0,1], combine with descending weight, take a
    # soft-max so stones of different sizes read as separate clasts not a blur
    def up(x): return np.clip(0.5 + 0.5 * x, 0, 1)
    stones = np.maximum(np.maximum(up(big), 0.8 * up(mid)), 0.6 * up(sml))
    grit = up(_bandpass(h, w, w / 3.0, rng, bw=0.7))   # fine sand between stones
    base = 0.80 * stones + 0.20 * grit
    # flatten any large-scale variation: a tiled ground must look uniform across
    # the whole plane, like the original's pure high-frequency speckle
    base = _highpass(base, min_cells=10)
    base = np.clip((base - base.mean()) * 1.1 + base.mean(), -3, 3)
    return base


def gen_rock(h, w, rng):
    """Rock face: fractured stone built from a dominant low-frequency facet field
    (big light/dark stone blocks) with darkened fracture lines between them and
    only a light dusting of grain. Low frequencies carry most of the energy so it
    reads as STONE STRUCTURE, not per-pixel static, even at the original's very
    high contrast (sd~63)."""
    # broadband fractal relief reads as a craggy cliff face (less "round" than a
    # single band-pass), with sharp dark fracture veins cutting through it
    relief = _fbm(h, w, rng, persist=0.55)
    veins = np.abs(_bandpass(h, w, w / 14.0, rng, bw=0.4))    # |ridge| -> thin dark cracks
    grain = _fbm(h, w, rng, persist=0.22)                    # fine rock grain
    def up(x): return 0.5 + 0.5 * x
    base = 0.58 * up(relief) - 0.26 * veins + 0.26 * up(grain)
    # tile-uniform: keep crag/crack detail, drop the large dark patches that
    # otherwise read as a dark smear across the minified wall at distance
    base = _highpass(base, min_cells=6)
    base = np.clip((base - base.mean()) * 1.2 + base.mean(), -3, 3)
    return base


def gen_concrete(h, w, rng):
    """Cast concrete: a fine broadband aggregate body carrying the bulk tone,
    with two speckle scales laid over it -- sparse air pores and fine surface
    grit. Low-persistence _fbm keeps the body from clumping into big patches;
    _highpass makes it tile-uniform; a mild S-curve firms the midtones so the
    aggregate reads as cast stone, not static. Neutral gray, roughly [-3,3]."""
    def up(x): return np.clip(0.5 + 0.5 * x, 0, 1)
    aggregate = _fbm(h, w, rng, persist=0.35)              # fine broadband body
    pores = _bandpass(h, w, w / 40.0, rng, bw=0.7)         # sparse air pockets
    grit = _bandpass(h, w, w / 6.0, rng, bw=0.6)           # fine surface grit
    base = 0.55 * up(aggregate) + 0.25 * up(pores) + 0.20 * up(grit)
    # tile-uniform: drop structure coarser than the tile so it doesn't repeat
    base = _highpass(base, min_cells=8)
    # mild S-curve: gentle midtone contrast about the mean
    base = np.clip((base - base.mean()) * 1.1 + base.mean(), -3, 3)
    return base


def gen_sand(h, w, rng):
    """Sand: dense fine grain (a high-frequency band-pass speckle) crossed by
    broad wind ripples. The ripple layer is anisotropic -- its y frequency is
    stretched 3x so the blobs elongate into horizontal banks -- yet it is built
    in the periodic DFT domain so it still tiles. _highpass keeps the plane flat
    at distance. Neutral gray, roughly [-3,3]."""
    def up(x): return np.clip(0.5 + 0.5 * x, 0, 1)
    grain = _bandpass(h, w, w / 3.0, rng, bw=0.8)          # fine sand grain
    ripples = _bandpass(h, w, w / 24.0, rng, bw=0.45, aniso_y=3.0)  # wind ripples
    base = 0.7 * up(grain) + 0.3 * up(ripples)
    base = _highpass(base, min_cells=12)
    return base


def gen_snow(h, w, rng):
    """Snow: a soft low-persistence _fbm body (gentle dune undulation) with a
    coarse sparkle band on top. The brightest 2% of the field is pushed to the
    top of the range so those glints blow out to pure white after tone-mapping,
    reading as ice crystals. _highpass keeps it tile-uniform. Pairs with the
    high-mean / tiny-sd default tone (215/8). Roughly [-3,3] before tone."""
    def up(x): return np.clip(0.5 + 0.5 * x, 0, 1)
    body = _fbm(h, w, rng, persist=0.25)                   # soft dune body
    sparkle = _bandpass(h, w, w / 2.0, rng, bw=0.9)        # coarse crystal glints
    base = 0.8 * up(body) + 0.2 * up(sparkle)
    base = _highpass(base, min_cells=10)
    # sparkle: hard-clip the brightest 2% to the top of the range so they clip
    # to 255 after tone-mapping (blown-out ice glints). Isolated bright points
    # are high-frequency, so this does not add tiled macro structure.
    thr = np.quantile(base, 0.98)
    span = base.max() - base.min() + 1e-6
    base = np.where(base >= thr, base.max() + 0.5 * span, base)
    return base


GEN = {"gravel": gen_gravel, "rock": gen_rock,
       "concrete": gen_concrete, "sand": gen_sand, "snow": gen_snow}

# Generic-tone (Tier A1) default mean/sd per preset (roadmap 02 §4.4). --mean/--sd
# override these; --match (local-only Tier B) replaces them with dump statistics.
DEFAULT_TONE = {
    "gravel": (64.0, 22.0),
    "rock": (64.0, 22.0),
    "concrete": (110.0, 14.0),
    "sand": (150.0, 12.0),
    "snow": (215.0, 8.0),
}


def synthesize(kind, w, h, seed, mean, sd, target_vals=None, use_hist=True):
    """Deterministic core: build the preset field (GEN[kind]) then apply tone.
    Returns an H*W*3 uint8 grayscale-RGB array. Same (kind, w, h, seed, tone)
    always yields identical bytes. When `target_vals` is set and `use_hist`, the
    full luma histogram of the dump is reproduced (Tier B); otherwise the field
    is normalized to the requested mean/sd (generic Tier A1)."""
    rng = np.random.default_rng(seed)
    g = GEN[kind](h, w, rng)
    if target_vals is not None and use_hist:
        # exact tonal distribution of the original (brightness, contrast, flecks)
        lum = np.clip(_hist_match(g, target_vals), 0, 255)
    else:
        # normalize to the requested mean/sd luma
        g = (g - g.mean()) / (g.std() + 1e-6)
        lum = np.clip(g * sd + mean, 0, 255)
    return np.repeat(lum[..., None], 3, axis=2).astype(np.uint8)


def main():
    ap = argparse.ArgumentParser(description="Generate a seamless, tone-matched procedural texture.")
    ap.add_argument("kind", choices=sorted(GEN))
    ap.add_argument("--out", required=True)
    ap.add_argument("--size", default="1024x1024", help="WxH (keep the source aspect)")
    ap.add_argument("--match", help="dump original to tone-match (histogram by default)")
    ap.add_argument("--mean", type=float, help="target mean luma 0-255 (overrides --match)")
    ap.add_argument("--sd", type=float, help="target luma stddev (overrides --match)")
    ap.add_argument("--no-hist", action="store_true",
                    help="with --match, match only mean/sd instead of the full histogram")
    ap.add_argument("--seed", type=int, default=20640622)
    args = ap.parse_args()

    if os.path.splitext(args.out)[1].lower() != ".png":
        sys.exit("synth_texture.py writes runtime texture-pack PNGs only; use --out .../tok####.png")

    w, h = (int(v) for v in args.size.lower().split("x"))
    mean, sd = DEFAULT_TONE[args.kind]
    target_vals = None
    if args.match:
        target_vals, mean, sd = _load_match_luma(args.match)
    if args.mean is not None:
        mean = args.mean
    if args.sd is not None:
        sd = args.sd

    use_hist = (not args.no_hist) and args.mean is None and args.sd is None
    rgb = synthesize(args.kind, w, h, args.seed, mean, sd, target_vals, use_hist)

    from PIL import Image
    out = Image.fromarray(rgb, "RGB")
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    out.save(args.out)
    print(f"{args.kind}: {w}x{h} mean={mean:.0f} sd={sd:.0f} -> {args.out}")


if __name__ == "__main__":
    main()
