#!/usr/bin/env python3
"""Reconstruct texnum-654 room-glass source pixels from native SETTEX traces.

This read-only diagnostic connects the already-focused pad10092 evidence:

  route ROI -> SETTEX-MATERIAL-CC triangles -> dumped texnum 654 payload
  -> reconstructed shader source -> fixed-alpha composition against underlay.

It deliberately self-checks the reconstructed UV/sampler/shader math against
the trace's center samples before reporting per-pixel conclusions.
"""

from __future__ import annotations

import argparse
from collections import Counter
import json
import math
import re
import sys
from pathlib import Path
from typing import Any

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
import analyze_room_glass_required_source as required_source  # noqa: E402
import compare_roi_pixel_semantics as roi_semantics  # noqa: E402
import compare_screenshots as screenshots  # noqa: E402
import summarize_texgen_roi_materials as texgen  # noqa: E402


SETTEX_RE = re.compile(r"\[SETTEX-MATERIAL-CC\]\s+(?P<body>.*)")
SETTEX_FB_RE = re.compile(r"\[SETTEX-FB-CAPTURE\]\s+(?P<body>.*)")
G_TX_MIRROR = 0x1
G_TX_CLAMP = 0x2

G_CCMUX_COMBINED = 0
G_CCMUX_TEXEL0 = 1
G_CCMUX_TEXEL1 = 2
G_CCMUX_PRIMITIVE = 3
G_CCMUX_SHADE = 4
G_CCMUX_ENVIRONMENT = 5
G_CCMUX_1 = 6
G_CCMUX_NOISE = 7
G_CCMUX_TEXEL0_ALPHA = 8
G_CCMUX_TEXEL1_ALPHA = 9
G_CCMUX_PRIMITIVE_ALPHA = 10
G_CCMUX_SHADE_ALPHA = 11
G_CCMUX_ENV_ALPHA = 12
G_CCMUX_LOD_FRACTION = 13
G_CCMUX_PRIM_LOD_FRAC = 14
G_CCMUX_0 = 31

G_ACMUX_COMBINED = 0
G_ACMUX_TEXEL0 = 1
G_ACMUX_TEXEL1 = 2
G_ACMUX_PRIMITIVE = 3
G_ACMUX_SHADE = 4
G_ACMUX_ENVIRONMENT = 5
G_ACMUX_1 = 6
G_ACMUX_0 = 7

SHADER_OPT_ALPHA = 1 << 0
SHADER_OPT_FOG = 1 << 1
SHADER_OPT_TEXTURE_EDGE = 1 << 2
SHADER_OPT_2CYC = 1 << 4
SHADER_OPT_NOPERSPECTIVE_TEXCOORDS = 1 << 14
SHADER_OPT_NOPERSPECTIVE_INPUTS = 1 << 15

Pixel = tuple[int, int, int]
Rgba = tuple[int, int, int, int]
Float3 = tuple[float, float, float]
Float4 = tuple[float, float, float, float]


