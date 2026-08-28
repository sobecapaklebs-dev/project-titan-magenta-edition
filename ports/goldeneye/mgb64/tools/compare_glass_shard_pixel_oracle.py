#!/usr/bin/env python3
"""Measure stock/native pixel deltas under projected active-shard masks.

This is a spatial attribution tool, not a causality proof. Pair it with a
native default-vs-GE007_GLASS_SHARDS=0 control when deciding whether the
falling-shard draw pass itself changed the framebuffer.
"""

from __future__ import annotations

import argparse
import json
import math
import statistics
import sys
from pathlib import Path
from typing import Any, Iterable

from PIL import Image

import compare_glass_projected_visual as projected
import compare_glass_projection_pieces as projection_pieces
import compare_screenshots as screenshots


PixelId = int
Point = tuple[float, float]


def luma(rgb: tuple[int, int, int]) -> float:
    r, g, b = rgb
    return 0.2126 * r + 0.7152 * g + 0.0722 * b


def saturation(rgb: tuple[int, int, int]) -> float:
    r, g, b = (channel / 255.0 for channel in rgb)
    hi = max(r, g, b)
    lo = min(r, g, b)
    return 0.0 if hi <= 0.0 else (hi - lo) / hi


def bucket(rgb: tuple[int, int, int]) -> str:
    r, g, b = rgb
    lum = luma(rgb)
    sat = saturation(rgb)
    if r > 200 and g > 200 and b > 170:
        return "near_white"
    if r > 170 and g > 170 and b > 120:
        return "bright"
    if r > 120 and g > 20 and b < 140 and r >= g and r > b + 40:
        return "warm"
    if b > r + 35 and b > g + 25:
        return "blue"
    if abs(r - g) <= 8 and abs(g - b) <= 8:
        return "gray"
    if sat < 0.12:
        return "low_sat"
    if lum < 45:
        return "dark"
    return "other"


def as_float(value: Any, default: float = 0.0) -> float:
    try:
        parsed = float(value)
    except (TypeError, ValueError):
        return default
    return parsed if math.isfinite(parsed) else default


def as_int(value: Any, default: int = 0) -> int:
    try:
        return int(value)
    except (TypeError, ValueError):
        return default


def summarize_values(values: list[float]) -> dict[str, float]:
    if not values:
        return {"mean": 0.0, "min": 0.0, "max": 0.0, "p50": 0.0, "p90": 0.0, "p95": 0.0}
    ordered = sorted(values)

    def percentile(frac: float) -> float:
        if len(ordered) == 1:
            return ordered[0]
        pos = (len(ordered) - 1) * frac
        lo = math.floor(pos)
        hi = math.ceil(pos)
        if lo == hi:
            return ordered[lo]
        mix = pos - lo
        return ordered[lo] * (1.0 - mix) + ordered[hi] * mix

    return {
        "mean": statistics.fmean(values),
        "min": ordered[0],
        "max": ordered[-1],
        "p50": percentile(0.50),
        "p90": percentile(0.90),
        "p95": percentile(0.95),
    }


def count_buckets(pixels: Iterable[tuple[int, int, int]]) -> dict[str, int]:
    counts: dict[str, int] = {}
    for pixel in pixels:
        key = bucket(pixel)
        counts[key] = counts.get(key, 0) + 1
    return dict(sorted(counts.items()))


def projection_summary(label: str, frame: int | None, proj: dict[str, Any]) -> dict[str, Any]:
    return {
        "label": label,
        "frame": frame,
        "present": as_int(proj.get("present")),
        "source": proj.get("source"),
        "scale_mode": proj.get("scale_mode"),
        "active": as_int(proj.get("active")),
        "projected": as_int(proj.get("projected")),
        "onscreen": as_int(proj.get("onscreen")),
        "behind": as_int(proj.get("behind")),
        "sample_all": as_int(proj.get("sample_all")),
        "sample_limit": as_int(proj.get("sample_limit")),
        "sample_count": as_int(proj.get("sample_count")),
        "sample_truncated": as_int(proj.get("sample_truncated")),
    }


