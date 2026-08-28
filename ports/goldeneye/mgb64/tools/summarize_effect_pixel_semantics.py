#!/usr/bin/env python3
"""Summarize localized effect pixel-output semantics from native trace logs.

This diagnostic is intentionally read-only. It turns verbose one-off renderer
logs into a compact artifact that answers whether a labelled effect emitted
screen-space triangles, which material signatures those triangles used, and
whether their projected bboxes overlap the route's visual regions.
"""

from __future__ import annotations

import argparse
import json
import math
import re
import sys
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any


NUMBER_RE = r"[-+]?(?:\d+(?:\.\d*)?|\.\d+)"
MATERIAL_RE = re.compile(r"\[BULLET-IMPACT-MATERIAL\]\s+(?P<body>.*)")
EFFECT_TRI_RE = re.compile(r"\[EFFECT-TRI\]\s+(?P<body>.*)")
CREATE_RE = re.compile(r"\[BULLET-IMPACT-CREATE\]\s+(?P<body>.*)")
RENDER_RE = re.compile(r"\[BULLET-IMPACT-RENDER\]\s+(?P<body>.*)")
RANGE_RE = re.compile(r"\[EFFECT-RANGE\]\s+(?P<body>.*)")


def value_for(body: str, key: str) -> str | None:
    pattern = re.compile(
        rf"(?:^|\s){re.escape(key)}="
        rf"(?P<value>\{{[^}}]*\}}|\([^)]*\)|\[[^\]]*\]|\S+)"
    )
    match = pattern.search(body)
    return match.group("value") if match else None


def token_value_for(body: str, key: str) -> str | None:
    match = re.search(rf"(?:^|\s){re.escape(key)}=(?P<value>\S+)", body)
    return match.group("value") if match else None


def int_for(body: str, key: str) -> int | None:
    value = value_for(body, key)
    if value is None:
        return None
    try:
        return int(value, 0)
    except ValueError:
        return None


def bool_int_for(body: str, key: str) -> int | None:
    value = value_for(body, key)
    if value is None:
        return None
    if value in ("1", "true", "True", "yes", "on"):
        return 1
    if value in ("0", "false", "False", "no", "off"):
        return 0
    return None


def parse_numeric_tuple(value: str | None) -> list[float] | None:
    if value is None:
        return None
    numbers = [float(item) for item in re.findall(NUMBER_RE, value)]
    return numbers or None


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
    x0, y0, x1, y1 = bbox
    if x1 <= x0 or y1 <= y0:
        return None
    return bbox


def rect_union(a: list[float] | None, b: list[float] | None) -> list[float] | None:
    if a is None:
        return b
    if b is None:
        return a
    return [
        min(a[0], b[0]),
        min(a[1], b[1]),
        max(a[2], b[2]),
        max(a[3], b[3]),
    ]


def rect_area(rect: list[float] | None) -> float:
    if rect is None:
        return 0.0
    return max(0.0, rect[2] - rect[0]) * max(0.0, rect[3] - rect[1])


def rect_overlap(a: list[float] | None, b: list[float] | None) -> dict[str, Any]:
    if a is None or b is None:
        return {"area": 0.0, "a_pct": 0.0, "b_pct": 0.0}
    left = max(a[0], b[0])
    top = max(a[1], b[1])
    right = min(a[2], b[2])
    bottom = min(a[3], b[3])
    if right <= left or bottom <= top:
        area = 0.0
    else:
        area = (right - left) * (bottom - top)
    a_area = rect_area(a)
    b_area = rect_area(b)
    return {
        "area": area,
        "a_pct": 100.0 * area / a_area if a_area else 0.0,
        "b_pct": 100.0 * area / b_area if b_area else 0.0,
    }


def ndc_to_logical(
    bbox: list[float] | None,
    viewport: list[float],
) -> list[float] | None:
    if bbox is None:
        return None
    vx, vy, vw, vh = viewport
    x0 = vx + (bbox[0] + 1.0) * 0.5 * vw
    x1 = vx + (bbox[2] + 1.0) * 0.5 * vw
    y_top = vy + (1.0 - bbox[3]) * 0.5 * vh
    y_bottom = vy + (1.0 - bbox[1]) * 0.5 * vh
    return [x0, y_top, x1, y_bottom]


