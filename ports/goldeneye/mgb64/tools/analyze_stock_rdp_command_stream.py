#!/usr/bin/env python3
"""Summarize an instrumented ares RDP command-stream sidecar.

The sidecar is produced by the dev-only hook injected by
tools/prepare_ares_movement_oracle_build.sh when
MGB64_ARES_TRACE_RDP_COMMANDS=1 is set. It records the actual RDP command buffers
that ares is executing, plus compact draw-state summaries for triangle,
texture-rectangle, and fill-rectangle commands.

This analyzer answers the first-order oracle questions:

* Did the capture cover the expected stock frames?
* Did command parsing preserve enough state to avoid all-zero draw states?
* Which texture images/states were actually drawn?
* Did named room-texture candidates appear in real draw ops?
* Which real draw ops overlap named route regions, and in what order?
* Which draw states are the final conservative owners of route-region pixels?
* What stock screenshot colors correspond to those final owner pixels?
* Is command-buffer truncation present and worth investigating?
"""

from __future__ import annotations

import argparse
from collections import Counter, defaultdict
import json
from pathlib import Path
from typing import Any


def parse_int(value: Any, fallback: int = 0) -> int:
    if value is None:
        return fallback
    if isinstance(value, int):
        return value
    if isinstance(value, str):
        try:
            return int(value, 0)
        except ValueError:
            return fallback
    return fallback


def hexn(value: int, width: int = 8) -> str:
    return f"0x{value & ((1 << (width * 4)) - 1):0{width}x}"


def image_physical(value: int) -> int:
    return value & 0x03FFFFFF


def image_kseg0(value: int) -> int:
    physical = image_physical(value)
    return physical | 0x80000000 if physical else 0


def parse_bbox(value: Any) -> tuple[float, float, float, float] | None:
    if not isinstance(value, list) or len(value) != 4:
        return None
    try:
        x0, y0, x1, y1 = [float(item) for item in value]
    except (TypeError, ValueError):
        return None
    if x1 <= x0 or y1 <= y0:
        return None
    return x0, y0, x1, y1


def rect_area(rect: tuple[float, float, float, float] | None) -> float:
    if rect is None:
        return 0.0
    return max(0.0, rect[2] - rect[0]) * max(0.0, rect[3] - rect[1])


def rect_intersection(
    left_rect: tuple[float, float, float, float] | None,
    right_rect: tuple[float, float, float, float] | None,
) -> tuple[float, float, float, float] | None:
    if left_rect is None or right_rect is None:
        return None
    x0 = max(left_rect[0], right_rect[0])
    y0 = max(left_rect[1], right_rect[1])
    x1 = min(left_rect[2], right_rect[2])
    y1 = min(left_rect[3], right_rect[3])
    if x1 <= x0 or y1 <= y0:
        return None
    return x0, y0, x1, y1


def rect_union(
    left_rect: tuple[float, float, float, float] | None,
    right_rect: tuple[float, float, float, float] | None,
) -> tuple[float, float, float, float] | None:
    if left_rect is None:
        return right_rect
    if right_rect is None:
        return left_rect
    return (
        min(left_rect[0], right_rect[0]),
        min(left_rect[1], right_rect[1]),
        max(left_rect[2], right_rect[2]),
        max(left_rect[3], right_rect[3]),
    )


def rect_list(rect: tuple[float, float, float, float] | None) -> list[float] | None:
    if rect is None:
        return None
    return [round(value, 3) for value in rect]


def raster_pixels(rect: tuple[float, float, float, float] | None) -> set[int]:
    if rect is None:
        return set()
    x0 = int(rect[0])
    y0 = int(rect[1])
    x1 = int(rect[2] + 0.999999)
    y1 = int(rect[3] + 0.999999)
    if x1 <= x0 or y1 <= y0:
        return set()
    return {y * 4096 + x for y in range(y0, y1) for x in range(x0, x1)}


def pixel_bounds(rect: tuple[float, float, float, float]) -> tuple[int, int, int, int]:
    return (
        int(rect[0]),
        int(rect[1]),
        int(rect[2] + 0.999999),
        int(rect[3] + 0.999999),
    )


def pixels_bbox(pixels: set[int]) -> tuple[float, float, float, float] | None:
    if not pixels:
        return None
    xs = [pixel & 4095 for pixel in pixels]
    ys = [pixel >> 12 for pixel in pixels]
    return (float(min(xs)), float(min(ys)), float(max(xs) + 1), float(max(ys) + 1))


def parse_size(value: str) -> tuple[int, int]:
    parts = value.lower().replace("x", ",").split(",")
    if len(parts) != 2:
        raise argparse.ArgumentTypeError("expected size as WxH or W,H")
    try:
        width, height = (int(part) for part in parts)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("size must contain integers") from exc
    if width <= 0 or height <= 0:
        raise argparse.ArgumentTypeError("size must be positive")
    return width, height


def route_visual_metadata(route_path: Path | None) -> dict[str, Any]:
    metadata: dict[str, Any] = {
        "logical_size": None,
        "baseline_frame": None,
        "test_frame": None,
    }
    if route_path is None:
        return metadata
    with route_path.open("r", encoding="utf-8") as handle:
        route = json.load(handle)
    value = route.get("visual_logical_size")
    if isinstance(value, list) and len(value) == 2:
        try:
            width, height = (int(item) for item in value)
        except (TypeError, ValueError):
            width, height = 0, 0
        if width > 0 and height > 0:
            metadata["logical_size"] = (width, height)

    baseline_frame = route.get("visual_baseline_logical_frame")
    if baseline_frame in ("active", "full"):
        metadata["baseline_frame"] = baseline_frame
    test_frame = route.get("visual_test_logical_frame")
    if test_frame in ("active", "full"):
        metadata["test_frame"] = test_frame
    return metadata