def load_json(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def normalize_hex(value: str | None, width: int) -> str | None:
    if value is None:
        return None
    try:
        return f"0x{int(value, 0):0{width}x}"
    except ValueError:
        return value.lower()


def parse_tuple(value: str | None) -> list[float]:
    return texgen.parse_tuple_numbers(value)


def parse_int(body: str, key: str) -> int | None:
    value = texgen.value_for(body, key)
    if value is None:
        return None
    try:
        return int(value, 0)
    except ValueError:
        return None


def parse_size(value: str | None) -> tuple[int, int] | None:
    if value is None:
        return None
    parts = value.lower().split("x")
    if len(parts) != 2:
        return None
    try:
        width = int(parts[0])
        height = int(parts[1])
    except ValueError:
        return None
    if width <= 0 or height <= 0:
        return None
    return width, height


def parse_tile(value: str | None) -> dict[str, int] | None:
    values = [int(item) for item in parse_tuple(value)]
    if len(values) < 9 or values[0] == 0:
        return None
    return {
        "valid": values[0],
        "cms": values[1],
        "cmt": values[2],
        "shifts": values[3],
        "shiftt": values[4],
        "uls": values[5],
        "ult": values[6],
        "width": values[7],
        "height": values[8],
    }


def clamp_u8(value: float) -> int:
    if not math.isfinite(value):
        value = 0.0
    if value < 0.0:
        return 0
    if value > 255.0:
        return 255
    return int(value + 0.5)


def mean(values: list[float]) -> float:
    return sum(values) / len(values) if values else 0.0


def percentile(values: list[float], pct: float) -> float:
    if not values:
        return 0.0
    ordered = sorted(values)
    index = (len(ordered) - 1) * pct
    lo = int(index)
    hi = min(lo + 1, len(ordered) - 1)
    frac = index - lo
    return ordered[lo] * (1.0 - frac) + ordered[hi] * frac


def stats(values: list[float]) -> dict[str, float]:
    return {
        "min": min(values) if values else 0.0,
        "p05": percentile(values, 0.05),
        "mean": mean(values),
        "median": percentile(values, 0.50),
        "p95": percentile(values, 0.95),
        "max": max(values) if values else 0.0,
    }


def luma(pixel: Pixel | Rgba | Float3) -> float:
    return 0.2126 * pixel[0] + 0.7152 * pixel[1] + 0.0722 * pixel[2]


def read_ppm(path: Path) -> tuple[int, int, list[Pixel]]:
    data = path.read_bytes()
    offset = 0

    def token() -> bytes:
        nonlocal offset
        while offset < len(data) and data[offset] in b" \t\r\n":
            offset += 1
        if offset < len(data) and data[offset] == ord("#"):
            while offset < len(data) and data[offset] not in b"\r\n":
                offset += 1
            return token()
        start = offset
        while offset < len(data) and data[offset] not in b" \t\r\n":
            offset += 1
        return data[start:offset]

    magic = token()
    if magic != b"P6":
        raise ValueError(f"{path} is not a binary PPM")
    width = int(token())
    height = int(token())
    max_value = int(token())
    if max_value != 255:
        raise ValueError(f"{path} has unsupported max value {max_value}")
    if offset < len(data) and data[offset] in b" \t\r\n":
        offset += 1
    body = data[offset:]
    expected = width * height * 3
    if len(body) < expected:
        raise ValueError(f"{path} is truncated: expected {expected} bytes, got {len(body)}")
    pixels: list[Pixel] = []
    for index in range(0, expected, 3):
        pixels.append((body[index], body[index + 1], body[index + 2]))
    return width, height, pixels


def read_pgm(path: Path) -> tuple[int, int, list[int]]:
    data = path.read_bytes()
    offset = 0

    def token() -> bytes:
        nonlocal offset
        while offset < len(data) and data[offset] in b" \t\r\n":
            offset += 1
        if offset < len(data) and data[offset] == ord("#"):
            while offset < len(data) and data[offset] not in b"\r\n":
                offset += 1
            return token()
        start = offset
        while offset < len(data) and data[offset] not in b" \t\r\n":
            offset += 1
        return data[start:offset]

    magic = token()
    if magic != b"P5":
        raise ValueError(f"{path} is not a binary PGM")
    width = int(token())
    height = int(token())
    max_value = int(token())
    if max_value != 255:
        raise ValueError(f"{path} has unsupported max value {max_value}")
    if offset < len(data) and data[offset] in b" \t\r\n":
        offset += 1
    body = data[offset:]
    expected = width * height
    if len(body) < expected:
        raise ValueError(f"{path} is truncated: expected {expected} bytes, got {len(body)}")
    return width, height, list(body[:expected])


class Texture:
    def __init__(self, rgba_path: Path, alpha_path: Path | None) -> None:
        width, height, rgb = read_ppm(rgba_path)
        alpha: list[int]
        if alpha_path is not None:
            alpha_width, alpha_height, alpha = read_pgm(alpha_path)
            if (alpha_width, alpha_height) != (width, height):
                raise ValueError("texture alpha dimensions do not match RGB dimensions")
        else:
            alpha = [255] * (width * height)
        self.width = width
        self.height = height
        self.pixels: list[Rgba] = [
            (rgb[index][0], rgb[index][1], rgb[index][2], alpha[index])
            for index in range(width * height)
        ]

    @staticmethod
    def wrap_index(value: int, size: int, mode: int) -> int:
        if size <= 0:
            return 0
        if mode == G_TX_CLAMP:
            return min(size - 1, max(0, value))
        if mode == G_TX_MIRROR:
            period = size * 2
            wrapped = value % period
            return period - 1 - wrapped if wrapped >= size else wrapped
        return value % size

    def fetch(self, x: int, y: int, cms: int, cmt: int) -> Rgba:
        ix = self.wrap_index(x, self.width, cms)
        iy = self.wrap_index(y, self.height, cmt)
        return self.pixels[iy * self.width + ix]

    def nearest(self, u: float, v: float, cms: int, cmt: int) -> Rgba:
        return self.fetch(math.floor(u * self.width), math.floor(v * self.height), cms, cmt)

    def linear(self, u: float, v: float, cms: int, cmt: int) -> Rgba:
        sx = u * self.width - 0.5
        sy = v * self.height - 0.5
        x0 = math.floor(sx)
        y0 = math.floor(sy)
        tx = min(1.0, max(0.0, sx - x0))
        ty = min(1.0, max(0.0, sy - y0))
        c00 = self.fetch(x0, y0, cms, cmt)
        c10 = self.fetch(x0 + 1, y0, cms, cmt)
        c01 = self.fetch(x0, y0 + 1, cms, cmt)
        c11 = self.fetch(x0 + 1, y0 + 1, cms, cmt)
        out = []
        for channel in range(4):
            a = c00[channel] + (c10[channel] - c00[channel]) * tx
            b = c01[channel] + (c11[channel] - c01[channel]) * tx
            out.append(clamp_u8(a + (b - a) * ty))
        return (out[0], out[1], out[2], out[3])

    @staticmethod
    def fract(value: float) -> float:
        return value - math.floor(value)

    @staticmethod
    def sign_int(value: float) -> int:
        if value > 0.0:
            return 1
        if value < 0.0:
            return -1
        return 0

    def threepoint(self, u: float, v: float, cms: int, cmt: int) -> Rgba:
        texel_x = u * self.width
        texel_y = v * self.height
        offset_x = self.fract(texel_x - 0.5)
        offset_y = self.fract(texel_y - 0.5)
        step = 1.0 if offset_x + offset_y >= 1.0 else 0.0
        offset_x -= step
        offset_y -= step
        base_u = u - offset_x / self.width
        base_v = v - offset_y / self.height

        c0 = self.nearest(base_u, base_v, cms, cmt)
        c1 = self.nearest(
            base_u + self.sign_int(offset_x) / self.width,
            base_v,
            cms,
            cmt,
        )
        c2 = self.nearest(
            base_u,
            base_v + self.sign_int(offset_y) / self.height,
            cms,
            cmt,
        )
        out = []
        for channel in range(4):
            value = (
                c0[channel]
                + abs(offset_x) * (c1[channel] - c0[channel])
                + abs(offset_y) * (c2[channel] - c0[channel])
            )
            out.append(clamp_u8(value))
        return (out[0], out[1], out[2], out[3])


def apply_tile_uv(value: float, shift: int, lo: int, linear_filter: bool) -> float:
    if shift != 0:
        if shift <= 10:
            value /= float(1 << shift)
        else:
            value *= float(1 << (16 - shift))
    value -= lo / 4.0
    if linear_filter:
        value += 0.5
    return value


def vertex_uv(raw_uv: list[float], tile: dict[str, int], width: int, height: int) -> tuple[float, float]:
    u = raw_uv[0] / 32.0
    v = raw_uv[1] / 32.0
    u = apply_tile_uv(u, tile["shifts"], tile["uls"], linear_filter=True)
    v = apply_tile_uv(v, tile["shiftt"], tile["ult"], linear_filter=True)
    return (u / max(1, width), v / max(1, height))


def perspective_interp(values: list[float], bary: tuple[float, float, float], ws: list[float]) -> float:
    denom = 0.0
    numer = 0.0
    for index, weight in enumerate(bary):
        w = ws[index]
        if abs(w) < 0.0001:
            return sum(values) / len(values)
        iw = 1.0 / w
        numer += weight * values[index] * iw
        denom += weight * iw
    if not math.isfinite(denom) or abs(denom) < 0.0001:
        return sum(values) / len(values)
    return numer / denom


def screen_from_clip(clip: list[float], logical_size: tuple[float, float]) -> tuple[float, float]:
    x, y, _z, w = clip
    ndc_x = x / w
    ndc_y = y / w
    return (
        (ndc_x * 0.5 + 0.5) * logical_size[0],
        (0.5 - ndc_y * 0.5) * logical_size[1],
    )


def barycentric(
    point: tuple[float, float],
    tri: list[tuple[float, float]],
) -> tuple[float, float, float] | None:
    px, py = point
    ax, ay = tri[0]
    bx, by = tri[1]
    cx, cy = tri[2]
    denom = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy)
    if abs(denom) < 1e-8:
        return None
    w0 = ((by - cy) * (px - cx) + (cx - bx) * (py - cy)) / denom
    w1 = ((cy - ay) * (px - cx) + (ax - cx) * (py - cy)) / denom
    w2 = 1.0 - w0 - w1
    return (w0, w1, w2)


def bary_inside(bary: tuple[float, float, float], epsilon: float = 1e-5) -> bool:
    return all(value >= -epsilon for value in bary) and all(value <= 1.0 + epsilon for value in bary)


def route_regions(route: dict[str, Any]) -> dict[str, tuple[int, int, int, int]]:
    return roi_semantics.route_regions(route)


def route_mapping(route: dict[str, Any], aligned_size: tuple[int, int]) -> dict[str, Any]:
    logical_size = route.get("visual_logical_size", [320, 240])
    viewport = route.get("visual_logical_viewport", [0, 10, 320, 220])
    viewport_f = [float(item) for item in viewport]
    return {
        "logical_size": (float(logical_size[0]), float(logical_size[1])),
        "viewport": viewport_f,
        "scale": (
            float(aligned_size[0]) / viewport_f[2],
            float(aligned_size[1]) / viewport_f[3],
        ),
        "aligned_size": list(aligned_size),
    }


def aligned_images_for_trace(
    route: dict[str, Any],
    stock_image: Path,
    default_image: Path,
    underlay_image: Path,
    active_threshold: int,
    align_to: str,
) -> tuple[Image.Image, Image.Image, Image.Image, dict[str, Any]]:
    source_stock = Image.open(stock_image).convert("RGB")
    source_default = Image.open(default_image).convert("RGB")
    source_underlay = Image.open(underlay_image).convert("RGB")

    logical_size = tuple(route.get("visual_logical_size", [320, 240]))
    logical_viewport = tuple(route.get("visual_logical_viewport", [0, 10, 320, 220]))
    stock_frame = route.get("visual_baseline_logical_frame", "active")
    native_frame = route.get("visual_test_logical_frame", "full")

    stock_crop = screenshots.logical_crop_bbox(
        source_stock, active_threshold, logical_size, logical_viewport, stock_frame
    )
    default_crop = screenshots.logical_crop_bbox(
        source_default, active_threshold, logical_size, logical_viewport, native_frame
    )
    underlay_crop = screenshots.logical_crop_bbox(
        source_underlay, active_threshold, logical_size, logical_viewport, native_frame
    )

    stock = screenshots.crop_bbox(source_stock, stock_crop)
    default = screenshots.crop_bbox(source_default, default_crop)
    underlay = screenshots.crop_bbox(source_underlay, underlay_crop)

    target_size = default.size if align_to == "native" else stock.size
    resized = {"stock": False, "default": False, "underlay": False}
    resampling = getattr(Image, "Resampling", Image).BILINEAR
    if stock.size != target_size:
        stock = stock.resize(target_size, resampling)
        resized["stock"] = True
    if default.size != target_size:
        default = default.resize(target_size, resampling)
        resized["default"] = True
    if underlay.size != target_size:
        underlay = underlay.resize(target_size, resampling)
        resized["underlay"] = True

    return stock, default, underlay, {
        "source_size": {
            "stock": list(source_stock.size),
            "default": list(source_default.size),
            "underlay": list(source_underlay.size),
        },
        "aligned_size": list(target_size),
        "align_to": align_to,
        "logical_viewport": {
            "logical_size": list(logical_size),
            "viewport": list(logical_viewport),
            "stock_frame": stock_frame,
            "native_frame": native_frame,
            "crop": {
                "stock": list(stock_crop),
                "default": list(default_crop),
                "underlay": list(underlay_crop),
            },
        },
        "resized_to_target": resized,
    }


