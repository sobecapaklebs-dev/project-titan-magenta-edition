#!/usr/bin/env python3
"""Infer per-pixel glass source colors required by stock/native screenshots.

This read-only diagnostic inverts a fixed source-over blend:

    output = source * alpha + underlay * (1 - alpha)

Given stock, default-native, and skip-tex654-underlay screenshots, it reports
the source colors that stock and native would require for the same underlay and
alpha. This distinguishes plausible source-color mismatches from impossible
values that point at ordering or framebuffer semantics instead.
"""

from __future__ import annotations

import argparse
from collections import Counter
import json
import math
import sys
from pathlib import Path
from typing import Any

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
import compare_actor_masked_visual as actor_masked  # noqa: E402
import compare_roi_pixel_semantics as roi_semantics  # noqa: E402
import compare_screenshots as screenshots  # noqa: E402


Pixel = tuple[int, int, int]
SourcePixel = tuple[float, float, float]


def load_json(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def route_regions(route: dict[str, Any]) -> dict[str, tuple[int, int, int, int]]:
    return roi_semantics.route_regions(route)


def route_excludes(
    route: dict[str, Any],
    regions: dict[str, tuple[int, int, int, int]],
) -> list[dict[str, Any]]:
    return roi_semantics.route_excludes(route, regions)


def aligned_three_images(
    route: dict[str, Any],
    stock_image: Path,
    default_image: Path,
    underlay_image: Path,
    active_threshold: int,
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

    resized_default = False
    resized_underlay = False
    if default.size != stock.size:
        resampling = getattr(Image, "Resampling", Image).BILINEAR
        default = default.resize(stock.size, resampling)
        resized_default = True
    if underlay.size != stock.size:
        resampling = getattr(Image, "Resampling", Image).BILINEAR
        underlay = underlay.resize(stock.size, resampling)
        resized_underlay = True

    return stock, default, underlay, {
        "source_size": {
            "stock": list(source_stock.size),
            "default": list(source_default.size),
            "underlay": list(source_underlay.size),
        },
        "aligned_size": list(stock.size),
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
        "resized_to_stock": {
            "default": resized_default,
            "underlay": resized_underlay,
        },
    }


def luma(pixel: Pixel | SourcePixel) -> float:
    r, g, b = pixel
    return 0.2126 * r + 0.7152 * g + 0.0722 * b


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


def summarize_values(values: list[float]) -> dict[str, float]:
    return {
        "min": min(values) if values else 0.0,
        "p05": percentile(values, 0.05),
        "mean": mean(values),
        "median": percentile(values, 0.50),
        "p95": percentile(values, 0.95),
        "max": max(values) if values else 0.0,
    }


def clamp_u8(value: float) -> int:
    if not math.isfinite(value):
        return 0
    return min(255, max(0, int(value + 0.5)))


def clip_source(pixel: SourcePixel) -> Pixel:
    return tuple(clamp_u8(value) for value in pixel)  # type: ignore[return-value]


def required_source_pixel(output: Pixel, underlay: Pixel, alpha: float) -> SourcePixel:
    inv_alpha = 1.0 - alpha
    return tuple(
        (float(output[channel]) - float(underlay[channel]) * inv_alpha) / alpha
        for channel in range(3)
    )  # type: ignore[return-value]


def recompose_pixel(source: SourcePixel, underlay: Pixel, alpha: float) -> Pixel:
    inv_alpha = 1.0 - alpha
    return tuple(
        clamp_u8(source[channel] * alpha + float(underlay[channel]) * inv_alpha)
        for channel in range(3)
    )  # type: ignore[return-value]


def final_score(a_pixels: list[Pixel], b_pixels: list[Pixel]) -> dict[str, Any]:
    abs_rgb: list[float] = []
    luma_delta: list[float] = []
    changed_count = 0
    for a, b in zip(a_pixels, b_pixels):
        if roi_semantics.changed(a, b):
            changed_count += 1
        luma_delta.append(luma(b) - luma(a))
        for channel in range(3):
            abs_rgb.append(abs(float(b[channel] - a[channel])))
    total = len(a_pixels)
    return {
        "pixels": total,
        "changed_pixels": changed_count,
        "changed_pct": 100.0 * changed_count / total if total else 0.0,
        "mean_abs_rgb": mean(abs_rgb),
        "luma_delta": summarize_values(luma_delta),
    }


def top_counter(counter: Counter[str], total: int, limit: int) -> list[dict[str, Any]]:
    return [
        {"key": key, "pixels": count, "pct": 100.0 * count / total if total else 0.0}
        for key, count in counter.most_common(limit)
    ]


def shader_summary_from_sample(path: Path | None, region: str, field: str) -> dict[str, Any] | None:
    if path is None:
        return None
    payload = load_json(path)
    region_payload = (payload.get("regions") or {}).get(region) or {}
    sample_summary = region_payload.get("sample_summary") or {}
    summary = sample_summary.get(field)
    if not isinstance(summary, dict):
        return None
    luma_summary = summary.get("luma") or {}
    return {
        "source": str(path),
        "field": field,
        "count": summary.get("count", 0),
        "alpha_counts": summary.get("alpha_counts", {}),
        "luma": {
            "min": float(luma_summary.get("min", 0.0)),
            "mean": float(luma_summary.get("mean", 0.0)),
            "max": float(luma_summary.get("max", 0.0)),
        },
        "unique": summary.get("unique", []),
    }


def summarize_shader_band(
    sources: list[SourcePixel],
    shader_summary: dict[str, Any] | None,
    tolerance: float,
) -> dict[str, Any] | None:
    if shader_summary is None:
        return None
    band = shader_summary["luma"]
    low = float(band["min"]) - tolerance
    high = float(band["max"]) + tolerance
    lumas = [luma(source) for source in sources]
    inside = sum(1 for value in lumas if low <= value <= high)
    below = sum(1 for value in lumas if value < low)
    above = sum(1 for value in lumas if value > high)
    distance = [
        0.0 if low <= value <= high else min(abs(value - low), abs(value - high))
        for value in lumas
    ]
    total = len(sources)
    return {
        "field": shader_summary["field"],
        "expected_luma": shader_summary["luma"],
        "tolerance": tolerance,
        "inside_pixels": inside,
        "inside_pct": 100.0 * inside / total if total else 0.0,
        "below_pixels": below,
        "below_pct": 100.0 * below / total if total else 0.0,
        "above_pixels": above,
        "above_pct": 100.0 * above / total if total else 0.0,
        "mean_abs_luma_distance": mean(distance),
    }


def source_summary(
    sources: list[SourcePixel],
    shader_summary: dict[str, Any] | None,
    shader_tolerance: float,
    top: int,
) -> dict[str, Any]:
    clipped = [clip_source(source) for source in sources]
    lumas = [luma(source) for source in sources]
    clipped_lumas = [luma(pixel) for pixel in clipped]
    total = len(sources)
    in_gamut = sum(1 for source in sources if all(0.0 <= value <= 255.0 for value in source))
    below_zero = sum(1 for source in sources if any(value < 0.0 for value in source))
    above_255 = sum(1 for source in sources if any(value > 255.0 for value in source))
    classes = Counter(roi_semantics.color_class(pixel) for pixel in clipped)
    bins = Counter(roi_semantics.quantized_bin(pixel) for pixel in clipped)
    channels = [
        summarize_values([source[channel] for source in sources])
        for channel in range(3)
    ]
    return {
        "pixels": total,
        "raw_mean_rgb": [
            mean([source[channel] for source in sources])
            for channel in range(3)
        ],
        "raw_channels": channels,
        "raw_luma": summarize_values(lumas),
        "clipped_mean_rgb": [
            mean([float(pixel[channel]) for pixel in clipped])
            for channel in range(3)
        ],
        "clipped_luma": summarize_values(clipped_lumas),
        "in_gamut_pixels": in_gamut,
        "in_gamut_pct": 100.0 * in_gamut / total if total else 0.0,
        "below_zero_pixels": below_zero,
        "below_zero_pct": 100.0 * below_zero / total if total else 0.0,
        "above_255_pixels": above_255,
        "above_255_pct": 100.0 * above_255 / total if total else 0.0,
        "classes": top_counter(classes, total, top),
        "top_bins": top_counter(bins, total, top),
        "shader_luma_band": summarize_shader_band(sources, shader_summary, shader_tolerance),
    }


def source_delta_summary(
    native_sources: list[SourcePixel],
    stock_sources: list[SourcePixel],
) -> dict[str, Any]:
    abs_rgb: list[float] = []
    rgb_delta: list[list[float]] = [[], [], []]
    luma_delta: list[float] = []
    stock_brighter = 0
    stock_darker = 0
    for native, stock in zip(native_sources, stock_sources):
        delta_luma = luma(stock) - luma(native)
        luma_delta.append(delta_luma)
        if delta_luma > 8.0:
            stock_brighter += 1
        if delta_luma < -8.0:
            stock_darker += 1
        for channel in range(3):
            delta = stock[channel] - native[channel]
            rgb_delta[channel].append(delta)
            abs_rgb.append(abs(delta))
    total = len(native_sources)
    return {
        "pixels": total,
        "mean_rgb": [mean(channel) for channel in rgb_delta],
        "mean_abs_rgb": mean(abs_rgb),
        "luma": summarize_values(luma_delta),
        "stock_source_brighter_pixels": stock_brighter,
        "stock_source_brighter_pct": 100.0 * stock_brighter / total if total else 0.0,
        "stock_source_darker_pixels": stock_darker,
        "stock_source_darker_pct": 100.0 * stock_darker / total if total else 0.0,
    }


def pixel_lists_for_points(
    stock: Image.Image,
    default: Image.Image,
    underlay: Image.Image,
    points: set[tuple[int, int]],
) -> tuple[list[Pixel], list[Pixel], list[Pixel]]:
    stock_px = stock.load()
    default_px = default.load()
    underlay_px = underlay.load()
    ordered = sorted(points)
    return (
        [stock_px[x, y] for x, y in ordered],
        [default_px[x, y] for x, y in ordered],
        [underlay_px[x, y] for x, y in ordered],
    )


def recompose_error(
    outputs: list[Pixel],
    sources: list[SourcePixel],
    underlays: list[Pixel],
    alpha: float,
) -> dict[str, Any]:
    recomposed = [
        recompose_pixel(source, underlay, alpha)
        for source, underlay in zip(sources, underlays)
    ]
    return final_score(outputs, recomposed)


def analyze_point_set(
    stock: Image.Image,
    default: Image.Image,
    underlay: Image.Image,
    points: set[tuple[int, int]],
    source_pixels: int,
    excluded_pixels: int,
    alpha: float,
    shader_summary: dict[str, Any] | None,
    shader_tolerance: float,
    top: int,
) -> dict[str, Any]:
    stock_pixels, default_pixels, underlay_pixels = pixel_lists_for_points(
        stock, default, underlay, points
    )
    native_sources = [
        required_source_pixel(output, underlay_px, alpha)
        for output, underlay_px in zip(default_pixels, underlay_pixels)
    ]
    stock_sources = [
        required_source_pixel(output, underlay_px, alpha)
        for output, underlay_px in zip(stock_pixels, underlay_pixels)
    ]
    return {
        "source_pixels": source_pixels,
        "sampled_pixels": len(points),
        "excluded_pixels": excluded_pixels,
        "excluded_pct": 100.0 * excluded_pixels / source_pixels if source_pixels else 0.0,
        "output_stock_vs_native": final_score(stock_pixels, default_pixels),
        "output_stock_vs_underlay": final_score(stock_pixels, underlay_pixels),
        "native_required_source": source_summary(
            native_sources, shader_summary, shader_tolerance, top
        ),
        "stock_required_source": source_summary(
            stock_sources, shader_summary, shader_tolerance, top
        ),
        "required_source_delta_stock_minus_native": source_delta_summary(
            native_sources, stock_sources
        ),
        "recompose_error": {
            "native": recompose_error(default_pixels, native_sources, underlay_pixels, alpha),
            "stock": recompose_error(stock_pixels, stock_sources, underlay_pixels, alpha),
        },
    }


def analyze(args: argparse.Namespace) -> tuple[dict[str, Any], int]:
    failures: list[str] = []
    route = load_json(args.route_json)
    route_name = str(route.get("name") or "unknown")
    if route_name == "unknown":
        failures.append("route JSON is missing name")

    stock_image = args.stock_image or args.base_case_dir / f"stock_{route_name}.ppm"
    default_image = args.default_image or args.base_case_dir / f"native_{route_name}.bmp"
    underlay_image = args.underlay_image
    for label, path in (
        ("stock image", stock_image),
        ("default native image", default_image),
        ("underlay native image", underlay_image),
    ):
        if path is None or not path.exists():
            failures.append(f"missing {label}: {path}")
    if args.settex_sample_json is not None and not args.settex_sample_json.exists():
        failures.append(f"missing settex sample JSON: {args.settex_sample_json}")

    regions = route_regions(route)
    excludes = route_excludes(route, regions)
    selected_regions = args.region or ["tower_pane", "projected_impact", "impact_side"]
    for name in selected_regions:
        if name not in regions:
            failures.append(f"route has no visual region: {name}")

    alpha = args.alpha_u8 / 255.0
    payload: dict[str, Any] = {
        "status": "fail" if failures else "pass",
        "failures": failures,
        "route": route_name,
        "inputs": {
            "route_json": str(args.route_json),
            "stock_image": str(stock_image),
            "default_image": str(default_image),
            "underlay_image": str(underlay_image),
            "settex_sample_json": str(args.settex_sample_json) if args.settex_sample_json else None,
            "shader_field": args.shader_field,
            "alpha_u8": args.alpha_u8,
            "alpha": alpha,
            "model": "output=source*alpha+underlay*(1-alpha)",
        },
        "regions": {},
        "interpretation": [],
    }
    if failures:
        return payload, 1

    stock, default, underlay, alignment = aligned_three_images(
        route, stock_image, default_image, underlay_image, args.active_threshold
    )
    payload["alignment"] = alignment

    for name in selected_regions:
        region = regions[name]
        shader_summary = shader_summary_from_sample(
            args.settex_sample_json, name, args.shader_field
        )
        unmasked_points, unmasked_excluded = actor_masked.points_for_region(
            stock.size[0], stock.size[1], region=region, exclude_regions=[]
        )
        masked_points, masked_excluded = actor_masked.points_for_region(
            stock.size[0], stock.size[1], region=region, exclude_regions=excludes
        )
        source_pixels = region[2] * region[3]
        payload["regions"][name] = {
            "roi": list(region),
            "shader_sample": shader_summary,
            "unmasked": analyze_point_set(
                stock, default, underlay, unmasked_points, source_pixels,
                unmasked_excluded, alpha, shader_summary, args.shader_tolerance,
                args.top,
            ),
            "route_masked": analyze_point_set(
                stock, default, underlay, masked_points, source_pixels,
                masked_excluded, alpha, shader_summary, args.shader_tolerance,
                args.top,
            ),
        }

    for name in selected_regions:
        metrics = payload["regions"][name]["unmasked"]
        native_source = metrics["native_required_source"]
        stock_source = metrics["stock_required_source"]
        delta = metrics["required_source_delta_stock_minus_native"]
        band = stock_source.get("shader_luma_band")
        band_text = ""
        if band is not None:
            band_text = (
                f"; stock_source_in_{args.shader_field}_band="
                f"{band['inside_pct']:.3f}%"
            )
        payload["interpretation"].append(
            f"{name}: native_req_luma={native_source['raw_luma']['mean']:.2f} "
            f"stock_req_luma={stock_source['raw_luma']['mean']:.2f} "
            f"stock_req_gamut={stock_source['in_gamut_pct']:.3f}% "
            f"source_delta_luma={delta['luma']['mean']:.2f} "
            f"source_delta_abs_rgb={delta['mean_abs_rgb']:.2f}{band_text}"
        )
        masked = payload["regions"][name]["route_masked"]
        if masked["excluded_pixels"] > 0:
            masked_stock = masked["stock_required_source"]
            masked_delta = masked["required_source_delta_stock_minus_native"]
            masked_band = masked_stock.get("shader_luma_band")
            masked_band_text = ""
            if masked_band is not None:
                masked_band_text = (
                    f"; stock_source_in_{args.shader_field}_band="
                    f"{masked_band['inside_pct']:.3f}%"
                )
            payload["interpretation"].append(
                f"{name} route_masked: stock_req_luma="
                f"{masked_stock['raw_luma']['mean']:.2f} "
                f"stock_req_gamut={masked_stock['in_gamut_pct']:.3f}% "
                f"source_delta_luma={masked_delta['luma']['mean']:.2f}"
                f"{masked_band_text}"
            )

    return payload, 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--route-json", type=Path, required=True)
    parser.add_argument("--base-case-dir", type=Path, required=True)
    parser.add_argument("--stock-image", type=Path)
    parser.add_argument("--default-image", type=Path)
    parser.add_argument("--underlay-image", type=Path, required=True)
    parser.add_argument("--settex-sample-json", type=Path)
    parser.add_argument("--shader-field", default="shaderL_frag")
    parser.add_argument("--alpha-u8", type=int, default=102)
    parser.add_argument("--shader-tolerance", type=float, default=2.0)
    parser.add_argument("--active-threshold", type=screenshots.channel_threshold, default=0)
    parser.add_argument("--region", action="append")
    parser.add_argument("--top", type=int, default=8)
    parser.add_argument("--json-out", type=Path)
    args = parser.parse_args(argv)
    if args.alpha_u8 <= 0 or args.alpha_u8 > 255:
        parser.error("--alpha-u8 must be in 1..255")
    if not math.isfinite(args.shader_tolerance) or args.shader_tolerance < 0.0:
        parser.error("--shader-tolerance must be a non-negative finite number")

    payload, status = analyze(args)
    encoded = json.dumps(payload, indent=2, sort_keys=True) + "\n"
    if args.json_out:
        args.json_out.parent.mkdir(parents=True, exist_ok=True)
        args.json_out.write_text(encoded, encoding="utf-8")
    print(encoded, end="")
    return status


if __name__ == "__main__":
    raise SystemExit(main())
