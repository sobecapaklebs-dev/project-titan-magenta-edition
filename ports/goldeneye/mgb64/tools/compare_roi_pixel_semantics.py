#!/usr/bin/env python3
"""Summarize stock/native color semantics inside route visual ROIs.

This is a read-only screenshot analysis tool. It aligns stock and native images
with the route logical viewport policy, then reports color/luma statistics for
the same changed pixels used by visual regression comparisons.
"""

from __future__ import annotations

import argparse
from collections import Counter
import json
import sys
from pathlib import Path
from typing import Any

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
import compare_actor_masked_visual as actor_masked  # noqa: E402
import compare_screenshots as screenshots  # noqa: E402


Pixel = tuple[int, int, int]


def load_json(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def route_regions(route: dict[str, Any]) -> dict[str, tuple[int, int, int, int]]:
    regions: dict[str, tuple[int, int, int, int]] = {}
    for item in route.get("visual_regions", route.get("regions", [])):
        if not isinstance(item, dict) or not item.get("name"):
            continue
        roi = item.get("roi")
        if not isinstance(roi, list) or len(roi) != 4:
            continue
        try:
            x, y, w, h = [int(value) for value in roi]
        except (TypeError, ValueError):
            continue
        regions[str(item["name"])] = (x, y, w, h)
    return regions


def route_excludes(route: dict[str, Any], regions: dict[str, tuple[int, int, int, int]]) -> list[dict[str, Any]]:
    excludes: list[dict[str, Any]] = []
    for name in route.get("visual_mask_exclude_regions", []) or []:
        roi = regions.get(str(name))
        if roi is not None:
            excludes.append({"name": str(name), "roi": list(roi)})
    return excludes


def aligned_images(
    route: dict[str, Any],
    stock_image: Path,
    native_image: Path,
    active_threshold: int,
) -> tuple[Image.Image, Image.Image, dict[str, Any]]:
    source_stock = Image.open(stock_image).convert("RGB")
    source_native = Image.open(native_image).convert("RGB")

    logical_size = tuple(route.get("visual_logical_size", [320, 240]))
    logical_viewport = tuple(route.get("visual_logical_viewport", [0, 10, 320, 220]))
    baseline_frame = route.get("visual_baseline_logical_frame", "active")
    test_frame = route.get("visual_test_logical_frame", "full")

    stock_crop = screenshots.logical_crop_bbox(
        source_stock,
        active_threshold,
        logical_size,
        logical_viewport,
        baseline_frame,
    )
    native_crop = screenshots.logical_crop_bbox(
        source_native,
        active_threshold,
        logical_size,
        logical_viewport,
        test_frame,
    )
    stock = screenshots.crop_bbox(source_stock, stock_crop)
    native = screenshots.crop_bbox(source_native, native_crop)
    resized_native = False
    if native.size != stock.size:
        resampling = getattr(Image, "Resampling", Image).BILINEAR
        native = native.resize(stock.size, resampling)
        resized_native = True

    alignment = {
        "source_size": {
            "stock": list(source_stock.size),
            "native": list(source_native.size),
        },
        "logical_viewport": {
            "logical_size": list(logical_size),
            "viewport": list(logical_viewport),
            "baseline_frame": baseline_frame,
            "test_frame": test_frame,
            "crop": {
                "stock": list(stock_crop),
                "native": list(native_crop),
            },
        },
        "aligned_size": list(stock.size),
        "resized_native_to_stock": resized_native,
        "presentation": {
            "stock": screenshots.presentation_metrics(source_stock, active_threshold),
            "native": screenshots.presentation_metrics(source_native, active_threshold),
        },
    }
    return stock, native, alignment


def luma(pixel: Pixel) -> float:
    r, g, b = pixel
    return 0.2126 * r + 0.7152 * g + 0.0722 * b


def changed(a: Pixel, b: Pixel) -> bool:
    return abs(a[0] - b[0]) + abs(a[1] - b[1]) + abs(a[2] - b[2]) > screenshots.DIFF_THRESHOLD


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


def color_class(pixel: Pixel) -> str:
    r, g, b = pixel
    lum = luma(pixel)
    spread = max(pixel) - min(pixel)
    if lum < 12:
        return "black"
    if r > 210 and g > 210 and b > 190:
        return "near_white"
    if spread <= 12:
        return "gray"
    if b > 60 and b >= r + 24 and b >= g + 8:
        return "blue_sky"
    if r > 90 and r >= g + 28 and r >= b + 28:
        return "red_warm"
    if r > 110 and g > 80 and b < min(r, g) - 20:
        return "yellow_warm"
    if g > 70 and g >= r + 20 and g >= b + 16:
        return "green"
    if lum < 45:
        return "dark_mixed"
    return "mixed"


def quantized_bin(pixel: Pixel, step: int = 32) -> str:
    r, g, b = pixel
    rr = min(255, (r // step) * step)
    gg = min(255, (g // step) * step)
    bb = min(255, (b // step) * step)
    return f"{rr:03d},{gg:03d},{bb:03d}"


def top_counter(counter: Counter[str], total: int, limit: int) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    for key, count in counter.most_common(limit):
        rows.append({
            "key": key,
            "pixels": count,
            "pct": 100.0 * count / total if total else 0.0,
        })
    return rows


def pixel_summary(pixels: list[Pixel], top: int) -> dict[str, Any]:
    rgb = [
        mean([float(pixel[channel]) for pixel in pixels])
        for channel in range(3)
    ]
    lumas = [luma(pixel) for pixel in pixels]
    classes = Counter(color_class(pixel) for pixel in pixels)
    bins = Counter(quantized_bin(pixel) for pixel in pixels)
    total = len(pixels)
    return {
        "pixels": total,
        "mean_rgb": rgb,
        "luma": summarize_values(lumas),
        "classes": top_counter(classes, total, top),
        "top_bins": top_counter(bins, total, top),
    }


def pair_summary(pairs: list[tuple[Pixel, Pixel]], top: int) -> dict[str, Any]:
    total = len(pairs)
    changed_pairs = [(stock, native) for stock, native in pairs if changed(stock, native)]
    changed_total = len(changed_pairs)
    stock_pixels = [stock for stock, _ in pairs]
    native_pixels = [native for _, native in pairs]
    changed_stock = [stock for stock, _ in changed_pairs]
    changed_native = [native for _, native in changed_pairs]
    rgb_delta = [
        [
            float(native[channel] - stock[channel])
            for stock, native in changed_pairs
        ]
        for channel in range(3)
    ]
    luma_delta = [luma(native) - luma(stock) for stock, native in changed_pairs]
    abs_rgb_delta = [
        [
            float(abs(native[channel] - stock[channel]))
            for stock, native in changed_pairs
        ]
        for channel in range(3)
    ]
    transitions = Counter(
        f"{color_class(stock)}->{color_class(native)}"
        for stock, native in changed_pairs
    )
    native_brighter = sum(1 for value in luma_delta if value > 8.0)
    native_darker = sum(1 for value in luma_delta if value < -8.0)
    native_bluer = sum(
        1
        for stock, native in changed_pairs
        if native[2] - stock[2] > 12 and native[2] - native[0] > 16
    )
    native_warmer = sum(
        1
        for stock, native in changed_pairs
        if native[0] - stock[0] > 12 and native[0] - native[2] > 16
    )

    return {
        "sampled_pixels": total,
        "changed_pixels": changed_total,
        "changed_pct": 100.0 * changed_total / total if total else 0.0,
        "stock": pixel_summary(stock_pixels, top),
        "native": pixel_summary(native_pixels, top),
        "changed_stock": pixel_summary(changed_stock, top),
        "changed_native": pixel_summary(changed_native, top),
        "changed_delta": {
            "mean_rgb": [mean(channel) for channel in rgb_delta],
            "mean_abs_rgb": [mean(channel) for channel in abs_rgb_delta],
            "luma": summarize_values(luma_delta),
            "native_brighter_pixels": native_brighter,
            "native_brighter_pct": 100.0 * native_brighter / changed_total if changed_total else 0.0,
            "native_darker_pixels": native_darker,
            "native_darker_pct": 100.0 * native_darker / changed_total if changed_total else 0.0,
            "native_bluer_pixels": native_bluer,
            "native_bluer_pct": 100.0 * native_bluer / changed_total if changed_total else 0.0,
            "native_warmer_pixels": native_warmer,
            "native_warmer_pct": 100.0 * native_warmer / changed_total if changed_total else 0.0,
            "class_transitions": top_counter(transitions, changed_total, top),
        },
    }


def metric_for_region(
    stock: Image.Image,
    native: Image.Image,
    region: tuple[int, int, int, int] | None,
    excludes: list[dict[str, Any]],
    top: int,
) -> dict[str, Any]:
    points, excluded = actor_masked.points_for_region(
        stock.size[0],
        stock.size[1],
        region=region,
        exclude_regions=excludes,
    )
    stock_px = stock.load()
    native_px = native.load()
    pairs = [(stock_px[x, y], native_px[x, y]) for x, y in points]
    source_pixels = stock.size[0] * stock.size[1] if region is None else region[2] * region[3]
    return {
        **pair_summary(pairs, top),
        "source_pixels": source_pixels,
        "excluded_pixels": excluded,
        "excluded_pct": 100.0 * excluded / source_pixels if source_pixels else 0.0,
    }


def compare(args: argparse.Namespace) -> tuple[dict[str, Any], int]:
    failures: list[str] = []
    route = load_json(args.route_json)
    route_name = str(route.get("name") or "unknown")
    if route_name == "unknown":
        failures.append("route JSON is missing name")

    stock_image = args.stock_image or args.base_case_dir / f"stock_{route_name}.ppm"
    native_image = args.native_image or args.base_case_dir / f"native_{route_name}.bmp"
    if not stock_image.exists():
        failures.append(f"missing stock image: {stock_image}")
    if not native_image.exists():
        failures.append(f"missing native image: {native_image}")

    regions = route_regions(route)
    excludes = route_excludes(route, regions)
    selected_regions = args.region or ["tower_pane", "projected_impact", "impact_side"]
    for name in selected_regions:
        if name not in regions:
            failures.append(f"route has no visual region: {name}")

    payload: dict[str, Any] = {
        "status": "fail" if failures else "pass",
        "failures": failures,
        "route": route_name,
        "inputs": {
            "stock_image": str(stock_image),
            "native_image": str(native_image),
            "route_json": str(args.route_json),
        },
        "regions": {},
        "interpretation": [],
    }
    if failures:
        return payload, 1

    stock, native, alignment = aligned_images(route, stock_image, native_image, args.active_threshold)
    payload["alignment"] = alignment
    payload["full"] = metric_for_region(stock, native, None, excludes, args.top)
    for name in selected_regions:
        unmasked = metric_for_region(stock, native, regions[name], [], args.top)
        route_masked = metric_for_region(stock, native, regions[name], excludes, args.top)
        payload["regions"][name] = {
            "unmasked": unmasked,
            "route_masked": route_masked,
        }

    for name in selected_regions:
        metrics = payload["regions"][name]["unmasked"]
        route_masked = payload["regions"][name]["route_masked"]
        delta = metrics["changed_delta"]
        payload["interpretation"].append(
            f"{name}: unmasked_changed={metrics['changed_pct']:.3f}% "
            f"route_masked_changed={route_masked['changed_pct']:.3f}% "
            f"luma_delta={delta['luma']['mean']:.2f} "
            f"native_bluer={delta['native_bluer_pct']:.3f}% "
            f"native_brighter={delta['native_brighter_pct']:.3f}% "
            f"native_darker={delta['native_darker_pct']:.3f}%"
        )

    return payload, 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--route-json", type=Path, required=True)
    parser.add_argument("--base-case-dir", type=Path, required=True)
    parser.add_argument("--stock-image", type=Path)
    parser.add_argument("--native-image", type=Path)
    parser.add_argument("--active-threshold", type=screenshots.channel_threshold, default=0)
    parser.add_argument("--region", action="append")
    parser.add_argument("--top", type=int, default=8)
    parser.add_argument("--json-out", type=Path)
    args = parser.parse_args(argv)

    payload, status = compare(args)
    encoded = json.dumps(payload, indent=2, sort_keys=True) + "\n"
    if args.json_out:
        args.json_out.parent.mkdir(parents=True, exist_ok=True)
        args.json_out.write_text(encoded, encoding="utf-8")
    print(encoded, end="")
    return status


if __name__ == "__main__":
    raise SystemExit(main())