def logical_to_aligned(
    point: tuple[float, float],
    mapping: dict[str, Any],
) -> tuple[float, float]:
    viewport = mapping["viewport"]
    scale_x, scale_y = mapping["scale"]
    return ((point[0] - viewport[0]) * scale_x, (point[1] - viewport[1]) * scale_y)


def aligned_to_logical(
    point: tuple[float, float],
    mapping: dict[str, Any],
) -> tuple[float, float]:
    viewport = mapping["viewport"]
    scale_x, scale_y = mapping["scale"]
    return (viewport[0] + point[0] / scale_x, viewport[1] + point[1] / scale_y)


def parse_log_rows(path: Path, args: argparse.Namespace) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    with path.open("r", encoding="utf-8", errors="replace") as handle:
        for line_no, line in enumerate(handle, start=1):
            match = SETTEX_RE.search(line)
            if not match:
                continue
            body = match.group("body")
            texnum = parse_int(body, "texnum")
            if texnum != args.texnum:
                continue
            effcc = normalize_hex(texgen.value_for(body, "effcc"), 16)
            opts = normalize_hex(texgen.value_for(body, "opts"), 8)
            oml_raw = normalize_hex(texgen.value_for(body, "oml_raw"), 8)
            if args.effcc and effcc != normalize_hex(args.effcc, 16):
                continue
            if args.opts and opts != normalize_hex(args.opts, 8):
                continue
            if args.oml_raw and oml_raw != normalize_hex(args.oml_raw, 8):
                continue
            wh = texgen.value_for(body, "wh")
            rgba_wh = texgen.value_for(body, "rgba_wh")
            if args.wh and wh != args.wh:
                continue
            if args.rgba_wh and rgba_wh != args.rgba_wh:
                continue
            tile0 = parse_tile(texgen.value_for(body, "tile0"))
            if tile0 is None:
                continue
            tile1 = parse_tile(texgen.value_for(body, "tile1"))
            clips = [parse_tuple(texgen.value_for(body, f"vclip{index}"))[:4] for index in range(3)]
            raw_uvs = [parse_tuple(texgen.value_for(body, f"vuv{index}"))[:2] for index in range(3)]
            shades = [parse_tuple(texgen.value_for(body, f"shade{index}"))[:4] for index in range(3)]
            if any(len(item) != 4 for item in clips) or any(len(item) != 2 for item in raw_uvs):
                continue
            if any(len(item) != 4 for item in shades):
                continue
            width_height = parse_size(wh)
            if width_height is None:
                width_height = (tile0["width"], tile0["height"])
            row = {
                "source": str(path),
                "line": line_no,
                "body": body,
                "frame": parse_int(body, "frame"),
                "tri": parse_int(body, "tri"),
                "cc_options": parse_int(body, "opts") or 0,
                "effective_cc_id_int": int(effcc, 16) if effcc is not None and effcc.startswith("0x") else 0,
                "effcc": effcc,
                "opts": opts,
                "oml_raw": oml_raw,
                "texnum": texnum,
                "wh": wh,
                "rgba_wh": rgba_wh,
                "tile0": tile0,
                "tile1": tile1,
                "tex_size": width_height,
                "prim": parse_tuple(texgen.value_for(body, "prim"))[:4],
                "env": parse_tuple(texgen.value_for(body, "env"))[:4],
                "fogrgba": parse_tuple(texgen.value_for(body, "fogrgba"))[:4],
                "fog_enabled": parse_int(body, "fog") == 1,
                "blend": texgen.value_for(body, "blend"),
                "lod_fraction": parse_int(body, "lod") or 0,
                "vclip": clips,
                "vuv": raw_uvs,
                "shade": shades,
                "uv0_logged": parse_tuple(texgen.value_for(body, "uv0"))[:2],
                "uv1_logged": parse_tuple(texgen.value_for(body, "uv1"))[:2],
                "t0n_logged": parse_tuple(texgen.value_for(body, "t0n"))[:4],
                "t0l_logged": parse_tuple(texgen.value_for(body, "t0l"))[:4],
                "t0p_logged": parse_tuple(texgen.value_for(body, "t0p"))[:4],
                "t1n_logged": parse_tuple(texgen.value_for(body, "t1n"))[:4],
                "t1l_logged": parse_tuple(texgen.value_for(body, "t1l"))[:4],
                "t1p_logged": parse_tuple(texgen.value_for(body, "t1p"))[:4],
                "shadec_logged": parse_tuple(texgen.value_for(body, "shadec"))[:4],
                "lodc_logged": parse_int(body, "lodc"),
                "shaderL_logged": parse_tuple(texgen.value_for(body, "shaderL_frag"))[:4],
                "shaderP_logged": parse_tuple(texgen.value_for(body, "shaderP_frag"))[:4],
                "fogc_logged": parse_tuple(texgen.value_for(body, "fogc"))[:4],
            }
            rows.append(row)
    return rows


def parse_changed_fraction(value: str | None) -> tuple[int | None, int | None]:
    if value is None or "/" not in value:
        return None, None
    left, right = value.split("/", 1)
    try:
        return int(left), int(right)
    except ValueError:
        return None, None


def parse_fb_capture_rows(path: Path, args: argparse.Namespace) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    expected_cc = normalize_hex(args.effcc, 16) if args.effcc else None
    with path.open("r", encoding="utf-8", errors="replace") as handle:
        for line_no, line in enumerate(handle, start=1):
            match = SETTEX_FB_RE.search(line)
            if not match:
                continue
            body = match.group("body")
            status = texgen.value_for(body, "status")
            texnum = parse_int(body, "texnum")
            if texnum != args.texnum:
                continue
            cc = normalize_hex(texgen.value_for(body, "cc"), 16)
            if expected_cc is not None and cc != expected_cc:
                continue
            wh = texgen.value_for(body, "wh")
            if args.wh and wh != args.wh:
                continue
            changed, total = parse_changed_fraction(texgen.value_for(body, "changed"))
            row = {
                "source": str(path),
                "line": line_no,
                "body": body,
                "status": status,
                "frame": parse_int(body, "frame"),
                "tri": parse_int(body, "tri"),
                "serial": parse_int(body, "serial"),
                "cc": cc,
                "texnum": texnum,
                "wh": wh,
                "rect": parse_tuple(texgen.value_for(body, "rect"))[:4],
                "gl_rect": parse_tuple(texgen.value_for(body, "gl_rect"))[:4],
                "screen_bbox": parse_tuple(texgen.value_for(body, "screen_bbox"))[:4],
                "pre_mean": parse_tuple(texgen.value_for(body, "pre_mean"))[:3],
                "post_mean": parse_tuple(texgen.value_for(body, "post_mean"))[:3],
                "mean_abs_rgb": parse_int(body, "mean_abs_rgb"),
                "changed": changed,
                "changed_total": total,
                "changed_pct": texgen.value_for(body, "changed_pct"),
                "pre_ppm": texgen.value_for(body, "pre_ppm"),
                "post_ppm": texgen.value_for(body, "post_ppm"),
            }
            try:
                row["mean_abs_rgb"] = float(texgen.value_for(body, "mean_abs_rgb") or "0")
                row["changed_pct"] = float(texgen.value_for(body, "changed_pct") or "0")
            except ValueError:
                row["mean_abs_rgb"] = 0.0
                row["changed_pct"] = 0.0
            rows.append(row)
    return rows


