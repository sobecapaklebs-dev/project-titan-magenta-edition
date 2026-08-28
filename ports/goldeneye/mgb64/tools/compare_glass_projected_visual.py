#!/usr/bin/env python3
"""Compare screenshots inside the traced glass projection envelope."""

from __future__ import annotations

import argparse
import json
import math
import sys
from pathlib import Path
from typing import Any

from PIL import Image

import compare_screenshots as screenshots


def load_records(path: Path) -> list[dict[str, Any]]:
    records: list[dict[str, Any]] = []
    with path.open("r", encoding="utf-8", errors="replace") as handle:
        for line_no, raw in enumerate(handle, 1):
            line = raw.strip()
            if not line:
                continue
            try:
                record = json.loads(line)
            except json.JSONDecodeError as exc:
                raise SystemExit(f"FAIL: {path}:{line_no}: invalid JSONL: {exc}") from exc
            if isinstance(record, dict):
                records.append(record)
    return records


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


def record_frame(record: dict[str, Any], fallback: int) -> int:
    return as_int(record.get("f"), fallback)


def projection(record: dict[str, Any] | None) -> dict[str, Any]:
    if not isinstance(record, dict):
        return {}
    value = record.get("glass_projection")
    return value if isinstance(value, dict) else {}


def first_active_projection(records: list[dict[str, Any]]) -> tuple[int | None, dict[str, Any] | None]:
    for index, record in enumerate(records):
        proj = projection(record)
        if as_int(proj.get("active")) > 0:
            return record_frame(record, index + 1), record
    return None, None


def list4(value: Any) -> list[float] | None:
    if not isinstance(value, list) or len(value) != 4:
        return None
    parsed = [as_float(item, float("nan")) for item in value]
    if any(not math.isfinite(item) for item in parsed):
        return None
    return parsed


def union_bbox(a: list[float], b: list[float]) -> list[float]:
    return [min(a[0], b[0]), min(a[1], b[1]), max(a[2], b[2]), max(a[3], b[3])]


def clamp_bbox_to_viewport(bbox: list[float], viewport: list[float]) -> list[float]:
    vx, vy, vw, vh = viewport
    return [
        max(vx, min(vx + vw, bbox[0])),
        max(vy, min(vy + vh, bbox[1])),
        max(vx, min(vx + vw, bbox[2])),
        max(vy, min(vy + vh, bbox[3])),
    ]


def expand_bbox(bbox: list[float], amount: float) -> list[float]:
    return [bbox[0] - amount, bbox[1] - amount, bbox[2] + amount, bbox[3] + amount]


def map_projection_bbox_to_crop(
    bbox: list[float],
    viewport: list[float],
    crop_size: tuple[int, int],
) -> tuple[int, int, int, int]:
    vx, vy, vw, vh = viewport
    width, height = crop_size
    if vw <= 0.0 or vh <= 0.0:
        raise ValueError(f"invalid viewport: {viewport}")
    left = int(math.floor((bbox[0] - vx) * width / vw))
    top = int(math.floor((bbox[1] - vy) * height / vh))
    right = int(math.ceil((bbox[2] - vx) * width / vw))
    bottom = int(math.ceil((bbox[3] - vy) * height / vh))
    left = max(0, min(width, left))
    top = max(0, min(height, top))
    right = max(0, min(width, right))
    bottom = max(0, min(height, bottom))
    if right <= left or bottom <= top:
        raise ValueError(f"projected bbox maps to empty crop: {bbox} viewport={viewport}")
    return left, top, right - left, bottom - top


def open_logical_crop(
    path: Path,
    *,
    active_threshold: int,
    logical_size: tuple[int, int],
    logical_viewport: tuple[int, int, int, int],
    frame_mode: str,
) -> tuple[Image.Image, tuple[int, int, int, int]]:
    source = Image.open(path).convert("RGB")
    crop = screenshots.logical_crop_bbox(
        source,
        active_threshold,
        logical_size,
        logical_viewport,
        frame_mode,
    )
    return screenshots.crop_bbox(source, crop), crop


def roi_pixels(image: Image.Image, roi: tuple[int, int, int, int]) -> list[tuple[int, int, int]]:
    x, y, w, h = roi
    px = image.load()
    return [px[ix, iy] for iy in range(y, y + h) for ix in range(x, x + w)]


def feature_metrics(pixels: list[tuple[int, int, int]]) -> dict[str, Any]:
    total = len(pixels)
    if total == 0:
        return {
            "pixels": 0,
            "mean_rgb": [0.0, 0.0, 0.0],
            "bright_pixels": 0,
            "bright_pct": 0.0,
            "near_white_pixels": 0,
            "near_white_pct": 0.0,
            "warm_pixels": 0,
            "warm_pct": 0.0,
        }
    bright = sum(1 for r, g, b in pixels if r > 170 and g > 170 and b > 120)
    near_white = sum(1 for r, g, b in pixels if r > 200 and g > 200 and b > 170)
    warm = sum(1 for r, g, b in pixels if r > 120 and g > 20 and b < 140 and r >= g and r > b + 40)
    return {
        "pixels": total,
        "mean_rgb": [
            sum(pixel[0] for pixel in pixels) / total,
            sum(pixel[1] for pixel in pixels) / total,
            sum(pixel[2] for pixel in pixels) / total,
        ],
        "bright_pixels": bright,
        "bright_pct": 100.0 * bright / total,
        "near_white_pixels": near_white,
        "near_white_pct": 100.0 * near_white / total,
        "warm_pixels": warm,
        "warm_pct": 100.0 * warm / total,
    }


