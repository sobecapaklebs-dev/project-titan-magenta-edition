#!/usr/bin/env python3
"""Score whether presentation/crop alignment explains a visual mismatch.

The tool compares two screenshots after applying the same logical-viewport crop
rules used by the visual regression scripts, then enumerates small alternate
test-image crop offsets and size deltas. It is a diagnostic scorer: a large
score improvement means viewport/presentation mapping deserves attention; a
small improvement means the remaining mismatch is more likely material/output
semantics inside an already aligned view.
"""

from __future__ import annotations

import argparse
import json
import math
import sys
from pathlib import Path
from typing import Any

from PIL import Image

try:
    import numpy as np
except ImportError:  # pragma: no cover - numpy is available in the dev env
    np = None  # type: ignore[assignment]

sys.path.insert(0, str(Path(__file__).resolve().parent))
import compare_actor_masked_visual as actor_masked  # noqa: E402
import compare_screenshots as screenshots  # noqa: E402


def unique_modes(values: list[str] | None) -> list[str]:
    if not values:
        return ["active", "full"]
    out: list[str] = []
    for value in values:
        if value not in out:
            out.append(value)
    return out


def int_range(limit: int, step: int) -> range:
    return range(-limit, limit + 1, step)


def crop_for(
    image: Image.Image,
    *,
    active_threshold: int,
    logical_size: tuple[int, int],
    logical_viewport: tuple[int, int, int, int],
    frame_mode: str,
) -> tuple[int, int, int, int]:
    return screenshots.logical_crop_bbox(
        image,
        active_threshold,
        logical_size,
        logical_viewport,
        frame_mode,
    )


def adjust_crop(
    crop: tuple[int, int, int, int],
    *,
    dx: int,
    dy: int,
    dw: int,
    dh: int,
    image_size: tuple[int, int],
) -> tuple[int, int, int, int] | None:
    x, y, w, h = crop
    new_w = w + dw
    new_h = h + dh
    if new_w <= 0 or new_h <= 0:
        return None
    new_x = int(round(x - dw * 0.5)) + dx
    new_y = int(round(y - dh * 0.5)) + dy
    image_w, image_h = image_size
    if new_x < 0 or new_y < 0 or new_x + new_w > image_w or new_y + new_h > image_h:
        return None
    return new_x, new_y, new_w, new_h


def make_region_mask(
    size: tuple[int, int],
    region: dict[str, Any] | None,
    exclude_regions: list[dict[str, Any]],
) -> tuple[tuple[int, int, int, int], Any, int]:
    width, height = size
    if region is None:
        roi = (0, 0, width, height)
    else:
        roi = tuple(region["roi"])
    rx, ry, rw, rh = roi
    if rx < 0 or ry < 0 or rw <= 0 or rh <= 0 or rx + rw > width or ry + rh > height:
        raise ValueError(f"primary region out of bounds: {rx},{ry},{rw},{rh} for {width}x{height}")

    if np is None:
        return roi, None, 0

    mask = np.ones((rh, rw), dtype=bool)
    excluded = 0
    region_rect = (rx, ry, rw, rh)
    for exclude in exclude_regions:
        intersection = actor_masked.rect_intersection(region_rect, tuple(exclude["roi"]))
        if intersection is None:
            continue
        ix, iy, iw, ih = intersection
        local_x = ix - rx
        local_y = iy - ry
        mask[local_y : local_y + ih, local_x : local_x + iw] = False
        excluded += iw * ih
    return roi, mask, excluded


def changed_pct_numpy(
    baseline: Image.Image,
    test: Image.Image,
    roi: tuple[int, int, int, int],
    mask: Any,
) -> float:
    assert np is not None
    rx, ry, rw, rh = roi
    base = np.asarray(baseline, dtype=np.int16)[ry : ry + rh, rx : rx + rw, :3]
    trial = np.asarray(test, dtype=np.int16)[ry : ry + rh, rx : rx + rw, :3]
    changed = np.abs(base - trial).sum(axis=2) > screenshots.DIFF_THRESHOLD
    if mask is not None:
        changed = changed & mask
        total = int(mask.sum())
    else:
        total = rw * rh
    if total <= 0:
        return 0.0
    return 100.0 * int(changed.sum()) / total


def region_metric(
    baseline: Image.Image,
    test: Image.Image,
    region: dict[str, Any] | None,
    exclude_regions: list[dict[str, Any]],
) -> dict[str, Any]:
    width, height = baseline.size
    if region is None:
        points, excluded = actor_masked.points_for_region(width, height, exclude_regions=exclude_regions)
        source_pixels = width * height
    else:
        roi = tuple(region["roi"])
        points, excluded = actor_masked.points_for_region(
            width,
            height,
            region=roi,
            exclude_regions=exclude_regions,
        )
        source_pixels = roi[2] * roi[3]
    return actor_masked.metric_block(
        baseline,
        test,
        points,
        source_pixels=source_pixels,
        excluded_pixels=excluded,
    )


