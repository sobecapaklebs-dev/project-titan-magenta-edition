#!/usr/bin/env python3
"""Select stock RDP pixel-probe targets from projected glass-shard masks."""

from __future__ import annotations

import argparse
import json
import math
import sys
from pathlib import Path
from typing import Any

import compare_glass_shard_pixel_oracle as shard_oracle
import compare_glass_projected_visual as projected
import compare_screenshots as screenshots


def as_int(value: Any, default: int = 0) -> int:
    try:
        return int(value)
    except (TypeError, ValueError):
        return default


def luma(rgb: tuple[int, int, int]) -> float:
    return 0.2126 * rgb[0] + 0.7152 * rgb[1] + 0.0722 * rgb[2]


def load_route_defaults(path: Path | None, region_name: str | None) -> dict[str, Any]:
    if path is None:
        return {}
    with path.open("r", encoding="utf-8") as handle:
        route = json.load(handle)

    region_roi = None
    if region_name:
        for region in route.get("visual_regions", []) or []:
            if region.get("name") == region_name:
                roi = region.get("roi")
                if isinstance(roi, list) and len(roi) == 4:
                    region_roi = tuple(int(value) for value in roi)
                break
        if region_roi is None:
            raise SystemExit(f"FAIL: {path}: visual region {region_name!r} not found")

    return {
        "logical_size": tuple(route.get("visual_logical_size", ())) or None,
        "logical_viewport": tuple(route.get("visual_logical_viewport", ())) or None,
        "baseline_logical_frame": route.get("visual_baseline_logical_frame"),
        "test_logical_frame": route.get("visual_test_logical_frame"),
        "region_roi": region_roi,
    }


def crop_to_logical(
    x: int,
    y: int,
    crop_size: tuple[int, int],
    logical_viewport: tuple[int, int, int, int],
) -> tuple[int, int]:
    crop_w, crop_h = crop_size
    lx, ly, lw, lh = logical_viewport
    logical_x = lx + int(math.floor(((x + 0.5) * lw) / max(1, crop_w)))
    logical_y = ly + int(math.floor(((y + 0.5) * lh) / max(1, crop_h)))
    logical_x = max(lx, min(lx + lw - 1, logical_x))
    logical_y = max(ly, min(ly + lh - 1, logical_y))
    return logical_x, logical_y


def point_in_roi(point: tuple[int, int], roi: tuple[int, int, int, int] | None) -> bool:
    if roi is None:
        return True
    x, y = point
    rx, ry, rw, rh = roi
    return rx <= x < rx + rw and ry <= y < ry + rh


def far_enough(
    point: tuple[int, int],
    selected: list[dict[str, Any]],
    min_distance: float,
) -> bool:
    if min_distance <= 0.0:
        return True
    for item in selected:
        other = item["target"]
        dx = point[0] - other[0]
        dy = point[1] - other[1]
        if math.hypot(dx, dy) < min_distance:
            return False
    return True