def route_logical_size(route_path: Path | None) -> tuple[int, int] | None:
    value = route_visual_metadata(route_path).get("logical_size")
    return value if isinstance(value, tuple) else None


def active_bbox(image: Any, threshold: int = 0) -> tuple[int, int, int, int] | None:
    rgb = image.convert("RGB")
    pixels = rgb.load()
    width, height = rgb.size
    min_x = width
    min_y = height
    max_x = -1
    max_y = -1
    for y in range(height):
        for x in range(width):
            if max(pixels[x, y]) <= threshold:
                continue
            min_x = min(min_x, x)
            min_y = min(min_y, y)
            max_x = max(max_x, x)
            max_y = max(max_y, y)
    if max_x < min_x or max_y < min_y:
        return None
    return min_x, min_y, max_x - min_x + 1, max_y - min_y + 1


def load_screenshot_sampler(
    path: Path | None,
    logical_size: tuple[int, int] | None,
    frame_mode: str = "full",
    active_threshold: int = 0,
) -> dict[str, Any] | None:
    if path is None:
        return None
    if logical_size is None:
        raise ValueError("--screenshot requires --screenshot-logical-size or route visual_logical_size")
    if frame_mode not in ("active", "full"):
        raise ValueError(f"unsupported screenshot frame mode: {frame_mode}")
    try:
        from PIL import Image
    except ImportError as exc:
        raise RuntimeError("--screenshot requires Pillow (PIL)") from exc
    image = Image.open(path).convert("RGB")
    logical_width, logical_height = logical_size
    if frame_mode == "full":
        frame_bbox = (0, 0, image.size[0], image.size[1])
    else:
        frame_bbox = active_bbox(image, active_threshold)
        if frame_bbox is None:
            raise ValueError(f"{path}: active screenshot frame is empty")
    return {
        "path": str(path),
        "image": image,
        "image_size": image.size,
        "logical_size": logical_size,
        "frame_mode": frame_mode,
        "frame_bbox": frame_bbox,
        "active_threshold": active_threshold,
        "scale_x": frame_bbox[2] / logical_width,
        "scale_y": frame_bbox[3] / logical_height,
    }


def sample_logical_pixel(sampler: dict[str, Any], pixel: int) -> tuple[float, float, float]:
    image = sampler["image"]
    logical_width, logical_height = sampler["logical_size"]
    frame_x, frame_y, frame_width, frame_height = sampler["frame_bbox"]
    logical_x = pixel & 4095
    logical_y = pixel >> 12
    x0 = int(round(frame_x + logical_x * frame_width / logical_width))
    y0 = int(round(frame_y + logical_y * frame_height / logical_height))
    x1 = int(round(frame_x + (logical_x + 1) * frame_width / logical_width))
    y1 = int(round(frame_y + (logical_y + 1) * frame_height / logical_height))
    x0 = max(0, min(image.size[0] - 1, x0))
    y0 = max(0, min(image.size[1] - 1, y0))
    x1 = max(x0 + 1, min(image.size[0], x1))
    y1 = max(y0 + 1, min(image.size[1], y1))
    total_r = 0
    total_g = 0
    total_b = 0
    samples = 0
    for y in range(y0, y1):
        for x in range(x0, x1):
            r, g, b = image.getpixel((x, y))
            total_r += r
            total_g += g
            total_b += b
            samples += 1
    if samples <= 0:
        return 0.0, 0.0, 0.0
    return total_r / samples, total_g / samples, total_b / samples


def color_stats_for_pixels(pixels: set[int], sampler: dict[str, Any] | None) -> dict[str, Any] | None:
    if sampler is None or not pixels:
        return None
    total_r = 0.0
    total_g = 0.0
    total_b = 0.0
    min_r = min_g = min_b = 255.0
    max_r = max_g = max_b = 0.0
    for pixel in pixels:
        r, g, b = sample_logical_pixel(sampler, pixel)
        total_r += r
        total_g += g
        total_b += b
        min_r = min(min_r, r)
        min_g = min(min_g, g)
        min_b = min(min_b, b)
        max_r = max(max_r, r)
        max_g = max(max_g, g)
        max_b = max(max_b, b)
    count = len(pixels)
    mean_r = total_r / count
    mean_g = total_g / count
    mean_b = total_b / count
    return {
        "samples": count,
        "mean_rgb": {
            "r": round(mean_r, 3),
            "g": round(mean_g, 3),
            "b": round(mean_b, 3),
        },
        "mean_luma": round(0.299 * mean_r + 0.587 * mean_g + 0.114 * mean_b, 3),
        "min_rgb": {
            "r": round(min_r, 3),
            "g": round(min_g, 3),
            "b": round(min_b, 3),
        },
        "max_rgb": {
            "r": round(max_r, 3),
            "g": round(max_g, 3),
            "b": round(max_b, 3),
        },
    }