def compare_candidate(
    source_test: Image.Image,
    baseline_crop: Image.Image,
    test_crop: tuple[int, int, int, int],
    *,
    region_roi: tuple[int, int, int, int],
    region_mask: Any,
) -> tuple[float, Image.Image]:
    test = screenshots.crop_bbox(source_test, test_crop)
    if test.size != baseline_crop.size:
        resampling = getattr(Image, "Resampling", Image).BILINEAR
        test = test.resize(baseline_crop.size, resampling)
    if np is not None:
        score = changed_pct_numpy(baseline_crop, test, region_roi, region_mask)
    else:
        score = region_metric(baseline_crop, test, None, [])["changed_pct"]
    return score, test


def compare(args: argparse.Namespace) -> tuple[dict[str, Any], int]:
    failures: list[str] = []
    source_baseline = Image.open(args.baseline).convert("RGB")
    source_test = Image.open(args.test).convert("RGB")
    baseline_modes = unique_modes(args.baseline_logical_frame)
    test_modes = unique_modes(args.test_logical_frame)
    regions = args.region or []
    primary_region = next((region for region in regions if region["name"] == args.primary_region), None)
    if primary_region is None and args.primary_region != "full":
        failures.append(f"primary region not found: {args.primary_region}")
    if primary_region is None and args.primary_region == "full":
        primary_region = None

    candidates: list[dict[str, Any]] = []
    default_candidate: dict[str, Any] | None = None
    best_test_image: Image.Image | None = None
    best_baseline_image: Image.Image | None = None
    best_region: dict[str, Any] | None = None
    best_score = math.inf

    for baseline_mode in baseline_modes:
        try:
            base_crop = crop_for(
                source_baseline,
                active_threshold=args.active_threshold,
                logical_size=args.logical_size,
                logical_viewport=args.logical_viewport,
                frame_mode=baseline_mode,
            )
        except ValueError as exc:
            failures.append(f"baseline {baseline_mode}: {exc}")
            continue
        baseline_image = screenshots.crop_bbox(source_baseline, base_crop)
        try:
            region_roi, region_mask, excluded_pixels = make_region_mask(
                baseline_image.size,
                primary_region,
                args.exclude_region,
            )
        except ValueError as exc:
            failures.append(f"baseline {baseline_mode}: {exc}")
            continue

        for test_mode in test_modes:
            try:
                original_test_crop = crop_for(
                    source_test,
                    active_threshold=args.active_threshold,
                    logical_size=args.logical_size,
                    logical_viewport=args.logical_viewport,
                    frame_mode=test_mode,
                )
            except ValueError as exc:
                failures.append(f"test {test_mode}: {exc}")
                continue

            for dw in int_range(args.max_size_delta_px, args.size_step):
                for dh in int_range(args.max_size_delta_px, args.size_step):
                    for dx in int_range(args.max_offset_px, args.offset_step):
                        for dy in int_range(args.max_offset_px, args.offset_step):
                            adjusted = adjust_crop(
                                original_test_crop,
                                dx=dx,
                                dy=dy,
                                dw=dw,
                                dh=dh,
                                image_size=source_test.size,
                            )
                            if adjusted is None:
                                continue
                            score, test_image = compare_candidate(
                                source_test,
                                baseline_image,
                                adjusted,
                                region_roi=region_roi,
                                region_mask=region_mask,
                            )
                            candidate = {
                                "changed_pct": score,
                                "baseline_logical_frame": baseline_mode,
                                "test_logical_frame": test_mode,
                                "baseline_crop": list(base_crop),
                                "test_crop_base": list(original_test_crop),
                                "test_crop": list(adjusted),
                                "adjustment": {"dx": dx, "dy": dy, "dw": dw, "dh": dh},
                                "aligned_size": list(baseline_image.size),
                                "primary_region": args.primary_region,
                                "primary_region_roi": list(region_roi),
                                "primary_region_excluded_pixels": excluded_pixels,
                            }
                            candidates.append(candidate)
                            if (
                                baseline_mode == args.default_baseline_logical_frame
                                and test_mode == args.default_test_logical_frame
                                and dx == 0
                                and dy == 0
                                and dw == 0
                                and dh == 0
                            ):
                                default_candidate = candidate
                            if score < best_score:
                                best_score = score
                                best_test_image = test_image
                                best_baseline_image = baseline_image
                                best_region = primary_region

    candidates.sort(key=lambda item: item["changed_pct"])

    if default_candidate is None and candidates:
        default_candidate = next(
            (
                item
                for item in candidates
                if item["adjustment"] == {"dx": 0, "dy": 0, "dw": 0, "dh": 0}
            ),
            candidates[0],
        )
    best = candidates[0] if candidates else None

    improvement = None
    best_metric = None
    default_metric = None
    if best is not None and default_candidate is not None:
        improvement = default_candidate["changed_pct"] - best["changed_pct"]

    if best is not None and best_test_image is not None and best_baseline_image is not None:
        best_metric = region_metric(best_baseline_image, best_test_image, best_region, args.exclude_region)

    if default_candidate is not None:
        base_crop = tuple(default_candidate["baseline_crop"])
        test_crop = tuple(default_candidate["test_crop"])
        baseline_image = screenshots.crop_bbox(source_baseline, base_crop)
        test_image = screenshots.crop_bbox(source_test, test_crop)
        if test_image.size != baseline_image.size:
            resampling = getattr(Image, "Resampling", Image).BILINEAR
            test_image = test_image.resize(baseline_image.size, resampling)
        default_metric = region_metric(baseline_image, test_image, primary_region, args.exclude_region)

    if not candidates and not failures:
        failures.append("no valid presentation candidates")

    result = {
        "status": "fail" if failures else "pass",
        "failures": failures,
        "baseline": str(args.baseline),
        "test": str(args.test),
        "active_threshold": args.active_threshold,
        "source_presentation": {
            "baseline": screenshots.presentation_metrics(source_baseline, args.active_threshold),
            "test": screenshots.presentation_metrics(source_test, args.active_threshold),
        },
        "search": {
            "logical_size": list(args.logical_size),
            "logical_viewport": list(args.logical_viewport),
            "baseline_logical_frames": baseline_modes,
            "test_logical_frames": test_modes,
            "default_baseline_logical_frame": args.default_baseline_logical_frame,
            "default_test_logical_frame": args.default_test_logical_frame,
            "max_offset_px": args.max_offset_px,
            "offset_step": args.offset_step,
            "max_size_delta_px": args.max_size_delta_px,
            "size_step": args.size_step,
            "candidate_count": len(candidates),
        },
        "primary_region": args.primary_region,
        "exclude_regions": args.exclude_region,
        "default": default_candidate,
        "default_metric": default_metric,
        "best": best,
        "best_metric": best_metric,
        "improvement_pct_points": improvement,
        "top": candidates[: args.top],
    }
    return result, 1 if failures else 0


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("baseline", type=Path, help="stock/baseline screenshot")
    parser.add_argument("test", type=Path, help="native/test screenshot")
    parser.add_argument("--json-out", type=Path)
    parser.add_argument("--logical-size", type=screenshots.parse_size, default=(320, 240))
    parser.add_argument("--logical-viewport", type=screenshots.parse_roi, default=(0, 10, 320, 220))
    parser.add_argument("--baseline-logical-frame", choices=("active", "full"), action="append")
    parser.add_argument("--test-logical-frame", choices=("active", "full"), action="append")
    parser.add_argument("--default-baseline-logical-frame", choices=("active", "full"), default="active")
    parser.add_argument("--default-test-logical-frame", choices=("active", "full"), default="full")
    parser.add_argument("--active-threshold", type=screenshots.channel_threshold, default=0)
    parser.add_argument("--region", type=screenshots.parse_region, action="append", default=[])
    parser.add_argument("--exclude-region", type=screenshots.parse_region, action="append", default=[])
    parser.add_argument("--primary-region", default="full")
    parser.add_argument("--max-offset-px", type=int, default=8)
    parser.add_argument("--offset-step", type=int, default=2)
    parser.add_argument("--max-size-delta-px", type=int, default=32)
    parser.add_argument("--size-step", type=int, default=8)
    parser.add_argument("--top", type=int, default=12)
    args = parser.parse_args(argv)

    for name in ("max_offset_px", "offset_step", "max_size_delta_px", "size_step", "top"):
        value = getattr(args, name)
        if value <= 0:
            parser.error(f"--{name.replace('_', '-')} must be positive")
    if args.max_offset_px % args.offset_step != 0:
        parser.error("--max-offset-px must be divisible by --offset-step")
    if args.max_size_delta_px % args.size_step != 0:
        parser.error("--max-size-delta-px must be divisible by --size-step")
    return args


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    result, status = compare(args)
    text = json.dumps(result, indent=2, sort_keys=True)
    if args.json_out:
        args.json_out.write_text(text + "\n", encoding="utf-8")
    print(text)
    return status


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
