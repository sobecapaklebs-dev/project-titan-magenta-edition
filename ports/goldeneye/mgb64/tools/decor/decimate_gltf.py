#!/usr/bin/env python3
"""decimate_gltf.py -- vertex-clustering decimation for CC0 photoscan glTF.

Takes a photogrammetry glTF (JSON + external .bin + textures, e.g. Poly
Haven's model exports, which run to millions of triangles) and produces a
game-budget .glb for the decor modern render path:

  - uniform-grid VERTEX CLUSTERING per primitive: vertices sharing a grid
    cell merge to their mean (positions/normals/UVs); degenerate and
    duplicate triangles drop. Deterministic, no RNG, and it preserves the
    scan's silhouette far better than its texel budget deserves.
  - textures resized to --tex-size (default 512) PNG.
  - a simple sun-lambert vertex-color bake (matches gen_tree3d.py), so the
    result reads shaded through the fixed-function-style modern shader.
  - provenance JSON with the source name/license/url and all args.

Usage:
  decimate_gltf.py <model.gltf> --name fir_sapling --grid 140 \
      --license CC0 --url https://polyhaven.com/a/fir_sapling_medium \
      --out assets/decor/models [--tex-size 512] [--cutout-material twig]
"""
import argparse
import json
import os
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from glbwrite import write_glb  # noqa: E402

SUN_N = np.array([0.35, 0.80, 0.49])
SUN_N = SUN_N / np.linalg.norm(SUN_N)

COMP_DTYPE = {5120: np.int8, 5121: np.uint8, 5122: np.int16,
              5123: np.uint16, 5125: np.uint32, 5126: np.float32}
TYPE_N = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}


def read_accessor(doc, blob, idx):
    a = doc["accessors"][idx]
    bv = doc["bufferViews"][a["bufferView"]]
    dt = COMP_DTYPE[a["componentType"]]
    n = TYPE_N[a["type"]]
    off = bv.get("byteOffset", 0) + a.get("byteOffset", 0)
    if bv.get("byteStride") not in (None, n * np.dtype(dt).itemsize):
        raise SystemExit("interleaved bufferViews not supported")
    arr = np.frombuffer(blob, dtype=dt, count=a["count"] * n, offset=off)
    return arr.reshape(a["count"], n) if n > 1 else arr


def cluster(pos, nrm, uv, idx, grid):
    """Uniform-grid clustering. Returns (pos, nrm, uv, tri) decimated."""
    lo = pos.min(axis=0)
    ext = float((pos.max(axis=0) - lo).max()) or 1.0
    cell = ext / grid
    key = np.floor((pos - lo) / cell).astype(np.int64)
    key = key[:, 0] * 73856093 ^ key[:, 1] * 19349663 ^ key[:, 2] * 83492791
    uniq, inverse = np.unique(key, return_inverse=True)
    k = len(uniq)
    cpos = np.zeros((k, 3), np.float64)
    cnrm = np.zeros((k, 3), np.float64)
    cuv = np.zeros((k, 2), np.float64)
    cnt = np.zeros(k, np.float64)
    np.add.at(cpos, inverse, pos)
    np.add.at(cnrm, inverse, nrm)
    np.add.at(cuv, inverse, uv)
    np.add.at(cnt, inverse, 1.0)
    cpos /= cnt[:, None]
    cuv /= cnt[:, None]
    nl = np.linalg.norm(cnrm, axis=1)
    cnrm = cnrm / np.where(nl < 1e-9, 1.0, nl)[:, None]
    tri = inverse[idx.reshape(-1, 3)]
    good = ((tri[:, 0] != tri[:, 1]) & (tri[:, 1] != tri[:, 2]) &
            (tri[:, 0] != tri[:, 2]))
    tri = tri[good]
    tri = np.unique(np.sort(tri, axis=1), axis=0) if len(tri) else tri
    return (cpos.astype(np.float32), cnrm.astype(np.float32),
            cuv.astype(np.float32), tri.astype(np.uint32))


