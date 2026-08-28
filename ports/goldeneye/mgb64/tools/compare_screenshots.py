#!/usr/bin/env python3
"""Compare two screenshots and report pixel differences."""

from __future__ import annotations

import argparse
import json
import math
import sys
from PIL import Image


DIFF_THRESHOLD = 5


def rgb_pixels(image):
    data = image.convert("RGB").tobytes()
    return list(zip(data[0::3], data[1::3], data[2::3]))


def active_bbox(image, threshold=0):
    bbox, _ = active_bbox_and_pixel_count(image, threshold)
    return bbox


def active_bbox_and_pixel_count(image, threshold=0):
    rgb = image.convert("RGB")
    pixels = rgb.load()
    width, height = rgb.size
    min_x = width
    min_y = height
    max_x = -1
    max_y = -1
    active_pixels = 0
    for y in range(height):
        for x in range(width):
            if max(pixels[x, y]) <= threshold:
                continue
            active_pixels += 1
            if x < min_x:
                min_x = x
            if y < min_y:
                min_y = y
            if x > max_x:
                max_x = x
            if y > max_y:
                max_y = y
    if max_x < min_x or max_y < min_y:
        return None, active_pixels
    return (min_x, min_y, max_x - min_x + 1, max_y - min_y + 1), active_pixels


def presentation_metrics(image, threshold=0):
    width, height = image.size
    bbox, active_pixels = active_bbox_and_pixel_count(image, threshold)
    total_pixels = width * height
    metrics = {
        "size": [width, height],
        "active_threshold": threshold,
        "active_bbox": list(bbox) if bbox else None,
        "active_pixels": active_pixels,
        "active_pixel_pct": 100.0 * active_pixels / total_pixels if total_pixels else 0.0,
        "active_bbox_pixels": 0,
        "active_bbox_pct": 0.0,
        "active_aspect": None,
        "margins": None,
        "center_offset_px": None,
        "center_offset_norm": None,
        "border_class": "empty",
    }

    if bbox is None:
        return metrics

    x, y, active_width, active_height = bbox
    right = width - x - active_width
    bottom = height - y - active_height
    bbox_pixels = active_width * active_height
    x_offset = x + active_width / 2.0 - width / 2.0
    y_offset = y + active_height / 2.0 - height / 2.0

    if x == 0 and right == 0 and y == 0 and bottom == 0:
        border_class = "full"
    elif x == 0 and right == 0:
        border_class = "letterbox"
    elif y == 0 and bottom == 0:
        border_class = "pillarbox"
    elif x == right and y == bottom:
        border_class = "windowbox"
    else:
        border_class = "offset"

    metrics.update({
        "active_bbox_pixels": bbox_pixels,
        "active_bbox_pct": 100.0 * bbox_pixels / total_pixels if total_pixels else 0.0,
        "active_aspect": active_width / active_height if active_height else None,
        "margins": {
            "left": x,
            "top": y,
            "right": right,
            "bottom": bottom,
        },
        "center_offset_px": [x_offset, y_offset],
        "center_offset_norm": [
            x_offset / width if width else 0.0,
            y_offset / height if height else 0.0,
        ],
        "border_class": border_class,
    })
    return metrics


def crop_bbox(image, bbox):
    x, y, width, height = bbox
    return image.crop((x, y, x + width, y + height))


def parse_roi(value):
    try:
        parts = [int(part) for part in value.split(",")]
    except ValueError:
        raise argparse.ArgumentTypeError("ROI must be X,Y,W,H") from None
    if len(parts) != 4:
        raise argparse.ArgumentTypeError("ROI must be X,Y,W,H")
    x, y, w, h = parts
    if w <= 0 or h <= 0:
        raise argparse.ArgumentTypeError("ROI width/height must be positive")
    if x < 0 or y < 0:
        raise argparse.ArgumentTypeError("ROI x/y must be non-negative")
    return x, y, w, h


def parse_size(value):
    text = value.replace("x", ",").replace("X", ",")
    try:
        parts = [int(part) for part in text.split(",")]
    except ValueError:
        raise argparse.ArgumentTypeError("size must be W,H or WxH") from None
    if len(parts) != 2:
        raise argparse.ArgumentTypeError("size must be W,H or WxH")
    width, height = parts
    if width <= 0 or height <= 0:
        raise argparse.ArgumentTypeError("size width/height must be positive")
    return width, height


