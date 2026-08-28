#!/usr/bin/env python3
"""Summarize TEXGEN-MATERIAL rows that overlap route visual ROIs."""

from __future__ import annotations

import argparse
import json
import math
import re
import sys
from collections import defaultdict
from pathlib import Path
from typing import Any


NUMBER_RE = r"[-+]?(?:\d+(?:\.\d*)?|\.\d+)"
TEXGEN_RE = re.compile(r"\[TEXGEN-MATERIAL\]\s+(?P<body>.*)")


def value_for(body: str, key: str) -> str | None:
    pattern = re.compile(
        rf"(?:^|\s){re.escape(key)}="
        rf"(?P<value>\{{[^}}]*\}}|\([^)]*\)|\[[^\]]*\]|\S+)"
    )
    match = pattern.search(body)
    return match.group("value") if match else None


def int_for(body: str, key: str) -> int | None:
    value = value_for(body, key)
    if value is None:
        return None
    try:
        return int(value, 0)
    except ValueError:
        return None


def parse_bbox(body: str, key: str) -> list[float] | None:
    match = re.search(
        rf"(?:^|\s){re.escape(key)}="
        rf"\[\s*({NUMBER_RE})\s*,\s*({NUMBER_RE})\s*\]"
        rf"\s*-\s*"
        rf"\[\s*({NUMBER_RE})\s*,\s*({NUMBER_RE})\s*\]",
        body,
    )
    if not match:
        return None
    bbox = [float(match.group(index)) for index in range(1, 5)]
    if any(not math.isfinite(item) for item in bbox):
        return None
    if bbox[2] <= bbox[0] or bbox[3] <= bbox[1]:
        return None
    return bbox


def parse_tuple_numbers(value: str | None) -> list[float]:
    if value is None:
        return []
    return [float(item) for item in re.findall(NUMBER_RE, value)]


def rect_area(rect: list[float] | None) -> float:
    if rect is None:
        return 0.0
    return max(0.0, rect[2] - rect[0]) * max(0.0, rect[3] - rect[1])


def rect_union(a: list[float] | None, b: list[float] | None) -> list[float] | None:
    if a is None:
        return b
    if b is None:
        return a
    return [min(a[0], b[0]), min(a[1], b[1]), max(a[2], b[2]), max(a[3], b[3])]


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


def ndc_to_logical(bbox: list[float] | None, viewport: list[float]) -> list[float] | None:
    if bbox is None:
        return None
    vx, vy, vw, vh = viewport
    return [
        vx + (bbox[0] + 1.0) * 0.5 * vw,
        vy + (1.0 - bbox[3]) * 0.5 * vh,
        vx + (bbox[2] + 1.0) * 0.5 * vw,
        vy + (1.0 - bbox[1]) * 0.5 * vh,
    ]


def ndc_to_aligned_crop(bbox: list[float] | None, aligned_size: list[float]) -> list[float] | None:
    if bbox is None:
        return None
    width, height = aligned_size
    return [
        (bbox[0] + 1.0) * 0.5 * width,
        (1.0 - bbox[3]) * 0.5 * height,
        (bbox[2] + 1.0) * 0.5 * width,
        (1.0 - bbox[1]) * 0.5 * height,
    ]


def route_aligned_size(route: dict[str, Any], override: str | None) -> list[float]:
    if override:
        parts = override.lower().replace("x", ",").split(",")
        if len(parts) != 2:
            raise ValueError("--aligned-size must be WIDTH,HEIGHT")
        width, height = [float(part) for part in parts]
        if width <= 0.0 or height <= 0.0:
            raise ValueError("--aligned-size must be positive")
        return [width, height]

    logical_size = route.get("visual_logical_size") or route.get("logical_size") or [320, 240]
    viewport = route.get("visual_logical_viewport") or route.get("logical_viewport") or [0, 10, 320, 220]
    native_config = route.get("native_config") or {}
    window_width = native_config.get("Video.WindowWidth")
    window_height = native_config.get("Video.WindowHeight")
    if isinstance(window_width, (int, float)) and isinstance(window_height, (int, float)):
        logical_w = float(logical_size[0])
        logical_h = float(logical_size[1])
        return [
            float(window_width) * float(viewport[2]) / logical_w,
            float(window_height) * float(viewport[3]) / logical_h,
        ]
    return [float(viewport[2]), float(viewport[3])]