def color_delta(reference: dict[str, Any], compare: dict[str, Any]) -> dict[str, Any]:
    reference_rgb = reference["mean_rgb"]
    compare_rgb = compare["mean_rgb"]
    delta_r = float(compare_rgb["r"]) - float(reference_rgb["r"])
    delta_g = float(compare_rgb["g"]) - float(reference_rgb["g"])
    delta_b = float(compare_rgb["b"]) - float(reference_rgb["b"])
    return {
        "mean_rgb_delta": {
            "r": round(delta_r, 3),
            "g": round(delta_g, 3),
            "b": round(delta_b, 3),
        },
        "mean_abs_rgb_delta": round((abs(delta_r) + abs(delta_g) + abs(delta_b)) / 3.0, 3),
        "mean_luma_delta": round(float(compare["mean_luma"]) - float(reference["mean_luma"]), 3),
    }


def quantize_x(value: int) -> int:
    sticky = 1 if (value & 0xFFF) != 0 else 0
    return (value >> 12) | sticky


def triangle_span_pixels(
    draw_op: dict[str, Any],
    region: tuple[float, float, float, float],
    scissor: list[int],
) -> set[int] | None:
    op_code = parse_int(draw_op.get("op"))
    if op_code < 0x08 or op_code > 0x0F:
        return None
    setup = draw_op.get("setup")
    if not isinstance(setup, dict):
        return None

    xh = parse_int(setup.get("xh"))
    xm = parse_int(setup.get("xm"))
    xl = parse_int(setup.get("xl"))
    yh = parse_int(setup.get("yh"))
    ym = parse_int(setup.get("ym"))
    yl = parse_int(setup.get("yl"))
    dxhdy = parse_int(setup.get("dxhdy"))
    dxmdy = parse_int(setup.get("dxmdy"))
    dxldy = parse_int(setup.get("dxldy"))
    flip = (parse_int(setup.get("flags")) & 1) != 0
    if yl <= yh:
        return set()

    region_x0, region_y0, region_x1, region_y1 = pixel_bounds(region)
    scissor_xlo, scissor_ylo, scissor_xhi, scissor_yhi = scissor
    active_ylo = max(yh, scissor_ylo)
    active_yhi = min(yl, scissor_yhi)
    if active_yhi <= active_ylo:
        return set()

    y_start = max(region_y0, active_ylo >> 2)
    y_end = min(region_y1 - 1, (active_yhi - 1) >> 2)
    if y_end < y_start:
        return set()

    yh_base = yh & ~3
    lo_scissor = scissor_xlo << 1
    hi_scissor = scissor_xhi << 1
    pixels: set[int] = set()

    for y in range(y_start, y_end + 1):
        for y_sub in range(y * 4, y * 4 + 4):
            if y_sub < active_ylo or y_sub >= active_yhi:
                continue
            xh_sub = xh + (y_sub - yh_base) * dxhdy
            xm_sub = xm + (y_sub - yh_base) * dxmdy
            xl_sub = xl + (y_sub - ym) * dxldy
            if y_sub < ym:
                xl_sub = xm_sub

            xh_quant = quantize_x(xh_sub)
            xl_quant = quantize_x(xl_sub)
            if flip:
                xleft = xh_quant
                xright = xl_quant
            else:
                xleft = xl_quant
                xright = xh_quant

            if (xleft >> 1) > (xright >> 1):
                continue
            if min(xleft, xright) >= hi_scissor or max(xleft, xright) < lo_scissor:
                continue

            xleft = min(max(xleft, lo_scissor), hi_scissor)
            xright = min(max(xright, lo_scissor), hi_scissor)
            x_start = max(region_x0, xleft >> 3)
            x_end = min(region_x1 - 1, xright >> 3)
            if x_end < x_start:
                continue
            for x in range(x_start, x_end + 1):
                pixels.add(y * 4096 + x)

    return pixels


def draw_op_region_pixels(
    draw_op: dict[str, Any],
    bbox: tuple[float, float, float, float],
    region: tuple[float, float, float, float],
    scissor: list[int],
    coverage_model: str,
) -> set[int]:
    if coverage_model == "span":
        pixels = triangle_span_pixels(draw_op, region, scissor)
        if pixels is not None:
            return pixels
    return raster_pixels(rect_intersection(bbox, region))


def route_regions(route_path: Path | None) -> dict[str, tuple[float, float, float, float]]:
    if route_path is None:
        return {}
    with route_path.open("r", encoding="utf-8") as handle:
        route = json.load(handle)
    regions: dict[str, tuple[float, float, float, float]] = {}
    for item in route.get("visual_regions", route.get("regions", [])) or []:
        if not isinstance(item, dict) or not item.get("name"):
            continue
        roi = item.get("roi")
        if not isinstance(roi, list) or len(roi) != 4:
            continue
        try:
            x, y, width, height = [float(value) for value in roi]
        except (TypeError, ValueError):
            continue
        if width <= 0.0 or height <= 0.0:
            continue
        regions[str(item["name"])] = (x, y, x + width, y + height)
    return regions