def parse_region(value):
    if ":" in value:
        name, roi_value = value.split(":", 1)
        if not name:
            raise argparse.ArgumentTypeError("region name must not be empty")
    else:
        name = f"region{parse_region.counter}"
        parse_region.counter += 1
        roi_value = value
    return {"name": name, "roi": parse_roi(roi_value)}


parse_region.counter = 1


def logical_crop_bbox(image, active_threshold, logical_size, logical_viewport, frame_mode):
    if frame_mode == "full":
        frame = (0, 0, image.size[0], image.size[1])
    elif frame_mode == "active":
        frame = active_bbox(image, active_threshold)
        if frame is None:
            raise ValueError("active bbox missing for logical viewport frame")
    else:
        raise ValueError(f"unsupported logical frame mode: {frame_mode}")

    logical_w, logical_h = logical_size
    lx, ly, lw, lh = logical_viewport
    fx, fy, fw, fh = frame
    if lx < 0 or ly < 0 or lw <= 0 or lh <= 0:
        raise ValueError("logical viewport has invalid bounds")
    if lx + lw > logical_w or ly + lh > logical_h:
        raise ValueError("logical viewport extends outside logical size")

    left = int(round(fx + lx * fw / logical_w))
    top = int(round(fy + ly * fh / logical_h))
    right = int(round(fx + (lx + lw) * fw / logical_w))
    bottom = int(round(fy + (ly + lh) * fh / logical_h))

    left = max(0, min(image.size[0], left))
    top = max(0, min(image.size[1], top))
    right = max(0, min(image.size[0], right))
    bottom = max(0, min(image.size[1], bottom))
    if right <= left or bottom <= top:
        raise ValueError("logical viewport maps to an empty crop")
    return (left, top, right - left, bottom - top)


def non_negative_finite(value):
    try:
        parsed = float(value)
    except ValueError:
        raise argparse.ArgumentTypeError("expected a number") from None
    if not math.isfinite(parsed) or parsed < 0.0:
        raise argparse.ArgumentTypeError("expected a non-negative finite number")
    return parsed


def channel_threshold(value):
    try:
        parsed = int(value)
    except ValueError:
        raise argparse.ArgumentTypeError("expected an integer from 0 to 255") from None
    if parsed < 0 or parsed > 255:
        raise argparse.ArgumentTypeError("expected an integer from 0 to 255")
    return parsed


def write_json(path, payload):
    if not path:
        return
    with open(path, "w", encoding="utf-8") as handle:
        json.dump(payload, handle, indent=2, sort_keys=True)
        handle.write("\n")


def diff_metrics(pa, pb):
    total = len(pa)
    changed = sum(
        1
        for x, y in zip(pa, pb)
        if abs(x[0] - y[0]) + abs(x[1] - y[1]) + abs(x[2] - y[2]) > DIFF_THRESHOLD
    )
    identical = total - changed
    r_diffs = [abs(x[0] - y[0]) for x, y in zip(pa, pb)]
    g_diffs = [abs(x[1] - y[1]) for x, y in zip(pa, pb)]
    b_diffs = [abs(x[2] - y[2]) for x, y in zip(pa, pb)]

    def mean_rgb(pixels):
        if not pixels:
            return [0.0, 0.0, 0.0]
        return [
            sum(p[0] for p in pixels) / total,
            sum(p[1] for p in pixels) / total,
            sum(p[2] for p in pixels) / total,
        ]

    return {
        "pixels": total,
        "changed_pixels": changed,
        "changed_pct": 100.0 * changed / total if total else 0.0,
        "identical_pixels": identical,
        "identical_pct": 100.0 * identical / total if total else 0.0,
        "unique_colors": {"baseline": len(set(pa)), "test": len(set(pb))},
        "blue_pixels": {
            "baseline": sum(1 for p in pa if p[2] > 10),
            "test": sum(1 for p in pb if p[2] > 10),
        },
        "red_dominant_pixels": {
            "baseline": sum(1 for p in pa if p[0] > 60 and p[1] < 30),
            "test": sum(1 for p in pb if p[0] > 60 and p[1] < 30),
        },
        "mean_rgb": {"baseline": mean_rgb(pa), "test": mean_rgb(pb)},
        "per_channel": {
            "r": {"avg": sum(r_diffs) / total if total else 0.0, "max": max(r_diffs) if r_diffs else 0},
            "g": {"avg": sum(g_diffs) / total if total else 0.0, "max": max(g_diffs) if g_diffs else 0},
            "b": {"avg": sum(b_diffs) / total if total else 0.0, "max": max(b_diffs) if b_diffs else 0},
        },
    }