def logical_to_capture_roi(
    bbox: list[float] | None,
    viewport: list[float],
    scale: float,
) -> list[float] | None:
    if bbox is None:
        return None
    vx, vy, _vw, _vh = viewport
    return [
        (bbox[0] - vx) * scale,
        (bbox[1] - vy) * scale,
        (bbox[2] - vx) * scale,
        (bbox[3] - vy) * scale,
    ]


def region_rect(region: dict[str, Any]) -> list[float]:
    x, y, w, h = region["roi"]
    return [float(x), float(y), float(x + w), float(y + h)]


def frame_span(values: list[int]) -> dict[str, Any]:
    if not values:
        return {"min": None, "max": None, "count": 0, "unique": []}
    unique = sorted(set(values))
    return {"min": min(values), "max": max(values), "count": len(values), "unique": unique}


def signature_key_from_material(body: str) -> tuple[tuple[str, str | None], ...]:
    keys = (
        "combcc",
        "oml_raw",
        "oml",
        "omh",
        "geom",
        "inputs",
        "mapC",
        "mapA",
        "blend",
        "alpha",
        "fog",
        "texedge",
        "depth",
        "tex_used",
        "tex_wh",
        "bound",
        "tile0",
        "tile1",
    )
    token_keys = {"tex_wh"}
    return tuple(
        (key, token_value_for(body, key) if key in token_keys else value_for(body, key))
        for key in keys
    )


def signature_key_from_effect(body: str) -> tuple[tuple[str, str | None], ...]:
    keys = (
        "cc",
        "raw",
        "eff",
        "omh",
        "geom",
        "inputs",
        "blend",
        "alpha",
        "fog",
        "texedge",
        "depth",
        "used",
        "texwh",
        "gl",
    )
    return tuple((key, value_for(body, key)) for key in keys)


def signature_dict(key: tuple[tuple[str, str | None], ...]) -> dict[str, str | None]:
    return {name: value for name, value in key}