def state_key(state: dict[str, Any]) -> tuple[int, ...]:
    image = parse_int(state.get("image"))
    kseg0 = parse_int(state.get("image_kseg0"), image_kseg0(image))
    combine = state.get("combine") or []
    other = state.get("other") or []
    combine_hi = parse_int(combine[0] if len(combine) > 0 else 0)
    combine_lo = parse_int(combine[1] if len(combine) > 1 else 0)
    other_hi = parse_int(other[0] if len(other) > 0 else 0)
    other_lo = parse_int(other[1] if len(other) > 1 else 0)
    return (
        image_physical(image),
        image_kseg0(kseg0),
        parse_int(state.get("fmt")),
        parse_int(state.get("siz")),
        parse_int(state.get("tile")),
        parse_int(state.get("width")),
        parse_int(state.get("height")),
        combine_hi,
        combine_lo,
        other_hi,
        other_lo,
        parse_int(state.get("env")),
        parse_int(state.get("color_image")),
    )


def draw_op_state_key(state: dict[str, Any]) -> tuple[int, ...]:
    return state_key(state)


def key_to_summary(key: tuple[int, ...], aggregate: dict[str, Any]) -> dict[str, Any]:
    (
        image,
        kseg0,
        fmt,
        siz,
        tile,
        width,
        height,
        combine_hi,
        combine_lo,
        other_hi,
        other_lo,
        env,
        color_image,
    ) = key
    frames = aggregate["frames"]
    return {
        "image": hexn(image, 6),
        "image_kseg0": hexn(kseg0, 8) if kseg0 else "0x00000000",
        "fmt": fmt,
        "siz": siz,
        "tile": tile,
        "width": width,
        "height": height,
        "combine": [hexn(combine_hi, 8), hexn(combine_lo, 8)],
        "other": [hexn(other_hi, 8), hexn(other_lo, 8)],
        "env": hexn(env, 8),
        "color_image": hexn(color_image, 6),
        "draws": aggregate["draws"],
        "records": aggregate["records"],
        "frames": len(frames),
        "first_frame": min(frames) if frames else None,
        "last_frame": max(frames) if frames else None,
        "op_mask": hexn(aggregate["op_mask"], 16),
    }


def draw_hit_summary(
    sequence: int,
    frame: int,
    draw_op: dict[str, Any],
    key: tuple[int, ...],
    bbox: tuple[float, float, float, float],
    coverage_bbox: tuple[float, float, float, float] | None,
    pixel_count: int,
    coverage_model: str,
) -> dict[str, Any]:
    aggregate = {
        "draws": 1,
        "records": 1,
        "frames": {frame},
        "op_mask": parse_int(draw_op.get("op")),
    }
    row = key_to_summary(key, aggregate)
    row.update(
        {
            "sequence": sequence,
            "frame": frame,
            "addr": draw_op.get("addr"),
            "op": draw_op.get("op"),
            "bbox": rect_list(bbox),
            "coverage_bbox": rect_list(coverage_bbox),
            "coverage_model": coverage_model,
            "pixel_count": pixel_count,
        }
    )
    return row


def final_owner_summary(
    final_owners: dict[int, tuple[tuple[int, ...], dict[str, Any]]],
    region_area: float,
    top: int,
    screenshot_sampler: dict[str, Any] | None,
    compare_screenshot_sampler: dict[str, Any] | None,
) -> dict[str, Any]:
    aggregates: dict[tuple[int, ...], dict[str, Any]] = defaultdict(
        lambda: {
            "final_pixels": 0,
            "frames": set(),
            "op_mask": 0,
            "sequences": set(),
            "last_hit": None,
            "pixels": set(),
        }
    )
    for pixel, (key, hit) in final_owners.items():
        aggregate = aggregates[key]
        aggregate["final_pixels"] += 1
        aggregate["frames"].add(int(hit["frame"]))
        aggregate["op_mask"] |= parse_int(hit.get("op"))
        aggregate["sequences"].add(int(hit["sequence"]))
        aggregate["pixels"].add(pixel)
        last_hit = aggregate["last_hit"]
        if last_hit is None or int(hit["sequence"]) > int(last_hit["sequence"]):
            aggregate["last_hit"] = hit

    rows: list[dict[str, Any]] = []
    for key, aggregate in aggregates.items():
        row = key_to_summary(
            key,
            {
                "draws": len(aggregate["sequences"]),
                "records": len(aggregate["sequences"]),
                "frames": aggregate["frames"],
                "op_mask": aggregate["op_mask"],
            },
        )
        final_pixels = int(aggregate["final_pixels"])
        row.update(
            {
                "final_pixels": final_pixels,
                "final_region_pct": 100.0 * final_pixels / region_area if region_area else 0.0,
                "owner_hits": len(aggregate["sequences"]),
                "last_sequence": int(aggregate["last_hit"]["sequence"]) if aggregate["last_hit"] else None,
                "last_hit": aggregate["last_hit"],
            }
        )
        color_stats = color_stats_for_pixels(aggregate["pixels"], screenshot_sampler)
        if color_stats is not None:
            row["screenshot_color"] = color_stats
        compare_color_stats = color_stats_for_pixels(aggregate["pixels"], compare_screenshot_sampler)
        if compare_color_stats is not None:
            row["compare_screenshot_color"] = compare_color_stats
        if color_stats is not None and compare_color_stats is not None:
            row["compare_minus_screenshot"] = color_delta(color_stats, compare_color_stats)
        rows.append(row)

    rows.sort(
        key=lambda item: (
            -int(item["final_pixels"]),
            -(int(item["last_sequence"]) if item["last_sequence"] is not None else -1),
            item["image_kseg0"],
        )
    )
    return {
        "final_owner_pixels": len(final_owners),
        "final_owner_pct": 100.0 * len(final_owners) / region_area if region_area else 0.0,
        "final_owner_states": len(rows),
        "screenshot": (
            {
                "path": screenshot_sampler["path"],
                "image_size": list(screenshot_sampler["image_size"]),
                "logical_size": list(screenshot_sampler["logical_size"]),
                "frame_mode": screenshot_sampler["frame_mode"],
                "frame_bbox": list(screenshot_sampler["frame_bbox"]),
                "active_threshold": screenshot_sampler["active_threshold"],
            }
            if screenshot_sampler is not None
            else None
        ),
        "compare_screenshot": (
            {
                "path": compare_screenshot_sampler["path"],
                "image_size": list(compare_screenshot_sampler["image_size"]),
                "logical_size": list(compare_screenshot_sampler["logical_size"]),
                "frame_mode": compare_screenshot_sampler["frame_mode"],
                "frame_bbox": list(compare_screenshot_sampler["frame_bbox"]),
                "active_threshold": compare_screenshot_sampler["active_threshold"],
            }
            if compare_screenshot_sampler is not None
            else None
        ),
        "top_final_owners": rows[:top],
    }


