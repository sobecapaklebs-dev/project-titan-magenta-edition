#!/usr/bin/env python3
"""Summarize SETTEX-MATERIAL-CC rows that overlap route visual ROIs."""

from __future__ import annotations

import argparse
from collections import Counter
import json
import math
import re
import sys
from pathlib import Path
from typing import Any

sys.path.insert(0, str(Path(__file__).resolve().parent))
import summarize_texgen_roi_materials as texgen  # noqa: E402


SETTEX_RE = re.compile(r"\[SETTEX-MATERIAL-CC\]\s+(?P<body>.*)")


def load_json(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def normalize_hex(value: Any, width: int = 8) -> str | None:
    if value is None:
        return None
    text = str(value).strip().lower()
    if not text:
        return None
    try:
        return f"0x{int(text, 0):0{width}x}"
    except ValueError:
        return text


def normalized_set(values: list[str] | None, hex_values: bool = False, width: int = 8) -> set[str] | None:
    if not values:
        return None
    out: set[str] = set()
    for value in values:
        if value in ("*", "1"):
            return None
        normalized = normalize_hex(value, width) if hex_values else str(value)
        if normalized is not None:
            out.add(normalized)
    return out


def parse_tuple(value: str | None) -> list[float]:
    return texgen.parse_tuple_numbers(value)


def int_value(body: str, key: str) -> int | None:
    return texgen.int_for(body, key)


def value(body: str, key: str) -> str | None:
    return texgen.value_for(body, key)


def parse_screen_bbox(body: str) -> list[float] | None:
    values = parse_tuple(value(body, "screen_bbox"))
    if len(values) != 4:
        return None
    if any(not math.isfinite(item) for item in values):
        return None
    if values[2] <= values[0] or values[3] <= values[1]:
        return None
    return values


def route_regions(route: dict[str, Any]) -> dict[str, list[float]]:
    regions: dict[str, list[float]] = {}
    for item in route.get("visual_regions", route.get("regions", [])):
        if not isinstance(item, dict) or not item.get("name"):
            continue
        roi = item.get("roi")
        if not isinstance(roi, list) or len(roi) != 4:
            continue
        try:
            x, y, width, height = [float(value) for value in roi]
        except (TypeError, ValueError):
            continue
        regions[str(item["name"])] = [x, y, x + width, y + height]
    return regions


def parse_size(value_text: str | None) -> tuple[float, float] | None:
    if value_text is None:
        return None
    parts = value_text.lower().replace("x", ",").split(",")
    if len(parts) != 2:
        return None
    try:
        width, height = [float(part) for part in parts]
    except ValueError:
        return None
    if width <= 0.0 or height <= 0.0:
        return None
    return width, height


def route_mapping(route: dict[str, Any], aligned_size_override: str | None) -> dict[str, Any]:
    logical_size = route.get("visual_logical_size") or route.get("logical_size") or [320, 240]
    viewport = route.get("visual_logical_viewport") or route.get("logical_viewport") or [0, 10, 320, 220]
    logical_w = float(logical_size[0])
    logical_h = float(logical_size[1])
    viewport_f = [float(item) for item in viewport]

    override_size = parse_size(aligned_size_override)
    if aligned_size_override and override_size is None:
        raise ValueError("--aligned-size must be WIDTH,HEIGHT")

    native_config = route.get("native_config") or {}
    window_w = native_config.get("Video.WindowWidth")
    window_h = native_config.get("Video.WindowHeight")
    if isinstance(window_w, (int, float)) and isinstance(window_h, (int, float)):
        scale_x = float(window_w) / logical_w
        scale_y = float(window_h) / logical_h
        aligned_size = [viewport_f[2] * scale_x, viewport_f[3] * scale_y]
    elif override_size is not None:
        aligned_size = [override_size[0], override_size[1]]
        scale_x = aligned_size[0] / viewport_f[2]
        scale_y = aligned_size[1] / viewport_f[3]
    else:
        aligned_size = [viewport_f[2], viewport_f[3]]
        scale_x = 1.0
        scale_y = 1.0

    if override_size is not None:
        aligned_size = [override_size[0], override_size[1]]
        scale_x = aligned_size[0] / viewport_f[2]
        scale_y = aligned_size[1] / viewport_f[3]

    return {
        "logical_size": [logical_w, logical_h],
        "viewport": viewport_f,
        "scale": [scale_x, scale_y],
        "aligned_size": aligned_size,
    }


def logical_bbox_to_aligned(bbox: list[float], mapping: dict[str, Any]) -> list[float] | None:
    viewport = mapping["viewport"]
    scale_x, scale_y = mapping["scale"]
    out = [
        (bbox[0] - viewport[0]) * scale_x,
        (bbox[1] - viewport[1]) * scale_y,
        (bbox[2] - viewport[0]) * scale_x,
        (bbox[3] - viewport[1]) * scale_y,
    ]
    if any(not math.isfinite(item) for item in out):
        return None
    if out[2] <= out[0] or out[3] <= out[1]:
        return None
    return out


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


def rect_union(a: list[float] | None, b: list[float] | None) -> list[float] | None:
    if a is None:
        return b
    if b is None:
        return a
    return [min(a[0], b[0]), min(a[1], b[1]), max(a[2], b[2]), max(a[3], b[3])]


def raster_rect(rect: list[float], aligned_size: list[float]) -> set[int]:
    width = int(round(aligned_size[0]))
    height = int(round(aligned_size[1]))
    left = max(0, min(width, int(math.floor(rect[0]))))
    top = max(0, min(height, int(math.floor(rect[1]))))
    right = max(0, min(width, int(math.ceil(rect[2]))))
    bottom = max(0, min(height, int(math.ceil(rect[3]))))
    if right <= left or bottom <= top:
        return set()
    return {y * width + x for y in range(top, bottom) for x in range(left, right)}


def luma(rgb: list[float]) -> float:
    if len(rgb) < 3:
        return 0.0
    return 0.2126 * rgb[0] + 0.7152 * rgb[1] + 0.0722 * rgb[2]


def mean(values: list[float]) -> float:
    return sum(values) / len(values) if values else 0.0


def stats(values: list[float]) -> dict[str, float | None]:
    if not values:
        return {"min": None, "mean": None, "max": None}
    return {"min": min(values), "mean": mean(values), "max": max(values)}


def tuple_stats(rows: list[dict[str, Any]], key: str) -> dict[str, Any]:
    tuples = [row[key] for row in rows if len(row.get(key) or []) >= 3]
    if not tuples:
        return {"count": 0}
    max_len = max(len(item) for item in tuples)
    channels: list[dict[str, float | None]] = []
    for index in range(max_len):
        channels.append(stats([item[index] for item in tuples if len(item) > index]))
    alphas = [int(item[3]) for item in tuples if len(item) > 3]
    return {
        "count": len(tuples),
        "channels": channels,
        "luma": stats([luma(item) for item in tuples]),
        "alpha_counts": {str(key): value for key, value in sorted(Counter(alphas).items())},
        "unique": sorted({tuple(int(round(value)) for value in item) for item in tuples}),
    }


def row_signature(row: dict[str, Any]) -> dict[str, Any]:
    return {
        "class": row.get("class"),
        "cc": row.get("cc"),
        "effcc": row.get("effcc"),
        "opts": row.get("opts"),
        "texnum": row.get("texnum"),
        "wh": row.get("wh"),
        "rgba_wh": row.get("rgba_wh"),
        "blend": row.get("blend"),
        "alpha": row.get("alpha"),
        "fog": row.get("fog"),
        "texedge": row.get("texedge"),
        "depth": row.get("depth"),
        "oml_raw": row.get("oml_raw"),
        "geom": row.get("geom"),
    }


def parse_log(paths: list[Path], mapping: dict[str, Any], regions: dict[str, list[float]]) -> dict[str, Any]:
    rows: list[dict[str, Any]] = []
    parsed_lines = 0
    bbox_lines = 0
    for path in paths:
        with path.open("r", encoding="utf-8", errors="replace") as handle:
            for line_no, line in enumerate(handle, start=1):
                match = SETTEX_RE.search(line)
                if not match:
                    continue
                parsed_lines += 1
                body = match.group("body")
                screen_bbox = parse_screen_bbox(body)
                if screen_bbox is None:
                    continue
                aligned_bbox = logical_bbox_to_aligned(screen_bbox, mapping)
                if aligned_bbox is None:
                    continue
                bbox_lines += 1
                overlaps = {name: rect_overlap(aligned_bbox, rect) for name, rect in regions.items()}
                row = {
                    "source": str(path),
                    "line": line_no,
                    "frame": int_value(body, "frame"),
                    "tri": int_value(body, "tri"),
                    "class": value(body, "class"),
                    "prop": int_value(body, "prop"),
                    "cc": normalize_hex(value(body, "cc"), 16),
                    "settexcc": normalize_hex(value(body, "settexcc"), 16),
                    "effcc": normalize_hex(value(body, "effcc"), 16),
                    "opts": normalize_hex(value(body, "opts"), 8),
                    "texnum": int_value(body, "texnum"),
                    "wh": value(body, "wh"),
                    "type": int_value(body, "type"),
                    "offset": int_value(body, "offset"),
                    "minlod": int_value(body, "minlod"),
                    "lod": int_value(body, "lod"),
                    "tex_used": value(body, "tex_used"),
                    "blend": value(body, "blend"),
                    "alpha": int_value(body, "alpha"),
                    "fog": int_value(body, "fog"),
                    "texedge": int_value(body, "texedge"),
                    "depth": value(body, "depth"),
                    "prim": parse_tuple(value(body, "prim"))[:4],
                    "env": parse_tuple(value(body, "env"))[:4],
                    "fogrgba": parse_tuple(value(body, "fogrgba"))[:4],
                    "shade0": parse_tuple(value(body, "shade0"))[:4],
                    "shade1": parse_tuple(value(body, "shade1"))[:4],
                    "shade2": parse_tuple(value(body, "shade2"))[:4],
                    "oml_raw": normalize_hex(value(body, "oml_raw"), 8),
                    "oml": normalize_hex(value(body, "oml"), 8),
                    "omh": normalize_hex(value(body, "omh"), 8),
                    "geom": normalize_hex(value(body, "geom"), 8),
                    "screen_bbox": screen_bbox,
                    "aligned_bbox": aligned_bbox,
                    "screen_area2": float(value(body, "screen_area2") or 0.0),
                    "sample": int_value(body, "sample"),
                    "sample_valid": value(body, "sample_valid"),
                    "interp": value(body, "interp"),
                    "rgba_wh": value(body, "rgba_wh"),
                    "t0n": parse_tuple(value(body, "t0n"))[:4],
                    "t0l": parse_tuple(value(body, "t0l"))[:4],
                    "t0p": parse_tuple(value(body, "t0p"))[:4],
                    "t1n": parse_tuple(value(body, "t1n"))[:4],
                    "t1l": parse_tuple(value(body, "t1l"))[:4],
                    "t1p": parse_tuple(value(body, "t1p"))[:4],
                    "shadec": parse_tuple(value(body, "shadec"))[:4],
                    "lodc": int_value(body, "lodc"),
                    "fogc": parse_tuple(value(body, "fogc"))[:4],
                    "combN_float": parse_tuple(value(body, "combN_float"))[:4],
                    "combL_float": parse_tuple(value(body, "combL_float"))[:4],
                    "combL_255": parse_tuple(value(body, "combL_255"))[:4],
                    "combL_256": parse_tuple(value(body, "combL_256"))[:4],
                    "combP_float": parse_tuple(value(body, "combP_float"))[:4],
                    "shaderN_comb": parse_tuple(value(body, "shaderN_comb"))[:4],
                    "shaderN_frag": parse_tuple(value(body, "shaderN_frag"))[:4],
                    "shaderL_comb": parse_tuple(value(body, "shaderL_comb"))[:4],
                    "shaderL_frag": parse_tuple(value(body, "shaderL_frag"))[:4],
                    "shaderP_comb": parse_tuple(value(body, "shaderP_comb"))[:4],
                    "shaderP_frag": parse_tuple(value(body, "shaderP_frag"))[:4],
                    "overlaps": overlaps,
                }
                row["signature"] = row_signature(row)
                rows.append(row)
    return {"parsed_lines": parsed_lines, "bbox_lines": bbox_lines, "rows": rows}


def filter_rows(rows: list[dict[str, Any]], args: argparse.Namespace) -> list[dict[str, Any]]:
    allowed_class = normalized_set(args.material_class)
    allowed_blend = normalized_set(args.blend)
    allowed_wh = normalized_set(args.wh)
    allowed_rgba_wh = normalized_set(args.rgba_wh)
    allowed_depth = normalized_set(args.depth)
    allowed_cc = normalized_set(args.cc, hex_values=True, width=16)
    allowed_effcc = normalized_set(args.effcc, hex_values=True, width=16)
    allowed_opts = normalized_set(args.opts, hex_values=True, width=8)
    allowed_oml_raw = normalized_set(args.oml_raw, hex_values=True, width=8)
    allowed_geom = normalized_set(args.geom, hex_values=True, width=8)
    allowed_texnum = set(args.texnum) if args.texnum else None
    allowed_alpha = set(args.alpha) if args.alpha else None
    allowed_fog = set(args.fog) if args.fog else None

    out: list[dict[str, Any]] = []
    for row in rows:
        checks: tuple[tuple[Any, set[Any] | None], ...] = (
            (row.get("class"), allowed_class),
            (row.get("blend"), allowed_blend),
            (row.get("wh"), allowed_wh),
            (row.get("rgba_wh"), allowed_rgba_wh),
            (row.get("depth"), allowed_depth),
            (row.get("cc"), allowed_cc),
            (row.get("effcc"), allowed_effcc),
            (row.get("opts"), allowed_opts),
            (row.get("oml_raw"), allowed_oml_raw),
            (row.get("geom"), allowed_geom),
            (row.get("texnum"), allowed_texnum),
            (row.get("alpha"), allowed_alpha),
            (row.get("fog"), allowed_fog),
        )
        if all(allowed is None or value in allowed for value, allowed in checks):
            out.append(row)
    return out


def select_rows(rows: list[dict[str, Any]], frame: str) -> tuple[list[dict[str, Any]], dict[str, Any]]:
    frames = sorted({row["frame"] for row in rows if row.get("frame") is not None})
    if frame == "all" or not frames:
        return rows, {"mode": "all", "available_frames": frames, "selected": None}
    selected = frames[-1] if frame == "latest" else int(frame)
    return [row for row in rows if row.get("frame") == selected], {
        "mode": frame,
        "available_frames": frames,
        "selected": selected,
    }


def sample_summary(rows: list[dict[str, Any]]) -> dict[str, Any]:
    fields = (
        "t0n",
        "t0l",
        "t0p",
        "t1n",
        "t1l",
        "t1p",
        "shadec",
        "fogrgba",
        "fogc",
        "combN_float",
        "combL_float",
        "combL_255",
        "combL_256",
        "combP_float",
        "shaderN_comb",
        "shaderN_frag",
        "shaderL_comb",
        "shaderL_frag",
        "shaderP_comb",
        "shaderP_frag",
    )
    return {field: tuple_stats(rows, field) for field in fields}


def concise_row(row: dict[str, Any], region: str | None = None) -> dict[str, Any]:
    out = {
        "source": row.get("source"),
        "line": row.get("line"),
        "frame": row.get("frame"),
        "tri": row.get("tri"),
        "signature": row.get("signature"),
        "screen_bbox": row.get("screen_bbox"),
        "aligned_bbox": row.get("aligned_bbox"),
        "sample_valid": row.get("sample_valid"),
        "interp": row.get("interp"),
        "t0l": row.get("t0l"),
        "t1l": row.get("t1l"),
        "shadec": row.get("shadec"),
        "fogrgba": row.get("fogrgba"),
        "fogc": row.get("fogc"),
        "combL_float": row.get("combL_float"),
        "combP_float": row.get("combP_float"),
        "shaderL_comb": row.get("shaderL_comb"),
        "shaderL_frag": row.get("shaderL_frag"),
    }
    if region is not None:
        out["region_overlap"] = (row.get("overlaps") or {}).get(region)
    return out


def summarize_region(
    name: str,
    region: list[float],
    rows: list[dict[str, Any]],
    aligned_size: list[float],
    top: int,
) -> dict[str, Any]:
    matched: list[dict[str, Any]] = []
    union_bbox: list[float] | None = None
    covered: set[int] = set()
    for row in rows:
        overlap = rect_intersection(row["aligned_bbox"], region)
        if overlap is None:
            continue
        matched.append(row)
        union_bbox = rect_union(union_bbox, row["aligned_bbox"])
        covered.update(raster_rect(overlap, aligned_size))

    source_pixels = int(round(rect_area(region)))
    matched.sort(key=lambda item: -float((item.get("overlaps") or {}).get(name, {}).get("area", 0.0)))
    return {
        "roi_rect": region,
        "source_pixels": source_pixels,
        "matched_rows": len(matched),
        "coverage_pixels": len(covered),
        "coverage_pct": 100.0 * len(covered) / source_pixels if source_pixels else 0.0,
        "union_aligned_bbox": union_bbox,
        "signature_counts": {
            key: dict(Counter(str(row.get(key)) for row in matched))
            for key in ("texnum", "wh", "blend", "alpha", "fog", "opts", "oml_raw")
        },
        "sample_summary": sample_summary(matched),
        "examples": [concise_row(row, name) for row in matched[:top]],
    }


def counter_by(rows: list[dict[str, Any]], key: str) -> dict[str, int]:
    counter = Counter(str(row.get(key)) for row in rows)
    return {key: counter[key] for key in sorted(counter, key=lambda item: (-counter[item], item))}


def build_interpretation(payload: dict[str, Any]) -> list[str]:
    out: list[str] = []
    counts = payload["line_counts"]
    frame = payload["frame_selection"]
    out.append(
        f"parsed {counts['settex_rows']} SETTEX rows; "
        f"{counts['filtered_rows']} matched the material filter"
    )
    if frame.get("selected") is not None:
        out.append(f"selected target frame {frame['selected']} from frames {frame['available_frames']}")
    primary = payload["regions"].get(payload["primary_region"]) or {}
    out.append(
        f"{payload['primary_region']}: {primary.get('matched_rows', 0)} filtered rows cover "
        f"{primary.get('coverage_pct', 0.0):.3f}% of the ROI"
    )
    sample = primary.get("sample_summary") or {}
    comb = ((sample.get("combL_float") or {}).get("luma") or {})
    shader_comb = ((sample.get("shaderL_comb") or {}).get("luma") or {})
    shader_frag = ((sample.get("shaderL_frag") or {}).get("luma") or {})
    t0 = sample.get("t0l") or {}
    t1 = sample.get("t1l") or {}
    out.append(
        f"{payload['primary_region']}: legacy center combL luma min/mean/max="
        f"{comb.get('min')}/{comb.get('mean')}/{comb.get('max')}; "
        f"t0 alpha counts={t0.get('alpha_counts', {})}; t1 alpha counts={t1.get('alpha_counts', {})}"
    )
    if (sample.get("shaderL_comb") or {}).get("count"):
        out.append(
            f"{payload['primary_region']}: shaderL comb luma min/mean/max="
            f"{shader_comb.get('min')}/{shader_comb.get('mean')}/{shader_comb.get('max')}, "
            f"frag luma min/mean/max="
            f"{shader_frag.get('min')}/{shader_frag.get('mean')}/{shader_frag.get('max')}; "
            f"comb alpha counts={(sample.get('shaderL_comb') or {}).get('alpha_counts', {})}; "
            f"frag alpha counts={(sample.get('shaderL_frag') or {}).get('alpha_counts', {})}"
        )
    fog = sample.get("fogrgba") or {}
    if fog.get("alpha_counts"):
        out.append(f"{payload['primary_region']}: fog alpha counts={fog['alpha_counts']}")
    fogc = sample.get("fogc") or {}
    if fogc.get("alpha_counts"):
        out.append(f"{payload['primary_region']}: center fog alpha counts={fogc['alpha_counts']}")
    return out


def summarize(args: argparse.Namespace) -> tuple[dict[str, Any], int]:
    failures: list[str] = []
    route = load_json(args.route_json)
    try:
        mapping = route_mapping(route, args.aligned_size)
    except ValueError as exc:
        mapping = {
            "logical_size": [0.0, 0.0],
            "viewport": [0.0, 0.0, 0.0, 0.0],
            "scale": [0.0, 0.0],
            "aligned_size": [0.0, 0.0],
        }
        failures.append(str(exc))

    regions = route_regions(route)
    if args.primary_region not in regions:
        failures.append(f"missing primary region in route: {args.primary_region}")

    parsed = parse_log(args.log, mapping, regions)
    filtered = filter_rows(parsed["rows"], args)
    selected, frame_selection = select_rows(filtered, args.frame)
    primary_rows = [
        row for row in selected
        if (row.get("overlaps") or {}).get(args.primary_region, {}).get("area", 0.0) > 0.0
    ]

    if parsed["parsed_lines"] == 0:
        failures.append("no SETTEX-MATERIAL-CC rows found")
    if len(filtered) < args.expect_min_filtered_rows:
        failures.append(
            f"expected at least {args.expect_min_filtered_rows} filtered rows, got {len(filtered)}"
        )
    if len(primary_rows) < args.expect_min_primary_rows:
        failures.append(
            f"expected at least {args.expect_min_primary_rows} filtered rows overlapping "
            f"{args.primary_region}, got {len(primary_rows)}"
        )

    region_payload = {
        name: summarize_region(name, rect, selected, mapping["aligned_size"], args.top)
        for name, rect in regions.items()
    }
    primary_coverage = (region_payload.get(args.primary_region) or {}).get("coverage_pct", 0.0)
    if primary_coverage < args.expect_min_primary_coverage_pct:
        failures.append(
            f"expected {args.primary_region} coverage >= "
            f"{args.expect_min_primary_coverage_pct:.3f}%, got {primary_coverage:.3f}%"
        )

    payload: dict[str, Any] = {
        "status": "fail" if failures else "pass",
        "failures": failures,
        "sources": [str(path) for path in args.log],
        "route": {
            "name": route.get("name"),
            "route_json": str(args.route_json),
            "coordinate_space": "aligned_crop_pixels",
            "logical_size": mapping["logical_size"],
            "logical_viewport": mapping["viewport"],
            "logical_to_aligned_scale": mapping["scale"],
            "aligned_size": mapping["aligned_size"],
            "regions": regions,
        },
        "filters": {
            "material_class": args.material_class,
            "texnum": args.texnum,
            "wh": args.wh,
            "rgba_wh": args.rgba_wh,
            "blend": args.blend,
            "alpha": args.alpha,
            "fog": args.fog,
            "depth": args.depth,
            "cc": args.cc,
            "effcc": args.effcc,
            "opts": args.opts,
            "oml_raw": args.oml_raw,
            "geom": args.geom,
        },
        "primary_region": args.primary_region,
        "frame_selection": frame_selection,
        "line_counts": {
            "settex_rows": parsed["parsed_lines"],
            "settex_rows_with_screen_bbox": parsed["bbox_lines"],
            "filtered_rows": len(filtered),
            "selected_rows": len(selected),
            "primary_rows": len(primary_rows),
        },
        "filtered_counts": {
            key: counter_by(filtered, key)
            for key in ("frame", "texnum", "wh", "blend", "alpha", "fog", "opts", "oml_raw")
        },
        "regions": region_payload,
        "top_primary_rows": [concise_row(row, args.primary_region) for row in primary_rows[: args.top]],
    }
    payload["interpretation"] = build_interpretation(payload)
    return payload, 1 if failures else 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", nargs="+", type=Path)
    parser.add_argument("--route-json", type=Path, required=True)
    parser.add_argument("--primary-region", default="projected_impact")
    parser.add_argument("--frame", default="latest", help="latest, all, or a frame number after filtering")
    parser.add_argument("--aligned-size", help="aligned crop size as WIDTH,HEIGHT")
    parser.add_argument("--material-class", action="append")
    parser.add_argument("--texnum", type=int, action="append")
    parser.add_argument("--wh", action="append")
    parser.add_argument("--rgba-wh", action="append")
    parser.add_argument("--blend", action="append")
    parser.add_argument("--alpha", type=int, action="append")
    parser.add_argument("--fog", type=int, action="append")
    parser.add_argument("--depth", action="append")
    parser.add_argument("--cc", action="append")
    parser.add_argument("--effcc", action="append")
    parser.add_argument("--opts", action="append")
    parser.add_argument("--oml-raw", action="append")
    parser.add_argument("--geom", action="append")
    parser.add_argument("--expect-min-filtered-rows", type=int, default=1)
    parser.add_argument("--expect-min-primary-rows", type=int, default=1)
    parser.add_argument("--expect-min-primary-coverage-pct", type=float, default=0.0)
    parser.add_argument("--top", type=int, default=12)
    parser.add_argument("--json-out", type=Path)
    args = parser.parse_args(argv)

    for path in args.log:
        if not path.exists():
            parser.error(f"log not found: {path}")
    if not args.route_json.exists():
        parser.error(f"route JSON not found: {args.route_json}")
    if args.expect_min_filtered_rows < 0:
        parser.error("--expect-min-filtered-rows must be non-negative")
    if args.expect_min_primary_rows < 0:
        parser.error("--expect-min-primary-rows must be non-negative")
    if args.expect_min_primary_coverage_pct < 0.0:
        parser.error("--expect-min-primary-coverage-pct must be non-negative")

    payload, status = summarize(args)
    encoded = json.dumps(payload, indent=2, sort_keys=True) + "\n"
    if args.json_out is not None:
        args.json_out.parent.mkdir(parents=True, exist_ok=True)
        args.json_out.write_text(encoded, encoding="utf-8")
    print(encoded, end="")
    return status


if __name__ == "__main__":
    raise SystemExit(main())
