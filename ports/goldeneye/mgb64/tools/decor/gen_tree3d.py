#!/usr/bin/env python3
"""gen_tree3d.py -- first-party 3D snowy-spruce model generator (glTF 2.0).

Builds game-ready conifer models the way shipped titles do it: a tapered
trunk cylinder plus whorls of radial needle-CARD quads (alpha-cutout), a few
hundred triangles per tree -- not the millions of a photoscan. Everything is
parameterized and deterministic (same args => byte-identical .glb), so trees
are reproducible art, not baked blobs.

Outputs:
  <out>/<name>.glb            two primitives: trunk (opaque), cards (MASK,
                              doubleSided), u8 COLOR_0 vertex bake
  <out>/<name>_needles.png    first-party painted needle-spray card (RGBA)
  <out>/<name>_bark.png       tone-adjusted copy of --bark (or first-party
                              procedural bark if --bark is omitted)
  <out>/<name>.provenance.json

The needle card is painted here (strokes + snow load, native alpha); the bark
may come from an open-licensed source (e.g. ambientCG / Poly Haven CC0 --
record it in --bark-license) or be generated. Vertex colors carry the light
bake: a fixed sun direction lambert + height/interior ambient occlusion, so
the model reads shaded even through a shadeless fixed-function path.

Usage:
  gen_tree3d.py --name spruce_a --seed 7 --height 1.0 \
      [--whorls 10] [--cards 6] [--snow 0.55] \
      [--bark bark_diff.jpg --bark-license CC0 --bark-url https://...] \
      --out assets/decor/models
"""
import argparse
import json
import math
import os

import struct

import numpy as np
from PIL import Image, ImageDraw

SUN = np.array([0.35, 0.80, 0.49])   # matches the level's high back-sun feel
SUN_N = SUN / np.linalg.norm(SUN)


# ------------------------------------------------------------------ textures