def draw_region_summary(
    region: tuple[float, float, float, float],
    aggregates: dict[tuple[int, ...], dict[str, Any]],
    top: int,
    hits: list[dict[str, Any]],
    final_owners: dict[int, tuple[tuple[int, ...], dict[str, Any]]],
    stack_limit: int,
    coverage_model: str,
    screenshot_sampler: dict[str, Any] | None,
    compare_screenshot_sampler: dict[str, Any] | None,
) -> dict[str, Any]:
    region_area = rect_area(region)
    rows: list[dict[str, Any]] = []
    total_overlap = 0.0
    total_draws = 0
    covered_pixels: set[int] = set()
    for key, aggregate in aggregates.items():
        overlap_area = float(aggregate["overlap_area"])
        if overlap_area <= 0.0:
            continue
        state_pixels = aggregate["pixels"]
        covered_pixels.update(state_pixels)
        total_overlap += overlap_area
        total_draws += int(aggregate["draws"])
        row = key_to_summary(key, aggregate)
        row.update(
            {
                "overlap_area": round(overlap_area, 3),
                "region_area_pct": 100.0 * overlap_area / region_area if region_area else 0.0,
                "unique_pixels": len(state_pixels),
                "unique_region_pct": 100.0 * len(state_pixels) / region_area if region_area else 0.0,
                "bbox_union": rect_list(aggregate["bbox_union"]),
                "examples": aggregate["examples"][:5],
            }
        )
        rows.append(row)
    rows.sort(key=lambda item: (-float(item["overlap_area"]), -int(item["draws"]), item["image_kseg0"]))
    first_hits: list[dict[str, Any]] = []
    last_hits: list[dict[str, Any]] = []
    if stack_limit > 0:
        first_hits = hits[:stack_limit]
        last_hits = hits[-stack_limit:]

    summary = {
        "coverage_model": coverage_model,
        "region": rect_list(region),
        "region_area": region_area,
        "overlap_area_sum": round(total_overlap, 3),
        "overlap_area_sum_pct": 100.0 * total_overlap / region_area if region_area else 0.0,
        "unique_covered_pixels": len(covered_pixels),
        "unique_covered_pct": 100.0 * len(covered_pixels) / region_area if region_area else 0.0,
        "overlapping_draws": total_draws,
        "overlapping_states": len(rows),
        "ordered_hit_count": len(hits),
        "first_hits": first_hits,
        "last_hits": last_hits,
        "top_states": rows[:top],
    }
    summary.update(
        final_owner_summary(
            final_owners,
            region_area,
            top,
            screenshot_sampler,
            compare_screenshot_sampler,
        )
    )
    return summary