def route_region_rects(route: dict[str, Any]) -> dict[str, list[float]]:
    regions: dict[str, list[float]] = {}
    for item in route.get("visual_regions", route.get("regions", [])):
        if not isinstance(item, dict) or not item.get("name"):
            continue
        roi = item.get("roi")
        if not isinstance(roi, list) or len(roi) != 4:
            continue
        try:
            x, y, w, h = [float(value) for value in roi]
        except (TypeError, ValueError):
            continue
        regions[item["name"]] = [x, y, x + w, y + h]
    return regions


def load_route(path: Path) -> dict[str, Any]:
    data = json.loads(path.read_text(encoding="utf-8"))
    if "visual_regions" in data:
        return data
    return {
        "name": data.get("name"),
        "visual_logical_size": data.get("visual_logical_size", data.get("logical_size")),
        "visual_logical_viewport": data.get("visual_logical_viewport", data.get("logical_viewport")),
        "visual_regions": data.get("visual_regions", data.get("regions", [])),
    }


def signature_key(body: str) -> tuple[tuple[str, str | None], ...]:
    keys = (
        "class",
        "effect",
        "cc",
        "effcc",
        "combcc",
        "opts",
        "oml_raw",
        "oml",
        "omh",
        "geom",
        "inputs",
        "lodfrac",
        "tex_wh",
        "bound",
        "blend",
        "api_blend",
        "alpha",
        "fog",
        "texedge",
        "depth",
        "settex",
    )
    return tuple((key, value_for(body, key)) for key in keys)


def parse_log(paths: list[Path], aligned_size: list[float], regions: dict[str, list[float]]) -> dict[str, Any]:
    rows: list[dict[str, Any]] = []
    groups: dict[tuple[tuple[str, str | None], ...], dict[str, Any]] = {}

    for path in paths:
        with path.open("r", encoding="utf-8", errors="replace") as handle:
            for line_no, line in enumerate(handle, start=1):
                match = TEXGEN_RE.search(line)
                if not match:
                    continue
                body = match.group("body")
                if int_for(body, "ndc_ok") != 1:
                    continue
                ndc_bbox = parse_bbox(body, "ndc")
                logical_bbox = ndc_to_aligned_crop(ndc_bbox, aligned_size)
                if logical_bbox is None:
                    continue
                overlaps = {
                    name: rect_overlap(logical_bbox, rect)
                    for name, rect in regions.items()
                }
                if not any(value["area"] > 0.0 for value in overlaps.values()):
                    continue
                row = {
                    "source": str(path),
                    "line": line_no,
                    "frame": int_for(body, "frame"),
                    "tri": int_for(body, "tri"),
                    "class": value_for(body, "class"),
                    "effect": value_for(body, "effect"),
                    "cc": value_for(body, "cc"),
                    "effcc": value_for(body, "effcc"),
                    "combcc": value_for(body, "combcc"),
                    "opts": value_for(body, "opts"),
                    "oml_raw": value_for(body, "oml_raw"),
                    "oml": value_for(body, "oml"),
                    "omh": value_for(body, "omh"),
                    "geom": value_for(body, "geom"),
                    "tex_wh": value_for(body, "tex_wh"),
                    "load0": value_for(body, "load0"),
                    "load1": value_for(body, "load1"),
                    "bound": value_for(body, "bound"),
                    "blend": value_for(body, "blend"),
                    "api_blend": value_for(body, "api_blend"),
                    "alpha": value_for(body, "alpha"),
                    "fog": value_for(body, "fog"),
                    "texedge": value_for(body, "texedge"),
                    "depth": value_for(body, "depth"),
                    "settex": value_for(body, "settex"),
                    "shade0": parse_tuple_numbers(value_for(body, "shade0"))[:4],
                    "env": parse_tuple_numbers(value_for(body, "env"))[:4],
                    "prim": parse_tuple_numbers(value_for(body, "prim"))[:4],
                    "fogc": parse_tuple_numbers(value_for(body, "fogc"))[:4],
                    "ndc_bbox": ndc_bbox,
                    "logical_bbox": logical_bbox,
                    "overlaps": overlaps,
                }
                rows.append(row)

                key = signature_key(body)
                group = groups.setdefault(
                    key,
                    {
                        "signature": {name: value for name, value in key},
                        "count": 0,
                        "frames": [],
                        "triangles": [],
                        "logical_bbox": None,
                        "overlap_area_sum": defaultdict(float),
                        "max_overlap_b_pct": defaultdict(float),
                        "source_examples": [],
                        "shade0_min": None,
                        "shade0_max": None,
                    },
                )
                group["count"] += 1
                if row["frame"] is not None:
                    group["frames"].append(row["frame"])
                if row["tri"] is not None:
                    group["triangles"].append(row["tri"])
                group["logical_bbox"] = rect_union(group["logical_bbox"], logical_bbox)
                for name, overlap in overlaps.items():
                    group["overlap_area_sum"][name] += overlap["area"]
                    group["max_overlap_b_pct"][name] = max(group["max_overlap_b_pct"][name], overlap["b_pct"])
                if len(group["source_examples"]) < 3:
                    group["source_examples"].append({"source": str(path), "line": line_no})
                shade = row["shade0"]
                if shade:
                    if group["shade0_min"] is None:
                        group["shade0_min"] = shade
                        group["shade0_max"] = shade
                    else:
                        group["shade0_min"] = [min(group["shade0_min"][i], shade[i]) for i in range(len(shade))]
                        group["shade0_max"] = [max(group["shade0_max"][i], shade[i]) for i in range(len(shade))]

    return {"rows": rows, "groups": groups}


