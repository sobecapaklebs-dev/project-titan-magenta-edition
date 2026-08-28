#!/usr/bin/env python3
"""Encode and validate Project Titan theme RGB565 assets.

Host-side authoring helper only. The Project Titan runtime does not depend on
Pillow or this tool.
"""
from __future__ import annotations

import argparse
import struct
import json
import hashlib
from pathlib import Path

SIZES = {
    "scene": (512, 256),
    "preview": (256, 128),
}


def expected_bytes(kind: str) -> int:
    w, h = SIZES[kind]
    return w * h * 2


def load_pillow():
    try:
        from PIL import Image
    except ImportError as exc:
        raise SystemExit(
            "Pillow is required for image conversion. Install python-pillow "
            "on Arch/Garuda or 'Pillow' in your Python environment."
        ) from exc
    return Image


def encode(input_path: Path, output_path: Path, kind: str, resize: bool) -> None:
    Image = load_pillow()
    width, height = SIZES[kind]
    with Image.open(input_path) as source:
        image = source.convert("RGB")
        if image.size != (width, height):
            if not resize:
                raise SystemExit(
                    f"{input_path}: expected {width}x{height}, got "
                    f"{image.size[0]}x{image.size[1]}; use --resize explicitly"
                )
            image = image.resize((width, height), Image.Resampling.LANCZOS)
        pixels = image.load()
        data = bytearray(expected_bytes(kind))
        offset = 0
        for y in range(height):
            for x in range(width):
                r, g, b = pixels[x, y]
                value = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
                struct.pack_into("<H", data, offset, value)
                offset += 2
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_bytes(data)
    print(f"created {output_path} ({width}x{height} RGB565 LE, {len(data)} bytes)")


def verify(path: Path, kind: str) -> None:
    size = path.stat().st_size
    expected = expected_bytes(kind)
    if size != expected:
        width, height = SIZES[kind]
        raise SystemExit(
            f"{path}: invalid {kind} asset size {size}; expected {expected} "
            f"bytes for {width}x{height} RGB565 LE"
        )
    print(f"valid {kind}: {path} ({size} bytes)")



def compile_manifest(input_path: Path, output_path: Path) -> None:
    """Validate a declarative BuildFix23 scene and emit a compact runtime manifest."""
    spec=json.loads(input_path.read_text(encoding="utf-8"))
    layers=spec.get("layers",[])
    if not isinstance(layers,list) or len(layers)>32:
        raise SystemExit("layers must be a list with at most 32 entries")
    allowed={"BACKGROUND","MIDGROUND","FOREGROUND","SPRITE","LIGHT","PARTICLE","RIBBON","DECORATION","OVERLAY"}
    seen={}; compiled=[]; peak_resident=0
    for order,layer in enumerate(layers):
        typ=layer.get("type")
        if typ not in allowed: raise SystemExit(f"layer {order}: invalid type {typ!r}")
        frames=layer.get("frames",[])
        if len(frames)>128: raise SystemExit(f"layer {order}: >128 declared frames")
        resident=min(3,len(frames)); frame_bytes=0
        out_frames=[]
        for fi,frame in enumerate(frames):
            dur=int(frame.get("duration_ms",100))
            if not 40<=dur<=2000: raise SystemExit(f"layer {order} frame {fi}: duration outside 40..2000ms")
            asset=Path(frame["asset"])
            resolved=(input_path.parent/asset).resolve()
            if not resolved.is_file(): raise SystemExit(f"missing asset: {asset}")
            data=resolved.read_bytes(); digest=hashlib.sha256(data).hexdigest()
            seen[digest]=seen.get(digest,0)+1
            frame_bytes=max(frame_bytes,len(data))
            out_frames.append({"asset":str(asset),"duration_ms":dur,"sha256":digest,"bytes":len(data)})
        peak_resident += resident*frame_bytes
        compiled.append({"type":typ,"z":int(layer.get("z",0)),"order":order,"mode":layer.get("mode","LOOP"),"frames":out_frames,"resident_cap":resident})
    result={"format":"titan-theme-compiled-v1","layers":compiled,"layer_count":len(compiled),"peak_residency_estimate":peak_resident,"duplicate_instances":sum(v-1 for v in seen.values()),"unique_assets":len(seen)}
    output_path.parent.mkdir(parents=True,exist_ok=True)
    output_path.write_text(json.dumps(result,separators=(",",":"),sort_keys=True),encoding="utf-8")
    print(f"compiled {output_path}: layers={len(compiled)} peak_residency={peak_resident} duplicates={result['duplicate_instances']}")