def compare_roi(
    baseline: Image.Image,
    test: Image.Image,
    roi: tuple[int, int, int, int],
) -> dict[str, Any]:
    baseline_pixels = roi_pixels(baseline, roi)
    test_pixels = roi_pixels(test, roi)
    diff = screenshots.diff_metrics(baseline_pixels, test_pixels)
    return {
        **diff,
        "roi": list(roi),
        "features": {
            "baseline": feature_metrics(baseline_pixels),
            "test": feature_metrics(test_pixels),
        },
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
    parser.add_argument("--projection-padding", type=float, default=4.0)
    parser.add_argument("--max-changed-pct", type=float)
    parser.add_argument("--json-out", type=Path)
    args = parser.parse_args(argv)

    baseline_frame, baseline_record = first_active_projection(load_records(args.baseline_trace))
    test_frame, test_record = first_active_projection(load_records(args.test_trace))
    baseline_projection = projection(baseline_record)
    test_projection = projection(test_record)

    baseline_bbox = list4(baseline_projection.get("union_screen_bbox"))
    test_bbox = list4(test_projection.get("union_screen_bbox"))
    viewport = list4(baseline_projection.get("viewport")) or [0.0, 10.0, 320.0, 220.0]
    test_viewport = list4(test_projection.get("viewport")) or viewport
    failures: list[str] = []

    if baseline_bbox is None:
        failures.append("baseline projection union bbox missing")
        baseline_bbox = [viewport[0], viewport[1], viewport[0] + viewport[2], viewport[1] + viewport[3]]
    if test_bbox is None:
        failures.append("test projection union bbox missing")
        test_bbox = [test_viewport[0], test_viewport[1], test_viewport[0] + test_viewport[2], test_viewport[1] + test_viewport[3]]
    if any(abs(viewport[index] - test_viewport[index]) > 0.01 for index in range(4)):
        failures.append(f"viewport mismatch: {viewport} vs {test_viewport}")

    projected_bbox = clamp_bbox_to_viewport(
        expand_bbox(union_bbox(baseline_bbox, test_bbox), args.projection_padding),
        viewport,
    )

    baseline_crop, baseline_crop_box = open_logical_crop(
        args.baseline_image,
        active_threshold=args.active_threshold,
        logical_size=args.logical_size,
        logical_viewport=args.logical_viewport,
        frame_mode=args.baseline_logical_frame,
    )
    test_crop, test_crop_box = open_logical_crop(
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

    roi = map_projection_bbox_to_crop(projected_bbox, viewport, baseline_crop.size)
    metrics = compare_roi(baseline_crop, test_crop, roi)
    if args.max_changed_pct is not None and metrics["changed_pct"] > args.max_changed_pct:
        failures.append(
            f"projected glass changed_pct {metrics['changed_pct']:.3f} exceeds {args.max_changed_pct:.3f}"
        )

    payload = {
        "status": "fail" if failures else "pass",
        "failures": failures,
        "baseline_frame": baseline_frame,
        "test_frame": test_frame,
        "viewport": viewport,
        "projection_padding": args.projection_padding,
        "baseline_projection_bbox": baseline_bbox,
        "test_projection_bbox": test_bbox,
        "combined_projection_bbox": projected_bbox,
        "baseline_crop": list(baseline_crop_box),
        "test_crop": list(test_crop_box),
        "resized_test_to_baseline": resized_test,
        "crop_size": list(baseline_crop.size),
        "projected_roi": metrics,
    }

    if args.json_out:
        args.json_out.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")

    print(
        "=== glass projected visual: "
        f"baseline frame={baseline_frame} vs test frame={test_frame} ==="
    )
    print(f"  projection bbox: {projected_bbox} -> roi={metrics['roi']} crop={baseline_crop.size}")
    print(
        "  changed={:.3f}% bright={} -> {} warm={} -> {} mean={} -> {}".format(
            metrics["changed_pct"],
            metrics["features"]["baseline"]["bright_pixels"],
            metrics["features"]["test"]["bright_pixels"],
            metrics["features"]["baseline"]["warm_pixels"],
            metrics["features"]["test"]["warm_pixels"],
            [round(value, 2) for value in metrics["features"]["baseline"]["mean_rgb"]],
            [round(value, 2) for value in metrics["features"]["test"]["mean_rgb"]],
        )
    )
    if failures:
        print("FAIL: glass projected visual comparison failed")
        for failure in failures:
            print(f"  - {failure}")
        return 1
    print("PASS: glass projected visual comparison")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