def span(values: list[int]) -> dict[str, Any]:
    if not values:
        return {"count": 0, "min": None, "max": None, "unique": []}
    return {"count": len(values), "min": min(values), "max": max(values), "unique": sorted(set(values))}


def finalize_groups(
    groups: dict[Any, dict[str, Any]],
    regions: dict[str, list[float]],
    primary_region: str,
) -> list[dict[str, Any]]:
    out: list[dict[str, Any]] = []
    for group in groups.values():
        bbox = group["logical_bbox"]
        out.append(
            {
                "signature": group["signature"],
                "count": group["count"],
                "frames": span(group["frames"]),
                "triangles": span(group["triangles"]),
                "logical_bbox": bbox,
                "region_overlaps": {
                    name: rect_overlap(bbox, rect)
                    for name, rect in regions.items()
                },
                "overlap_area_sum": dict(group["overlap_area_sum"]),
                "max_overlap_b_pct": dict(group["max_overlap_b_pct"]),
                "shade0_min": group["shade0_min"],
                "shade0_max": group["shade0_max"],
                "source_examples": group["source_examples"],
            }
        )
    out.sort(
        key=lambda item: (
            -float((item.get("max_overlap_b_pct") or {}).get(primary_region, 0.0)),
            -int(item.get("count") or 0),
            json.dumps(item.get("signature"), sort_keys=True),
        )
    )
    return out