def parse_log(paths: list[Path], effect: str) -> dict[str, Any]:
    creates: list[dict[str, Any]] = []
    renders: list[dict[str, Any]] = []
    ranges: list[dict[str, Any]] = []
    material_groups: dict[tuple[tuple[str, str | None], ...], dict[str, Any]] = {}
    effect_groups: dict[tuple[tuple[str, str | None], ...], dict[str, Any]] = {}
    line_counts = Counter()

    for path in paths:
        with path.open("r", encoding="utf-8", errors="replace") as handle:
            for line_no, line in enumerate(handle, start=1):
                if match := CREATE_RE.search(line):
                    body = match.group("body")
                    event = {
                        "source": str(path),
                        "line": line_no,
                        "slot": int_for(body, "slot"),
                        "impact": int_for(body, "impact"),
                        "room": int_for(body, "room"),
                        "prop": value_for(body, "prop"),
                        "model_pos": int_for(body, "model_pos"),
                        "clear": int_for(body, "clear"),
                        "wh": value_for(body, "wh"),
                        "pos": value_for(body, "pos"),
                        "normal": value_for(body, "normal"),
                        "size": value_for(body, "size"),
                        "offset": value_for(body, "offset"),
                    }
                    event["world"] = event["prop"] in ("0x0", "0", "(nil)") or event["model_pos"] == -1
                    creates.append(event)
                    line_counts["create"] += 1
                    continue

                if match := RENDER_RE.search(line):
                    body = match.group("body")
                    renders.append(
                        {
                            "source": str(path),
                            "line": line_no,
                            "world": bool_int_for(body, "world"),
                            "alpha_pass": bool_int_for(body, "alpha_pass"),
                            "flat": bool_int_for(body, "flat"),
                            "rendered": int_for(body, "rendered"),
                            "last_impact": int_for(body, "last_impact"),
                            "current_slot": int_for(body, "current_slot"),
                        }
                    )
                    line_counts["render"] += 1
                    continue

                if match := RANGE_RE.search(line):
                    body = match.group("body")
                    label = value_for(body, "label")
                    if label == effect:
                        ranges.append(
                            {
                                "source": str(path),
                                "line": line_no,
                                "frame": int_for(body, "frame"),
                                "event": value_for(body, "event"),
                                "label": label,
                                "count": int_for(body, "count"),
                            }
                        )
                        line_counts["range"] += 1
                    continue

                if match := MATERIAL_RE.search(line):
                    body = match.group("body")
                    if value_for(body, "effect") != effect:
                        continue
                    key = signature_key_from_material(body)
                    frame = int_for(body, "frame")
                    ndc = parse_bbox(body, "ndc")
                    group = material_groups.setdefault(
                        key,
                        {
                            "signature": signature_dict(key),
                            "count": 0,
                            "frames": [],
                            "triangles": [],
                            "ndc_bbox": None,
                            "shade_samples": [],
                            "source_examples": [],
                        },
                    )
                    group["count"] += 1
                    if frame is not None:
                        group["frames"].append(frame)
                    tri = int_for(body, "tri")
                    if tri is not None:
                        group["triangles"].append(tri)
                    group["ndc_bbox"] = rect_union(group["ndc_bbox"], ndc)
                    shade = parse_numeric_tuple(value_for(body, "shade0"))
                    if shade:
                        group["shade_samples"].append(shade[:4])
                    if len(group["source_examples"]) < 3:
                        group["source_examples"].append({"source": str(path), "line": line_no})
                    line_counts["material"] += 1
                    continue

                if match := EFFECT_TRI_RE.search(line):
                    body = match.group("body")
                    if value_for(body, "label") != effect:
                        continue
                    key = signature_key_from_effect(body)
                    frame = int_for(body, "frame")
                    bbox = parse_bbox(body, "bbox")
                    group = effect_groups.setdefault(
                        key,
                        {
                            "signature": signature_dict(key),
                            "count": 0,
                            "frames": [],
                            "triangles": [],
                            "ndc_bbox": None,
                            "events": Counter(),
                            "source_examples": [],
                        },
                    )
                    group["count"] += 1
                    if frame is not None:
                        group["frames"].append(frame)
                    tri = int_for(body, "tri")
                    if tri is not None:
                        group["triangles"].append(tri)
                    group["ndc_bbox"] = rect_union(group["ndc_bbox"], bbox)
                    event_name = value_for(body, "event")
                    if event_name:
                        group["events"][event_name] += 1
                    if len(group["source_examples"]) < 3:
                        group["source_examples"].append({"source": str(path), "line": line_no})
                    line_counts["effect_tri"] += 1

    return {
        "creates": creates,
        "renders": renders,
        "ranges": ranges,
        "material_groups": material_groups,
        "effect_groups": effect_groups,
        "line_counts": dict(line_counts),
    }


def finalize_groups(
    groups: dict[tuple[tuple[str, str | None], ...], dict[str, Any]],
    *,
    logical_viewport: list[float],
    capture_scale: float,
    regions: list[dict[str, Any]],
) -> list[dict[str, Any]]:
    finalized: list[dict[str, Any]] = []
    for group in groups.values():
        ndc_bbox = group.get("ndc_bbox")
        logical_bbox = ndc_to_logical(ndc_bbox, logical_viewport)
        capture_bbox = logical_to_capture_roi(logical_bbox, logical_viewport, capture_scale)
        region_overlaps = {}
        for region in regions:
            rect = region_rect(region)
            region_overlaps[region["name"]] = rect_overlap(logical_bbox, rect)
        frames = frame_span(group.get("frames", []))
        triangles = frame_span(group.get("triangles", []))
        payload = {
            "signature": group["signature"],
            "count": group["count"],
            "frames": frames,
            "triangles": triangles,
            "ndc_bbox": ndc_bbox,
            "logical_bbox": logical_bbox,
            "capture_bbox": capture_bbox,
            "region_overlaps": region_overlaps,
            "source_examples": group.get("source_examples", []),
        }
        if "shade_samples" in group:
            shades = group["shade_samples"]
            if shades:
                payload["shade0_min"] = [min(sample[i] for sample in shades) for i in range(len(shades[0]))]
                payload["shade0_max"] = [max(sample[i] for sample in shades) for i in range(len(shades[0]))]
        if "events" in group:
            payload["events"] = dict(group["events"])
        finalized.append(payload)
    finalized.sort(
        key=lambda item: (
            item["frames"]["min"] if item["frames"]["min"] is not None else 10**9,
            -item["count"],
            json.dumps(item["signature"], sort_keys=True),
        )
    )
    return finalized


