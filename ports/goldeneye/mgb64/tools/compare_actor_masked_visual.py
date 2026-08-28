#!/usr/bin/env python3
"""Compare screenshots with named ROI metrics and explicit actor masks.

This complements compare_screenshots.py for visual fixtures where gameplay state
is clean enough for renderer work, but small actor/viewmodel composition deltas
must be excluded from the aggregate without hiding the unmasked ROI evidence.
"""

from __future__ import annotations

import argparse
import json
import math
import sys
from pathlib import Path
from typing import Any

from PIL import Image, ImageDraw

import compare_screenshots as screenshots


def parse_named_float(value: str) -> tuple[str, float]:
    if "=" not in value:
        raise argparse.ArgumentTypeError("expected NAME=VALUE")
    name, raw = value.split("=", 1)
    name = name.strip()
    if not name:
        raise argparse.ArgumentTypeError("name must not be empty")
    try:
        parsed = float(raw)
    except ValueError:
        raise argparse.ArgumentTypeError("value must be numeric") from None
    if not math.isfinite(parsed) or parsed < 0.0:
        raise argparse.ArgumentTypeError("value must be a non-negative finite number")
    return name, parsed


def rect_intersection(a: tuple[int, int, int, int], b: tuple[int, int, int, int]) -> tuple[int, int, int, int] | None:
    ax, ay, aw, ah = a
    bx, by, bw, bh = b
    left = max(ax, bx)
    top = max(ay, by)
    right = min(ax + aw, bx + bw)
    bottom = min(ay + ah, by + bh)
    if right <= left or bottom <= top:
        return None
    return left, top, right - left, bottom - top


def points_for_region(
    width: int,
    height: int,
    *,
    region: tuple[int, int, int, int] | None = None,
    exclude_regions: list[dict[str, Any]] | None = None,
) -> tuple[list[tuple[int, int]], int]:
    exclude_regions = exclude_regions or []
    if region is None:
        rx, ry, rw, rh = 0, 0, width, height
    else:
        rx, ry, rw, rh = region
    if rx < 0 or ry < 0 or rw <= 0 or rh <= 0 or rx + rw > width or ry + rh > height:
        raise ValueError(f"region out of bounds: {rx},{ry},{rw},{rh} for {width}x{height}")

    excluded = set()
    region_rect = (rx, ry, rw, rh)
    for mask in exclude_regions:
        intersection = rect_intersection(region_rect, tuple(mask["roi"]))
        if intersection is None:
            continue
        mx, my, mw, mh = intersection
        for y in range(my, my + mh):
            for x in range(mx, mx + mw):
                excluded.add((x, y))

    points: list[tuple[int, int]] = []
    for y in range(ry, ry + rh):
        for x in range(rx, rx + rw):
            if (x, y) in excluded:
                continue
            points.append((x, y))
    return points, len(excluded)


def pixels_at(image: Image.Image, points: list[tuple[int, int]]) -> list[tuple[int, int, int]]:
    px = image.load()
    return [px[x, y] for x, y in points]


def feature_metrics(image: Image.Image, points: list[tuple[int, int]]) -> dict[str, Any]:
    px = image.load()
    total = len(points)
    bright_points: list[tuple[int, int]] = []
    near_white_points: list[tuple[int, int]] = []
    warm_points: list[tuple[int, int]] = []

    for x, y in points:
        r, g, b = px[x, y]
        if r > 170 and g > 170 and b > 120:
            bright_points.append((x, y))
        if r > 200 and g > 200 and b > 170:
            near_white_points.append((x, y))
        if r > 120 and g > 20 and b < 140 and r >= g and r > b + 40:
            warm_points.append((x, y))

    width, height = image.size
    return {
        "sampled_pixels": total,
        "bright_pixels": len(bright_points),
        "bright_pct": 100.0 * len(bright_points) / total if total else 0.0,
        "bright_bbox": screenshots.bbox_from_points(bright_points),
        "bright_components": screenshots.connected_components(bright_points, width, height),
        "near_white_pixels": len(near_white_points),
        "near_white_pct": 100.0 * len(near_white_points) / total if total else 0.0,
        "near_white_bbox": screenshots.bbox_from_points(near_white_points),
        "warm_pixels": len(warm_points),
        "warm_pct": 100.0 * len(warm_points) / total if total else 0.0,
        "warm_bbox": screenshots.bbox_from_points(warm_points),
        "warm_components": screenshots.connected_components(warm_points, width, height),
    }