def summarize(args: argparse.Namespace) -> tuple[dict[str, Any], int]:
    failures: list[str] = []
    route = load_route(args.route_json)
    viewport = route.get("visual_logical_viewport") or [0, 10, 320, 220]
    viewport = [float(item) for item in viewport]
    try:
        aligned_size = route_aligned_size(route, args.aligned_size)
    except ValueError as exc:
        aligned_size = [0.0, 0.0]
        failures.append(str(exc))
    regions = route_region_rects(route)
    if args.primary_region not in regions:
        failures.append(f"missing primary region in route: {args.primary_region}")

    parsed = parse_log(args.log, aligned_size, regions)
    rows = parsed["rows"]
    groups = finalize_groups(parsed["groups"], regions, args.primary_region)
    primary_rows = [
        row for row in rows
        if (row.get("overlaps") or {}).get(args.primary_region, {}).get("area", 0.0) > 0.0
    ]
    if len(primary_rows) < args.expect_min_primary_rows:
        failures.append(
            f"expected at least {args.expect_min_primary_rows} rows overlapping "
            f"{args.primary_region}, got {len(primary_rows)}"
        )

    effect_counts: dict[str, int] = {}
    class_counts: dict[str, int] = {}
    for row in primary_rows:
        effect_counts[str(row.get("effect"))] = effect_counts.get(str(row.get("effect")), 0) + 1
        class_counts[str(row.get("class"))] = class_counts.get(str(row.get("class")), 0) + 1

    top_primary = []
    for row in primary_rows:
        item = {
            key: row.get(key)
            for key in (
                "frame",
                "tri",
                "class",
                "effect",
                "cc",
                "combcc",
                "oml_raw",
                "geom",
                "tex_wh",
                "blend",
                "api_blend",
                "logical_bbox",
                "source",
                "line",
            )
        }
        item["primary_overlap"] = row["overlaps"][args.primary_region]
        top_primary.append(item)
    top_primary.sort(key=lambda item: -float(item["primary_overlap"]["b_pct"]))

    interpretation = [
        f"{len(primary_rows)} TEXGEN-MATERIAL rows overlap {args.primary_region}",
    ]
    if effect_counts:
        dominant = sorted(effect_counts.items(), key=lambda item: (-item[1], item[0]))[0]
        interpretation.append(f"dominant overlapping effect is {dominant[0]} ({dominant[1]} rows)")
    if top_primary:
        best = top_primary[0]
        interpretation.append(
            f"largest single-row overlap is {best['primary_overlap']['b_pct']:.3f}% "
            f"from effect={best.get('effect')} class={best.get('class')}"
        )

    payload = {
        "status": "fail" if failures else "pass",
        "failures": failures,
        "route": {
            "name": route.get("name"),
            "route_json": str(args.route_json),
            "logical_viewport": viewport,
            "coordinate_space": "aligned_crop_pixels",
            "aligned_size": aligned_size,
            "regions": regions,
        },
        "sources": [str(path) for path in args.log],
        "primary_region": args.primary_region,
        "line_counts": {
            "texgen_rows_in_regions": len(rows),
            "texgen_rows_in_primary_region": len(primary_rows),
            "signature_count": len(groups),
        },
        "primary_effect_counts": effect_counts,
        "primary_class_counts": class_counts,
        "top_primary_rows": top_primary[: args.top],
        "groups": groups[: args.top_groups],
        "interpretation": interpretation,
    }
    return payload, 1 if failures else 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", nargs="+", type=Path)
    parser.add_argument("--route-json", type=Path, required=True)
    parser.add_argument("--primary-region", default="projected_impact")
    parser.add_argument("--expect-min-primary-rows", type=int, default=1)
    parser.add_argument("--top", type=int, default=20)
    parser.add_argument("--top-groups", type=int, default=20)
    parser.add_argument("--aligned-size", help="aligned crop size as WIDTH,HEIGHT; defaults from route native_config")
    parser.add_argument("--json-out", type=Path)
    args = parser.parse_args(argv)

    for path in args.log:
        if not path.exists():
            parser.error(f"log not found: {path}")
    if not args.route_json.exists():
        parser.error(f"route JSON not found: {args.route_json}")
    if args.expect_min_primary_rows < 0:
        parser.error("--expect-min-primary-rows must be non-negative")

    payload, status = summarize(args)
    encoded = json.dumps(payload, indent=2, sort_keys=True) + "\n"
    if args.json_out is not None:
        args.json_out.parent.mkdir(parents=True, exist_ok=True)
        args.json_out.write_text(encoded, encoding="utf-8")
    print(encoded, end="")
    return status


if __name__ == "__main__":
    raise SystemExit(main())
