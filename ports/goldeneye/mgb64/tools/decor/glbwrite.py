#!/usr/bin/env python3
"""glbwrite.py -- minimal deterministic glTF 2.0 binary writer (shared).

Used by gen_tree3d.py (generated art) and decimate_gltf.py (CC0 photoscan
reduction). One mesh, one node; per-primitive material with baseColorTexture,
optional MASK alpha + doubleSided; POSITION/NORMAL/TEXCOORD_0/COLOR_0(u8) +
u16 or u32 indices. Deterministic: sorted keys, no timestamps.
"""
import json
import struct

import numpy as np


def write_glb(path, prims, materials):
    """prims: list of dicts {name, pos(N,3)f32, nrm(N,3)f32, uv(N,2)f32,
    col(N,4)u8, idx(M,)u16|u32, material:int}. materials: list of dicts
    {name, tex_uri, cutout:bool, double_sided:bool}."""
    bin_parts, buffer_views, accessors = [], [], []
    offset = 0

    def add_view(data, target):
        nonlocal offset
        raw = np.ascontiguousarray(data).tobytes()
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
            acc["min"] = vmin
            acc["max"] = vmax
        accessors.append(acc)
        return len(accessors) - 1

    doc_materials, textures, images = [], [], []
    for m in materials:
        images.append({"uri": m["tex_uri"]})
        textures.append({"sampler": 0, "source": len(images) - 1})
        mat = {"name": m["name"], "pbrMetallicRoughness": {
                   "baseColorTexture": {"index": len(textures) - 1},
                   "metallicFactor": 0.0, "roughnessFactor": 1.0}}
        if m.get("cutout"):
            mat["alphaMode"] = "MASK"
            mat["alphaCutoff"] = 0.45
        if m.get("double_sided"):
            mat["doubleSided"] = True
        doc_materials.append(mat)

    primitives = []
    for pr in prims:
        pos = np.asarray(pr["pos"], np.float32)
        idx = np.asarray(pr["idx"])
        idx_comp = 5125 if idx.dtype == np.uint32 else 5123
        pv = add_view(pos, 34962)
        nv = add_view(np.asarray(pr["nrm"], np.float32), 34962)
        uvv = add_view(np.asarray(pr["uv"], np.float32), 34962)
        cv = add_view(np.asarray(pr["col"], np.uint8), 34962)
        iv = add_view(idx, 34963)
        pa = add_acc(pv, 5126, len(pos), "VEC3",
                     vmin=[float(v) for v in pos.min(axis=0)],
                     vmax=[float(v) for v in pos.max(axis=0)])
        na = add_acc(nv, 5126, len(pos), "VEC3")
        ua = add_acc(uvv, 5126, len(pos), "VEC2")
        ca = add_acc(cv, 5121, len(pos), "VEC4", normalized=True)
        ia = add_acc(iv, idx_comp, len(idx), "SCALAR")
        primitives.append({"attributes": {"POSITION": pa, "NORMAL": na,
                                          "TEXCOORD_0": ua, "COLOR_0": ca},
                           "indices": ia, "material": pr["material"]})

    blob = b"".join(bin_parts)
    doc = {"asset": {"version": "2.0", "generator": "mgb64 decor tools"},
           "scene": 0, "scenes": [{"nodes": [0]}],
           "nodes": [{"mesh": 0, "name": "decor"}],
           "meshes": [{"primitives": primitives}],
           "materials": doc_materials, "textures": textures,
           "images": images,
           "samplers": [{"magFilter": 9729, "minFilter": 9987,
                         "wrapS": 10497, "wrapT": 10497}],
           "accessors": accessors, "bufferViews": buffer_views,
           "buffers": [{"byteLength": len(blob)}]}
    js = json.dumps(doc, separators=(",", ":"), sort_keys=True).encode()
    js += b" " * ((4 - len(js) % 4) % 4)
    total = 12 + 8 + len(js) + 8 + len(blob)
    with open(path, "wb") as f:
        f.write(struct.pack("<III", 0x46546C67, 2, total))
        f.write(struct.pack("<II", len(js), 0x4E4F534A))
        f.write(js)
        f.write(struct.pack("<II", len(blob), 0x004E4942))
        f.write(blob)