def bbox_from_points(points):
    if not points:
        return None
    xs = [p[0] for p in points]
    ys = [p[1] for p in points]
    return [min(xs), min(ys), max(xs) - min(xs) + 1, max(ys) - min(ys) + 1]


def connected_components(points, width, height, max_components=8, min_pixels=8):
    point_set = set(points)
    visited = set()
    components = []

    for start in points:
        if start in visited:
            continue
        stack = [start]
        visited.add(start)
        xs = []
        ys = []

        while stack:
            x, y = stack.pop()
            xs.append(x)
            ys.append(y)
            for ny in (y - 1, y, y + 1):
                if ny < 0 or ny >= height:
                    continue
                for nx in (x - 1, x, x + 1):
                    if nx < 0 or nx >= width or (nx == x and ny == y):
                        continue
                    point = (nx, ny)
                    if point not in point_set or point in visited:
                        continue
                    visited.add(point)
                    stack.append(point)

        if len(xs) >= min_pixels:
            components.append({
                "pixels": len(xs),
                "bbox": [min(xs), min(ys), max(xs) - min(xs) + 1, max(ys) - min(ys) + 1],
            })

    components.sort(key=lambda item: item["pixels"], reverse=True)
    return components[:max_components]


def image_feature_metrics(image):
    rgb = image.convert("RGB")
    width, height = rgb.size
    pixels = rgb.load()
    total = width * height
    bright_points = []
    near_white_points = []
    warm_points = []

    for y in range(height):
        for x in range(width):
            r, g, b = pixels[x, y]
            if r > 170 and g > 170 and b > 120:
                bright_points.append((x, y))
            if r > 200 and g > 200 and b > 170:
                near_white_points.append((x, y))
            if r > 120 and g > 20 and b < 140 and r >= g and r > b + 40:
                warm_points.append((x, y))

    return {
        "bright_pixels": len(bright_points),
        "bright_pct": 100.0 * len(bright_points) / total if total else 0.0,
        "bright_bbox": bbox_from_points(bright_points),
        "bright_components": connected_components(bright_points, width, height),
        "near_white_pixels": len(near_white_points),
        "near_white_pct": 100.0 * len(near_white_points) / total if total else 0.0,
        "near_white_bbox": bbox_from_points(near_white_points),
        "warm_pixels": len(warm_points),
        "warm_pct": 100.0 * len(warm_points) / total if total else 0.0,
        "warm_bbox": bbox_from_points(warm_points),
        "warm_components": connected_components(warm_points, width, height),
    }


def roi_contains(rois, x, y):
    for roi in rois:
        rx, ry, rw, rh = roi["roi"]
        if x >= rx and x < rx + rw and y >= ry and y < ry + rh:
            return True
    return False


def validate_regions_for_size(regions, size, label):
    width, height = size
    failures = []
    for region in regions:
        name = region["name"]
        rx, ry, rw, rh = region["roi"]
        if rx + rw > width or ry + rh > height:
            failures.append(f"{label} {name} out of bounds: {rx},{ry},{rw},{rh} for size {size}")
    return failures


def masked_rgb_pixels(image, exclude_regions):
    if not exclude_regions:
        return rgb_pixels(image)

    rgb = image.convert("RGB")
    width, height = rgb.size
    pixels = rgb.load()
    out = []
    for y in range(height):
        for x in range(width):
            if roi_contains(exclude_regions, x, y):
                continue
            out.append(pixels[x, y])
    return out