def build_candidates(args: argparse.Namespace) -> dict[str, Any]:
    defaults = load_route_defaults(args.route, args.region)
    logical_size = args.logical_size or defaults.get("logical_size")
    logical_viewport = args.logical_viewport or defaults.get("logical_viewport")
    baseline_logical_frame = (
        args.baseline_logical_frame or defaults.get("baseline_logical_frame") or "active"
    )
    test_logical_frame = args.test_logical_frame or defaults.get("test_logical_frame") or "full"
    region_roi = args.roi or defaults.get("region_roi")

    if logical_size is None or len(logical_size) != 2:
        raise SystemExit("FAIL: provide --logical-size or a route with visual_logical_size")
    if logical_viewport is None or len(logical_viewport) != 4:
        raise SystemExit("FAIL: provide --logical-viewport or a route with visual_logical_viewport")

    baseline_frame, baseline_proj, baseline_samples = shard_oracle.load_projection(
        args.baseline_trace, args.baseline_frame, args.baseline_variant
    )
    test_frame, test_proj, test_samples = shard_oracle.load_projection(
        args.test_trace, args.test_frame, args.test_variant
    )
    viewport = projected.list4(baseline_proj.get("viewport")) or [
        float(logical_viewport[0]),
        float(logical_viewport[1]),
        float(logical_viewport[2]),
        float(logical_viewport[3]),
    ]

    open_args = argparse.Namespace(
        baseline_image=args.baseline_image,
        test_image=args.test_image,
        active_threshold=args.active_threshold,
        logical_size=logical_size,
        logical_viewport=logical_viewport,
        baseline_logical_frame=baseline_logical_frame,
        test_logical_frame=test_logical_frame,
    )
    baseline_crop, test_crop, crop_meta = shard_oracle.open_crops(open_args)
    crop_w, _crop_h = baseline_crop.size
    baseline_px = baseline_crop.load()
    test_px = test_crop.load()

    common_indices = sorted(set(baseline_samples) & set(test_samples))
    coverage: dict[shard_oracle.PixelId, int] = {}
    pieces_by_pixel: dict[shard_oracle.PixelId, list[int]] = {}
    for index in common_indices:
        mask = shard_oracle.rasterize_sample(
            baseline_samples[index],
            test_samples[index],
            viewport,
            baseline_crop.size,
            mode=args.mask_mode,
            padding=args.mask_padding,
        )
        for pixel_id in mask:
            coverage[pixel_id] = coverage.get(pixel_id, 0) + 1
            pieces_by_pixel.setdefault(pixel_id, []).append(index)

    candidates: list[dict[str, Any]] = []
    for pixel_id, overlap in coverage.items():
        y, x = divmod(pixel_id, crop_w)
        target = crop_to_logical(x, y, baseline_crop.size, logical_viewport)
        if not point_in_roi(target, region_roi):
            continue
        stock = baseline_px[x, y]
        native = test_px[x, y]
        abs_rgb = (
            abs(stock[0] - native[0]) +
            abs(stock[1] - native[1]) +
            abs(stock[2] - native[2])
        )
        if abs_rgb <= args.min_abs_rgb:
            continue
        luma_delta = luma(native) - luma(stock)
        candidates.append(
            {
                "target": [target[0], target[1]],
                "crop_xy": [x, y],
                "stock_rgb": list(stock),
                "native_rgb": list(native),
                "abs_rgb_delta": abs_rgb,
                "luma_delta": luma_delta,
                "overlap": overlap,
                "pieces": sorted(pieces_by_pixel.get(pixel_id, []))[:12],
                "piece_count": len(pieces_by_pixel.get(pixel_id, [])),
            }
        )

    candidates.sort(
        key=lambda item: (
            item["abs_rgb_delta"],
            abs(item["luma_delta"]),
            item["overlap"],
            -item["target"][1],
            -item["target"][0],
        ),
        reverse=True,
    )

    selected: list[dict[str, Any]] = []
    seen_targets: set[tuple[int, int]] = set()
    for item in candidates:
        target_tuple = (item["target"][0], item["target"][1])
        if target_tuple in seen_targets:
            continue
        if not far_enough(target_tuple, selected, args.min_distance):
            continue
        seen_targets.add(target_tuple)
        selected.append(item)
        if len(selected) >= args.top:
            break

    return {
        "baseline_frame": baseline_frame,
        "test_frame": test_frame,
        "baseline_projection": shard_oracle.projection_summary("baseline", baseline_frame, baseline_proj),
        "test_projection": shard_oracle.projection_summary("test", test_frame, test_proj),
        "logical_size": list(logical_size),
        "logical_viewport": list(logical_viewport),
        "region": args.region,
        "region_roi": list(region_roi) if region_roi else None,
        "mask_mode": args.mask_mode,
        "mask_padding": args.mask_padding,
        "crop": crop_meta,
        "candidate_pixels": len(candidates),
        "covered_pixels": len(coverage),
        "selected": selected,
    }


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline-trace", type=Path, required=True)
    parser.add_argument("--test-trace", type=Path, required=True)
    parser.add_argument("--baseline-image", type=Path, required=True)
    parser.add_argument("--test-image", type=Path, required=True)
    parser.add_argument("--route", type=Path)
    parser.add_argument("--region")
    parser.add_argument("--roi", type=screenshots.parse_roi)
    parser.add_argument("--logical-size", type=screenshots.parse_size)
    parser.add_argument("--logical-viewport", type=screenshots.parse_roi)
    parser.add_argument("--baseline-logical-frame", choices=("active", "full"))
    parser.add_argument("--test-logical-frame", choices=("active", "full"))
    parser.add_argument("--active-threshold", type=screenshots.channel_threshold, default=0)
    parser.add_argument("--baseline-frame", type=int)
    parser.add_argument("--test-frame", type=int)
    parser.add_argument("--baseline-variant")
    parser.add_argument("--test-variant")
    parser.add_argument("--mask-mode", choices=("triangle", "bbox"), default="triangle")
    parser.add_argument("--mask-padding", type=float, default=1.0)
    parser.add_argument("--min-abs-rgb", type=int, default=5)
    parser.add_argument("--min-distance", type=float, default=6.0)
    parser.add_argument("--top", type=int, default=12)
    parser.add_argument("--json-out", type=Path)
    args = parser.parse_args(argv)

    payload = build_candidates(args)
    if args.json_out:
        args.json_out.parent.mkdir(parents=True, exist_ok=True)
        args.json_out.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")

    print(
        "=== glass shard pixel targets: "
        f"baseline frame={payload['baseline_frame']} vs test frame={payload['test_frame']} ==="
    )
    print(
        f"covered_pixels={payload['covered_pixels']} "
        f"candidate_pixels={payload['candidate_pixels']} selected={len(payload['selected'])}"
    )
    if payload["region_roi"]:
        print(f"region {payload['region']}: roi={payload['region_roi']}")
    for index, item in enumerate(payload["selected"], 1):
        print(
            f"{index:2d}. target={item['target'][0]},{item['target'][1]} "
            f"stock={item['stock_rgb']} native={item['native_rgb']} "
            f"abs_rgb={item['abs_rgb_delta']} luma_delta={item['luma_delta']:.2f} "
            f"overlap={item['overlap']} pieces={item['pieces'][:6]}"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