def load_projection(
    trace: Path,
    frame: int | None,
    variant: str | None,
) -> tuple[int | None, dict[str, Any], dict[int, dict[str, Any]]]:
    records = projection_pieces.load_records(trace)
    found_frame, record = projection_pieces.frame_record(records, frame, variant)
    proj = projection_pieces.projection(record, variant)
    return found_frame, proj, projection_pieces.sample_index(proj)


def map_point(point: Point, viewport: list[float], crop_size: tuple[int, int]) -> Point:
    vx, vy, vw, vh = viewport
    width, height = crop_size
    if vw <= 0.0 or vh <= 0.0:
        raise ValueError(f"invalid projection viewport: {viewport}")
    return ((point[0] - vx) * width / vw, (point[1] - vy) * height / vh)


def sample_screen_points(sample: dict[str, Any], viewport: list[float], crop_size: tuple[int, int]) -> list[Point]:
    return [map_point(point, viewport, crop_size) for point in projection_pieces.screen_points(sample)]


def sample_screen_bbox(sample: dict[str, Any], viewport: list[float], crop_size: tuple[int, int]) -> tuple[int, int, int, int] | None:
    bbox = sample.get("screen_bbox")
    if not isinstance(bbox, list) or len(bbox) != 4:
        return None
    x0, y0 = map_point((as_float(bbox[0]), as_float(bbox[1])), viewport, crop_size)
    x1, y1 = map_point((as_float(bbox[2]), as_float(bbox[3])), viewport, crop_size)
    width, height = crop_size
    left = max(0, min(width, int(math.floor(min(x0, x1)))))
    top = max(0, min(height, int(math.floor(min(y0, y1)))))
    right = max(0, min(width, int(math.ceil(max(x0, x1)))))
    bottom = max(0, min(height, int(math.ceil(max(y0, y1)))))
    if right <= left or bottom <= top:
        return None
    return left, top, right, bottom


def edge(a: Point, b: Point, c: Point) -> float:
    return (c[0] - a[0]) * (b[1] - a[1]) - (c[1] - a[1]) * (b[0] - a[0])


def point_in_triangle(point: Point, tri: list[Point]) -> bool:
    a, b, c = tri
    area = edge(a, b, c)
    if abs(area) < 0.000001:
        return False
    e0 = edge(a, b, point)
    e1 = edge(b, c, point)
    e2 = edge(c, a, point)
    if area < 0.0:
        e0, e1, e2 = -e0, -e1, -e2
    return e0 >= -0.000001 and e1 >= -0.000001 and e2 >= -0.000001


def rasterize_bbox(bbox: tuple[int, int, int, int], width: int) -> set[PixelId]:
    left, top, right, bottom = bbox
    return {y * width + x for y in range(top, bottom) for x in range(left, right)}


def rasterize_triangle(tri: list[Point], size: tuple[int, int], padding: float) -> set[PixelId]:
    if len(tri) != 3:
        return set()
    width, height = size
    min_x = max(0, int(math.floor(min(point[0] for point in tri) - padding)))
    max_x = min(width, int(math.ceil(max(point[0] for point in tri) + padding)))
    min_y = max(0, int(math.floor(min(point[1] for point in tri) - padding)))
    max_y = min(height, int(math.ceil(max(point[1] for point in tri) + padding)))
    if max_x <= min_x or max_y <= min_y:
        return set()
    out: set[PixelId] = set()
    for y in range(min_y, max_y):
        for x in range(min_x, max_x):
            if point_in_triangle((x + 0.5, y + 0.5), tri):
                out.add(y * width + x)
    return out


def rasterize_sample(
    baseline: dict[str, Any],
    test: dict[str, Any],
    viewport: list[float],
    size: tuple[int, int],
    *,
    mode: str,
    padding: float,
) -> set[PixelId]:
    width, _height = size
    pixels: set[PixelId] = set()
    for sample in (baseline, test):
        if mode == "bbox":
            bbox = sample_screen_bbox(sample, viewport, size)
            if bbox is not None:
                left, top, right, bottom = bbox
                left = max(0, int(math.floor(left - padding)))
                top = max(0, int(math.floor(top - padding)))
                right = min(size[0], int(math.ceil(right + padding)))
                bottom = min(size[1], int(math.ceil(bottom + padding)))
                if right > left and bottom > top:
                    pixels.update(rasterize_bbox((left, top, right, bottom), width))
            continue
        points = sample_screen_points(sample, viewport, size)
        pixels.update(rasterize_triangle(points, size, padding))
    return pixels