def select_frame(rows: list[dict[str, Any]], frame: str) -> tuple[list[dict[str, Any]], dict[str, Any]]:
    frames = sorted({row["frame"] for row in rows if row.get("frame") is not None})
    if frame == "all" or not frames:
        return rows, {"mode": frame, "available_frames": frames, "selected": None}
    selected = frames[-1] if frame == "latest" else int(frame)
    return [row for row in rows if row.get("frame") == selected], {
        "mode": frame,
        "available_frames": frames,
        "selected": selected,
    }


def fb_capture_overlaps_roi(row: dict[str, Any], mapping: dict[str, Any], roi: tuple[int, int, int, int]) -> bool:
    bbox = row.get("screen_bbox") or []
    if len(bbox) != 4:
        return False
    min_x, min_y = logical_to_aligned((bbox[0], bbox[1]), mapping)
    max_x, max_y = logical_to_aligned((bbox[2], bbox[3]), mapping)
    if min_x > max_x:
        min_x, max_x = max_x, min_x
    if min_y > max_y:
        min_y, max_y = max_y, min_y
    left, top, width, height = roi
    right = left + width
    bottom = top + height
    return max_x >= left and min_x <= right and max_y >= top and min_y <= bottom


def fb_capture_roi_pixels(
    row: dict[str, Any],
    image_meta: dict[str, Any],
    roi: tuple[int, int, int, int],
    default: Image.Image,
    underlay: Image.Image,
) -> dict[str, Any]:
    pre_path_text = row.get("pre_ppm")
    post_path_text = row.get("post_ppm")
    rect = row.get("rect") or []
    if (
        not pre_path_text
        or not post_path_text
        or pre_path_text == "-"
        or post_path_text == "-"
        or len(rect) != 4
    ):
        return {"status": "missing_capture_artifact", "pixels": 0}

    pre_path = Path(pre_path_text)
    post_path = Path(post_path_text)
    if not pre_path.exists() or not post_path.exists():
        return {
            "status": "capture_artifact_not_found",
            "pixels": 0,
            "pre_ppm": str(pre_path),
            "post_ppm": str(post_path),
        }

    crop = image_meta.get("logical_viewport", {}).get("crop", {}).get("default", [0, 0, default.size[0], default.size[1]])
    crop_x = int(crop[0])
    crop_y = int(crop[1])
    rect_x = int(rect[0])
    rect_y = int(rect[1])
    rect_w = int(rect[2])
    rect_h = int(rect[3])
    cap_left = rect_x - crop_x
    cap_top = rect_y - crop_y
    cap_right = cap_left + rect_w
    cap_bottom = cap_top + rect_h

    roi_left, roi_top, roi_w, roi_h = roi
    roi_right = roi_left + roi_w
    roi_bottom = roi_top + roi_h
    left = max(roi_left, cap_left, 0)
    top = max(roi_top, cap_top, 0)
    right = min(roi_right, cap_right, default.size[0], underlay.size[0])
    bottom = min(roi_bottom, cap_bottom, default.size[1], underlay.size[1])
    if right <= left or bottom <= top:
        return {"status": "no_roi_intersection", "pixels": 0}

    pre_img = Image.open(pre_path).convert("RGB")
    post_img = Image.open(post_path).convert("RGB")
    pre_px = pre_img.load()
    post_px = post_img.load()
    default_px = default.load()
    underlay_px = underlay.load()
    pre_pixels: list[Pixel] = []
    post_pixels: list[Pixel] = []
    default_pixels: list[Pixel] = []
    underlay_pixels: list[Pixel] = []

    for ay in range(top, bottom):
        for ax in range(left, right):
            local_x = ax + crop_x - rect_x
            local_y = ay + crop_y - rect_y
            if local_x < 0 or local_y < 0 or local_x >= pre_img.size[0] or local_y >= pre_img.size[1]:
                continue
            pre_pixels.append(pre_px[local_x, local_y])
            post_pixels.append(post_px[local_x, local_y])
            default_pixels.append(default_px[ax, ay])
            underlay_pixels.append(underlay_px[ax, ay])

    if not pre_pixels:
        return {"status": "empty_roi_intersection", "pixels": 0}

    return {
        "status": "ok",
        "pixels": len(pre_pixels),
        "aligned_roi_intersection": [left, top, right - left, bottom - top],
        "pre_vs_underlay": score_pixels(underlay_pixels, pre_pixels),
        "post_vs_default": score_pixels(default_pixels, post_pixels),
        "pre_vs_default": score_pixels(default_pixels, pre_pixels),
        "post_vs_underlay": score_pixels(underlay_pixels, post_pixels),
        "pre_ppm": str(pre_path),
        "post_ppm": str(post_path),
    }


def summarize_fb_capture_roi_join(
    captures: list[dict[str, Any]],
    image_meta: dict[str, Any],
    roi: tuple[int, int, int, int],
    default: Image.Image,
    underlay: Image.Image,
) -> dict[str, Any]:
    if not captures:
        return {"status": "no_captures", "first_pre": {}, "last_post": {}}
    ordered = sorted(
        captures,
        key=lambda row: (
            row.get("frame") if row.get("frame") is not None else -1,
            row.get("tri") if row.get("tri") is not None else -1,
            row.get("serial") if row.get("serial") is not None else -1,
        ),
    )
    first = ordered[0]
    last = ordered[-1]
    first_join = fb_capture_roi_pixels(first, image_meta, roi, default, underlay)
    last_join = fb_capture_roi_pixels(last, image_meta, roi, default, underlay)
    status = "ok" if first_join.get("status") == "ok" and last_join.get("status") == "ok" else "incomplete"
    return {
        "status": status,
        "first_capture": {
            "frame": first.get("frame"),
            "tri": first.get("tri"),
            "serial": first.get("serial"),
        },
        "last_capture": {
            "frame": last.get("frame"),
            "tri": last.get("tri"),
            "serial": last.get("serial"),
        },
        "first_pre": first_join,
        "last_post": last_join,
    }


def row_geometry(row: dict[str, Any], mapping: dict[str, Any]) -> dict[str, Any]:
    logical_size = mapping["logical_size"]
    logical = [screen_from_clip(clip, logical_size) for clip in row["vclip"]]
    aligned = [logical_to_aligned(point, mapping) for point in logical]
    return {
        "logical": logical,
        "aligned": aligned,
        "clip_w": [clip[3] for clip in row["vclip"]],
    }


def row_overlaps_roi(row: dict[str, Any], mapping: dict[str, Any], roi: tuple[int, int, int, int]) -> bool:
    geom = row_geometry(row, mapping)
    xs = [point[0] for point in geom["aligned"]]
    ys = [point[1] for point in geom["aligned"]]
    bbox = (min(xs), min(ys), max(xs), max(ys))
    left, top, width, height = roi
    roi_rect = (left, top, left + width, top + height)
    return not (
        bbox[2] <= roi_rect[0]
        or bbox[0] >= roi_rect[2]
        or bbox[3] <= roi_rect[1]
        or bbox[1] >= roi_rect[3]
    )


def compute_uvs(row: dict[str, Any], unit: int) -> list[tuple[float, float]]:
    width, height = row["tex_size"]
    tile = row.get(f"tile{unit}") or row["tile0"]
    return [vertex_uv(raw_uv, tile, width, height) for raw_uv in row["vuv"]]


def interp_pair(
    pairs: list[tuple[float, float]],
    bary: tuple[float, float, float],
    ws: list[float],
) -> tuple[float, float]:
    return (
        perspective_interp([item[0] for item in pairs], bary, ws),
        perspective_interp([item[1] for item in pairs], bary, ws),
    )


def interp_rgba(
    values: list[list[float]],
    bary: tuple[float, float, float],
    ws: list[float],
    noperspective: bool = False,
) -> Rgba:
    return tuple(
        clamp_u8(
            sum(item[channel] * bary[index] for index, item in enumerate(values))
            if noperspective
            else perspective_interp([item[channel] for item in values], bary, ws)
        )
        for channel in range(4)
    )  # type: ignore[return-value]