def metric_block(
    baseline: Image.Image,
    test: Image.Image,
    points: list[tuple[int, int]],
    *,
    source_pixels: int,
    excluded_pixels: int,
) -> dict[str, Any]:
    baseline_pixels = pixels_at(baseline, points)
    test_pixels = pixels_at(test, points)
    return {
        **screenshots.diff_metrics(baseline_pixels, test_pixels),
        "source_pixels": source_pixels,
        "sampled_pixels": len(points),
        "excluded_pixels": excluded_pixels,
        "excluded_pct": 100.0 * excluded_pixels / source_pixels if source_pixels else 0.0,
        "features": {
            "baseline": feature_metrics(baseline, points),
            "test": feature_metrics(test, points),
        },
    }


def open_and_align(args: argparse.Namespace) -> tuple[Image.Image, Image.Image, dict[str, Any]]:
    source_a = Image.open(args.baseline).convert("RGB")
    source_b = Image.open(args.test).convert("RGB")
    active_a = screenshots.active_bbox(source_a, args.active_threshold)
    active_b = screenshots.active_bbox(source_b, args.active_threshold)
    presentation_a = screenshots.presentation_metrics(source_a, args.active_threshold)
    presentation_b = screenshots.presentation_metrics(source_b, args.active_threshold)

    a = source_a
    b = source_b
    logical_crop = None
    resized_test = False

    if args.logical_viewport is not None:
        if args.logical_size is None:
            raise ValueError("--logical-viewport requires --logical-size")
        crop_a = screenshots.logical_crop_bbox(
            source_a,
            args.active_threshold,
            args.logical_size,
            args.logical_viewport,
            args.baseline_logical_frame,
        )
        crop_b = screenshots.logical_crop_bbox(
            source_b,
            args.active_threshold,
            args.logical_size,
            args.logical_viewport,
            args.test_logical_frame,
        )
        a = screenshots.crop_bbox(source_a, crop_a)
        b = screenshots.crop_bbox(source_b, crop_b)
        logical_crop = {"baseline": list(crop_a), "test": list(crop_b)}

    if a.size != b.size:
        resampling = getattr(Image, "Resampling", Image).BILINEAR
        b = b.resize(a.size, resampling)
        resized_test = True

    return a, b, {
        "source_size": {"baseline": list(source_a.size), "test": list(source_b.size)},
        "active_bbox": {
            "baseline": list(active_a) if active_a else None,
            "test": list(active_b) if active_b else None,
        },
        "presentation": {"baseline": presentation_a, "test": presentation_b},
        "logical_viewport": {
            "logical_size": list(args.logical_size) if args.logical_size else None,
            "viewport": list(args.logical_viewport) if args.logical_viewport else None,
            "baseline_frame": args.baseline_logical_frame,
            "test_frame": args.test_logical_frame,
            "crop": logical_crop,
        },
        "resized_test_to_baseline": resized_test,
    }


def validate_regions(regions: list[dict[str, Any]], width: int, height: int, label: str) -> list[str]:
    failures: list[str] = []
    for region in regions:
        x, y, w, h = region["roi"]
        if x < 0 or y < 0 or w <= 0 or h <= 0 or x + w > width or y + h > height:
            failures.append(f"{label} {region['name']} out of bounds: {x},{y},{w},{h} for {width}x{height}")
    return failures