def pixels_for_ids(image: Image.Image, ids: Iterable[PixelId]) -> list[tuple[int, int, int]]:
    width, _height = image.size
    px = image.load()
    out: list[tuple[int, int, int]] = []
    for pixel_id in ids:
        y, x = divmod(pixel_id, width)
        out.append(px[x, y])
    return out


def pixel_metrics(
    baseline: Image.Image,
    test: Image.Image,
    pixel_ids: set[PixelId],
) -> dict[str, Any]:
    ordered = sorted(pixel_ids)
    baseline_pixels = pixels_for_ids(baseline, ordered)
    test_pixels = pixels_for_ids(test, ordered)
    changed = 0
    luma_delta: list[float] = []
    sat_delta: list[float] = []
    abs_rgb_delta: list[float] = []
    for left, right in zip(baseline_pixels, test_pixels):
        delta = abs(left[0] - right[0]) + abs(left[1] - right[1]) + abs(left[2] - right[2])
        if delta > screenshots.DIFF_THRESHOLD:
            changed += 1
        abs_rgb_delta.append(float(delta))
        luma_delta.append(luma(right) - luma(left))
        sat_delta.append(saturation(right) - saturation(left))

    return {
        "pixels": len(ordered),
        "changed_pixels": changed,
        "changed_pct": 100.0 * changed / len(ordered) if ordered else 0.0,
        "luma_delta": summarize_values(luma_delta),
        "saturation_delta": summarize_values(sat_delta),
        "abs_rgb_delta": summarize_values(abs_rgb_delta),
        "baseline_buckets": count_buckets(baseline_pixels),
        "test_buckets": count_buckets(test_pixels),
    }


def coverage_summary(coverage: dict[PixelId, int]) -> dict[str, Any]:
    counts: dict[int, int] = {}
    for value in coverage.values():
        counts[value] = counts.get(value, 0) + 1
    overlap = sum(count for value, count in counts.items() if value > 1)
    total = sum(counts.values())
    return {
        "pixels": total,
        "overlap_pixels": overlap,
        "overlap_pct": 100.0 * overlap / total if total else 0.0,
        "max_overlap": max(counts) if counts else 0,
        "histogram": {str(key): counts[key] for key in sorted(counts)},
    }


def piece_payload(
    index: int,
    baseline_sample: dict[str, Any],
    test_sample: dict[str, Any],
    mask: set[PixelId],
    coverage: dict[PixelId, int],
    baseline_image: Image.Image,
    test_image: Image.Image,
) -> dict[str, Any]:
    metrics = pixel_metrics(baseline_image, test_image, mask)
    overlap_pixels = sum(1 for pixel_id in mask if coverage.get(pixel_id, 0) > 1)
    b_bright = metrics["baseline_buckets"].get("bright", 0) + metrics["baseline_buckets"].get("near_white", 0)
    t_bright = metrics["test_buckets"].get("bright", 0) + metrics["test_buckets"].get("near_white", 0)
    return {
        "index": index,
        "pixels": metrics["pixels"],
        "overlap_pixels": overlap_pixels,
        "overlap_pct": 100.0 * overlap_pixels / metrics["pixels"] if metrics["pixels"] else 0.0,
        "timer": [as_int(baseline_sample.get("timer")), as_int(test_sample.get("timer"))],
        "onscreen": [as_int(baseline_sample.get("onscreen")), as_int(test_sample.get("onscreen"))],
        "screen_bbox": {
            "baseline": baseline_sample.get("screen_bbox"),
            "test": test_sample.get("screen_bbox"),
        },
        "changed_pct": metrics["changed_pct"],
        "abs_rgb_delta_mean": metrics["abs_rgb_delta"]["mean"],
        "luma_delta_mean": metrics["luma_delta"]["mean"],
        "saturation_delta_mean": metrics["saturation_delta"]["mean"],
        "bright_or_white_delta": t_bright - b_bright,
        "metrics": metrics,
    }