def unit_rgba(pixel: Rgba) -> Float4:
    return tuple(float(channel) / 255.0 for channel in pixel)  # type: ignore[return-value]


def clamp_unit(value: float) -> float:
    if not math.isfinite(value):
        return 0.0
    return min(1.0, max(0.0, value))


def unit_to_rgba(values: Float4) -> Rgba:
    return tuple(clamp_u8(clamp_unit(channel) * 255.0) for channel in values)  # type: ignore[return-value]


def rgba_from_row_source(row: dict[str, Any], key: str, default_alpha: int) -> Rgba:
    value = row.get(key)
    if isinstance(value, list) and len(value) >= 4:
        return tuple(clamp_u8(float(value[index])) for index in range(4))  # type: ignore[return-value]
    return (0, 0, 0, default_alpha)


def combiner_rgb_source(
    source: int,
    cycle: int,
    tex0: Float4,
    tex1: Float4,
    shade: Float4,
    prim: Float4,
    env: Float4,
    lod_fraction: float,
    combined: Float4,
    channel: int,
) -> float:
    if source == G_CCMUX_COMBINED:
        return combined[channel] if cycle > 0 else 0.0
    if source == G_CCMUX_TEXEL0:
        return tex0[channel]
    if source == G_CCMUX_TEXEL1:
        return tex1[channel]
    if source == G_CCMUX_PRIMITIVE:
        return prim[channel]
    if source == G_CCMUX_SHADE:
        return shade[channel]
    if source == G_CCMUX_ENVIRONMENT:
        return env[channel]
    if source == G_CCMUX_1:
        return 1.0
    if source == G_CCMUX_TEXEL0_ALPHA:
        return tex0[3]
    if source == G_CCMUX_TEXEL1_ALPHA:
        return tex1[3]
    if source == G_CCMUX_PRIMITIVE_ALPHA:
        return prim[3]
    if source == G_CCMUX_SHADE_ALPHA:
        return shade[3]
    if source == G_CCMUX_ENV_ALPHA:
        return env[3]
    if source == G_CCMUX_LOD_FRACTION:
        return lod_fraction
    return 0.0


def combiner_alpha_source(
    source: int,
    slot: int,
    cycle: int,
    tex0: Float4,
    tex1: Float4,
    shade: Float4,
    prim: Float4,
    env: Float4,
    lod_fraction: float,
    combined: Float4,
) -> float:
    if source == G_ACMUX_COMBINED:
        return lod_fraction if slot == 2 else (combined[3] if cycle > 0 else 0.0)
    if source == G_ACMUX_TEXEL0:
        return tex0[3]
    if source == G_ACMUX_TEXEL1:
        return tex1[3]
    if source == G_ACMUX_PRIMITIVE:
        return prim[3]
    if source == G_ACMUX_SHADE:
        return shade[3]
    if source == G_ACMUX_ENVIRONMENT:
        return env[3]
    if source == G_ACMUX_1:
        return 0.0 if slot == 2 else 1.0
    return 0.0


def eval_combiner_channel(a: float, b: float, c: float, d: float) -> float:
    return (a - b) * c + d


def shader_source_from_combiner(
    row: dict[str, Any],
    tex0_pixel: Rgba,
    tex1_pixel: Rgba,
    shade_pixel: Rgba,
    fog_pixel: Rgba | None,
) -> Rgba:
    cc_id = int(row.get("effective_cc_id_int") or 0)
    cc_options = int(row.get("cc_options") or 0)
    tex0 = unit_rgba(tex0_pixel)
    tex1 = unit_rgba(tex1_pixel)
    shade = unit_rgba(shade_pixel)
    prim = unit_rgba(rgba_from_row_source(row, "prim", 0))
    env = unit_rgba(rgba_from_row_source(row, "env", 255))
    lod_fraction = clamp_unit(float(row.get("lod_fraction") or 0) / 255.0)
    cycles = 2 if (cc_options & SHADER_OPT_2CYC) else 1
    combined: Float4 = (0.0, 0.0, 0.0, 0.0)

    for cycle in range(cycles):
        rgb_a = (cc_id >> (cycle * 28)) & 0xF
        rgb_b = (cc_id >> (cycle * 28 + 4)) & 0xF
        rgb_c = (cc_id >> (cycle * 28 + 8)) & 0x1F
        rgb_d = (cc_id >> (cycle * 28 + 13)) & 0x7
        alp_a = (cc_id >> (cycle * 28 + 16)) & 0x7
        alp_b = (cc_id >> (cycle * 28 + 19)) & 0x7
        alp_c = (cc_id >> (cycle * 28 + 22)) & 0x7
        alp_d = (cc_id >> (cycle * 28 + 25)) & 0x7

        if rgb_a >= 8:
            rgb_a = G_CCMUX_0
        if rgb_b >= 8:
            rgb_b = G_CCMUX_0
        if rgb_c >= 16:
            rgb_c = G_CCMUX_0
        if rgb_d == G_CCMUX_NOISE:
            rgb_d = G_CCMUX_0
        if rgb_a == rgb_b or rgb_c == G_CCMUX_COMBINED:
            rgb_a = rgb_b = rgb_c = G_CCMUX_COMBINED
        if alp_a == alp_b or alp_c == G_ACMUX_COMBINED:
            alp_a = alp_b = alp_c = G_ACMUX_COMBINED

        rgb: list[float] = []
        for channel in range(3):
            rgb.append(
                eval_combiner_channel(
                    combiner_rgb_source(rgb_a, cycle, tex0, tex1, shade, prim, env, lod_fraction, combined, channel),
                    combiner_rgb_source(rgb_b, cycle, tex0, tex1, shade, prim, env, lod_fraction, combined, channel),
                    combiner_rgb_source(rgb_c, cycle, tex0, tex1, shade, prim, env, lod_fraction, combined, channel),
                    combiner_rgb_source(rgb_d, cycle, tex0, tex1, shade, prim, env, lod_fraction, combined, channel),
                )
            )

        alpha = 1.0
        if cc_options & SHADER_OPT_ALPHA:
            alpha = eval_combiner_channel(
                combiner_alpha_source(alp_a, 0, cycle, tex0, tex1, shade, prim, env, lod_fraction, combined),
                combiner_alpha_source(alp_b, 1, cycle, tex0, tex1, shade, prim, env, lod_fraction, combined),
                combiner_alpha_source(alp_c, 2, cycle, tex0, tex1, shade, prim, env, lod_fraction, combined),
                combiner_alpha_source(alp_d, 3, cycle, tex0, tex1, shade, prim, env, lod_fraction, combined),
            )

        if cycle == 0 and cycles == 2:
            combined = tuple(min(1.01, max(-1.01, channel)) for channel in (*rgb, alpha))  # type: ignore[assignment]
        else:
            combined = (rgb[0], rgb[1], rgb[2], alpha)

    combined = tuple(clamp_unit(channel) for channel in combined)  # type: ignore[assignment]

    if (cc_options & SHADER_OPT_FOG) and row.get("fog_enabled") and fog_pixel is not None and fog_pixel[3] > 0:
        fog = unit_rgba(fog_pixel)
        combined = (
            combined[0] + (fog[0] - combined[0]) * fog[3],
            combined[1] + (fog[1] - combined[1]) * fog[3],
            combined[2] + (fog[2] - combined[2]) * fog[3],
            combined[3],
        )

    if (cc_options & SHADER_OPT_TEXTURE_EDGE) and (cc_options & SHADER_OPT_ALPHA):
        combined = (combined[0], combined[1], combined[2], 1.0 if combined[3] > 0.19 else 0.0)

    return unit_to_rgba(combined)