def write_heatmap(path: Path, baseline: Image.Image, test: Image.Image, exclude_regions: list[dict[str, Any]]) -> None:
    width, height = baseline.size
    excluded = set()
    for region in exclude_regions:
        x, y, w, h = region["roi"]
        for iy in range(max(0, y), min(height, y + h)):
            for ix in range(max(0, x), min(width, x + w)):
                excluded.add((ix, iy))

    heatmap = Image.new("RGB", baseline.size)
    draw = ImageDraw.Draw(heatmap)
    for y in range(height):
        for x in range(width):
            a = baseline.getpixel((x, y))
            b = test.getpixel((x, y))
            if (x, y) in excluded:
                heatmap.putpixel((x, y), (0, 0, max(32, a[2] // 2)))
                continue
            diff = abs(a[0] - b[0]) + abs(a[1] - b[1]) + abs(a[2] - b[2])
            if diff <= screenshots.DIFF_THRESHOLD:
                heatmap.putpixel((x, y), (a[0] // 3, a[1] // 3, a[2] // 3))
            else:
                heatmap.putpixel((x, y), (min(255, diff * 255 // 200), 0, 0))
    for region in exclude_regions:
        x, y, w, h = region["roi"]
        draw.rectangle((x, y, x + w - 1, y + h - 1), outline=(0, 128, 255), width=2)
    heatmap.save(path)


def compare(args: argparse.Namespace) -> tuple[dict[str, Any], int]:
    baseline, test, alignment = open_and_align(args)
    width, height = baseline.size
    failures: list[str] = []
    failures.extend(validate_regions(args.region, width, height, "region"))
    failures.extend(validate_regions(args.exclude_region, width, height, "exclude-region"))

    all_points, all_excluded = points_for_region(
        width,
        height,
        exclude_regions=args.exclude_region,
    )
    full_points, _ = points_for_region(width, height)
    full = metric_block(
        baseline,
        test,
        full_points,
        source_pixels=width * height,
        excluded_pixels=0,
    )
    masked = metric_block(
        baseline,
        test,
        all_points,
        source_pixels=width * height,
        excluded_pixels=all_excluded,
    )

    regions: list[dict[str, Any]] = []
    for region in args.region:
        roi = tuple(region["roi"])
        rx, ry, rw, rh = roi
        source_pixels = rw * rh
        region_points, _ = points_for_region(width, height, region=roi)
        masked_points, excluded = points_for_region(
            width,
            height,
            region=roi,
            exclude_regions=args.exclude_region,
        )
        regions.append({
            "name": region["name"],
            "roi": list(roi),
            "full": metric_block(
                baseline,
                test,
                region_points,
                source_pixels=source_pixels,
                excluded_pixels=0,
            ),
            "masked": metric_block(
                baseline,
                test,
                masked_points,
                source_pixels=source_pixels,
                excluded_pixels=excluded,
            ),
        })

    max_region_changed = dict(args.max_region_changed_pct or [])
    max_region_masked_changed = dict(args.max_region_masked_changed_pct or [])
    max_region_excluded = dict(args.max_region_excluded_pct or [])
    by_name = {region["name"]: region for region in regions}

    if args.max_changed_pct is not None and full["changed_pct"] > args.max_changed_pct:
        failures.append(
            f"full changed_pct {full['changed_pct']:.3f} > {args.max_changed_pct:.3f}"
        )
    if args.max_masked_changed_pct is not None and masked["changed_pct"] > args.max_masked_changed_pct:
        failures.append(
            f"masked changed_pct {masked['changed_pct']:.3f} > {args.max_masked_changed_pct:.3f}"
        )
    if args.max_masked_excluded_pct is not None and masked["excluded_pct"] > args.max_masked_excluded_pct:
        failures.append(
            f"masked excluded_pct {masked['excluded_pct']:.3f} > {args.max_masked_excluded_pct:.3f}"
        )

    for name, limit in max_region_changed.items():
        region = by_name.get(name)
        if region is None:
            failures.append(f"missing region for threshold: {name}")
        elif region["full"]["changed_pct"] > limit:
            failures.append(
                f"region {name} changed_pct {region['full']['changed_pct']:.3f} > {limit:.3f}"
            )
    for name, limit in max_region_masked_changed.items():
        region = by_name.get(name)
        if region is None:
            failures.append(f"missing region for masked threshold: {name}")
        elif region["masked"]["changed_pct"] > limit:
            failures.append(
                f"region {name} masked changed_pct {region['masked']['changed_pct']:.3f} > {limit:.3f}"
            )
    for name, limit in max_region_excluded.items():
        region = by_name.get(name)
        if region is None:
            failures.append(f"missing region for excluded threshold: {name}")
        elif region["masked"]["excluded_pct"] > limit:
            failures.append(
                f"region {name} excluded_pct {region['masked']['excluded_pct']:.3f} > {limit:.3f}"
            )

    payload = {
        "status": "fail" if failures else "pass",
        "failures": failures,
        "baseline": args.baseline,
        "test": args.test,
        **alignment,
        "size": [width, height],
        "active_threshold": args.active_threshold,
        "diff_threshold": screenshots.DIFF_THRESHOLD,
        "full": full,
        "masked": masked,
        "exclude_regions": args.exclude_region,
        "regions": regions,
        "thresholds": {
            "max_changed_pct": args.max_changed_pct,
            "max_masked_changed_pct": args.max_masked_changed_pct,
            "max_masked_excluded_pct": args.max_masked_excluded_pct,
            "max_region_changed_pct": max_region_changed,
            "max_region_masked_changed_pct": max_region_masked_changed,
            "max_region_excluded_pct": max_region_excluded,
        },
        "heatmap": args.heatmap,
    }

    print(f"=== {args.baseline} vs {args.test} ===")
    print(f"Aligned size: {width}x{height}")
    print(
        f"Full:   changed={full['changed_pixels']}/{full['pixels']} "
        f"({full['changed_pct']:.3f}%) "
        f"bright={full['features']['baseline']['bright_pixels']}->"
        f"{full['features']['test']['bright_pixels']}"
    )
    print(
        f"Masked: changed={masked['changed_pixels']}/{masked['pixels']} "
        f"({masked['changed_pct']:.3f}%) excluded={masked['excluded_pct']:.3f}% "
        f"bright={masked['features']['baseline']['bright_pixels']}->"
        f"{masked['features']['test']['bright_pixels']}"
    )
    if regions:
        print("Regions:")
    for region in regions:
        full_region = region["full"]
        masked_region = region["masked"]
        print(
            f"  {region['name']}: full={full_region['changed_pct']:.3f}% "
            f"masked={masked_region['changed_pct']:.3f}% "
            f"excluded={masked_region['excluded_pct']:.3f}% "
            f"bright={masked_region['features']['baseline']['bright_pixels']}->"
            f"{masked_region['features']['test']['bright_pixels']}"
        )

    if args.heatmap:
        write_heatmap(Path(args.heatmap), baseline, test, args.exclude_region)
        print(f"Heatmap: {args.heatmap}")

    if failures:
        print("FAIL: actor-masked visual comparison failed")
        for failure in failures:
            print(f"  - {failure}")
        return payload, 1

    print("PASS: actor-masked visual comparison")
    return payload, 0


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("baseline", help="baseline screenshot")
    parser.add_argument("test", help="test screenshot")
    parser.add_argument("--json-out", help="write comparison metrics as JSON")
    parser.add_argument("--heatmap", help="write masked diff heatmap")
    parser.add_argument("--region", type=screenshots.parse_region, action="append", default=[])
    parser.add_argument("--exclude-region", type=screenshots.parse_region, action="append", default=[])
    parser.add_argument("--logical-size", type=screenshots.parse_size)
    parser.add_argument("--logical-viewport", type=screenshots.parse_roi)
    parser.add_argument("--baseline-logical-frame", choices=("active", "full"), default="active")
    parser.add_argument("--test-logical-frame", choices=("active", "full"), default="full")
    parser.add_argument("--active-threshold", type=screenshots.channel_threshold, default=0)
    parser.add_argument("--max-changed-pct", type=screenshots.non_negative_finite)
    parser.add_argument("--max-masked-changed-pct", type=screenshots.non_negative_finite)
    parser.add_argument("--max-masked-excluded-pct", type=screenshots.non_negative_finite)
    parser.add_argument("--max-region-changed-pct", type=parse_named_float, action="append")
    parser.add_argument("--max-region-masked-changed-pct", type=parse_named_float, action="append")
    parser.add_argument("--max-region-excluded-pct", type=parse_named_float, action="append")
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    try:
        payload, status = compare(args)
    except Exception as exc:
        payload = {
            "status": "fail",
            "error": type(exc).__name__,
            "message": str(exc),
            "baseline": args.baseline,
            "test": args.test,
        }
        print(f"FAIL: {type(exc).__name__}: {exc}")
        status = 2

    if args.json_out:
        with open(args.json_out, "w", encoding="utf-8") as handle:
            json.dump(payload, handle, indent=2, sort_keys=True)
            handle.write("\n")
    return status


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