def image_feature_metrics_masked(image, exclude_regions):
    if not exclude_regions:
        return image_feature_metrics(image)

    rgb = image.convert("RGB")
    width, height = rgb.size
    pixels = rgb.load()
    total = width * height
    kept = 0
    bright_points = []
    near_white_points = []
    warm_points = []

    for y in range(height):
        for x in range(width):
            if roi_contains(exclude_regions, x, y):
                continue
            kept += 1
            r, g, b = pixels[x, y]
            if r > 170 and g > 170 and b > 120:
                bright_points.append((x, y))
            if r > 200 and g > 200 and b > 170:
                near_white_points.append((x, y))
            if r > 120 and g > 20 and b < 140 and r >= g and r > b + 40:
                warm_points.append((x, y))

    return {
        "source_pixels": total,
        "sampled_pixels": kept,
        "excluded_pixels": total - kept,
        "bright_pixels": len(bright_points),
        "bright_pct": 100.0 * len(bright_points) / kept if kept else 0.0,
        "bright_bbox": bbox_from_points(bright_points),
        "bright_components": connected_components(bright_points, width, height),
        "near_white_pixels": len(near_white_points),
        "near_white_pct": 100.0 * len(near_white_points) / kept if kept else 0.0,
        "near_white_bbox": bbox_from_points(near_white_points),
        "warm_pixels": len(warm_points),
        "warm_pct": 100.0 * len(warm_points) / kept if kept else 0.0,
        "warm_bbox": bbox_from_points(warm_points),
        "warm_components": connected_components(warm_points, width, height),
    }