def sample_unit(
    row: dict[str, Any],
    unit: int,
    bary: tuple[float, float, float],
    geom: dict[str, Any],
    texture: Texture,
) -> dict[str, Any]:
    uvs = compute_uvs(row, unit)
    noperspective = (int(row.get("cc_options") or 0) & SHADER_OPT_NOPERSPECTIVE_TEXCOORDS) != 0
    if noperspective:
        uv = (
            sum(item[0] * bary[index] for index, item in enumerate(uvs)),
            sum(item[1] * bary[index] for index, item in enumerate(uvs)),
        )
    else:
        uv = interp_pair(uvs, bary, geom["clip_w"])
    tile = row.get(f"tile{unit}") or row["tile0"]
    return {
        "uv": uv,
        "nearest": texture.nearest(uv[0], uv[1], tile["cms"], tile["cmt"]),
        "linear": texture.linear(uv[0], uv[1], tile["cms"], tile["cmt"]),
        "threepoint": texture.threepoint(uv[0], uv[1], tile["cms"], tile["cmt"]),
    }


def reconstruct_at(
    row: dict[str, Any],
    bary: tuple[float, float, float],
    geom: dict[str, Any],
    texture: Texture,
) -> dict[str, Rgba]:
    sample0 = sample_unit(row, 0, bary, geom, texture)
    sample1 = sample_unit(row, 1, bary, geom, texture) if row.get("tile1") else sample0
    noperspective_inputs = (int(row.get("cc_options") or 0) & SHADER_OPT_NOPERSPECTIVE_INPUTS) != 0
    shade = interp_rgba(row["shade"], bary, geom["clip_w"], noperspective=noperspective_inputs)
    fog_logged = row.get("fogc_logged")
    fog = None
    if isinstance(fog_logged, list) and len(fog_logged) >= 4 and any(fog_logged):
        fog = tuple(clamp_u8(float(fog_logged[index])) for index in range(4))  # type: ignore[assignment]
    return {
        "nearest": shader_source_from_combiner(row, sample0["nearest"], sample1["nearest"], shade, fog),
        "linear": shader_source_from_combiner(row, sample0["linear"], sample1["linear"], shade, fog),
        "threepoint": shader_source_from_combiner(row, sample0["threepoint"], sample1["threepoint"], shade, fog),
        "tex_nearest": sample0["nearest"],
        "tex_linear": sample0["linear"],
        "tex_threepoint": sample0["threepoint"],
        "tex1_nearest": sample1["nearest"],
        "tex1_linear": sample1["linear"],
        "tex1_threepoint": sample1["threepoint"],
        "shade": shade,
    }


def center_validation(rows: list[dict[str, Any]], texture: Texture, mapping: dict[str, Any]) -> dict[str, Any]:
    checks: list[dict[str, Any]] = []
    max_abs = Counter()
    failures: list[str] = []
    for row in rows:
        geom = row_geometry(row, mapping)
        bary = (1.0 / 3.0, 1.0 / 3.0, 1.0 / 3.0)
        uvs0 = compute_uvs(row, 0)
        uvs1 = compute_uvs(row, 1) if row.get("tile1") else uvs0
        center_uv = interp_pair(uvs0, bary, geom["clip_w"])
        center_uv1 = interp_pair(uvs1, bary, geom["clip_w"])
        recon = reconstruct_at(row, bary, geom, texture)
        check = {
            "tri": row.get("tri"),
            "line": row.get("line"),
            "uv": list(center_uv),
            "logged_uv": row.get("uv0_logged"),
            "uv1": list(center_uv1),
            "logged_uv1": row.get("uv1_logged"),
            "tex_linear": list(recon["tex_linear"]),
            "logged_t0l": row.get("t0l_logged"),
            "tex_threepoint": list(recon["tex_threepoint"]),
            "logged_t0p": row.get("t0p_logged"),
            "tex1_linear": list(recon["tex1_linear"]),
            "logged_t1l": row.get("t1l_logged"),
            "tex1_threepoint": list(recon["tex1_threepoint"]),
            "logged_t1p": row.get("t1p_logged"),
            "shader_linear": list(recon["linear"]),
            "logged_shaderL": row.get("shaderL_logged"),
            "shader_threepoint": list(recon["threepoint"]),
            "logged_shaderP": row.get("shaderP_logged"),
        }
        checks.append(check)
        comparisons = (
            ("uv", center_uv, row.get("uv0_logged") or []),
            ("uv1", center_uv1, row.get("uv1_logged") or []),
            ("t0l", recon["tex_linear"], row.get("t0l_logged") or []),
            ("t0p", recon["tex_threepoint"], row.get("t0p_logged") or []),
            ("t1l", recon["tex1_linear"], row.get("t1l_logged") or []),
            ("t1p", recon["tex1_threepoint"], row.get("t1p_logged") or []),
            ("shaderL", recon["linear"], row.get("shaderL_logged") or []),
            ("shaderP", recon["threepoint"], row.get("shaderP_logged") or []),
        )
        for name, actual, expected in comparisons:
            if len(expected) < len(actual):
                failures.append(f"tri {row.get('tri')}: missing logged {name}")
                continue
            tolerance = 0.001 if name in ("uv", "uv1") else 1.0
            deltas = [abs(float(actual[index]) - float(expected[index])) for index in range(len(actual))]
            max_abs[name] = max(max_abs[name], max(deltas))
            if max(deltas) > tolerance:
                failures.append(
                    f"tri {row.get('tri')}: {name} max abs delta {max(deltas):.3f} exceeds {tolerance}"
                )
    return {
        "status": "fail" if failures else "pass",
        "failures": failures,
        "max_abs_delta": dict(max_abs),
        "checks": checks[:12],
    }


def recompose(source: Rgba, underlay: Pixel) -> Pixel:
    alpha = source[3] / 255.0
    inv_alpha = 1.0 - alpha
    return tuple(
        clamp_u8(float(source[channel]) * alpha + float(underlay[channel]) * inv_alpha)
        for channel in range(3)
    )  # type: ignore[return-value]


def composition_score(
    actual_default_pixels: list[Pixel],
    actual_stock_pixels: list[Pixel],
    synthetic_pixels: list[Pixel],
) -> dict[str, Any]:
    return {
        "vs_default": score_pixels(actual_default_pixels, synthetic_pixels),
        "vs_stock": score_pixels(actual_stock_pixels, synthetic_pixels),
    }


def score_pixels(a_pixels: list[Pixel], b_pixels: list[Pixel]) -> dict[str, Any]:
    changed = 0
    abs_rgb: list[float] = []
    luma_delta: list[float] = []
    for a, b in zip(a_pixels, b_pixels):
        if roi_semantics.changed(a, b):
            changed += 1
        luma_delta.append(luma(b) - luma(a))
        for channel in range(3):
            abs_rgb.append(abs(float(b[channel] - a[channel])))
    total = len(a_pixels)
    return {
        "pixels": total,
        "changed_pixels": changed,
        "changed_pct": 100.0 * changed / total if total else 0.0,
        "mean_abs_rgb": mean(abs_rgb),
        "luma_delta": stats(luma_delta),
    }


def source_delta(actual: list[Rgba], required: list[Float3]) -> dict[str, Any]:
    abs_rgb: list[float] = []
    luma_deltas: list[float] = []
    actual_lumas: list[float] = []
    required_lumas: list[float] = []
    for source, req in zip(actual, required):
        actual_rgb = (float(source[0]), float(source[1]), float(source[2]))
        actual_lumas.append(luma(actual_rgb))
        required_lumas.append(luma(req))
        luma_deltas.append(luma(actual_rgb) - luma(req))
        for channel in range(3):
            abs_rgb.append(abs(actual_rgb[channel] - req[channel]))
    return {
        "pixels": len(actual),
        "mean_abs_rgb": mean(abs_rgb),
        "actual_luma": stats(actual_lumas),
        "required_luma": stats(required_lumas),
        "actual_minus_required_luma": stats(luma_deltas),
    }