def compile_v4(input_path: Path, output_path: Path) -> None:
    spec = json.loads(input_path.read_text(encoding="utf-8"))
    layers = spec.get("layers", [])
    if not isinstance(layers, list) or not 1 <= len(layers) <= 32:
        raise SystemExit("scene-v4 layers must contain 1..32 entries")
    vram_budget = int(spec.get("vram_budget", 512 * 1024))
    ee_ram_budget = int(spec.get("ee_ram_budget", 2 * 1024 * 1024))
    headroom = int(spec.get("reserved_headroom", 256 * 1024))
    if not 64*1024 <= vram_budget <= 1024*1024:
        raise SystemExit("scene-v4 vram_budget outside 64KiB..1MiB")
    if not 128*1024 <= ee_ram_budget <= 8*1024*1024:
        raise SystemExit("scene-v4 ee_ram_budget outside 128KiB..8MiB")
    if vram_budget + headroom > 2*1024*1024:
        raise SystemExit("scene-v4 vram budget + headroom exceeds 2MiB shell allocation envelope")
    forbidden={".elf",".irx",".tmod",".so",".vsm"}
    priorities={"ESSENTIAL":0,"ENHANCED":1,"DECORATIVE":2}
    contexts={"HOME":1,"TOP":2,"BOTH":3}
    modes={"STATIC":0,"LOOP":1,"PINGPONG":2,"ONCE":3}
    events=spec.get("rare_events", []) or []
    if not isinstance(events,list) or len(events)>8: raise SystemExit("scene-v4 rare_events must contain 0..8 entries")
    out=["format=titan-scene-v4","version=1",f"layer_count={len(layers)}",f"vram_budget={vram_budget}",f"ee_ram_budget={ee_ram_budget}",f"reserved_headroom={headroom}",f"event_count={len(events)}"]
    for ei,event in enumerate(events):
        layer_index=int(event.get("layer",-1)); interval=int(event.get("interval_ms",0)); duration=int(event.get("duration_ms",0)); phase=int(event.get("phase_ms",0))
        if not 0<=layer_index<len(layers): raise SystemExit(f"rare event {ei}: invalid layer")
        if not 1000<=interval<=600000 or not 40<=duration<interval: raise SystemExit(f"rare event {ei}: invalid interval/duration")
        out.extend([f"event{ei}_layer={layer_index}",f"event{ei}_interval_ms={interval}",f"event{ei}_duration_ms={duration}",f"event{ei}_phase_ms={phase}"])
    total_bytes=0
    for i,layer in enumerate(layers):
        asset=Path(layer["asset"])
        if asset.is_absolute() or ".." in asset.parts or asset.suffix.lower() in forbidden:
            raise SystemExit(f"layer {i}: unsafe asset path")
        resolved=(input_path.parent/asset).resolve()
        if not resolved.is_file(): raise SystemExit(f"layer {i}: missing asset {asset}")
        w=int(layer.get("width",0)); h=int(layer.get("height",0))
        if not 1<=w<=256 or not 1<=h<=128: raise SystemExit(f"layer {i}: dimensions must be 1..256 x 1..128")
        data=resolved.read_bytes()
        if len(data)!=w*h*2: raise SystemExit(f"layer {i}: RGB565 byte size mismatch")
        total_bytes += len(data)
        pr=str(layer.get("priority","DECORATIVE")).upper(); ctx=str(layer.get("context","BOTH")).upper(); mode=str(layer.get("mode","STATIC")).upper()
        if pr not in priorities or ctx not in contexts or mode not in modes: raise SystemExit(f"layer {i}: invalid priority/context/mode")
        anim=layer.get("animation", {}) or {}
        duration_ms=int(anim.get("duration_ms", layer.get("duration_ms", 1000 if mode != "STATIC" else 0)))
        if mode != "STATIC" and not 40 <= duration_ms <= 60000:
            raise SystemExit(f"layer {i}: animation duration outside 40..60000ms")
        top=layer.get("top", {}) or {}
        vals={
          "asset":asset.as_posix(),"width":w,"height":h,"z":int(layer.get("z",0)),
          "x_milli":int(float(layer.get("x",0))*1000),"y_milli":int(float(layer.get("y",0))*1000),
          "scale_x_milli":int(float(layer.get("scale_x",1))*1000),"scale_y_milli":int(float(layer.get("scale_y",1))*1000),
          "rotation_mdeg":int(float(layer.get("rotation",0))*1000),"alpha":int(layer.get("alpha",255)),
          "parallax_x_milli":int(float(layer.get("parallax_x",0))*1000),"parallax_y_milli":int(float(layer.get("parallax_y",0))*1000),
          "priority":priorities[pr],"context":contexts[ctx],"mode":modes[mode],"phase_ms":int(layer.get("phase_ms",0)),
          "duration_ms":duration_ms,
          "anim_x_milli":int(float(anim.get("translate_x",0))*1000),
          "anim_y_milli":int(float(anim.get("translate_y",0))*1000),
          "anim_scale_x_milli":int(float(anim.get("scale_x",0))*1000),
          "anim_scale_y_milli":int(float(anim.get("scale_y",0))*1000),
          "anim_rotation_mdeg":int(float(anim.get("rotation",0))*1000),
          "anim_alpha":int(anim.get("alpha",0)),
          "top_x_milli":int(float(top.get("translate_x",0))*1000),
          "top_y_milli":int(float(top.get("translate_y",0))*1000),
          "top_scale_x_milli":int(float(top.get("scale_x",0))*1000),
          "top_scale_y_milli":int(float(top.get("scale_y",0))*1000),
          "top_rotation_mdeg":int(float(top.get("rotation",0))*1000),
          "top_alpha_delta":int(top.get("alpha_delta",0)),
          "sha256":hashlib.sha256(data).hexdigest(),"bytes":len(data)}
        if not 0<=vals["alpha"]<=255: raise SystemExit(f"layer {i}: alpha outside 0..255")
        for k,v in vals.items(): out.append(f"layer{i}_{k}={v}")
    if total_bytes > ee_ram_budget: raise SystemExit("scene-v4 declared assets exceed EE RAM budget")
    output_path.parent.mkdir(parents=True,exist_ok=True)
    output_path.write_text("\n".join(out)+"\n",encoding="utf-8")
    print(f"compiled scene-v4 {output_path}: layers={len(layers)} declared_bytes={total_bytes}")