def load_summary(
    path: Path,
    known_images: set[int],
    top: int,
    min_draws: int,
    stack_limit: int,
    coverage_model: str,
    screenshot_sampler: dict[str, Any] | None = None,
    compare_screenshot_sampler: dict[str, Any] | None = None,
    regions: dict[str, tuple[float, float, float, float]] | None = None,
) -> dict[str, Any]:
    frame_counter: Counter[int] = Counter()
    op_counts: Counter[str] = Counter()
    truncated_ops: Counter[str] = Counter()
    draw_states: dict[tuple[int, ...], dict[str, Any]] = defaultdict(
        lambda: {"draws": 0, "records": 0, "frames": set(), "op_mask": 0}
    )
    records = 0
    command_count = 0
    truncated_records = 0
    summary_truncated_records = 0
    records_with_draw_state = 0
    total_draws = 0
    zero_image_draws = 0
    draw_ops_total = 0
    draw_ops_valid = 0
    draw_ops_invalid = 0
    draw_op_truncated_records = 0
    draw_op_bbox_union: tuple[float, float, float, float] | None = None
    draw_region_aggregates: dict[str, dict[tuple[int, ...], dict[str, Any]]] = {
        name: defaultdict(lambda: {"draws": 0, "records": 0, "frames": set(), "op_mask": 0, "overlap_area": 0.0, "pixels": set(), "bbox_union": None, "examples": []})
        for name in (regions or {})
    }
    draw_region_hits: dict[str, list[dict[str, Any]]] = {name: [] for name in (regions or {})}
    draw_region_final_owners: dict[str, dict[int, tuple[tuple[int, ...], dict[str, Any]]]] = {
        name: {} for name in (regions or {})
    }
    draw_sequence = 0

    with path.open("r", encoding="utf-8") as handle:
        for line_no, line in enumerate(handle, start=1):
            if not line.strip():
                continue
            try:
                record = json.loads(line)
            except json.JSONDecodeError as exc:
                raise ValueError(f"{path}:{line_no}: invalid JSON: {exc}") from exc

            records += 1
            frame = int(record.get("frame", -1))
            frame_counter[frame] += 1
            command_count += int(record.get("commands", 0))
            if int(record.get("truncated", 0)) != 0:
                truncated_records += 1
                truncated_ops[str(record.get("truncated_op", "unknown"))] += 1
            if int(record.get("summary_truncated", 0)) != 0:
                summary_truncated_records += 1
            if int(record.get("draw_op_truncated", 0)) != 0:
                draw_op_truncated_records += 1
            for item in record.get("op_counts", []):
                op_counts[str(item.get("op", "unknown"))] += int(item.get("count", 0))

            states = record.get("draw_state", []) or []
            if states:
                records_with_draw_state += 1
            for state in states:
                draws = int(state.get("draws", 0))
                total_draws += draws
                key = state_key(state)
                aggregate = draw_states[key]
                aggregate["draws"] += draws
                aggregate["records"] += 1
                aggregate["frames"].add(frame)
                aggregate["op_mask"] |= parse_int(state.get("op_mask"))
                if key[0] == 0:
                    zero_image_draws += draws

            for draw_op in record.get("draw_ops", []) or []:
                draw_ops_total += 1
                draw_sequence += 1
                bbox = parse_bbox(draw_op.get("bbox"))
                if int(draw_op.get("valid", 0)) == 0 or bbox is None:
                    draw_ops_invalid += 1
                    continue
                draw_ops_valid += 1
                draw_op_bbox_union = rect_union(draw_op_bbox_union, bbox)
                scissor_raw = record.get("scissor")
                if isinstance(scissor_raw, list) and len(scissor_raw) == 4:
                    scissor = [parse_int(value) for value in scissor_raw]
                else:
                    scissor = [0, 0, 1280, 960]
                for name, region in (regions or {}).items():
                    pixels = draw_op_region_pixels(draw_op, bbox, region, scissor, coverage_model)
                    pixel_count = len(pixels)
                    if pixel_count <= 0:
                        continue
                    coverage_bbox = pixels_bbox(pixels)
                    key = draw_op_state_key(draw_op)
                    aggregate = draw_region_aggregates[name][key]
                    aggregate["draws"] += 1
                    aggregate["records"] += 1
                    aggregate["frames"].add(frame)
                    aggregate["op_mask"] |= parse_int(draw_op.get("op"))
                    aggregate["overlap_area"] += float(pixel_count)
                    aggregate["pixels"].update(pixels)
                    aggregate["bbox_union"] = rect_union(aggregate["bbox_union"], bbox)
                    if len(aggregate["examples"]) < 8:
                        aggregate["examples"].append(
                            {
                                "addr": draw_op.get("addr"),
                                "op": draw_op.get("op"),
                                "bbox": rect_list(bbox),
                                "coverage_bbox": rect_list(coverage_bbox),
                                "pixel_count": pixel_count,
                            }
                        )
                    hit = draw_hit_summary(
                        draw_sequence,
                        frame,
                        draw_op,
                        key,
                        bbox,
                        coverage_bbox,
                        pixel_count,
                        coverage_model,
                    )
                    draw_region_hits[name].append(hit)
                    for pixel in pixels:
                        draw_region_final_owners[name][pixel] = (key, hit)

    state_summaries = [
        key_to_summary(key, aggregate)
        for key, aggregate in draw_states.items()
        if aggregate["draws"] >= min_draws
    ]
    state_summaries.sort(key=lambda item: (-int(item["draws"]), item["image_kseg0"], item["combine"], item["other"]))

    known_physical = {image_physical(value) for value in known_images}
    known_matches = [item for item in state_summaries if image_physical(parse_int(item["image"])) in known_physical]
    known_matches.sort(key=lambda item: (-int(item["draws"]), item["image_kseg0"], item["combine"], item["other"]))

    warnings: list[str] = []
    if total_draws and zero_image_draws == total_draws:
        warnings.append(
            "all draw states have image zero; the trace window probably began after texture state was set"
        )
    elif total_draws and zero_image_draws:
        warnings.append(f"{zero_image_draws}/{total_draws} draw ops used zero texture image state")
    if truncated_records:
        warnings.append(
            f"{truncated_records} command-buffer records were truncated; inspect truncated_op and consider a wider/parser-specific probe"
        )
    if summary_truncated_records:
        warnings.append(f"{summary_truncated_records} records exceeded the per-buffer draw-state summary cap")

    frames = sorted(frame for frame in frame_counter if frame >= 0)
    return {
        "path": str(path),
        "records": records,
        "frames": {
            "count": len(frames),
            "first": frames[0] if frames else None,
            "last": frames[-1] if frames else None,
            "records_per_frame_top": [
                {"frame": frame, "records": count}
                for frame, count in frame_counter.most_common(10)
                if frame >= 0
            ],
        },
        "commands": command_count,
        "truncated_records": truncated_records,
        "summary_truncated_records": summary_truncated_records,
        "truncated_ops_top": [
            {"op": op, "records": count} for op, count in truncated_ops.most_common(10)
        ],
        "op_counts_top": [
            {"op": op, "count": count} for op, count in op_counts.most_common(32)
        ],
        "draw": {
            "records_with_draw_state": records_with_draw_state,
            "total_draws": total_draws,
            "unique_states": len(draw_states),
            "zero_image_draws": zero_image_draws,
            "top_states": state_summaries[:top],
            "known_image_matches": known_matches[:top],
        },
        "draw_ops": {
            "records_with_draw_ops": draw_ops_total,
            "valid": draw_ops_valid,
            "invalid": draw_ops_invalid,
            "truncated_records": draw_op_truncated_records,
            "bbox_union": rect_list(draw_op_bbox_union),
            "regions": {
                name: draw_region_summary(
                    region,
                    draw_region_aggregates[name],
                    top,
                    draw_region_hits[name],
                    draw_region_final_owners[name],
                    stack_limit,
                    coverage_model,
                    screenshot_sampler,
                    compare_screenshot_sampler,
                )
                for name, region in (regions or {}).items()
            },
        },
        "warnings": warnings,
    }