def analyze(args: argparse.Namespace) -> tuple[dict[str, Any], int]:
    failures: list[str] = []
    route = load_json(args.route_json)
    route_name = route.get("name") or "unknown"
    regions = route_regions(route)
    if args.region not in regions:
        failures.append(f"missing route region: {args.region}")
        roi = (0, 0, 0, 0)
    else:
        roi = regions[args.region]

    stock_image = args.stock_image or args.base_case_dir / f"stock_{route_name}.ppm"
    default_image = args.default_image or args.base_case_dir / f"native_{route_name}.bmp"
    underlay_image = args.underlay_image

    for path, label in (
        (args.log, "SETTEX material log"),
        (args.route_json, "route JSON"),
        (args.texture_rgba, "settex RGBA dump"),
        (stock_image, "stock screenshot"),
        (default_image, "default/native screenshot"),
        (underlay_image, "underlay screenshot"),
    ):
        if not path.exists():
            failures.append(f"{label} not found: {path}")

    alpha_path = args.texture_alpha
    if alpha_path is not None and not alpha_path.exists():
        failures.append(f"settex alpha dump not found: {alpha_path}")

    if failures:
        return {
            "status": "fail",
            "failures": failures,
            "interpretation": [],
        }, 1

    stock, default, underlay, image_meta = aligned_images_for_trace(
        route,
        stock_image,
        default_image,
        underlay_image,
        args.active_threshold,
        args.align_to,
    )
    texture = Texture(args.texture_rgba, alpha_path)
    mapping = route_mapping(route, stock.size)

    rows_all = parse_log_rows(args.log, args)
    selected_rows, frame_selection = select_frame(rows_all, args.frame)
    target_rows = [
        row for row in selected_rows
        if row_overlaps_roi(row, mapping, roi)
    ]
    fb_captures_all = parse_fb_capture_rows(args.log, args)
    fb_captures_selected, fb_frame_selection = select_frame(fb_captures_all, args.frame)
    fb_captures_target = [
        row for row in fb_captures_selected
        if row.get("status") == "ok" and fb_capture_overlaps_roi(row, mapping, roi)
    ]
    fb_capture_roi_join = summarize_fb_capture_roi_join(
        fb_captures_target,
        image_meta,
        roi,
        default,
        underlay,
    )

    if not rows_all:
        failures.append("no matching SETTEX-MATERIAL-CC rows found")
    if not target_rows:
        failures.append(f"no matching rows overlap {args.region}")
    if args.require_fb_capture and not fb_captures_all:
        failures.append("no SETTEX-FB-CAPTURE rows found")
    if args.require_fb_capture and not fb_captures_target:
        failures.append(f"no SETTEX-FB-CAPTURE rows overlap {args.region}")
    if args.require_fb_capture and fb_capture_roi_join.get("status") != "ok":
        failures.append(f"SETTEX-FB-CAPTURE ROI pixel join incomplete: {fb_capture_roi_join.get('status')}")

    center = center_validation(target_rows, texture, mapping) if target_rows else {
        "status": "fail",
        "failures": ["no target rows"],
        "max_abs_delta": {},
        "checks": [],
    }
    failures.extend(center["failures"])

    left, top, width, height = roi
    stock_px = stock.load()
    default_px = default.load()
    underlay_px = underlay.load()
    row_geoms = [(row, row_geometry(row, mapping)) for row in target_rows]

    covered_by_mode: dict[str, list[Rgba]] = {"nearest": [], "linear": [], "threepoint": []}
    required_stock: list[Float3] = []
    required_default: list[Float3] = []
    actual_default_pixels: list[Pixel] = []
    actual_stock_pixels: list[Pixel] = []
    synth_by_mode: dict[str, list[Pixel]] = {"nearest": [], "linear": [], "threepoint": []}
    synth_ordered_by_mode: dict[str, list[Pixel]] = {"nearest": [], "linear": [], "threepoint": []}
    synth_reverse_by_mode: dict[str, list[Pixel]] = {"nearest": [], "linear": [], "threepoint": []}
    coverage = 0
    row_hits: Counter[str] = Counter()
    all_row_hits: Counter[str] = Counter()
    hit_counts: Counter[str] = Counter()
    uncovered: list[tuple[int, int]] = []

    for y in range(top, top + height):
        for x in range(left, left + width):
            aligned_point = (x + 0.5, y + 0.5)
            logical_point = aligned_to_logical(aligned_point, mapping)
            hits: list[tuple[dict[str, Any], dict[str, Any], tuple[float, float, float]]] = []
            for row, geom in row_geoms:
                bary = barycentric(logical_point, geom["logical"])
                if bary is not None and bary_inside(bary):
                    hits.append((row, geom, bary))
            stock_pixel = stock_px[x, y]
            default_pixel = default_px[x, y]
            underlay_pixel = underlay_px[x, y]
            actual_stock_pixels.append(stock_pixel)
            actual_default_pixels.append(default_pixel)
            required_stock.append(required_source.required_source_pixel(stock_pixel, underlay_pixel, 102 / 255.0))
            required_default.append(required_source.required_source_pixel(default_pixel, underlay_pixel, 102 / 255.0))

            if not hits:
                uncovered.append((x, y))
                for mode in synth_by_mode:
                    synth_by_mode[mode].append(underlay_pixel)
                    synth_ordered_by_mode[mode].append(underlay_pixel)
                    synth_reverse_by_mode[mode].append(underlay_pixel)
                continue

            winner = hits[-1]
            row, geom, bary = winner
            row_hits[str(row.get("tri"))] += 1
            for hit_row, _hit_geom, _hit_bary in hits:
                all_row_hits[str(hit_row.get("tri"))] += 1
            hit_counts[str(len(hits))] += 1
            coverage += 1
            recon = reconstruct_at(row, bary, geom, texture)
            for mode in ("nearest", "linear", "threepoint"):
                source = recon[mode]
                covered_by_mode[mode].append(source)
                synth_by_mode[mode].append(recompose(source, underlay_pixel))
                ordered_pixel = underlay_pixel
                for hit_row, hit_geom, hit_bary in hits:
                    ordered_pixel = recompose(
                        reconstruct_at(hit_row, hit_bary, hit_geom, texture)[mode],
                        ordered_pixel,
                    )
                synth_ordered_by_mode[mode].append(ordered_pixel)
                reverse_pixel = underlay_pixel
                for hit_row, hit_geom, hit_bary in reversed(hits):
                    reverse_pixel = recompose(
                        reconstruct_at(hit_row, hit_bary, hit_geom, texture)[mode],
                        reverse_pixel,
                    )
                synth_reverse_by_mode[mode].append(reverse_pixel)

    source_pixels = width * height
    mode_payloads: dict[str, Any] = {}
    for mode in ("nearest", "linear", "threepoint"):
        mode_payloads[mode] = {
            "source": source_delta(covered_by_mode[mode], required_default[: len(covered_by_mode[mode])])
            if covered_by_mode[mode] else {},
            "source_vs_stock_required": source_delta(
                covered_by_mode[mode],
                required_stock[: len(covered_by_mode[mode])],
            ) if covered_by_mode[mode] else {},
            "synthetic_vs_default": score_pixels(actual_default_pixels, synth_by_mode[mode]),
            "synthetic_vs_stock": score_pixels(actual_stock_pixels, synth_by_mode[mode]),
            "synthetic_compositions": {
                "single_last": composition_score(
                    actual_default_pixels,
                    actual_stock_pixels,
                    synth_by_mode[mode],
                ),
                "ordered_all": composition_score(
                    actual_default_pixels,
                    actual_stock_pixels,
                    synth_ordered_by_mode[mode],
                ),
                "reverse_all": composition_score(
                    actual_default_pixels,
                    actual_stock_pixels,
                    synth_reverse_by_mode[mode],
                ),
            },
            "source_alpha_counts": dict(Counter(str(pixel[3]) for pixel in covered_by_mode[mode])),
        }

    best_default_mode = min(
        mode_payloads,
        key=lambda mode: mode_payloads[mode]["synthetic_vs_default"]["mean_abs_rgb"],
    )
    best_stock_mode = min(
        mode_payloads,
        key=lambda mode: mode_payloads[mode]["synthetic_vs_stock"]["mean_abs_rgb"],
    )

    interpretation = [
        (
            f"{args.region}: {coverage}/{source_pixels} pixels covered by texnum {args.texnum} "
            f"rows in frame {frame_selection.get('selected')}"
        ),
        (
            f"center reconstruction status={center['status']} max_abs_delta="
            f"{center.get('max_abs_delta', {})}"
        ),
        (
            f"best synthetic-vs-default mode is {best_default_mode} "
            f"(mean_abs_rgb={mode_payloads[best_default_mode]['synthetic_vs_default']['mean_abs_rgb']:.3f}, "
            f"changed={mode_payloads[best_default_mode]['synthetic_vs_default']['changed_pct']:.3f}%)"
        ),
        (
            f"best synthetic-vs-stock mode is {best_stock_mode} "
            f"(mean_abs_rgb={mode_payloads[best_stock_mode]['synthetic_vs_stock']['mean_abs_rgb']:.3f}, "
            f"changed={mode_payloads[best_stock_mode]['synthetic_vs_stock']['changed_pct']:.3f}%)"
        ),
    ]

    stock_delta = mode_payloads[best_default_mode]["source_vs_stock_required"]
    if stock_delta:
        interpretation.append(
            f"{best_default_mode} actual source minus stock-required luma mean="
            f"{stock_delta['actual_minus_required_luma']['mean']:.3f}; "
            f"mean_abs_rgb={stock_delta['mean_abs_rgb']:.3f}"
        )
    composition_candidates: list[tuple[float, str, str, str]] = []
    for mode, mode_payload in mode_payloads.items():
        for name, score in mode_payload["synthetic_compositions"].items():
            composition_candidates.append(
                (score["vs_default"]["mean_abs_rgb"], mode, name, "default")
            )
            composition_candidates.append(
                (score["vs_stock"]["mean_abs_rgb"], mode, name, "stock")
            )
    best_comp_default = min(
        (item for item in composition_candidates if item[3] == "default"),
        key=lambda item: item[0],
    )
    best_comp_stock = min(
        (item for item in composition_candidates if item[3] == "stock"),
        key=lambda item: item[0],
    )
    interpretation.append(
        f"best composition-vs-default is {best_comp_default[1]}/{best_comp_default[2]} "
        f"(mean_abs_rgb={best_comp_default[0]:.3f})"
    )
    interpretation.append(
        f"best composition-vs-stock is {best_comp_stock[1]}/{best_comp_stock[2]} "
        f"(mean_abs_rgb={best_comp_stock[0]:.3f})"
    )
    if any(int(key) > 1 for key in hit_counts):
        overlap_pixels = sum(count for key, count in hit_counts.items() if int(key) > 1)
        interpretation.append(
            f"{args.region}: {overlap_pixels}/{source_pixels} pixels hit multiple target rows "
            f"(hit-counts={dict(hit_counts)})"
        )
    if fb_captures_target:
        interpretation.append(
            f"{args.region}: {len(fb_captures_target)} same-frame framebuffer captures overlap ROI "
            f"(mean_abs_rgb mean={mean([row['mean_abs_rgb'] for row in fb_captures_target]):.3f}, "
            f"changed_pct mean={mean([row['changed_pct'] for row in fb_captures_target]):.3f}%)"
        )
    first_pre_score = fb_capture_roi_join.get("first_pre", {}).get("pre_vs_underlay", {})
    last_post_score = fb_capture_roi_join.get("last_post", {}).get("post_vs_default", {})
    if first_pre_score:
        interpretation.append(
            f"{args.region}: first capture pre-vs-skip-underlay "
            f"mean_abs_rgb={first_pre_score['mean_abs_rgb']:.3f}, "
            f"changed={first_pre_score['changed_pct']:.3f}%"
        )
    if last_post_score:
        interpretation.append(
            f"{args.region}: last capture post-vs-native-final "
            f"mean_abs_rgb={last_post_score['mean_abs_rgb']:.3f}, "
            f"changed={last_post_score['changed_pct']:.3f}%"
        )

    if coverage < source_pixels:
        failures.append(f"{args.region} coverage below 100%: {coverage}/{source_pixels}")

    payload: dict[str, Any] = {
        "status": "fail" if failures else "pass",
        "failures": failures,
        "interpretation": interpretation,
        "sources": {
            "log": str(args.log),
            "route_json": str(args.route_json),
            "texture_rgba": str(args.texture_rgba),
            "texture_alpha": str(alpha_path) if alpha_path is not None else None,
            "stock_image": str(stock_image),
            "default_image": str(default_image),
            "underlay_image": str(underlay_image),
        },
        "route": {
            "name": route_name,
            "region": args.region,
            "roi": list(roi),
            "mapping": mapping,
            "image_meta": image_meta,
        },
        "filters": {
            "frame": args.frame,
            "texnum": args.texnum,
            "effcc": args.effcc,
            "opts": args.opts,
            "oml_raw": args.oml_raw,
            "wh": args.wh,
            "rgba_wh": args.rgba_wh,
        },
        "frame_selection": frame_selection,
        "framebuffer_captures": {
            "frame_selection": fb_frame_selection,
            "matching_rows_all_frames": len(fb_captures_all),
            "matching_rows_selected_frame": len(fb_captures_selected),
            "target_rows": len(fb_captures_target),
            "target_status_counts": dict(Counter(str(row.get("status")) for row in fb_captures_target)),
            "target_changed_pct": stats([row["changed_pct"] for row in fb_captures_target]),
            "target_mean_abs_rgb": stats([row["mean_abs_rgb"] for row in fb_captures_target]),
            "roi_pixel_join": fb_capture_roi_join,
            "target_examples": [
                {
                    "frame": row.get("frame"),
                    "tri": row.get("tri"),
                    "serial": row.get("serial"),
                    "rect": row.get("rect"),
                    "screen_bbox": row.get("screen_bbox"),
                    "mean_abs_rgb": row.get("mean_abs_rgb"),
                    "changed_pct": row.get("changed_pct"),
                    "pre_ppm": row.get("pre_ppm"),
                    "post_ppm": row.get("post_ppm"),
                }
                for row in fb_captures_target[:12]
            ],
        },
        "line_counts": {
            "matching_rows_all_frames": len(rows_all),
            "matching_rows_selected_frame": len(selected_rows),
            "target_rows": len(target_rows),
        },
        "target_rows": [
            {"frame": row.get("frame"), "tri": row.get("tri"), "line": row.get("line")}
            for row in target_rows
        ],
        "center_validation": center,
        "region": {
            "source_pixels": source_pixels,
            "covered_pixels": coverage,
            "coverage_pct": 100.0 * coverage / source_pixels if source_pixels else 0.0,
            "uncovered_examples": [list(item) for item in uncovered[:20]],
            "row_pixel_counts": dict(row_hits),
            "all_row_pixel_counts": dict(all_row_hits),
            "hit_count_counts": dict(hit_counts),
        },
        "modes": mode_payloads,
    }
    if args.json_out is not None:
        args.json_out.parent.mkdir(parents=True, exist_ok=True)
        args.json_out.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return payload, 1 if failures else 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--log", type=Path, required=True)
    parser.add_argument("--route-json", type=Path, required=True)
    parser.add_argument("--texture-rgba", type=Path, required=True)
    parser.add_argument("--texture-alpha", type=Path)
    parser.add_argument("--base-case-dir", type=Path, required=True)
    parser.add_argument("--stock-image", type=Path)
    parser.add_argument("--default-image", type=Path)
    parser.add_argument("--underlay-image", type=Path, required=True)
    parser.add_argument("--region", default="projected_impact")
    parser.add_argument("--frame", default="latest")
    parser.add_argument("--texnum", type=int, default=654)
    parser.add_argument("--effcc", default="0x00738e4f020a2d12")
    parser.add_argument("--opts", default="0x00043C13")
    parser.add_argument("--oml-raw", default="0xC41049D8")
    parser.add_argument("--wh", default="54x54")
    parser.add_argument("--rgba-wh", default="54x54")
    parser.add_argument("--active-threshold", type=int, default=12)
    parser.add_argument("--align-to", choices=("native", "stock"), default="native")
    parser.add_argument("--require-fb-capture", action="store_true")
    parser.add_argument("--json-out", type=Path)
    args = parser.parse_args()

    payload, status = analyze(args)
    if args.json_out is None:
        json.dump(payload, sys.stdout, indent=2, sort_keys=True)
        sys.stdout.write("\n")
    return status


if __name__ == "__main__":
    raise SystemExit(main())
