#!/usr/bin/env python3
"""Compare a projected bullet-impact quad ROI between stock and native captures."""

from __future__ import annotations

import argparse
import json
import math
import sys
from pathlib import Path
from typing import Any

from PIL import Image, ImageDraw

sys.path.insert(0, str(Path(__file__).resolve().parent))
import compare_actor_masked_visual as actor_masked  # noqa: E402
import compare_screenshots as screenshots  # noqa: E402


def parse_pair(value: str) -> tuple[int, int]:
    parts = value.split(",")
    if len(parts) != 2:
        raise argparse.ArgumentTypeError("expected W,H")
    try:
        parsed = (int(parts[0]), int(parts[1]))
    except ValueError:
        raise argparse.ArgumentTypeError("expected integer W,H") from None
    if parsed[0] <= 0 or parsed[1] <= 0:
        raise argparse.ArgumentTypeError("dimensions must be positive")
    return parsed


def parse_rect(value: str) -> tuple[float, float, float, float]:
    parts = value.split(",")
    if len(parts) != 4:
        raise argparse.ArgumentTypeError("expected X,Y,W,H")
    try:
        parsed = tuple(float(part) for part in parts)
    except ValueError:
        raise argparse.ArgumentTypeError("expected numeric X,Y,W,H") from None
    if parsed[2] <= 0.0 or parsed[3] <= 0.0:
        raise argparse.ArgumentTypeError("width and height must be positive")
    return parsed  # type: ignore[return-value]


def load_frame(path: Path, frame: int) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as handle:
        for line in handle:
            if not line.strip():
                continue
            record = json.loads(line)
            if record.get("f") == frame:
                return record
    raise ValueError(f"frame {frame} not found in {path}")


def first_world_impact(record: dict[str, Any]) -> dict[str, Any]:
    state = record.get("impact_state")
    if not isinstance(state, dict):
        return {}
    sample = state.get("sample")
    if not isinstance(sample, list):
        return {}
    for item in sample:
        if isinstance(item, dict) and item.get("world"):
            return item
    return {}


def projection(sample: dict[str, Any]) -> dict[str, Any]:
    value = sample.get("projection")
    return value if isinstance(value, dict) else {}


def numeric_bbox(value: Any) -> list[float] | None:
    if (
        not isinstance(value, list)
        or len(value) != 4
        or any(not isinstance(item, (int, float)) or not math.isfinite(float(item)) for item in value)
    ):
        return None
    bbox = [float(item) for item in value]
    if bbox[2] <= bbox[0] or bbox[3] <= bbox[1]:
        return None
    return bbox


def bbox_center(bbox: list[float]) -> tuple[float, float]:
    return ((bbox[0] + bbox[2]) * 0.5, (bbox[1] + bbox[3]) * 0.5)


def bbox_area(bbox: list[float]) -> float:
    return max(0.0, bbox[2] - bbox[0]) * max(0.0, bbox[3] - bbox[1])


def union_bbox(a: list[float], b: list[float], padding: float) -> list[float]:
    return [
        min(a[0], b[0]) - padding,
        min(a[1], b[1]) - padding,
        max(a[2], b[2]) + padding,
        max(a[3], b[3]) + padding,
    ]


def clamp_to_viewport(bbox: list[float], viewport: tuple[float, float, float, float]) -> list[float] | None:
    vx, vy, vw, vh = viewport
    clamped = [
        max(vx, min(vx + vw, bbox[0])),
        max(vy, min(vy + vh, bbox[1])),
        max(vx, min(vx + vw, bbox[2])),
        max(vy, min(vy + vh, bbox[3])),
    ]
    if clamped[2] <= clamped[0] or clamped[3] <= clamped[1]:
        return None
    return clamped


def bbox_to_crop_roi(
    bbox: list[float],
    crop_size: tuple[int, int],
    viewport: tuple[float, float, float, float],
) -> tuple[int, int, int, int]:
    crop_w, crop_h = crop_size
    vx, vy, vw, vh = viewport
    left = math.floor((bbox[0] - vx) * crop_w / vw)
    top = math.floor((bbox[1] - vy) * crop_h / vh)
    right = math.ceil((bbox[2] - vx) * crop_w / vw)
    bottom = math.ceil((bbox[3] - vy) * crop_h / vh)
    left = max(0, min(crop_w - 1, left))
    top = max(0, min(crop_h - 1, top))
    right = max(left + 1, min(crop_w, right))
    bottom = max(top + 1, min(crop_h, bottom))
    return left, top, right - left, bottom - top


def impact_identity(sample: dict[str, Any]) -> dict[str, Any]:
    return {
        "index": sample.get("index"),
        "room": sample.get("room"),
        "impact": sample.get("impact"),
        "prop": sample.get("prop"),
        "prop_pad": sample.get("prop_pad"),
        "world_center": sample.get("world_center"),
    }


def write_heatmap(
    path: Path,
    baseline: Image.Image,
    test: Image.Image,
    region: dict[str, Any],
    exclude_regions: list[dict[str, Any]],
) -> None:
    actor_masked.write_heatmap(path, baseline, test, exclude_regions)
    image = Image.open(path).convert("RGB")
    draw = ImageDraw.Draw(image)
    x, y, w, h = region["roi"]
    draw.rectangle((x, y, x + w - 1, y + h - 1), outline=(255, 220, 0), width=2)
    image.save(path)