def compare(
    path_a,
    path_b,
    heatmap_path=None,
    roi=None,
    regions=None,
    exclude_regions=None,
    per_channel=False,
    max_changed_pct=None,
    normalize_active=False,
    active_threshold=0,
    logical_viewport=None,
    logical_size=None,
    baseline_logical_frame="active",
    test_logical_frame="full",
):
    regions = regions or []
    exclude_regions = exclude_regions or []
    source_a = Image.open(path_a).convert("RGB")
    source_b = Image.open(path_b).convert("RGB")
    bbox_a = active_bbox(source_a, active_threshold)
    bbox_b = active_bbox(source_b, active_threshold)
    presentation_a = presentation_metrics(source_a, active_threshold)
    presentation_b = presentation_metrics(source_b, active_threshold)

    if normalize_active and roi:
        print("FAIL: --normalize-active cannot be combined with --roi")
        return {
            "status": "fail",
            "error": "normalize_active_with_roi",
            "baseline": path_a,
            "test": path_b,
            "baseline_size": list(source_a.size),
            "test_size": list(source_b.size),
            "active_bbox": {
                "baseline": list(bbox_a) if bbox_a else None,
                "test": list(bbox_b) if bbox_b else None,
            },
            "roi": list(roi),
        }, 2

    if logical_viewport is not None and normalize_active:
        print("FAIL: --logical-viewport cannot be combined with --normalize-active")
        return {
            "status": "fail",
            "error": "logical_viewport_with_normalize_active",
            "baseline": path_a,
            "test": path_b,
            "baseline_size": list(source_a.size),
            "test_size": list(source_b.size),
            "active_bbox": {
                "baseline": list(bbox_a) if bbox_a else None,
                "test": list(bbox_b) if bbox_b else None,
            },
            "active_threshold": active_threshold,
        }, 2

    if logical_viewport is not None and roi:
        print("FAIL: --logical-viewport cannot be combined with --roi")
        return {
            "status": "fail",
            "error": "logical_viewport_with_roi",
            "baseline": path_a,
            "test": path_b,
            "baseline_size": list(source_a.size),
            "test_size": list(source_b.size),
            "active_bbox": {
                "baseline": list(bbox_a) if bbox_a else None,
                "test": list(bbox_b) if bbox_b else None,
            },
            "roi": list(roi),
            "active_threshold": active_threshold,
        }, 2

    a = source_a
    b = source_b
    resized_test = False
    logical_crop = None
    if logical_viewport is not None:
        if logical_size is None:
            print("FAIL: --logical-viewport requires --logical-size")
            return {
                "status": "fail",
                "error": "logical_viewport_missing_size",
                "baseline": path_a,
                "test": path_b,
                "baseline_size": list(source_a.size),
                "test_size": list(source_b.size),
                "active_threshold": active_threshold,
            }, 2
        try:
            crop_a = logical_crop_bbox(
                source_a,
                active_threshold,
                logical_size,
                logical_viewport,
                baseline_logical_frame,
            )
            crop_b = logical_crop_bbox(
                source_b,
                active_threshold,
                logical_size,
                logical_viewport,
                test_logical_frame,
            )
        except ValueError as exc:
            print(f"FAIL: {exc}")
            return {
                "status": "fail",
                "error": "logical_viewport_invalid",
                "message": str(exc),
                "baseline": path_a,
                "test": path_b,
                "baseline_size": list(source_a.size),
                "test_size": list(source_b.size),
                "active_bbox": {
                    "baseline": list(bbox_a) if bbox_a else None,
                    "test": list(bbox_b) if bbox_b else None,
                },
                "logical_size": list(logical_size),
                "logical_viewport": list(logical_viewport),
                "baseline_logical_frame": baseline_logical_frame,
                "test_logical_frame": test_logical_frame,
                "active_threshold": active_threshold,
            }, 2
        a = crop_bbox(source_a, crop_a)
        b = crop_bbox(source_b, crop_b)
        logical_crop = {
            "baseline": list(crop_a),
            "test": list(crop_b),
        }
        if b.size != a.size:
            resampling = getattr(Image, "Resampling", Image).BILINEAR
            b = b.resize(a.size, resampling)
            resized_test = True
    elif normalize_active:
        if bbox_a is None or bbox_b is None:
            print("FAIL: active bbox missing")
            return {
                "status": "fail",
                "error": "active_bbox_missing",
                "baseline": path_a,
                "test": path_b,
                "baseline_size": list(source_a.size),
                "test_size": list(source_b.size),
                "active_bbox": {
                    "baseline": list(bbox_a) if bbox_a else None,
                    "test": list(bbox_b) if bbox_b else None,
                },
                "active_threshold": active_threshold,
            }, 2
        a = crop_bbox(source_a, bbox_a)
        b = crop_bbox(source_b, bbox_b)
        if b.size != a.size:
            resampling = getattr(Image, "Resampling", Image).BILINEAR
            b = b.resize(a.size, resampling)
            resized_test = True
    elif a.size != b.size:
        print(f"SIZE MISMATCH: {a.size} vs {b.size}")
        return {
            "status": "fail",
            "error": "size_mismatch",
            "baseline": path_a,
            "test": path_b,
            "baseline_size": list(a.size),
            "test_size": list(b.size),
            "active_bbox": {
                "baseline": list(bbox_a) if bbox_a else None,
                "test": list(bbox_b) if bbox_b else None,
            },
            "active_threshold": active_threshold,
            "roi": list(roi) if roi else None,
        }, 1

    if roi:
        rx, ry, rw, rh = roi
        if rx + rw > a.size[0] or ry + rh > a.size[1]:
            print(f"ROI OUT OF BOUNDS: {rx},{ry},{rw},{rh} for size {a.size}")
            return {
                "status": "fail",
                "error": "roi_out_of_bounds",
                "baseline": path_a,
                "test": path_b,
                "size": list(a.size),
                "roi": list(roi),
            }, 2
        a = a.crop((rx, ry, rx + rw, ry + rh))
        b = b.crop((rx, ry, rx + rw, ry + rh))

    region_failures = validate_regions_for_size(regions, a.size, "region")
    exclude_failures = validate_regions_for_size(exclude_regions, a.size, "exclude-region")

    pa = rgb_pixels(a)
    pb = rgb_pixels(b)
    metrics = diff_metrics(pa, pb)
    features_a = image_feature_metrics(a)
    features_b = image_feature_metrics(b)
    total = metrics["pixels"]
    changed = metrics["changed_pixels"]
    changed_pct = metrics["changed_pct"]

    roi_label = f" (ROI {roi[0]},{roi[1]} {roi[2]}x{roi[3]})" if roi else ""
    if normalize_active:
        roi_label = " (active viewport normalized)"
    if logical_viewport is not None:
        roi_label = (
            f" (logical viewport {logical_viewport[0]},{logical_viewport[1]} "
            f"{logical_viewport[2]}x{logical_viewport[3]} of "
            f"{logical_size[0]}x{logical_size[1]})"
        )
    print(f"=== {path_a} vs {path_b}{roi_label} ===")
    print(f"Source sizes:    {source_a.size} -> {source_b.size}")
    print(f"Active bbox:     {bbox_a} -> {bbox_b}")
    print(
        "Presentation:    "
        f"{presentation_a['border_class']} margins={presentation_a['margins']} "
        f"active={presentation_a['active_bbox_pct']:.1f}% "
        f"center={presentation_a['center_offset_px']} -> "
        f"{presentation_b['border_class']} margins={presentation_b['margins']} "
        f"active={presentation_b['active_bbox_pct']:.1f}% "
        f"center={presentation_b['center_offset_px']}"
    )
    print(f"Changed pixels: {changed}/{total} ({changed_pct:.1f}%)")
    print(f"Identical:      {metrics['identical_pixels']}/{total} ({metrics['identical_pct']:.1f}%)")
    print(f"Unique colors:  {metrics['unique_colors']['baseline']} -> {metrics['unique_colors']['test']}")
    print(f"Blue (B>10):    {metrics['blue_pixels']['baseline']} -> {metrics['blue_pixels']['test']}")
    print(f"Red-dom (R>60,G<30): {metrics['red_dominant_pixels']['baseline']} -> {metrics['red_dominant_pixels']['test']}")
    print(
        "Bright burst:   "
        f"{features_a['bright_pixels']} bbox={features_a['bright_bbox']} "
        f"largest={features_a['bright_components'][:1]} -> "
        f"{features_b['bright_pixels']} bbox={features_b['bright_bbox']} "
        f"largest={features_b['bright_components'][:1]}"
    )
    print(
        "Warm pixels:    "
        f"{features_a['warm_pixels']} bbox={features_a['warm_bbox']} -> "
        f"{features_b['warm_pixels']} bbox={features_b['warm_bbox']}"
    )

    payload = {
        "status": "pass",
        "baseline": path_a,
        "test": path_b,
        "source_size": {"baseline": list(source_a.size), "test": list(source_b.size)},
        "size": list(a.size),
        "roi": list(roi) if roi else None,
        "active_bbox": {
            "baseline": list(bbox_a) if bbox_a else None,
            "test": list(bbox_b) if bbox_b else None,
        },
        "presentation": {
            "baseline": presentation_a,
            "test": presentation_b,
        },
        "active_threshold": active_threshold,
        "normalized_active": normalize_active,
        "normalized_logical_viewport": logical_viewport is not None,
        "logical_viewport": {
            "logical_size": list(logical_size) if logical_size else None,
            "viewport": list(logical_viewport) if logical_viewport else None,
            "baseline_frame": baseline_logical_frame,
            "test_frame": test_logical_frame,
            "crop": logical_crop,
        },
        "resized_test_to_baseline": resized_test,
        "resized_test_to_baseline_active": resized_test,
        "diff_threshold": DIFF_THRESHOLD,
        "pixels": total,
        "changed_pixels": changed,
        "changed_pct": changed_pct,
        "identical_pixels": metrics["identical_pixels"],
        "identical_pct": metrics["identical_pct"],
        "unique_colors": metrics["unique_colors"],
        "blue_pixels": metrics["blue_pixels"],
        "red_dominant_pixels": metrics["red_dominant_pixels"],
        "mean_rgb": metrics["mean_rgb"],
        "features": {"baseline": features_a, "test": features_b},
        "max_changed_pct": max_changed_pct,
        "per_channel": None,
        "regions": [],
        "excluded_regions": [],
        "masked": None,
        "heatmap": heatmap_path,
        "failures": region_failures + exclude_failures,
    }

    if payload["failures"]:
        payload["status"] = "fail"

    # Per-channel stats
    if per_channel:
        payload["per_channel"] = metrics["per_channel"]
        print("\nPer-channel diff (avg / max):")
        print(f"  R: {metrics['per_channel']['r']['avg']:.2f} / {metrics['per_channel']['r']['max']}")
        print(f"  G: {metrics['per_channel']['g']['avg']:.2f} / {metrics['per_channel']['g']['max']}")
        print(f"  B: {metrics['per_channel']['b']['avg']:.2f} / {metrics['per_channel']['b']['max']}")

    if regions:
        print("\nRegions:")
        for region in regions:
            name = region["name"]
            rx, ry, rw, rh = region["roi"]
            if rx + rw > a.size[0] or ry + rh > a.size[1]:
                payload["status"] = "fail"
                payload["failures"].append(
                    f"region {name} out of bounds: {rx},{ry},{rw},{rh} for size {a.size}"
                )
                print(f"  {name}: OUT OF BOUNDS {rx},{ry},{rw},{rh} for size {a.size}")
                continue
            region_a = a.crop((rx, ry, rx + rw, ry + rh))
            region_b = b.crop((rx, ry, rx + rw, ry + rh))
            region_metrics = diff_metrics(rgb_pixels(region_a), rgb_pixels(region_b))
            region_features_a = image_feature_metrics(region_a)
            region_features_b = image_feature_metrics(region_b)
            region_payload = {
                "name": name,
                "roi": [rx, ry, rw, rh],
                **region_metrics,
                "features": {
                    "baseline": region_features_a,
                    "test": region_features_b,
                },
            }
            payload["regions"].append(region_payload)
            print(
                f"  {name}: roi={rx},{ry},{rw},{rh} "
                f"changed={region_metrics['changed_pixels']}/{region_metrics['pixels']} "
                f"({region_metrics['changed_pct']:.1f}%) "
                f"mean={region_metrics['mean_rgb']['baseline']} -> "
                f"{region_metrics['mean_rgb']['test']} "
                f"bright={region_features_a['bright_pixels']}->{region_features_b['bright_pixels']} "
                f"warm={region_features_a['warm_pixels']}->{region_features_b['warm_pixels']}"
            )

    if exclude_regions:
        print("\nMasked aggregate:")
        for region in exclude_regions:
            name = region["name"]
            rx, ry, rw, rh = region["roi"]
            excluded_pixels = rw * rh
            payload["excluded_regions"].append({
                "name": name,
                "roi": [rx, ry, rw, rh],
                "pixels": excluded_pixels,
            })
            if rx + rw > a.size[0] or ry + rh > a.size[1]:
                print(f"  exclude {name}: OUT OF BOUNDS {rx},{ry},{rw},{rh} for size {a.size}")
            else:
                print(f"  exclude {name}: roi={rx},{ry},{rw},{rh} pixels={excluded_pixels}")

        if payload["status"] != "fail":
            masked_pa = masked_rgb_pixels(a, exclude_regions)
            masked_pb = masked_rgb_pixels(b, exclude_regions)
            masked_metrics = diff_metrics(masked_pa, masked_pb)
            masked_features_a = image_feature_metrics_masked(a, exclude_regions)
            masked_features_b = image_feature_metrics_masked(b, exclude_regions)
            payload["masked"] = {
                **masked_metrics,
                "features": {
                    "baseline": masked_features_a,
                    "test": masked_features_b,
                },
            }
            print(
                "  remaining: "
                f"changed={masked_metrics['changed_pixels']}/{masked_metrics['pixels']} "
                f"({masked_metrics['changed_pct']:.1f}%) "
                f"bright={masked_features_a['bright_pixels']}->{masked_features_b['bright_pixels']} "
                f"warm={masked_features_a['warm_pixels']}->{masked_features_b['warm_pixels']}"
            )

    # Sample grid
    w, h = a.size
    print(f"\nSample grid ({path_a} -> {path_b}):")
    for y in [h//6, h//3, h//2, 2*h//3, 5*h//6]:
        row = []
        for x in [w//6, w//3, w//2, 2*w//3, 5*w//6]:
            px_a = a.getpixel((x, y))
            px_b = b.getpixel((x, y))
            diff = abs(px_a[0]-px_b[0]) + abs(px_a[1]-px_b[1]) + abs(px_a[2]-px_b[2])
            marker = " " if diff <= 5 else "*"
            row.append(f"({px_a[0]:3},{px_a[1]:3},{px_a[2]:3})->({px_b[0]:3},{px_b[1]:3},{px_b[2]:3}){marker}")
        print(f"  y={y:3}: {'  '.join(row)}")

    # Heatmap generation
    if heatmap_path:
        heatmap = Image.new("RGB", a.size)
        hpx = heatmap.load()
        for iy in range(a.size[1]):
            for ix in range(a.size[0]):
                ax = a.getpixel((ix, iy))
                bx = b.getpixel((ix, iy))
                diff = abs(ax[0]-bx[0]) + abs(ax[1]-bx[1]) + abs(ax[2]-bx[2])
                if diff <= 5:
                    # Dim version of the baseline
                    hpx[ix, iy] = (ax[0]//3, ax[1]//3, ax[2]//3)
                else:
                    # Red intensity proportional to diff (scale 0-765 to 0-255)
                    intensity = min(255, diff * 255 // 200)
                    hpx[ix, iy] = (intensity, 0, 0)
        heatmap.save(heatmap_path)
        print(f"\nHeatmap saved: {heatmap_path}")

    if max_changed_pct is not None and changed_pct > max_changed_pct:
        payload["status"] = "fail"
        payload["failures"].append(
            f"changed pixels {changed_pct:.3f}% > threshold {max_changed_pct:.3f}%"
        )
        print(f"\nFAIL: changed pixels {changed_pct:.3f}% > threshold {max_changed_pct:.3f}%")
        return payload, 1

    if payload["status"] == "fail":
        print("\nFAIL: one or more requested regions were invalid")
        return payload, 2

    return payload, 0


def parse_args(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("baseline", help="baseline screenshot")
    parser.add_argument("test", help="test screenshot")
    parser.add_argument("--heatmap", help="save visual diff heatmap")
    parser.add_argument("--roi", type=parse_roi, help="only compare X,Y,W,H")
    parser.add_argument(
        "--region",
        type=parse_region,
        action="append",
        default=[],
        help="add named region metrics in final comparison coordinates: NAME:X,Y,W,H",
    )
    parser.add_argument(
        "--exclude-region",
        type=parse_region,
        action="append",
        default=[],
        help="exclude a named region from the masked aggregate: NAME:X,Y,W,H",
    )
    parser.add_argument(
        "--normalize-active",
        action="store_true",
        help="crop each image to its non-black active bbox and resize the test crop to the baseline crop size",
    )
    parser.add_argument(
        "--logical-size",
        type=parse_size,
        help="logical VI/frame size used with --logical-viewport, as W,H or WxH",
    )
    parser.add_argument(
        "--logical-viewport",
        type=parse_roi,
        help="crop a logical viewport X,Y,W,H from each provider before comparison",
    )
    parser.add_argument(
        "--baseline-logical-frame",
        choices=("active", "full"),
        default="active",
        help="map baseline logical coordinates through the active bbox or full image",
    )
    parser.add_argument(
        "--test-logical-frame",
        choices=("active", "full"),
        default="full",
        help="map test logical coordinates through the active bbox or full image",
    )
    parser.add_argument(
        "--active-threshold",
        type=channel_threshold,
        default=0,
        help="RGB channel threshold used to find active non-black image bounds",
    )
    parser.add_argument("--per-channel", action="store_true", help="show per-channel diff statistics")
    parser.add_argument(
        "--max-changed-pct",
        type=non_negative_finite,
        help="fail if changed-pixel percentage exceeds this threshold",
    )
    parser.add_argument("--json-out", help="write comparison metrics as JSON")
    return parser.parse_args(argv)


def main(argv):
    args = parse_args(argv)
    try:
        payload, exit_code = compare(
            args.baseline,
            args.test,
            heatmap_path=args.heatmap,
            roi=args.roi,
            regions=args.region,
            exclude_regions=args.exclude_region,
            per_channel=args.per_channel,
            max_changed_pct=args.max_changed_pct,
            normalize_active=args.normalize_active,
            active_threshold=args.active_threshold,
            logical_viewport=args.logical_viewport,
            logical_size=args.logical_size,
            baseline_logical_frame=args.baseline_logical_frame,
            test_logical_frame=args.test_logical_frame,
        )
    except FileNotFoundError as exc:
        missing = exc.filename or str(exc)
        payload = {
            "status": "fail",
            "error": "missing_file",
            "baseline": args.baseline,
            "test": args.test,
            "missing": missing,
        }
        print(f"FAIL: missing file: {missing}")
        write_json(args.json_out, payload)
        return 2
    except Exception as exc:
        payload = {
            "status": "fail",
            "error": type(exc).__name__,
            "baseline": args.baseline,
            "test": args.test,
            "message": str(exc),
        }
        print(f"FAIL: {type(exc).__name__}: {exc}")
        write_json(args.json_out, payload)
        return 2

    write_json(args.json_out, payload)
    return exit_code


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