def load_route(path: Path | None) -> dict[str, Any]:
    if path is None:
        return {}
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def load_json_if_present(path: Path) -> dict[str, Any] | None:
    if not path.exists():
        return None
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def pixel_evidence(base_case_dir: Path | None, route_name: str) -> dict[str, Any]:
    if base_case_dir is None:
        return {}
    actor_path = base_case_dir / f"actor_masked_visual_compare_{route_name}.json"
    projected_path = base_case_dir / f"impact_projected_pixel_oracle_{route_name}.json"
    summary_path = base_case_dir / "glass_pad10092_impact_visual_summary.json"
    evidence: dict[str, Any] = {
        "base_case_dir": str(base_case_dir),
        "actor_masked_visual_compare": str(actor_path) if actor_path.exists() else None,
        "projected_pixel_oracle": str(projected_path) if projected_path.exists() else None,
        "impact_summary": str(summary_path) if summary_path.exists() else None,
    }

    actor = load_json_if_present(actor_path)
    if actor:
        regions = {}
        for item in actor.get("regions", []):
            if not isinstance(item, dict) or not item.get("name"):
                continue
            metric = item.get("masked") or item.get("full") or {}
            regions[item["name"]] = {
                "changed_pct": metric.get("changed_pct"),
                "bright_pixels": {
                    "stock": (((metric.get("features") or {}).get("baseline") or {}).get("bright_pixels")),
                    "native": (((metric.get("features") or {}).get("test") or {}).get("bright_pixels")),
                },
                "mean_rgb": metric.get("mean_rgb"),
            }
        evidence["actor_masked_regions"] = regions

    projected = load_json_if_present(projected_path)
    if projected:
        metrics = projected.get("metrics") or {}
        evidence["projected_impact"] = {
            "status": projected.get("status"),
            "region": projected.get("region"),
            "changed_pct": metrics.get("changed_pct"),
            "mean_rgb": metrics.get("mean_rgb"),
            "bright_pixels": {
                "stock": (((metrics.get("features") or {}).get("baseline") or {}).get("bright_pixels")),
                "native": (((metrics.get("features") or {}).get("test") or {}).get("bright_pixels")),
            },
        }

    impact_summary = load_json_if_present(summary_path)
    if impact_summary:
        evidence["impact_summary_status"] = impact_summary.get("status")
        evidence["impact_summary_interpretation"] = impact_summary.get("interpretation")
    return evidence


def impact_sequence_evidence(path: Path | None) -> dict[str, Any]:
    if path is None:
        return {}
    data = load_json_if_present(path)
    if not data:
        return {"path": str(path), "present": False}
    return {
        "path": str(path),
        "present": True,
        "status": data.get("status"),
        "match": data.get("match"),
        "scope": data.get("scope"),
        "require_match": data.get("require_match"),
        "first_pair_identity_match": data.get("first_pair_identity_match"),
        "sequence": data.get("sequence"),
        "mismatches": data.get("mismatches"),
        "interpretation": data.get("interpretation"),
        "baseline_frame": (data.get("baseline") or {}).get("frame"),
        "test_frame": (data.get("test") or {}).get("frame"),
        "baseline_impact_type_counts": (data.get("baseline") or {}).get("impact_type_counts"),
        "test_impact_type_counts": (data.get("test") or {}).get("impact_type_counts"),
    }