def compare(args: argparse.Namespace) -> tuple[dict[str, Any], int]:
    failures: list[str] = []

    try:
        baseline_record = load_frame(args.baseline_trace, args.baseline_frame)
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        baseline_record = {}
        failures.append(str(exc))
    try:
        test_record = load_frame(args.test_trace, args.test_frame)
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        test_record = {}
        failures.append(str(exc))

    baseline_impact = first_world_impact(baseline_record)
    test_impact = first_world_impact(test_record)
    baseline_projection = projection(baseline_impact)
    test_projection = projection(test_impact)
    baseline_bbox = numeric_bbox(baseline_projection.get("screen_bbox"))
    test_bbox = numeric_bbox(test_projection.get("screen_bbox"))

    if not baseline_impact:
        failures.append("baseline frame has no world impact sample")
    if not test_impact:
        failures.append("test frame has no world impact sample")
    if baseline_projection.get("valid") != 1 or baseline_bbox is None:
        failures.append("baseline world impact projection is invalid")
    if test_projection.get("valid") != 1 or test_bbox is None:
        failures.append("test world impact projection is invalid")

    alignment: dict[str, Any] = {}
    region: dict[str, Any] | None = None
    metrics: dict[str, Any] = {}
    projection_delta: dict[str, Any] = {}

    try:
        baseline_image, test_image, alignment = actor_masked.open_and_align(args)
        width, height = baseline_image.size
        failures.extend(actor_masked.validate_regions(args.exclude_region, width, height, "exclude-region"))

        if baseline_bbox is not None and test_bbox is not None:
            viewport = args.logical_viewport
            projected = clamp_to_viewport(
                union_bbox(baseline_bbox, test_bbox, args.padding_logical),
                viewport,
            )
            if projected is None:
                failures.append("projected impact union does not intersect logical viewport")
            else:
                roi = bbox_to_crop_roi(projected, baseline_image.size, viewport)
                region = {
                    "name": "projected_impact",
                    "roi": list(roi),
                    "logical_screen_bbox": projected,
                }
                points, excluded = actor_masked.points_for_region(
                    width,
                    height,
                    region=roi,
                    exclude_regions=args.exclude_region,
                )
                metrics = actor_masked.metric_block(
                    baseline_image,
                    test_image,
                    points,
                    source_pixels=roi[2] * roi[3],
                    excluded_pixels=excluded,
                )
                if args.heatmap is not None:
                    write_heatmap(args.heatmap, baseline_image, test_image, region, args.exclude_region)

            base_center = bbox_center(baseline_bbox)
            test_center = bbox_center(test_bbox)
            projection_delta = {
                "center_pixels": math.dist(base_center, test_center),
                "center": [test_center[0] - base_center[0], test_center[1] - base_center[1]],
                "area": bbox_area(test_bbox) - bbox_area(baseline_bbox),
                "area_pct": (
                    100.0 * (bbox_area(test_bbox) - bbox_area(baseline_bbox)) / bbox_area(baseline_bbox)
                    if bbox_area(baseline_bbox) > 0.0
                    else None
                ),
                "bbox": [test_bbox[index] - baseline_bbox[index] for index in range(4)],
            }
    except Exception as exc:  # pragma: no cover - command-line diagnostics
        failures.append(str(exc))

    identity_match = (
        baseline_impact.get("room") == test_impact.get("room")
        and baseline_impact.get("impact") == test_impact.get("impact")
        and baseline_impact.get("prop") == test_impact.get("prop")
        and baseline_impact.get("prop_pad") == test_impact.get("prop_pad")
    )

    result = {
        "status": "fail" if failures else "pass",
        "failures": failures,
        "baseline_trace": str(args.baseline_trace),
        "test_trace": str(args.test_trace),
        "baseline_frame": args.baseline_frame,
        "test_frame": args.test_frame,
        "alignment": alignment,
        "padding_logical": args.padding_logical,
        "exclude_regions": args.exclude_region,
        "selected": {
            "identity_match": identity_match,
            "baseline": impact_identity(baseline_impact),
            "test": impact_identity(test_impact),
        },
        "projection": {
            "baseline": baseline_projection,
            "test": test_projection,
            "delta": projection_delta,
        },
        "region": region,
        "metrics": metrics,
        "heatmap": str(args.heatmap) if args.heatmap else None,
    }
    return result, 1 if failures else 0


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline-frame", type=int, required=True)
    parser.add_argument("--test-frame", type=int, required=True)
    parser.add_argument("--logical-size", type=parse_pair, required=True)
    parser.add_argument("--logical-viewport", type=parse_rect, required=True)
    parser.add_argument("--baseline-logical-frame", choices=("active", "full"), default="active")
    parser.add_argument("--test-logical-frame", choices=("active", "full"), default="full")
    parser.add_argument("--active-threshold", type=int, default=0)
    parser.add_argument("--padding-logical", type=float, default=8.0)
    parser.add_argument("--exclude-region", action="append", type=screenshots.parse_region, default=[])
    parser.add_argument("--heatmap", type=Path)
    parser.add_argument("--json-out", type=Path)
    parser.add_argument("baseline_trace", type=Path)
    parser.add_argument("test_trace", type=Path)
    parser.add_argument("baseline", type=Path)
    parser.add_argument("test", type=Path)
    args = parser.parse_args(argv)

    if args.padding_logical < 0.0 or not math.isfinite(args.padding_logical):
        parser.error("--padding-logical must be a non-negative finite number")

    result, exit_code = compare(args)
    text = json.dumps(result, indent=2, sort_keys=True)
    if args.json_out:
        args.json_out.write_text(text + "\n", encoding="utf-8")
    print(text)
    return exit_code


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
