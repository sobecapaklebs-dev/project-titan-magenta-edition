#!/usr/bin/env python3
"""Compare stock/native pixels inside a TEXGEN material bbox mask.

This read-only diagnostic maps native TEXGEN-MATERIAL rows into the aligned
stock/native screenshot crop, builds a raster mask for matching material bboxes,
then splits each visual ROI into in-mask and out-of-mask pixel semantics.
"""

from __future__ import annotations

import argparse
from collections import Counter
import json
import sys
from pathlib import Path
from typing import Any, Iterable

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
import compare_roi_pixel_semantics as roi_semantics  # noqa: E402
import correlate_texgen_roi_pixel_semantics as texgen_corr  # noqa: E402


Point = tuple[int, int]
Pixel = tuple[int, int, int]


def load_json(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def route_regions(route: dict[str, Any]) -> dict[str, tuple[int, int, int, int]]:
    return roi_semantics.route_regions(route)


def normalize_hex(value: str | None) -> str | None:
    if value is None:
        return None
    text = str(value).strip().lower()
    if not text:
        return None
    try:
        return f"0x{int(text, 0):016x}"
    except ValueError:
        return text


def normalized_set(values: list[str] | None, hex_values: bool = False) -> set[str] | None:
    if not values:
        return None
    out: set[str] = set()
    for value in values:
        if value in ("*", "1"):
            return None
        normalized = normalize_hex(value) if hex_values else str(value)
        if normalized:
            out.add(normalized)
    return out


def signature_matches(row: dict[str, Any], args: argparse.Namespace) -> bool:
    sig = row.get("signature") or {}
    filters = (
        ("effect", normalized_set(args.effect)),
        ("class", normalized_set(args.material_class)),
        ("blend", normalized_set(args.blend)),
        ("api_blend", normalized_set(args.api_blend)),
        ("settex", normalized_set(args.settex)),
    )
    for key, allowed in filters:
        if allowed is not None and str(sig.get(key)) not in allowed:
            return False

    effcc = normalized_set(args.effcc, hex_values=True)
    if effcc is not None and normalize_hex(sig.get("effcc")) not in effcc:
        return False

    cc = normalized_set(args.cc, hex_values=True)
    if cc is not None and normalize_hex(sig.get("cc")) not in cc:
        return False

    raw = normalized_set(args.oml_raw, hex_values=True)
    if raw is not None and normalize_hex(sig.get("oml_raw")) not in raw:
        return False

    return True


def points_for_region(region: tuple[int, int, int, int], size: tuple[int, int]) -> set[Point]:
    width, height = size
    x, y, w, h = region
    left = max(0, min(width, x))
    top = max(0, min(height, y))
    right = max(0, min(width, x + w))
    bottom = max(0, min(height, y + h))
    return {(px, py) for py in range(top, bottom) for px in range(left, right)}


def points_for_rect(rect: list[float], size: tuple[int, int]) -> set[Point]:
    width, height = size
    ids = texgen_corr.raster_rect(rect, size)
    return {(idx % width, idx // width) for idx in ids if 0 <= idx < width * height}


def pairs_for_points(stock: Image.Image, native: Image.Image, points: Iterable[Point]) -> list[tuple[Pixel, Pixel]]:
    stock_px = stock.load()
    native_px = native.load()
    return [(stock_px[x, y], native_px[x, y]) for x, y in points]


def summarize_points(
    stock: Image.Image,
    native: Image.Image,
    points: set[Point],
    source_pixels: int,
    top: int,
) -> dict[str, Any]:
    summary = roi_semantics.pair_summary(pairs_for_points(stock, native, sorted(points)), top)
    summary["source_pixels"] = source_pixels
    summary["sampled_pct_of_source"] = 100.0 * len(points) / source_pixels if source_pixels else 0.0
    return summary


def changed_point_set(stock: Image.Image, native: Image.Image, points: set[Point]) -> set[Point]:
    stock_px = stock.load()
    native_px = native.load()
    return {point for point in points if roi_semantics.changed(stock_px[point], native_px[point])}


def row_summary(row: dict[str, Any]) -> dict[str, Any]:
    sig = row.get("signature") or {}
    return {
        "source": row.get("source"),
        "line": row.get("line"),
        "frame": row.get("frame"),
        "tri": row.get("tri"),
        "effect": sig.get("effect"),
        "class": sig.get("class"),
        "effcc": sig.get("effcc"),
        "cc": sig.get("cc"),
        "oml_raw": sig.get("oml_raw"),
        "blend": sig.get("blend"),
        "api_blend": sig.get("api_blend"),
        "settex": sig.get("settex"),
        "tex_wh": sig.get("tex_wh"),
        "shade0": row.get("shade0"),
        "shade1": row.get("shade1"),
        "shade2": row.get("shade2"),
        "env": row.get("env"),
        "prim": row.get("prim"),
        "fogc": row.get("fogc"),
        "aligned_bbox": row.get("aligned_bbox"),
    }


def summarize_region(
    name: str,
    region: tuple[int, int, int, int],
    stock: Image.Image,
    native: Image.Image,
    rows: list[dict[str, Any]],
    aligned_size: tuple[int, int],
    top: int,
) -> dict[str, Any]:
    region_points = points_for_region(region, aligned_size)
    mask_points: set[Point] = set()
    matched_rows: list[dict[str, Any]] = []
    effect_counts: Counter[str] = Counter()
    tex_keys: Counter[str] = Counter()

    for row in rows:
        overlap = texgen_corr.rect_overlap(row["aligned_bbox"], region)
        if overlap is None:
            continue
        overlap_points = points_for_rect(overlap, aligned_size)
        if not overlap_points:
            continue
        matched_rows.append(row)
        mask_points.update(overlap_points)
        sig = row.get("signature") or {}
        effect_counts[str(sig.get("effect"))] += 1
        key = "|".join([
            str(sig.get("effcc")),
            str(sig.get("oml_raw")),
            str(sig.get("tex_wh")),
            str(sig.get("load0_key")),
            str(sig.get("load1_key")),
        ])
        tex_keys[key] += 1

    mask_points &= region_points
    outside_points = region_points - mask_points
    changed_points = changed_point_set(stock, native, region_points)
    changed_in_mask = changed_points & mask_points
    changed_outside = changed_points - mask_points
    source_pixels = len(region_points)

    return {
        "roi": list(region),
        "source_pixels": source_pixels,
        "matched_rows": len(matched_rows),
        "effect_counts": dict(effect_counts),
        "mask": {
            "pixels": len(mask_points),
            "pct_of_region": 100.0 * len(mask_points) / source_pixels if source_pixels else 0.0,
            "changed_pixels": len(changed_in_mask),
            "changed_pct_of_all_changed": (
                100.0 * len(changed_in_mask) / len(changed_points)
                if changed_points else 0.0
            ),
            "changed_density_pct": (
                100.0 * len(changed_in_mask) / len(mask_points)
                if mask_points else 0.0
            ),
        },
        "outside_mask": {
            "pixels": len(outside_points),
            "pct_of_region": 100.0 * len(outside_points) / source_pixels if source_pixels else 0.0,
            "changed_pixels": len(changed_outside),
            "changed_pct_of_all_changed": (
                100.0 * len(changed_outside) / len(changed_points)
                if changed_points else 0.0
            ),
            "changed_density_pct": (
                100.0 * len(changed_outside) / len(outside_points)
                if outside_points else 0.0
            ),
        },
        "all_pixels": summarize_points(stock, native, region_points, source_pixels, top),
        "mask_pixels": summarize_points(stock, native, mask_points, source_pixels, top),
        "outside_mask_pixels": summarize_points(stock, native, outside_points, source_pixels, top),
        "top_material_keys": [
            {"key": key, "rows": count}
            for key, count in tex_keys.most_common(top)
        ],
        "examples": [row_summary(row) for row in matched_rows[:top]],
    }


def build_interpretation(payload: dict[str, Any]) -> list[str]:
    out: list[str] = []
    for name, region in payload["regions"].items():
        mask = region["mask"]
        outside = region["outside_mask"]
        all_pixels = region["all_pixels"]
        mask_delta = region["mask_pixels"].get("changed_delta") or {}
        outside_delta = region["outside_mask_pixels"].get("changed_delta") or {}
        mask_luma = (mask_delta.get("luma") or {}).get("mean")
        outside_luma = (outside_delta.get("luma") or {}).get("mean")
        out.append(
            f"{name}: mask covers {mask['pct_of_region']:.3f}% of ROI and "
            f"{mask['changed_pct_of_all_changed']:.3f}% of changed pixels; "
            f"all_changed={all_pixels['changed_pct']:.3f}%"
        )
        out.append(
            f"{name}: mask changed_density={mask['changed_density_pct']:.3f}% "
            f"luma_delta={mask_luma}; outside changed_density="
            f"{outside['changed_density_pct']:.3f}% luma_delta={outside_luma}"
        )
        if mask["changed_pct_of_all_changed"] < 50.0 and outside["changed_pct_of_all_changed"] > 50.0:
            out.append(
                f"{name}: most wrong pixels are outside the selected material mask; "
                "look for background/post-composite/non-texgen contributors"
            )
        elif mask["changed_pct_of_all_changed"] > 90.0:
            out.append(
                f"{name}: selected material mask owns nearly all wrong pixels; "
                "prioritize exact output semantics for that material"
            )
    return out


def compare(args: argparse.Namespace) -> tuple[dict[str, Any], int]:
    failures: list[str] = []
    route = load_json(args.route_json)
    route_name = str(route.get("name") or "unknown")
    if route_name == "unknown":
        failures.append("route JSON is missing name")

    stock_image = args.stock_image or args.base_case_dir / f"stock_{route_name}.ppm"
    native_image = args.native_image or args.base_case_dir / f"native_{route_name}.bmp"
    for path, label in (
        (stock_image, "stock image"),
        (native_image, "native image"),
        (args.route_json, "route JSON"),
    ):
        if not path.exists():
            failures.append(f"missing {label}: {path}")
    for path in args.log:
        if not path.exists():
            failures.append(f"missing log: {path}")

    regions = route_regions(route)
    selected_regions = args.region or ["tower_pane", "projected_impact", "impact_side"]
    for name in selected_regions:
        if name not in regions:
            failures.append(f"route has no visual region: {name}")

    payload: dict[str, Any] = {
        "status": "fail" if failures else "pass",
        "failures": failures,
        "route": route_name,
        "inputs": {
            "route_json": str(args.route_json),
            "stock_image": str(stock_image),
            "native_image": str(native_image),
            "logs": [str(path) for path in args.log],
            "filters": {
                "effect": args.effect,
                "class": args.material_class,
                "effcc": args.effcc,
                "cc": args.cc,
                "oml_raw": args.oml_raw,
                "blend": args.blend,
                "api_blend": args.api_blend,
                "settex": args.settex,
            },
        },
        "regions": {},
        "interpretation": [],
    }
    if failures:
        return payload, 1

    stock, native, alignment = roi_semantics.aligned_images(
        route,
        stock_image,
        native_image,
        args.active_threshold,
    )
    aligned_size = tuple(int(value) for value in stock.size)
    rows = texgen_corr.parse_rows(args.log, aligned_size)
    selected_rows, frame_selection = texgen_corr.select_rows(rows, args.frame)
    filtered_rows = [row for row in selected_rows if signature_matches(row, args)]

    payload["alignment"] = alignment
    payload["frame_selection"] = frame_selection
    payload["row_counts"] = {
        "parsed": len(rows),
        "selected_frame": len(selected_rows),
        "filtered": len(filtered_rows),
    }
    if not filtered_rows:
        payload["status"] = "fail"
        payload["failures"].append("no TEXGEN-MATERIAL rows matched the selected filters")
        return payload, 1

    for name in selected_regions:
        payload["regions"][name] = summarize_region(
            name,
            regions[name],
            stock,
            native,
            filtered_rows,
            aligned_size,
            args.top,
        )
    payload["interpretation"] = build_interpretation(payload)
    return payload, 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", nargs="+", type=Path)
    parser.add_argument("--route-json", type=Path, required=True)
    parser.add_argument("--base-case-dir", type=Path, required=True)
    parser.add_argument("--stock-image", type=Path)
    parser.add_argument("--native-image", type=Path)
    parser.add_argument("--active-threshold", type=roi_semantics.screenshots.channel_threshold, default=0)
    parser.add_argument("--frame", default="latest", help="'latest', 'all', or an integer native frame")
    parser.add_argument("--region", action="append")
    parser.add_argument("--effect", action="append")
    parser.add_argument("--material-class", action="append")
    parser.add_argument("--effcc", action="append")
    parser.add_argument("--cc", action="append")
    parser.add_argument("--oml-raw", action="append")
    parser.add_argument("--blend", action="append")
    parser.add_argument("--api-blend", action="append")
    parser.add_argument("--settex", action="append")
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