def summarize(args: argparse.Namespace) -> tuple[dict[str, Any], int]:
    failures: list[str] = []
    route = load_route(args.route_json)
    route_name = route.get("name") or args.route_name or "unknown"
    logical_size = route.get("visual_logical_size") or list(args.logical_size)
    logical_viewport = route.get("visual_logical_viewport") or list(args.logical_viewport)
    regions = route.get("visual_regions") or []
    if not isinstance(regions, list):
        regions = []

    parsed = parse_log(args.log, args.effect)
    material_groups = finalize_groups(
        parsed["material_groups"],
        logical_viewport=[float(item) for item in logical_viewport],
        capture_scale=args.capture_scale,
        regions=regions,
    )
    effect_groups = finalize_groups(
        parsed["effect_groups"],
        logical_viewport=[float(item) for item in logical_viewport],
        capture_scale=args.capture_scale,
        regions=regions,
    )

    material_rows = parsed["line_counts"].get("material", 0)
    effect_rows = parsed["line_counts"].get("effect_tri", 0)
    material_signatures = len(material_groups)
    effect_signatures = len(effect_groups)
    if material_rows < args.expect_min_material_rows:
        failures.append(
            f"expected at least {args.expect_min_material_rows} material rows for {args.effect}, got {material_rows}"
        )
    if effect_rows < args.expect_min_effect_tri_rows:
        failures.append(
            f"expected at least {args.expect_min_effect_tri_rows} effect-tri rows for {args.effect}, got {effect_rows}"
        )
    if material_signatures < args.expect_min_signatures:
        failures.append(
            f"expected at least {args.expect_min_signatures} material signatures, got {material_signatures}"
        )
    if effect_signatures < args.expect_min_signatures:
        failures.append(
            f"expected at least {args.expect_min_signatures} effect-tri signatures, got {effect_signatures}"
        )

    primary_overlap = {}
    if args.primary_region:
        for group in material_groups:
            overlap = (group.get("region_overlaps") or {}).get(args.primary_region)
            if overlap and overlap.get("area", 0.0) > 0.0:
                primary_overlap[group["signature"].get("combcc") or group["signature"].get("cc") or "unknown"] = overlap
        if not primary_overlap:
            failures.append(f"no material signature overlapped primary region {args.primary_region}")

    world_creates = [item for item in parsed["creates"] if item.get("world")]
    prop_creates = [item for item in parsed["creates"] if not item.get("world")]
    world_renders = [item for item in parsed["renders"] if item.get("world") == 1]
    prop_renders = [item for item in parsed["renders"] if item.get("world") == 0]

    interpretation: list[str] = []
    if material_rows and effect_rows:
        interpretation.append(
            f"{args.effect} has matched material/effect-tri evidence; use drawclass=room for this route"
        )
    if material_signatures >= 2:
        interpretation.append(
            f"{args.effect} is split across {material_signatures} material signatures, so one broad impact knob is too coarse"
        )
    if primary_overlap:
        interpretation.append(
            f"at least one material signature overlaps {args.primary_region}; this probe is localized to the disputed pixels"
        )
    pixels = pixel_evidence(args.base_case_dir, str(route_name))
    projected = pixels.get("projected_impact", {})
    if projected.get("changed_pct") is not None:
        interpretation.append(
            f"base projected-impact stock/native mismatch remains {projected['changed_pct']:.3f}%"
        )
    sequence = impact_sequence_evidence(args.impact_sequence_json)
    if sequence.get("match") is False:
        interpretation.append(
            "stock/native sampled impact sequence diverges at the pixel checkpoint; treat this fixture as report-only"
        )

    summary = {
        "status": "fail" if failures else "pass",
        "failures": failures,
        "effect": args.effect,
        "route": {
            "name": route_name,
            "route_json": str(args.route_json) if args.route_json else None,
            "logical_size": logical_size,
            "logical_viewport": logical_viewport,
            "regions": regions,
        },
        "sources": [str(path) for path in args.log],
        "line_counts": parsed["line_counts"],
        "effect_ranges": {
            "count": len(parsed["ranges"]),
            "frame_span": frame_span([item["frame"] for item in parsed["ranges"] if item.get("frame") is not None]),
            "range_command_counts": sorted(set(item["count"] for item in parsed["ranges"] if item.get("count") is not None)),
        },
        "creation_events": {
            "count": len(parsed["creates"]),
            "world_count": len(world_creates),
            "prop_count": len(prop_creates),
            "world_samples": world_creates[:6],
            "prop_samples": prop_creates[:6],
        },
        "render_events": {
            "count": len(parsed["renders"]),
            "world_count": len(world_renders),
            "prop_count": len(prop_renders),
            "world_rendered_values": sorted(set(item.get("rendered") for item in world_renders)),
            "prop_rendered_values": sorted(set(item.get("rendered") for item in prop_renders)),
        },
        "material": {
            "row_count": material_rows,
            "signature_count": material_signatures,
            "groups": material_groups,
        },
        "effect_triangles": {
            "row_count": effect_rows,
            "signature_count": effect_signatures,
            "groups": effect_groups,
        },
        "primary_region_overlap": primary_overlap,
        "pixel_evidence": pixels,
        "impact_sequence_evidence": sequence,
        "interpretation": interpretation,
    }
    return summary, 1 if failures else 0


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