def print_human(summary: dict[str, Any]) -> None:
    frames = summary["frames"]
    print(f"path: {summary['path']}")
    print(
        "records: "
        f"{summary['records']}  frames: {frames['first']}..{frames['last']} "
        f"({frames['count']} unique)  commands: {summary['commands']}"
    )
    print(
        "truncation: "
        f"records={summary['truncated_records']} "
        f"summary_records={summary['summary_truncated_records']}"
    )
    if summary["truncated_ops_top"]:
        print("truncated ops:")
        for item in summary["truncated_ops_top"]:
            print(f"  {item['op']}: {item['records']}")
    print("top op counts:")
    for item in summary["op_counts_top"][:16]:
        print(f"  {item['op']}: {item['count']}")

    draw = summary["draw"]
    print(
        "draw states: "
        f"{draw['unique_states']} unique, {draw['total_draws']} draws, "
        f"{draw['records_with_draw_state']} records with draw state"
    )
    if draw["zero_image_draws"]:
        print(f"zero-image draws: {draw['zero_image_draws']}")

    draw_ops = summary.get("draw_ops") or {}
    if draw_ops.get("records_with_draw_ops"):
        print(
            "draw ops: "
            f"{draw_ops['records_with_draw_ops']} recorded, "
            f"{draw_ops['valid']} valid bboxes, {draw_ops['invalid']} invalid, "
            f"bbox_union={draw_ops.get('bbox_union')}"
        )
        if draw_ops.get("truncated_records"):
            print(f"draw-op truncated records: {draw_ops['truncated_records']}")
        for name, region in (draw_ops.get("regions") or {}).items():
            print(
                f"region {name}: "
                f"model={region.get('coverage_model', 'bbox')} "
                f"draws={region['overlapping_draws']} states={region['overlapping_states']} "
                f"unique={region['unique_covered_pixels']}/{int(region['region_area'])} "
                f"({region['unique_covered_pct']:.3f}%), "
                f"overlap_area_sum={region['overlap_area_sum']} "
                f"({region['overlap_area_sum_pct']:.3f}% summed)"
            )
            if region.get("final_owner_pixels"):
                print(
                    "  final owners: "
                    f"pixels={region['final_owner_pixels']}/{int(region['region_area'])} "
                    f"({region['final_owner_pct']:.3f}%) "
                    f"states={region['final_owner_states']}"
                )
                for owner in region.get("top_final_owners", [])[:5]:
                    last_hit = owner.get("last_hit") or {}
                    color = owner.get("screenshot_color") or {}
                    mean_rgb = color.get("mean_rgb") or {}
                    compare_color = owner.get("compare_screenshot_color") or {}
                    compare_mean_rgb = compare_color.get("mean_rgb") or {}
                    delta = owner.get("compare_minus_screenshot") or {}
                    color_text = ""
                    if mean_rgb:
                        color_text = (
                            f" stock_mean_rgb={mean_rgb.get('r'):.3f},"
                            f"{mean_rgb.get('g'):.3f},{mean_rgb.get('b'):.3f}"
                            f" luma={color.get('mean_luma'):.3f}"
                        )
                    if compare_mean_rgb:
                        color_text += (
                            f" compare_mean_rgb={compare_mean_rgb.get('r'):.3f},"
                            f"{compare_mean_rgb.get('g'):.3f},{compare_mean_rgb.get('b'):.3f}"
                            f" compare_luma={compare_color.get('mean_luma'):.3f}"
                        )
                    if delta:
                        color_text += (
                            f" delta_luma={delta.get('mean_luma_delta'):.3f}"
                            f" delta_abs_rgb={delta.get('mean_abs_rgb_delta'):.3f}"
                        )
                    print(
                        "    "
                        f"final_pixels={owner['final_pixels']} ({owner['final_region_pct']:.3f}%) "
                        f"hits={owner['owner_hits']} last_seq={owner['last_sequence']} "
                        f"image={owner['image_kseg0']} fmt/siz={owner['fmt']}/{owner['siz']} "
                        f"tile={owner['tile']} combine={owner['combine'][0]}/{owner['combine'][1]} "
                        f"other={owner['other'][0]}/{owner['other'][1]} "
                        f"env={owner['env']} last_bbox={last_hit.get('coverage_bbox')}"
                        f"{color_text}"
                    )
            for state in region.get("top_states", [])[:5]:
                print(
                    "  "
                    f"unique={state['unique_pixels']} ({state['unique_region_pct']:.3f}%) "
                    f"overlap={state['overlap_area']:.3f} draws={state['draws']} "
                    f"image={state['image_kseg0']} fmt/siz={state['fmt']}/{state['siz']} "
                    f"wh={state['width']}x{state['height']} "
                    f"combine={state['combine'][0]}/{state['combine'][1]} "
                    f"other={state['other'][0]}/{state['other'][1]} "
                    f"env={state['env']} bbox_union={state['bbox_union']}"
                )
            if region.get("last_hits"):
                print(f"  last {len(region['last_hits'])} ordered hits:")
                for hit in region["last_hits"]:
                    print(
                        "    "
                        f"seq={hit['sequence']} frame={hit['frame']} addr={hit['addr']} "
                        f"op={hit['op']} image={hit['image_kseg0']} "
                        f"fmt/siz={hit['fmt']}/{hit['siz']} tile={hit['tile']} "
                        f"combine={hit['combine'][0]}/{hit['combine'][1]} "
                        f"other={hit['other'][0]}/{hit['other'][1]} "
                        f"env={hit['env']} pixels={hit['pixel_count']} "
                        f"coverage={hit['coverage_bbox']}"
                    )

    def print_states(title: str, states: list[dict[str, Any]]) -> None:
        if not states:
            return
        print(title)
        for state in states:
            print(
                "  "
                f"draws={state['draws']} frames={state['first_frame']}..{state['last_frame']} "
                f"image={state['image_kseg0']} fmt/siz={state['fmt']}/{state['siz']} "
                f"tile={state['tile']} wh={state['width']}x{state['height']} "
                f"combine={state['combine'][0]}/{state['combine'][1]} "
                f"other={state['other'][0]}/{state['other'][1]} "
                f"env={state['env']} color={state['color_image']} "
                f"op_mask={state['op_mask']}"
            )

    print_states("known image matches:", draw["known_image_matches"])
    print_states("top draw states:", draw["top_states"])
    if summary["warnings"]:
        print("warnings:")
        for warning in summary["warnings"]:
            print(f"  {warning}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("sidecar", type=Path, help="rdp_command_stream.jsonl path")
    parser.add_argument("--known-image", action="append", default=[], help="candidate image address to highlight")
    parser.add_argument("--route", type=Path, help="route JSON; enables draw-op overlap summaries for visual regions")
    parser.add_argument("--top", type=int, default=20, help="top draw states to print")
    parser.add_argument("--min-draws", type=int, default=1, help="minimum draws for a state to be included")
    parser.add_argument("--stack-limit", type=int, default=8, help="first/last ordered ROI draw hits to retain per region")
    parser.add_argument(
        "--coverage-model",
        choices=("bbox", "span"),
        default="bbox",
        help="route-region ownership model: bbox is the historical conservative model; span uses triangle setup where available",
    )
    parser.add_argument("--screenshot", type=Path, help="stock screenshot to sample for final-owner color stats")
    parser.add_argument(
        "--screenshot-logical-size",
        type=parse_size,
        help="logical coordinate size for --screenshot, as WxH or W,H; defaults to route visual_logical_size",
    )
    parser.add_argument(
        "--screenshot-frame",
        choices=("active", "full"),
        help="logical frame for --screenshot; defaults to route visual_baseline_logical_frame, then full",
    )
    parser.add_argument(
        "--compare-screenshot",
        type=Path,
        help="second screenshot sampled over the same final-owner masks; useful for native-minus-stock color deltas",
    )
    parser.add_argument(
        "--compare-screenshot-logical-size",
        type=parse_size,
        help="logical coordinate size for --compare-screenshot; defaults to --screenshot-logical-size or route visual_logical_size",
    )
    parser.add_argument(
        "--compare-screenshot-frame",
        choices=("active", "full"),
        help="logical frame for --compare-screenshot; defaults to route visual_test_logical_frame, then --screenshot-frame",
    )
    parser.add_argument("--json", action="store_true", help="emit machine-readable JSON")
    args = parser.parse_args()

    known_images = {parse_int(value) for value in args.known_image}
    regions = route_regions(args.route)
    route_metadata = route_visual_metadata(args.route)
    route_size = route_metadata["logical_size"]
    logical_size = args.screenshot_logical_size or route_size
    compare_logical_size = args.compare_screenshot_logical_size or logical_size
    screenshot_frame = args.screenshot_frame or route_metadata["baseline_frame"] or "full"
    compare_screenshot_frame = (
        args.compare_screenshot_frame
        or route_metadata["test_frame"]
        or screenshot_frame
    )
    screenshot_sampler = load_screenshot_sampler(
        args.screenshot,
        logical_size,
        frame_mode=screenshot_frame,
    )
    compare_screenshot_sampler = load_screenshot_sampler(
        args.compare_screenshot,
        compare_logical_size,
        frame_mode=compare_screenshot_frame,
    )
    summary = load_summary(
        args.sidecar,
        known_images,
        args.top,
        args.min_draws,
        max(0, args.stack_limit),
        args.coverage_model,
        screenshot_sampler,
        compare_screenshot_sampler,
        regions,
    )
    if args.json:
        print(json.dumps(summary, indent=2, sort_keys=True))
    else:
        print_human(summary)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
