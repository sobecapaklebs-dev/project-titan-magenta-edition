#!/usr/bin/env python3
"""Compare a native control-change footprint against stock/native mismatch.

The tool aligns a stock screenshot and a default native screenshot using the
route's logical viewport policy, then aligns a native variant screenshot into
that same coordinate system. It reports, per visual ROI, how much of the
stock/native mismatch is inside pixels changed by the native control.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any

from PIL import Image, ImageDraw

sys.path.insert(0, str(Path(__file__).resolve().parent))
import compare_actor_masked_visual as actor_masked  # noqa: E402
import compare_screenshots as screenshots  # noqa: E402


Pixel = tuple[int, int]


def load_json(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def route_regions(route: dict[str, Any]) -> dict[str, tuple[int, int, int, int]]:
    out: dict[str, tuple[int, int, int, int]] = {}
    for item in route.get("visual_regions", []):
        if not isinstance(item, dict) or not item.get("name"):
            continue
        roi = item.get("roi")
        if not isinstance(roi, list) or len(roi) != 4:
            continue
        try:
            x, y, w, h = [int(value) for value in roi]
        except (TypeError, ValueError):
            continue
        out[str(item["name"])] = (x, y, w, h)
    return out


def route_excludes(route: dict[str, Any], regions: dict[str, tuple[int, int, int, int]]) -> list[dict[str, Any]]:
    out: list[dict[str, Any]] = []
    for name in route.get("visual_mask_exclude_regions", []) or []:
        roi = regions.get(str(name))
        if roi is not None:
            out.append({"name": str(name), "roi": list(roi)})
    return out


def aligned_images(
    route: dict[str, Any],
    stock_image: Path,
    native_image: Path,
    variant_image: Path,
    active_threshold: int,
) -> tuple[Image.Image, Image.Image, Image.Image, dict[str, Any]]:
    source_stock = Image.open(stock_image).convert("RGB")
    source_native = Image.open(native_image).convert("RGB")
    source_variant = Image.open(variant_image).convert("RGB")

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
    variant = screenshots.crop_bbox(source_variant, native_crop)
    resized_native = False
    resized_variant = False
    if native.size != stock.size:
        resampling = getattr(Image, "Resampling", Image).BILINEAR
        native = native.resize(stock.size, resampling)
        resized_native = True
    if variant.size != stock.size:
        resampling = getattr(Image, "Resampling", Image).BILINEAR
        variant = variant.resize(stock.size, resampling)
        resized_variant = True

    alignment = {
        "source_size": {
            "stock": list(source_stock.size),
            "native": list(source_native.size),
            "variant": list(source_variant.size),
        },
        "logical_viewport": {
            "logical_size": list(logical_size),
            "viewport": list(logical_viewport),
            "baseline_frame": baseline_frame,
            "test_frame": test_frame,
            "crop": {
                "stock": list(stock_crop),
                "native": list(native_crop),
                "variant": list(native_crop),
            },
        },
        "aligned_size": list(stock.size),
        "resized_native_to_stock": resized_native,
        "resized_variant_to_stock": resized_variant,
        "presentation": {
            "stock": screenshots.presentation_metrics(source_stock, active_threshold),
            "native": screenshots.presentation_metrics(source_native, active_threshold),
            "variant": screenshots.presentation_metrics(source_variant, active_threshold),
        },
    }
    return stock, native, variant, alignment


def pixel_changed(a: tuple[int, int, int], b: tuple[int, int, int]) -> bool:
    return abs(a[0] - b[0]) + abs(a[1] - b[1]) + abs(a[2] - b[2]) > screenshots.DIFF_THRESHOLD


def mask_points(
    stock: Image.Image,
    native: Image.Image,
    variant: Image.Image,
    points: list[Pixel],
) -> dict[str, Any]:
    stock_px = stock.load()
    native_px = native.load()
    variant_px = variant.load()
    stock_changed = 0
    control_changed = 0
    overlap = 0
    for x, y in points:
        stock_delta = pixel_changed(stock_px[x, y], native_px[x, y])
        control_delta = pixel_changed(native_px[x, y], variant_px[x, y])
        if stock_delta:
            stock_changed += 1
        if control_delta:
            control_changed += 1
        if stock_delta and control_delta:
            overlap += 1

    total = len(points)
    outside = total - control_changed
    outside_stock_changed = stock_changed - overlap
    return {
        "sampled_pixels": total,
        "stock_changed_pixels": stock_changed,
        "stock_changed_pct": 100.0 * stock_changed / total if total else 0.0,
        "control_changed_pixels": control_changed,
        "control_changed_pct": 100.0 * control_changed / total if total else 0.0,
        "overlap_pixels": overlap,
        "stock_mismatch_covered_pct": 100.0 * overlap / stock_changed if stock_changed else 0.0,
        "control_footprint_dirty_pct": 100.0 * overlap / control_changed if control_changed else 0.0,
        "outside_control_pixels": outside,
        "outside_control_stock_changed_pixels": outside_stock_changed,
        "outside_control_stock_changed_pct": (
            100.0 * outside_stock_changed / outside if outside else 0.0
        ),
    }


def metric_for_region(
    stock: Image.Image,
    native: Image.Image,
    variant: Image.Image,
    region: tuple[int, int, int, int] | None,
    excludes: list[dict[str, Any]],
) -> dict[str, Any]:
    points, excluded = actor_masked.points_for_region(
        stock.size[0],
        stock.size[1],
        region=region,
        exclude_regions=excludes,
    )
    source_pixels = stock.size[0] * stock.size[1] if region is None else region[2] * region[3]
    return {
        **mask_points(stock, native, variant, points),
        "source_pixels": source_pixels,
        "excluded_pixels": excluded,
        "excluded_pct": 100.0 * excluded / source_pixels if source_pixels else 0.0,
    }


def write_heatmap(
    path: Path,
    stock: Image.Image,
    native: Image.Image,
    variant: Image.Image,
    regions: dict[str, tuple[int, int, int, int]],
) -> None:
    width, height = stock.size
    out = Image.new("RGB", stock.size)
    stock_px = stock.load()
    native_px = native.load()
    variant_px = variant.load()
    for y in range(height):
        for x in range(width):
            stock_delta = pixel_changed(stock_px[x, y], native_px[x, y])
            control_delta = pixel_changed(native_px[x, y], variant_px[x, y])
            if stock_delta and control_delta:
                out.putpixel((x, y), (255, 220, 0))
            elif stock_delta:
                out.putpixel((x, y), (180, 0, 0))
            elif control_delta:
                out.putpixel((x, y), (0, 190, 120))
            else:
                r, g, b = native_px[x, y]
                out.putpixel((x, y), (r // 4, g // 4, b // 4))
    draw = ImageDraw.Draw(out)
    for name, roi in regions.items():
        x, y, w, h = roi
        draw.rectangle((x, y, x + w - 1, y + h - 1), outline=(120, 160, 255), width=1)
        draw.text((x + 2, y + 2), name, fill=(120, 160, 255))
    out.save(path)


def compare(args: argparse.Namespace) -> tuple[dict[str, Any], int]:
    failures: list[str] = []
    route = load_json(args.route_json)
    route_name = route.get("name")
    if not route_name:
        failures.append("route JSON is missing name")
        route_name = "unknown"
    stock_image = args.stock_image or args.base_case_dir / f"stock_{route_name}.ppm"
    native_image = args.native_image or args.base_case_dir / f"native_{route_name}.bmp"
    if not stock_image.exists():
        failures.append(f"missing stock image: {stock_image}")
    if not native_image.exists():
        failures.append(f"missing native image: {native_image}")
    if not args.variant_image.exists():
        failures.append(f"missing variant image: {args.variant_image}")

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
        "variant": args.variant_label,
        "inputs": {
            "stock_image": str(stock_image),
            "native_image": str(native_image),
            "variant_image": str(args.variant_image),
            "route_json": str(args.route_json),
        },
        "regions": {},
        "interpretation": [],
    }

    if failures:
        return payload, 1

    stock, native, variant, alignment = aligned_images(
        route,
        stock_image,
        native_image,
        args.variant_image,
        args.active_threshold,
    )
    payload["alignment"] = alignment
    payload["full"] = metric_for_region(stock, native, variant, None, excludes)
    for name in selected_regions:
        payload["regions"][name] = metric_for_region(
            stock,
            native,
            variant,
            regions[name],
            excludes,
        )

    tower = payload["regions"].get("tower_pane", {})
    impact = payload["regions"].get("impact_side", {})
    if tower:
        payload["interpretation"].append(
            "tower_pane control footprint covers "
            f"{tower['stock_mismatch_covered_pct']:.3f}% of stock/native changed pixels"
        )
    if impact:
        payload["interpretation"].append(
            "impact_side control footprint covers "
            f"{impact['stock_mismatch_covered_pct']:.3f}% of stock/native changed pixels"
        )

    if args.heatmap:
        args.heatmap.parent.mkdir(parents=True, exist_ok=True)
        write_heatmap(args.heatmap, stock, native, variant, regions)
        payload["heatmap"] = str(args.heatmap)

    return payload, 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--route-json", type=Path, required=True)
    parser.add_argument("--base-case-dir", type=Path, required=True)
    parser.add_argument("--variant-image", type=Path, required=True)
    parser.add_argument("--variant-label", default="variant")
    parser.add_argument("--stock-image", type=Path)
    parser.add_argument("--native-image", type=Path)
    parser.add_argument("--active-threshold", type=screenshots.channel_threshold, default=0)
    parser.add_argument("--region", action="append")
    parser.add_argument("--heatmap", type=Path)
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