def open_crops(args: argparse.Namespace) -> tuple[Image.Image, Image.Image, dict[str, Any]]:
    baseline_crop, baseline_crop_box = projected.open_logical_crop(
        args.baseline_image,
        active_threshold=args.active_threshold,
        logical_size=args.logical_size,
        logical_viewport=args.logical_viewport,
        frame_mode=args.baseline_logical_frame,
    )
    test_crop, test_crop_box = projected.open_logical_crop(
        args.test_image,
        active_threshold=args.active_threshold,
        logical_size=args.logical_size,
        logical_viewport=args.logical_viewport,
        frame_mode=args.test_logical_frame,
    )
    resized_test = False
    if baseline_crop.size != test_crop.size:
        resampling = getattr(Image, "Resampling", Image).BILINEAR
        test_crop = test_crop.resize(baseline_crop.size, resampling)
        resized_test = True
    return baseline_crop, test_crop, {
        "baseline_crop": list(baseline_crop_box),
        "test_crop": list(test_crop_box),
        "resized_test_to_baseline": resized_test,
        "crop_size": list(baseline_crop.size),
    }


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline-trace", type=Path, required=True)
    parser.add_argument("--test-trace", type=Path, required=True)
    parser.add_argument("--baseline-image", type=Path, required=True)
    parser.add_argument("--test-image", type=Path, required=True)
    parser.add_argument("--logical-size", type=screenshots.parse_size, required=True)
    parser.add_argument("--logical-viewport", type=screenshots.parse_roi, required=True)
    parser.add_argument("--baseline-logical-frame", choices=("active", "full"), default="active")
    parser.add_argument("--test-logical-frame", choices=("active", "full"), default="full")
    parser.add_argument("--active-threshold", type=screenshots.channel_threshold, default=0)
    parser.add_argument("--baseline-frame", type=int)
    parser.add_argument("--test-frame", type=int)
    parser.add_argument("--baseline-variant")
    parser.add_argument("--test-variant")
    parser.add_argument("--mask-mode", choices=("triangle", "bbox"), default="triangle")
    parser.add_argument("--mask-padding", type=float, default=1.0)
    parser.add_argument("--require-full-sample", action="store_true")
    parser.add_argument("--top", type=int, default=12)
    parser.add_argument("--json-out", type=Path)
    args = parser.parse_args(argv)

    failures: list[str] = []
    warnings: list[str] = []
    baseline_frame, baseline_proj, baseline_samples = load_projection(
        args.baseline_trace, args.baseline_frame, args.baseline_variant
    )
    test_frame, test_proj, test_samples = load_projection(
        args.test_trace, args.test_frame, args.test_variant
    )

    for label, proj, samples in (
        ("baseline", baseline_proj, baseline_samples),
        ("test", test_proj, test_samples),
    ):
        if as_int(proj.get("present")) != 1:
            failures.append(f"{label} glass_projection is not present")
        if as_int(proj.get("active")) <= 0:
            failures.append(f"{label} glass_projection has no active shards")
        active = as_int(proj.get("active"))
        if active > 0 and len(samples) < active:
            message = f"{label} projection sample is partial: {len(samples)} / {active}"
            if args.require_full_sample:
                failures.append(message)
            else:
                warnings.append(message)
        if as_int(proj.get("sample_truncated")):
            message = f"{label} projection sample is truncated"
            if args.require_full_sample:
                failures.append(message)
            else:
                warnings.append(message)

    viewport = projected.list4(baseline_proj.get("viewport")) or [0.0, 10.0, 320.0, 220.0]
    test_viewport = projected.list4(test_proj.get("viewport")) or viewport
    if any(abs(viewport[index] - test_viewport[index]) > 0.01 for index in range(4)):
        failures.append(f"projection viewport mismatch: {viewport} vs {test_viewport}")

    common_indices = sorted(set(baseline_samples) & set(test_samples))
    if not common_indices:
        failures.append("no common sampled shard indices")

    baseline_crop, test_crop, crop_meta = open_crops(args)
    if baseline_crop.size != test_crop.size:
        failures.append(f"internal crop size mismatch: {baseline_crop.size} vs {test_crop.size}")

    piece_masks: dict[int, set[PixelId]] = {}
    coverage: dict[PixelId, int] = {}
    for index in common_indices:
        mask = rasterize_sample(
            baseline_samples[index],
            test_samples[index],
            viewport,
            baseline_crop.size,
            mode=args.mask_mode,
            padding=args.mask_padding,
        )
        if not mask:
            continue
        piece_masks[index] = mask
        for pixel_id in mask:
            coverage[pixel_id] = coverage.get(pixel_id, 0) + 1

    if common_indices and not piece_masks:
        failures.append("sampled shard masks are empty")

    pieces = [
        piece_payload(
            index,
            baseline_samples[index],
            test_samples[index],
            piece_masks[index],
            coverage,
            baseline_crop,
            test_crop,
        )
        for index in sorted(piece_masks)
    ]

    union_pixels = set(coverage)
    union_metrics = pixel_metrics(baseline_crop, test_crop, union_pixels)
    top = max(0, args.top)
    top_abs = sorted(pieces, key=lambda item: item["abs_rgb_delta_mean"], reverse=True)[:top]
    top_luma = sorted(pieces, key=lambda item: abs(item["luma_delta_mean"]), reverse=True)[:top]
    top_bright = sorted(pieces, key=lambda item: abs(item["bright_or_white_delta"]), reverse=True)[:top]
    top_overlap = sorted(pieces, key=lambda item: item["overlap_pct"], reverse=True)[:top]

    payload: dict[str, Any] = {
        "status": "fail" if failures else "pass",
        "failures": failures,
        "warnings": warnings,
        "baseline": projection_summary("baseline", baseline_frame, baseline_proj),
        "test": projection_summary("test", test_frame, test_proj),
        "sample": {
            "baseline_indices": len(baseline_samples),
            "test_indices": len(test_samples),
            "common_indices": len(common_indices),
            "rasterized_pieces": len(piece_masks),
            "missing_from_test": sorted(set(baseline_samples) - set(test_samples)),
            "extra_in_test": sorted(set(test_samples) - set(baseline_samples)),
        },
        "viewport": viewport,
        "mask_mode": args.mask_mode,
        "mask_padding": args.mask_padding,
        **crop_meta,
        "coverage": coverage_summary(coverage),
        "union_metrics": union_metrics,
        "top_abs_rgb_delta": top_abs,
        "top_abs_luma_delta": top_luma,
        "top_bright_or_white_delta": top_bright,
        "top_overlap": top_overlap,
        "pieces": pieces,
    }

    if args.json_out:
        args.json_out.parent.mkdir(parents=True, exist_ok=True)
        args.json_out.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")

    print(
        "=== glass shard pixel oracle: "
        f"baseline frame={baseline_frame} vs test frame={test_frame} ==="
    )
    print(
        "  samples: "
        f"common={len(common_indices)} rasterized={len(piece_masks)} "
        f"baseline={len(baseline_samples)}/{as_int(baseline_proj.get('active'))} "
        f"test={len(test_samples)}/{as_int(test_proj.get('active'))}"
    )
    if warnings:
        print("  warnings: " + "; ".join(warnings))
    print(
        "  coverage: "
        f"pixels={payload['coverage']['pixels']} "
        f"overlap={payload['coverage']['overlap_pct']:.1f}% "
        f"max_overlap={payload['coverage']['max_overlap']}"
    )
    print(
        "  union: "
        f"changed={union_metrics['changed_pct']:.3f}% "
        f"luma_delta={union_metrics['luma_delta']['mean']:.2f} "
        f"sat_delta={union_metrics['saturation_delta']['mean']:.3f} "
        f"abs_rgb={union_metrics['abs_rgb_delta']['mean']:.2f}"
    )
    print(
        "  buckets: "
        f"baseline={union_metrics['baseline_buckets']} "
        f"test={union_metrics['test_buckets']}"
    )
    if top_abs:
        piece = top_abs[0]
        print(
            "  strongest piece: "
            f"index={piece['index']} pixels={piece['pixels']} "
            f"changed={piece['changed_pct']:.1f}% "
            f"abs_rgb={piece['abs_rgb_delta_mean']:.2f} "
            f"luma_delta={piece['luma_delta_mean']:.2f} "
            f"bright_delta={piece['bright_or_white_delta']}"
        )
    if failures:
        print("FAIL: glass shard pixel oracle failed")
        for failure in failures:
            print(f"  - {failure}")
        return 1
    print("PASS: glass shard pixel oracle")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