def bake_colors(pos, nrm, height, ambient=0.45, snow=0.0):
    """rgb = sun-lambert light bake; ALPHA = snow cover from upward-facing
    normals (the modern shader lerps toward snow white by COLOR_0.a)."""
    lam = np.clip(nrm @ SUN_N, 0.0, 1.0)
    hk = 0.55 + 0.45 * np.clip(pos[:, 1] / max(height, 1e-6), 0.0, 1.0)
    shade = (ambient + (1.15 - ambient) * lam) * hk
    up = np.clip(nrm[:, 1], 0.0, 1.0)
    cover = snow * up ** 1.5
    col = np.stack([shade, shade, shade, cover], axis=-1)
    return np.clip(col * 255, 0, 255).astype(np.uint8)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("gltf", help="source .gltf (JSON with external .bin)")
    ap.add_argument("--name", required=True)
    ap.add_argument("--grid", type=int, default=140,
                    help="clustering grid cells across the largest extent")
    ap.add_argument("--tex-size", type=int, default=512)
    ap.add_argument("--license", required=True)
    ap.add_argument("--url", default="")
    ap.add_argument("--ambient", type=float, default=0.45,
                    help="vertex-bake ambient floor (raise for bright scenes)")
    ap.add_argument("--snow", type=float, default=0.0,
                    help="snow cover on upward-facing surfaces (0..1)")
    ap.add_argument("--cutout-material", default=None,
                    help="material-name substring rendered as alpha-cutout "
                         "double-sided (e.g. twig/leaf cards)")
    ap.add_argument("--out", required=True)
    args = ap.parse_args(argv)

    src_dir = os.path.dirname(os.path.abspath(args.gltf))
    doc = json.load(open(args.gltf))
    blob = open(os.path.join(
        src_dir, doc["buffers"][0]["uri"]), "rb").read()
    os.makedirs(args.out, exist_ok=True)

    prims_out, mats_out = [], []
    tri_in = tri_out = 0
    all_top = 0.0
    parsed = []
    for pr in doc["meshes"][0]["primitives"]:
        pos = read_accessor(doc, blob, pr["attributes"]["POSITION"]).astype(
            np.float64)
        nrm = read_accessor(doc, blob, pr["attributes"]["NORMAL"]).astype(
            np.float64)
        uv = read_accessor(doc, blob, pr["attributes"]["TEXCOORD_0"]).astype(
            np.float64)
        idx = read_accessor(doc, blob, pr["indices"]).astype(np.uint32)
        parsed.append((pr, pos, nrm, uv, idx))
        all_top = max(all_top, float(pos[:, 1].max()))

    for pr, pos, nrm, uv, idx in parsed:
        tri_in += len(idx) // 3
        cpos, cnrm, cuv, tri = cluster(pos, nrm, uv, idx, args.grid)
        if len(tri) == 0:
            continue
        mat = doc["materials"][pr["material"]]
        mname = mat.get("name", "mat%d" % pr["material"])
        # texture: baseColor image resized to the budget
        img_idx = mat["pbrMetallicRoughness"]["baseColorTexture"]["index"]
        img_uri = doc["images"][doc["textures"][img_idx]["source"]]["uri"]
        tex = Image.open(os.path.join(src_dir, img_uri)).convert("RGBA")
        tex = tex.resize((args.tex_size, args.tex_size), Image.LANCZOS)
        tex_name = "%s_%s.png" % (args.name, mname)
        tex.save(os.path.join(args.out, tex_name))
        cutout = bool(args.cutout_material and
                      args.cutout_material in mname)
        mats_out.append({"name": mname, "tex_uri": tex_name,
                         "cutout": cutout, "double_sided": cutout})
        prims_out.append({"name": mname, "pos": cpos, "nrm": cnrm,
                          "uv": cuv,
                          "col": bake_colors(cpos, cnrm, all_top,
                                             args.ambient, args.snow),
                          "idx": tri.reshape(-1),
                          "material": len(mats_out) - 1})
        tri_out += len(tri)

    out_glb = os.path.join(args.out, args.name + ".glb")
    write_glb(out_glb, prims_out, mats_out)
    prov = {"model": args.name, "tool": "decimate_gltf.py",
            "source": os.path.basename(args.gltf),
            "license": args.license, "url": args.url,
            "tier": "A1" if args.license.strip().upper().startswith(
                ("CC0", "PD", "PUBLIC DOMAIN")) else "A2",
            "args": {"grid": args.grid, "tex_size": args.tex_size,
                     "ambient": args.ambient, "snow": args.snow,
                     "cutout_material": args.cutout_material},
            "triangles_in": tri_in, "triangles_out": tri_out}
    with open(os.path.join(args.out, args.name + ".provenance.json"),
              "w") as f:
        json.dump(prov, f, indent=2, sort_keys=True)
        f.write("\n")
    print("wrote %s: %d -> %d tris (grid %d)" %
          (out_glb, tri_in, tri_out, args.grid))


if __name__ == "__main__":
    main()