def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)

    enc = sub.add_parser("encode", help="convert a normal image into a Titan RGB565 asset")
    enc.add_argument("--kind", choices=sorted(SIZES), required=True)
    enc.add_argument("--input", type=Path, required=True)
    enc.add_argument("--output", type=Path, required=True)
    enc.add_argument(
        "--resize",
        action="store_true",
        help="explicitly resize input to the exact target dimensions",
    )

    chk = sub.add_parser("verify", help="validate raw RGB565 dimensions by file size")
    chk.add_argument("--kind", choices=sorted(SIZES), required=True)
    chk.add_argument("path", type=Path)

    comp = sub.add_parser("compile-manifest", help="validate layered timelines/assets and emit compiled runtime manifest")
    comp.add_argument("--input", type=Path, required=True)
    comp.add_argument("--output", type=Path, required=True)
    v4 = sub.add_parser("compile-v4", help="compile a declarative scene-v4 JSON spec into the bounded runtime manifest")
    v4.add_argument("--input", type=Path, required=True)
    v4.add_argument("--output", type=Path, required=True)

    args = parser.parse_args()
    if args.command == "encode":
        encode(args.input, args.output, args.kind, args.resize)
        verify(args.output, args.kind)
    elif args.command == "verify":
        verify(args.path, args.kind)
    elif args.command == "compile-manifest":
        compile_manifest(args.input, args.output)
    else:
        compile_v4(args.input, args.output)


if __name__ == "__main__":
    main()
