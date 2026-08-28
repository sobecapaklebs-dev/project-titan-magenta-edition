#!/usr/bin/env python3
"""Compare stock/native pixels inside localized effect-footprint bboxes.

This diagnostic consumes a pixel-semantics summary (for example the
pad-10092 bullet_impact_world summary) and reuses the same stock/native
screenshots from the base case. It answers whether the pixels inside the
native emitted effect footprint differ from stock, and how much of the broader
projected-impact ROI remains mismatched outside that footprint.
"""

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


def parse_float_list(value: str) -> list[float]:
    items: list[float] = []
    for raw in value.split(","):
        raw = raw.strip()
        if not raw:
            continue
        try:
            parsed = float(raw)
        except ValueError:
            raise argparse.ArgumentTypeError("expected comma-separated numbers") from None
        if not math.isfinite(parsed) or parsed < 0.0:
            raise argparse.ArgumentTypeError("padding values must be non-negative finite numbers")
        items.append(parsed)
    if not items:
        raise argparse.ArgumentTypeError("expected at least one padding value")
    return items


def load_json(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def numeric_bbox(value: Any) -> list[float] | None:
    if not isinstance(value, list) or len(value) != 4:
        return None
    try:
        bbox = [float(item) for item in value]
    except (TypeError, ValueError):
        return None
    if any(not math.isfinite(item) for item in bbox):
        return None
    if bbox[2] <= bbox[0] or bbox[3] <= bbox[1]:
        return None
    return bbox


def rect_area(rect: list[float] | None) -> float:
    if rect is None:
        return 0.0
    return max(0.0, rect[2] - rect[0]) * max(0.0, rect[3] - rect[1])


def rect_intersection(a: list[float] | None, b: list[float] | None) -> list[float] | None:
    if a is None or b is None:
        return None
    left = max(a[0], b[0])
    top = max(a[1], b[1])
    right = min(a[2], b[2])
    bottom = min(a[3], b[3])
    if right <= left or bottom <= top:
        return None
    return [left, top, right, bottom]


def rect_overlap(a: list[float] | None, b: list[float] | None) -> dict[str, float]:
    overlap = rect_intersection(a, b)
    area = rect_area(overlap)
    a_area = rect_area(a)
    b_area = rect_area(b)
    return {
        "area": area,
        "a_pct": 100.0 * area / a_area if a_area else 0.0,
        "b_pct": 100.0 * area / b_area if b_area else 0.0,
    }


def pad_bbox(bbox: list[float], padding: float) -> list[float]:
    return [
        bbox[0] - padding,
        bbox[1] - padding,
        bbox[2] + padding,
        bbox[3] + padding,
    ]


def clamp_to_viewport(bbox: list[float], viewport: list[float]) -> list[float] | None:
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


def logical_bbox_to_crop_roi(
    bbox: list[float],
    crop_size: tuple[int, int],
    viewport: list[float],
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


def metric_for_roi(
    baseline: Image.Image,
    test: Image.Image,
    roi: tuple[int, int, int, int],
    *,
    exclude_regions: list[dict[str, Any]] | None = None,
) -> dict[str, Any]:
    points, excluded = actor_masked.points_for_region(
        baseline.size[0],
        baseline.size[1],
        region=roi,
        exclude_regions=exclude_regions or [],
    )
    return actor_masked.metric_block(
        baseline,
        test,
        points,
        source_pixels=roi[2] * roi[3],
        excluded_pixels=excluded,
    )


def region_rect_from_route(route: dict[str, Any], name: str) -> list[float] | None:
    for region in route.get("regions", []):
        if not isinstance(region, dict) or region.get("name") != name:
            continue
        roi = region.get("roi")
        if not isinstance(roi, list) or len(roi) != 4:
            return None
        try:
            x, y, w, h = [float(item) for item in roi]
        except (TypeError, ValueError):
            return None
        return [x, y, x + w, y + h]
    return None


def group_key(group: dict[str, Any]) -> str:
    signature = group.get("signature") or {}
    return (
        signature.get("combcc")
        or signature.get("cc")
        or json.dumps(signature, sort_keys=True)[:80]
        or "unknown"
    )


def write_heatmap(
    path: Path,
    baseline: Image.Image,
    test: Image.Image,
    primary_roi: tuple[int, int, int, int] | None,
    footprint_rois: list[tuple[int, int, int, int]],
) -> None:
    width, height = baseline.size
    heatmap = Image.new("RGB", baseline.size)
    draw = ImageDraw.Draw(heatmap)
    for y in range(height):
        for x in range(width):
            a = baseline.getpixel((x, y))
            b = test.getpixel((x, y))
            diff = abs(a[0] - b[0]) + abs(a[1] - b[1]) + abs(a[2] - b[2])
            if diff <= screenshots.DIFF_THRESHOLD:
                heatmap.putpixel((x, y), (a[0] // 3, a[1] // 3, a[2] // 3))
            else:
                heatmap.putpixel((x, y), (min(255, diff * 255 // 200), 0, 0))
    if primary_roi is not None:
        x, y, w, h = primary_roi
        draw.rectangle((x, y, x + w - 1, y + h - 1), outline=(255, 220, 0), width=2)
    for roi in footprint_rois:
        x, y, w, h = roi
        draw.rectangle((x, y, x + w - 1, y + h - 1), outline=(0, 255, 128), width=2)
    heatmap.save(path)


def compare(args: argparse.Namespace) -> tuple[dict[str, Any], int]:
    failures: list[str] = []
    summary = load_json(args.summary)
    route = summary.get("route") or {}
    route_name = route.get("name")
    if not route_name:
        failures.append("summary route name is missing")
        route_name = "unknown"

    viewport = route.get("logical_viewport") or [0, 10, 320, 220]
    try:
        viewport = [float(item) for item in viewport]
    except (TypeError, ValueError):
        failures.append("summary logical viewport is invalid")
        viewport = [0.0, 10.0, 320.0, 220.0]

    base_case_dir = args.base_case_dir or Path((summary.get("pixel_evidence") or {}).get("base_case_dir", ""))
    if not base_case_dir:
        failures.append("base case dir is required")
    stock_image = base_case_dir / f"stock_{route_name}.ppm"
    native_image = base_case_dir / f"native_{route_name}.bmp"
    if not stock_image.exists():
        failures.append(f"missing stock screenshot: {stock_image}")
    if not native_image.exists():
        failures.append(f"missing native screenshot: {native_image}")

    groups = (((summary.get(args.group_source) or {}).get("groups")) or [])
    if not isinstance(groups, list) or not groups:
        failures.append(f"summary has no groups at {args.group_source}.groups")
        groups = []

    primary_rect = region_rect_from_route(route, args.primary_region)
    if primary_rect is None:
        failures.append(f"summary route has no primary region: {args.primary_region}")
    primary_bbox = clamp_to_viewport(primary_rect, viewport) if primary_rect is not None else None

    baseline = test = None
    alignment: dict[str, Any] = {}
    primary_roi = None
    footprint_rois: list[tuple[int, int, int, int]] = []
    group_payloads: list[dict[str, Any]] = []
    primary_metrics: dict[str, Any] | None = None
    primary_minus_by_padding: list[dict[str, Any]] = []

    if not failures:
        align_args = argparse.Namespace(
            baseline=stock_image,
            test=native_image,
            active_threshold=args.active_threshold,
            logical_size=tuple(route.get("logical_size") or [320, 240]),
            logical_viewport=tuple(viewport),
            baseline_logical_frame=args.baseline_logical_frame,
            test_logical_frame=args.test_logical_frame,
        )
        baseline, test, alignment = actor_masked.open_and_align(align_args)
        if primary_bbox is not None:
            primary_roi = logical_bbox_to_crop_roi(primary_bbox, baseline.size, viewport)
            primary_metrics = metric_for_roi(baseline, test, primary_roi)

        for index, group in enumerate(groups):
            logical_bbox = numeric_bbox(group.get("logical_bbox"))
            if logical_bbox is None:
                failures.append(f"group {index} has invalid logical_bbox")
                continue
            overlap = rect_overlap(logical_bbox, primary_bbox)
            group_result = {
                "index": index,
                "key": group_key(group),
                "signature": group.get("signature"),
                "count": group.get("count"),
                "frames": group.get("frames"),
                "triangles": group.get("triangles"),
                "logical_bbox": logical_bbox,
                "primary_overlap": overlap,
                "padded": [],
            }
            for padding in args.padding_logical:
                padded = clamp_to_viewport(pad_bbox(logical_bbox, padding), viewport)
                if padded is None:
                    continue
                roi = logical_bbox_to_crop_roi(padded, baseline.size, viewport)
                metrics = metric_for_roi(baseline, test, roi)
                footprint_rois.append(roi)
                group_result["padded"].append(
                    {
                        "padding_logical": padding,
                        "logical_bbox": padded,
                        "roi": list(roi),
                        "metrics": metrics,
                        "primary_overlap": rect_overlap(padded, primary_bbox),
                    }
                )
            group_payloads.append(group_result)

        if primary_roi is not None:
            for padding in args.padding_logical:
                padding_rois: list[tuple[int, int, int, int]] = []
                for group in groups:
                    logical_bbox = numeric_bbox(group.get("logical_bbox"))
                    if logical_bbox is None:
                        continue
                    padded = clamp_to_viewport(pad_bbox(logical_bbox, padding), viewport)
                    if padded is None:
                        continue
                    padding_rois.append(logical_bbox_to_crop_roi(padded, baseline.size, viewport))
                excludes = [
                    {"name": f"footprint_{index}", "roi": list(roi)}
                    for index, roi in enumerate(padding_rois)
                ]
                primary_minus_by_padding.append(
                    {
                        "padding_logical": padding,
                        "excluded_rois": [list(roi) for roi in padding_rois],
                        "metrics": metric_for_roi(
                            baseline,
                            test,
                            primary_roi,
                            exclude_regions=excludes,
                        ),
                    }
                )

        if args.heatmap is not None:
            write_heatmap(args.heatmap, baseline, test, primary_roi, footprint_rois)

    if not group_payloads and not failures:
        failures.append("no effect footprint groups were scored")

    interpretation: list[str] = []
    if group_payloads:
        max_primary_b_pct = max(
            (item.get("primary_overlap") or {}).get("b_pct", 0.0)
            for item in group_payloads
        )
        interpretation.append(
            f"largest unpadded effect footprint covers {max_primary_b_pct:.3f}% of {args.primary_region}"
        )
    if primary_metrics is not None:
        interpretation.append(
            f"{args.primary_region} changed_pct is {primary_metrics.get('changed_pct'):.3f}%"
        )
    for item in primary_minus_by_padding:
        metrics = item.get("metrics") or {}
        interpretation.append(
            f"primary ROI excluding padding {item['padding_logical']:.3f} footprints "
            f"changed_pct is {metrics.get('changed_pct'):.3f}% "
            f"(excluded {metrics.get('excluded_pct'):.3f}%)"
        )

    payload = {
        "status": "fail" if failures else "pass",
        "failures": failures,
        "summary": str(args.summary),
        "base_case_dir": str(base_case_dir) if base_case_dir else None,
        "route": route_name,
        "group_source": args.group_source,
        "primary_region": args.primary_region,
        "padding_logical": args.padding_logical,
        "alignment": alignment,
        "primary": {
            "logical_bbox": primary_bbox,
            "roi": list(primary_roi) if primary_roi else None,
            "metrics": primary_metrics,
        },
        "primary_minus_by_padding": primary_minus_by_padding,
        "groups": group_payloads,
        "heatmap": str(args.heatmap) if args.heatmap else None,
        "interpretation": interpretation,
    }
    return payload, 1 if failures else 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("summary", type=Path, help="pixel-semantics summary JSON")
    parser.add_argument("--base-case-dir", type=Path, help="override stock/native base case directory")
    parser.add_argument("--group-source", choices=("material", "effect_triangles"), default="effect_triangles")
    parser.add_argument("--primary-region", default="projected_impact")
    parser.add_argument("--padding-logical", type=parse_float_list, default=[0.0, 2.0, 8.0])
    parser.add_argument("--baseline-logical-frame", choices=("active", "full"), default="active")
    parser.add_argument("--test-logical-frame", choices=("active", "full"), default="full")
    parser.add_argument("--active-threshold", type=screenshots.channel_threshold, default=0)
    parser.add_argument("--heatmap", type=Path)
    parser.add_argument("--json-out", type=Path)
    args = parser.parse_args(argv)

    if not args.summary.exists():
        parser.error(f"summary not found: {args.summary}")
    if args.base_case_dir is not None and not args.base_case_dir.exists():
        parser.error(f"base case dir not found: {args.base_case_dir}")

    payload, status = compare(args)
    encoded = json.dumps(payload, indent=2, sort_keys=True) + "\n"
    if args.json_out is not None:
        args.json_out.parent.mkdir(parents=True, exist_ok=True)
        args.json_out.write_text(encoded, encoding="utf-8")
    print(encoded, end="")
    return status


if __name__ == "__main__":
    raise SystemExit(main())