def paint_needles(size, seed, snow, rng=None, supersample=1):
    """Needle-spray card: layered strokes fanning off a central stem, snow
    dusted on the upper edge. Returns an RGBA PIL image with native alpha.
    supersample>1 paints at N x resolution and LANCZOS-downsamples for
    anti-aliased needles (the modern render path keeps that fidelity)."""
    if supersample > 1:
        im = paint_needles(size * supersample, seed, snow, rng)
        return im.resize((size, size), Image.LANCZOS)
    rng = rng or np.random.default_rng(seed)
    im = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    dr = ImageDraw.Draw(im)
    cx = size // 2
    # stem: root (bottom center) to tip (top center)
    dr.line([(cx, size - 2), (cx, int(size * 0.06))],
            fill=(72, 58, 44, 255), width=max(2, size // 96))
    greens = [(58, 84, 58), (48, 74, 52), (66, 92, 62), (42, 64, 46)]
    n = int(size * 1.6)
    for _ in range(n):
        t = rng.uniform(0.05, 0.97)               # position along stem (0=tip)
        y0 = int(size * (0.06 + 0.92 * t))
        length = size * (0.06 + 0.30 * t) * rng.uniform(0.7, 1.0)
        side = rng.choice((-1.0, 1.0))
        ang = math.radians(rng.uniform(18, 55)) * side
        x1 = cx + math.sin(ang) * length
        y1 = y0 - math.cos(ang) * length * 0.35 + length * 0.18
        g = greens[int(rng.integers(0, len(greens)))]
        shade = rng.uniform(0.75, 1.1)
        col = tuple(min(255, int(c * shade)) for c in g) + (255,)
        dr.line([(cx, y0), (x1, y1)], fill=col, width=max(1, round(size / 40)))
    # snow load: bright strokes biased to the upper half / outer tips
    for _ in range(int(n * snow)):
        t = rng.uniform(0.05, 0.75)
        y0 = int(size * (0.06 + 0.92 * t))
        length = size * (0.05 + 0.26 * t) * rng.uniform(0.6, 0.95)
        side = rng.choice((-1.0, 1.0))
        ang = math.radians(rng.uniform(20, 60)) * side
        x1 = cx + math.sin(ang) * length
        y1 = y0 - math.cos(ang) * length * 0.4
        w = tuple(int(v) for v in rng.uniform(222, 245, 3)) + (255,)
        dr.line([(cx, y0), (x1, y1)], fill=w, width=max(1, round(size / 52)))
    return im


def paint_bark(size, seed):
    """Fallback first-party bark: vertical fibrous noise."""
    rng = np.random.default_rng(seed)
    base = rng.normal(0, 1, (size // 4, size)).repeat(4, axis=0)
    base = np.cumsum(base, axis=0)
    base -= base.mean(axis=0, keepdims=True)
    v = 60 + 18 * (base / (np.abs(base).max() + 1e-6))
    rgb = np.stack([v * 1.15, v * 0.95, v * 0.78], axis=-1)
    return Image.fromarray(np.clip(rgb, 0, 255).astype(np.uint8), "RGB")


# ------------------------------------------------------------------ geometry

def build_tree(seed, height, whorls, cards, snow, detail="classic"):
    """Returns dict of numpy arrays per primitive:
    {'trunk': (pos,nrm,uv,col,idx), 'cards': (...)}. Local space: Y up,
    origin at trunk base, height = `height` units."""
    rng = np.random.default_rng(seed)
    H = height

    def bake(pos, nrm, interior):
        lam = np.clip(nrm @ SUN_N, 0.0, 1.0)
        heightk = 0.55 + 0.45 * (pos[:, 1] / H)
        shade = (0.42 + 0.58 * lam) * heightk * (1.0 - 0.35 * interior)
        # COLOR_0.a = modern-shader snow cover; the painted textures already
        # carry snow, so bake none here
        col = np.stack([shade, shade, shade, np.zeros_like(shade)], axis=-1)
        return np.clip(col * 255, 0, 255).astype(np.uint8)

    # trunk: tapered cylinder
    sides, segs = (12, 6) if detail == "high" else (8, 3)
    r0, r1 = 0.050 * H, 0.012 * H
    tp, tn, tu, ti = [], [], [], []
    for s in range(segs + 1):
        y = H * s / segs
        r = r0 + (r1 - r0) * (s / segs)
        for i in range(sides + 1):
            a = 2 * math.pi * i / sides
            tp.append((math.cos(a) * r, y, math.sin(a) * r))
            tn.append((math.cos(a), 0.0, math.sin(a)))
            tu.append((i / sides * 2.0, y / H * 4.0))
    for s in range(segs):
        for i in range(sides):
            a = s * (sides + 1) + i
            b = a + sides + 1
            ti += [a, b, a + 1, a + 1, b, b + 1]
    tp = np.array(tp, np.float32); tn = np.array(tn, np.float32)
    tcol = bake(tp, tn, np.zeros(len(tp), np.float32))

    # needle cards: whorls of radial quads, drooping, shrinking with height
    cp, cn, cu, ci, cint = [], [], [], [], []
    y_lo, y_hi = 0.22 * H, 0.99 * H
    for wi in range(whorls):
        t = wi / max(1, whorls - 1)               # 0 bottom -> 1 top
        y = y_lo + (y_hi - y_lo) * t
        L = H * (0.40 - 0.31 * t) * float(rng.uniform(0.9, 1.1))
        Wd = L * 0.62
        droop = math.radians(16 + 10 * (1 - t))
        base_rot = float(rng.uniform(0, 2 * math.pi))
        for cidx in range(cards):
            a = base_rot + 2 * math.pi * cidx / cards
            dirv = np.array([math.cos(a), 0.0, math.sin(a)])
            out = dirv * math.cos(droop) + np.array([0.0, -math.sin(droop), 0.0])
            side = np.array([-math.sin(a), 0.0, math.cos(a)])
            root = np.array([0.0, y, 0.0]) + dirv * (0.02 * H)
            tip = root + out * L
            nrm = np.cross(side, out)
            nrm = nrm / (np.linalg.norm(nrm) + 1e-9)
            if nrm[1] < 0:
                nrm = -nrm                         # cards light from above
            # crossed-quad bough: a flat spray plus a perpendicular one, so
            # the branch keeps silhouette seen from below/side (a single
            # drooping quad collapses to a streak at eye level). High detail
            # adds a second, smaller cluster per branch (offset + rotated) so
            # boughs read dense instead of planar.
            up2 = np.cross(out, side)
            up2 = up2 / (np.linalg.norm(up2) + 1e-9)
            clusters = [(root, tip, Wd)]
            if detail == "high":
                for koff in (-0.45, 0.4):
                    a2 = a + koff
                    d2 = np.array([math.cos(a2), 0.0, math.sin(a2)])
                    o2 = d2 * math.cos(droop) + np.array(
                        [0.0, -math.sin(droop) * rng.uniform(0.8, 1.3), 0.0])
                    r2 = root + np.array([0.0, 0.035 * H * rng.uniform(-1, 1),
                                          0.0])
                    clusters.append((r2, r2 + o2 * L * 0.62, Wd * 0.62))
            for c_root, c_tip, c_w in clusters:
              c_side = c_tip - c_root
              c_side = np.cross(c_side / (np.linalg.norm(c_side) + 1e-9),
                                np.array([0.0, 1.0, 0.0]))
              nlen = np.linalg.norm(c_side)
              c_side = side if nlen < 1e-6 else c_side / nlen
              c_up = np.cross(c_tip - c_root, c_side)
              c_up = c_up / (np.linalg.norm(c_up) + 1e-9)
              for span in (c_side, c_up):
                base_i = len(cp)
                for corner, uvc in ((c_root - span * c_w / 2, (0.0, 1.0)),
                                    (c_root + span * c_w / 2, (1.0, 1.0)),
                                    (c_tip - span * c_w / 2, (0.0, 0.0)),
                                    (c_tip + span * c_w / 2, (1.0, 0.0))):
                    cp.append(corner); cn.append(nrm); cu.append(uvc)
                    cint.append(1.0 - t)           # lower cards sit in shadow
                ci += [base_i, base_i + 1, base_i + 2,
                       base_i + 2, base_i + 1, base_i + 3]
    cp = np.array(cp, np.float32); cn = np.array(cn, np.float32)
    ccol = bake(cp, cn, np.array(cint, np.float32) * 0.7)

    return {
        "trunk": (tp, tn, np.array(tu, np.float32), tcol,
                  np.array(ti, np.uint16)),
        "cards": (cp, cn, np.array(cu, np.float32), ccol,
                  np.array(ci, np.uint16)),
    }


# ------------------------------------------------------------------ glb emit

def write_glb(path, prims, tex_uris):
    """Minimal glTF 2.0 binary writer: one mesh, one node, per-primitive
    material. prims: {name: (pos,nrm,uv,col,idx)}; tex_uris: {name: uri}."""
    bin_parts, buffer_views, accessors = [], [], []
    offset = 0

    def add_view(data, target):
        nonlocal offset
        raw = data.tobytes()
        pad = (4 - len(raw) % 4) % 4
        bin_parts.append(raw + b"\x00" * pad)
        buffer_views.append({"buffer": 0, "byteOffset": offset,
                             "byteLength": len(raw), "target": target})
        offset += len(raw) + pad
        return len(buffer_views) - 1

    def add_acc(view, comp, count, atype, normalized=False,
                vmin=None, vmax=None):
        acc = {"bufferView": view, "componentType": comp, "count": count,
               "type": atype}
        if normalized:
            acc["normalized"] = True
        if vmin is not None:
            acc["min"] = vmin; acc["max"] = vmax
        accessors.append(acc)
        return len(accessors) - 1

    materials, textures, images, samplers = [], [], [], [{
        "magFilter": 9729, "minFilter": 9987,
        "wrapS": 10497, "wrapT": 10497}]
    primitives = []
    for name, (pos, nrm, uv, col, idx) in prims.items():
        pv = add_view(pos, 34962); nv = add_view(nrm, 34962)
        uvv = add_view(uv, 34962); cv = add_view(col, 34962)
        iv = add_view(idx, 34963)
        pa = add_acc(pv, 5126, len(pos), "VEC3",
                     vmin=[float(v) for v in pos.min(axis=0)],
                     vmax=[float(v) for v in pos.max(axis=0)])
        na = add_acc(nv, 5126, len(nrm), "VEC3")
        ua = add_acc(uvv, 5126, len(uv), "VEC2")
        ca = add_acc(cv, 5121, len(col), "VEC4", normalized=True)
        ia = add_acc(iv, 5123, len(idx), "SCALAR")
        images.append({"uri": tex_uris[name]})
        textures.append({"sampler": 0, "source": len(images) - 1})
        mat = {"name": name, "pbrMetallicRoughness": {
                   "baseColorTexture": {"index": len(textures) - 1},
                   "metallicFactor": 0.0, "roughnessFactor": 1.0}}
        if name == "cards":
            mat["alphaMode"] = "MASK"
            mat["alphaCutoff"] = 0.45
            mat["doubleSided"] = True
        materials.append(mat)
        primitives.append({"attributes": {"POSITION": pa, "NORMAL": na,
                                          "TEXCOORD_0": ua, "COLOR_0": ca},
                           "indices": ia, "material": len(materials) - 1})

    blob = b"".join(bin_parts)
    doc = {"asset": {"version": "2.0", "generator": "mgb64 gen_tree3d.py"},
           "scene": 0, "scenes": [{"nodes": [0]}],
           "nodes": [{"mesh": 0, "name": "tree"}],
           "meshes": [{"primitives": primitives}],
           "materials": materials, "textures": textures, "images": images,
           "samplers": samplers, "accessors": accessors,
           "bufferViews": buffer_views,
           "buffers": [{"byteLength": len(blob)}]}
    js = json.dumps(doc, separators=(",", ":"), sort_keys=True).encode()
    js += b" " * ((4 - len(js) % 4) % 4)
    total = 12 + 8 + len(js) + 8 + len(blob)
    with open(path, "wb") as f:
        f.write(struct.pack("<III", 0x46546C67, 2, total))
        f.write(struct.pack("<II", len(js), 0x4E4F534A)); f.write(js)
        f.write(struct.pack("<II", len(blob), 0x004E4942)); f.write(blob)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--name", required=True)
    ap.add_argument("--seed", type=int, required=True)
    ap.add_argument("--height", type=float, default=1.0)
    ap.add_argument("--whorls", type=int, default=10)
    ap.add_argument("--cards", type=int, default=6)
    ap.add_argument("--snow", type=float, default=0.55,
                    help="snow-stroke density on the needle card (0..1)")
    ap.add_argument("--detail", choices=("classic", "high"), default="classic",
                    help="classic = N64-path budgets (16-vert batches, 64px "
                         "textures); high = modern-path budgets (dense "
                         "clustered boughs, supersampled textures)")
    ap.add_argument("--tex-size", type=int, default=64,
                    help="card/bark texture edge; the engine standard texture path caps at 4096 texels (64x64)")
    ap.add_argument("--bark", default=None,
                    help="open-licensed bark diffuse to tone into the trunk")
    ap.add_argument("--bark-license", default="first-party original")
    ap.add_argument("--bark-url", default="")
    ap.add_argument("--out", required=True)
    args = ap.parse_args(argv)

    os.makedirs(args.out, exist_ok=True)
    needles = paint_needles(args.tex_size, args.seed, args.snow,
                            supersample=2 if args.detail == "high" else 1)
    needles.save(os.path.join(args.out, f"{args.name}_needles.png"))
    if args.bark:
        bark = Image.open(args.bark).convert("RGB").resize(
            (args.tex_size, args.tex_size), Image.LANCZOS)
    else:
        bark = paint_bark(args.tex_size, args.seed)
    bark.save(os.path.join(args.out, f"{args.name}_bark.png"))

    prims = build_tree(args.seed, args.height, args.whorls, args.cards,
                       args.snow, args.detail)
    tris = sum(len(p[4]) // 3 for p in prims.values())
    write_glb(os.path.join(args.out, f"{args.name}.glb"), prims,
              {"trunk": f"{args.name}_bark.png",
               "cards": f"{args.name}_needles.png"})

    prov = {"model": args.name, "tool": "gen_tree3d.py", "tier": "A1",
            "license": "first-party original (geometry, needle card)",
            "bark": {"source": os.path.basename(args.bark) if args.bark
                     else "first-party procedural",
                     "license": args.bark_license, "url": args.bark_url},
            "args": {"seed": args.seed, "height": args.height,
                     "whorls": args.whorls, "cards": args.cards,
                     "snow": args.snow, "tex_size": args.tex_size,
                     "detail": args.detail},
            "triangles": tris}
    with open(os.path.join(args.out, f"{args.name}.provenance.json"),
              "w") as f:
        json.dump(prov, f, indent=2, sort_keys=True)
        f.write("\n")
    print(f"wrote {args.name}.glb ({tris} tris) + textures + provenance")


if __name__ == "__main__":
    main()