def parse_rect(value: str) -> tuple[int, int, int, int]:
    parts = value.split(",")
    if len(parts) != 4:
        raise argparse.ArgumentTypeError("expected X,Y,W,H")
    try:
        parsed = tuple(int(part) for part in parts)
    except ValueError:
        raise argparse.ArgumentTypeError("expected integer X,Y,W,H") from None
    if parsed[2] <= 0 or parsed[3] <= 0:
        raise argparse.ArgumentTypeError("width and height must be positive")
    return parsed  # type: ignore[return-value]


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", nargs="+", type=Path, help="native log file(s) to summarize")
    parser.add_argument("--effect", default="bullet_impact_world", help="effect label to summarize")
    parser.add_argument("--route-json", type=Path, help="route JSON for visual regions")
    parser.add_argument("--route-name", help="route name when no route JSON is supplied")
    parser.add_argument("--base-case-dir", type=Path, help="existing stock/native case dir for pixel evidence")
    parser.add_argument("--impact-sequence-json", type=Path, help="sampled stock/native impact-sequence report")
    parser.add_argument("--logical-size", type=parse_pair, default=(320, 240))
    parser.add_argument("--logical-viewport", type=parse_rect, default=(0, 10, 320, 220))
    parser.add_argument("--capture-scale", type=float, default=2.0)
    parser.add_argument("--primary-region", default="projected_impact")
    parser.add_argument("--expect-min-material-rows", type=int, default=1)
    parser.add_argument("--expect-min-effect-tri-rows", type=int, default=1)
    parser.add_argument("--expect-min-signatures", type=int, default=1)
    parser.add_argument("--json-out", type=Path, help="write JSON summary")
    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    for path in args.log:
        if not path.exists():
            parser.error(f"log not found: {path}")
    if args.route_json and not args.route_json.exists():
        parser.error(f"route JSON not found: {args.route_json}")
    if args.base_case_dir and not args.base_case_dir.exists():
        parser.error(f"base case dir not found: {args.base_case_dir}")
    if args.impact_sequence_json and not args.impact_sequence_json.exists():
        parser.error(f"impact sequence JSON not found: {args.impact_sequence_json}")
    if args.capture_scale <= 0.0 or not math.isfinite(args.capture_scale):
        parser.error("--capture-scale must be positive")
    if args.expect_min_material_rows < 0 or args.expect_min_effect_tri_rows < 0:
        parser.error("minimum row expectations must be non-negative")
    if args.expect_min_signatures < 0:
        parser.error("--expect-min-signatures must be non-negative")

    summary, code = summarize(args)
    encoded = json.dumps(summary, indent=2, sort_keys=True) + "\n"
    if args.json_out:
        args.json_out.parent.mkdir(parents=True, exist_ok=True)
        args.json_out.write_text(encoded, encoding="utf-8")
    print(encoded, end="")
    return code


if __name__ == "__main__":
    sys.exit(main())
