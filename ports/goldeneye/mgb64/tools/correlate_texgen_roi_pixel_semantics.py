#!/usr/bin/env python3
"""Correlate TEXGEN material bboxes with stock/native ROI pixel semantics.

This read-only diagnostic maps native TEXGEN-MATERIAL NDC bboxes into the same
aligned screenshot crop used by visual ROI comparisons. It then reports which
material families cover each ROI and whether their bbox footprint is large
enough to plausibly explain the changed pixels measured versus stock.
"""

from __future__ import annotations

import argparse
from collections import Counter, defaultdict
import json
import math
import re
import sys
from pathlib import Path
from typing import Any

sys.path.insert(0, str(Path(__file__).resolve().parent))
import summarize_texgen_roi_materials as texgen  # noqa: E402


TEXGEN_RE = re.compile(r"\[TEXGEN-MATERIAL\]\s+(?P<body>.*)")


def load_json(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def route_regions(route: dict[str, Any]) -> dict[str, tuple[int, int, int, int]]:
    regions: dict[str, tuple[int, int, int, int]] = {}
    for item in route.get("visual_regions", route.get("regions", [])):
        if not isinstance(item, dict) or not item.get("name"):
            continue
        roi = item.get("roi")
        if not isinstance(roi, list) or len(roi) != 4:
            continue
        try:
            x, y, w, h = [int(value) for value in roi]
        except (TypeError, ValueError):
            continue
        regions[str(item["name"])] = (x, y, w, h)
    return regions


def ndc_to_aligned_crop(bbox: list[float], aligned_size: tuple[int, int]) -> list[float] | None:
    if bbox is None:
        return None
    width, height = aligned_size
    left = (bbox[0] + 1.0) * 0.5 * width
    right = (bbox[2] + 1.0) * 0.5 * width
    top = (1.0 - bbox[3]) * 0.5 * height
    bottom = (1.0 - bbox[1]) * 0.5 * height
    if any(not math.isfinite(value) for value in (left, top, right, bottom)):
        return None
    if right <= left or bottom <= top:
        return None
    return [left, top, right, bottom]


def rect_overlap(a: list[float], region: tuple[int, int, int, int]) -> list[float] | None:
    rx, ry, rw, rh = region
    left = max(a[0], float(rx))
    top = max(a[1], float(ry))
    right = min(a[2], float(rx + rw))
    bottom = min(a[3], float(ry + rh))
    if right <= left or bottom <= top:
        return None
    return [left, top, right, bottom]


def raster_rect(rect: list[float], aligned_size: tuple[int, int]) -> set[int]:
    width, height = aligned_size
    left = max(0, min(width, int(math.floor(rect[0]))))
    top = max(0, min(height, int(math.floor(rect[1]))))
    right = max(0, min(width, int(math.ceil(rect[2]))))
    bottom = max(0, min(height, int(math.ceil(rect[3]))))
    if right <= left or bottom <= top:
        return set()
    return {y * width + x for y in range(top, bottom) for x in range(left, right)}


def value(body: str, key: str) -> str | None:
    return texgen.value_for(body, key)


def parse_tuple(value_text: str | None) -> list[float]:
    return texgen.parse_tuple_numbers(value_text)


def load_key(value_text: str | None) -> str | None:
    if value_text is None:
        return None
    match = re.search(r"key=(0x[0-9a-fA-F]+|\d+)", value_text)
    return match.group(1).lower() if match else None


def material_signature(body: str) -> dict[str, str | None]:
    fields = (
        "class",
        "effect",
        "cc",
        "effcc",
        "opts",
        "oml_raw",
        "oml",
        "omh",
        "geom",
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
    out = {field: value(body, field) for field in fields}
    out["load0_key"] = load_key(value(body, "load0"))
    out["load1_key"] = load_key(value(body, "load1"))
    out["sampler_linear"] = value(body, "sampler_linear")
    out["mode_decode"] = value(body, "mode_decode")
    return out


def signature_key(signature: dict[str, str | None]) -> tuple[tuple[str, str | None], ...]:
    return tuple(sorted(signature.items()))


def parse_rows(logs: list[Path], aligned_size: tuple[int, int]) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    for path in logs:
        with path.open("r", encoding="utf-8", errors="replace") as handle:
            for line_no, line in enumerate(handle, start=1):
                match = TEXGEN_RE.search(line)
                if not match:
                    continue
                body = match.group("body")
                if texgen.int_for(body, "ndc_ok") != 1:
                    continue
                ndc_bbox = texgen.parse_bbox(body, "ndc")
                if ndc_bbox is None:
                    continue
                aligned_bbox = ndc_to_aligned_crop(ndc_bbox, aligned_size)
                if aligned_bbox is None:
                    continue
                row = {
                    "source": str(path),
                    "line": line_no,
                    "frame": texgen.int_for(body, "frame"),
                    "tri": texgen.int_for(body, "tri"),
                    "signature": material_signature(body),
                    "ndc_bbox": ndc_bbox,
                    "aligned_bbox": aligned_bbox,
                    "shade0": parse_tuple(value(body, "shade0"))[:4],
                    "shade1": parse_tuple(value(body, "shade1"))[:4],
                    "shade2": parse_tuple(value(body, "shade2"))[:4],
                    "env": parse_tuple(value(body, "env"))[:4],
                    "prim": parse_tuple(value(body, "prim"))[:4],
                    "fogc": parse_tuple(value(body, "fogc"))[:4],
                }
                rows.append(row)
    return rows


def select_rows(rows: list[dict[str, Any]], frame: str) -> tuple[list[dict[str, Any]], dict[str, Any]]:
    frames = sorted({row["frame"] for row in rows if row.get("frame") is not None})
    if frame == "all" or not frames:
        return rows, {"mode": "all", "available_frames": frames, "selected": None}
    if frame == "latest":
        selected = frames[-1]
    else:
        selected = int(frame)
    return (
        [row for row in rows if row.get("frame") == selected],
        {"mode": frame, "available_frames": frames, "selected": selected},
    )


def count_by(rows: list[dict[str, Any]], field: str) -> dict[str, int]:
    counter = Counter(str(row["signature"].get(field)) for row in rows)
    return {key: counter[key] for key in sorted(counter, key=lambda item: (-counter[item], item))}


def update_minmax(current: dict[str, Any], key: str, values: list[float]) -> None:
    if not values:
        return
    min_key = f"{key}_min"
    max_key = f"{key}_max"
    if current.get(min_key) is None:
        current[min_key] = values
        current[max_key] = values
        return
    current[min_key] = [min(current[min_key][index], values[index]) for index in range(len(values))]
    current[max_key] = [max(current[max_key][index], values[index]) for index in range(len(values))]


def region_semantics(roi_payload: dict[str, Any], region: str) -> dict[str, Any]:
    entry = (roi_payload.get("regions") or {}).get(region) or {}
    unmasked = entry.get("unmasked") or {}
    delta = unmasked.get("changed_delta") or {}
    return {
        "sampled_pixels": unmasked.get("sampled_pixels"),
        "changed_pixels": unmasked.get("changed_pixels"),
        "changed_pct": unmasked.get("changed_pct"),
        "luma_delta_mean": (delta.get("luma") or {}).get("mean"),
        "native_brighter_pct": delta.get("native_brighter_pct"),
        "native_darker_pct": delta.get("native_darker_pct"),
        "native_bluer_pct": delta.get("native_bluer_pct"),
    }


def summarize_region(
    rows: list[dict[str, Any]],
    region_name: str,
    region: tuple[int, int, int, int],
    aligned_size: tuple[int, int],
    roi_payload: dict[str, Any],
    top: int,
) -> dict[str, Any]:
    rx, ry, rw, rh = region
    region_pixels = rw * rh
    family_pixels: dict[tuple[tuple[str, str | None], ...], set[int]] = defaultdict(set)
    family_rows: dict[tuple[tuple[str, str | None], ...], dict[str, Any]] = {}
    effect_pixels: dict[str, set[int]] = defaultdict(set)
    class_pixels: dict[str, set[int]] = defaultdict(set)
    overlapping_rows: list[dict[str, Any]] = []
    all_pixels: set[int] = set()

    for row in rows:
        overlap = rect_overlap(row["aligned_bbox"], region)
        if overlap is None:
            continue
        pixels = raster_rect(overlap, aligned_size)
        if not pixels:
            continue
        overlapping_rows.append(row)
        all_pixels.update(pixels)

        sig = row["signature"]
        key = signature_key(sig)
        family_pixels[key].update(pixels)
        item = family_rows.setdefault(
            key,
            {
                "signature": sig,
                "rows": 0,
                "frames": [],
                "triangles": [],
                "examples": [],
                "shade0_min": None,
                "shade0_max": None,
                "shade1_min": None,
                "shade1_max": None,
                "shade2_min": None,
                "shade2_max": None,
                "env_min": None,
                "env_max": None,
                "prim_min": None,
                "prim_max": None,
                "fogc_min": None,
                "fogc_max": None,
            },
        )
        item["rows"] += 1
        if row.get("frame") is not None:
            item["frames"].append(row["frame"])
        if row.get("tri") is not None:
            item["triangles"].append(row["tri"])
        if len(item["examples"]) < 3:
            item["examples"].append({"source": row["source"], "line": row["line"]})
        for field in ("shade0", "shade1", "shade2", "env", "prim", "fogc"):
            update_minmax(item, field, row.get(field) or [])

        effect_pixels[str(sig.get("effect"))].update(pixels)
        class_pixels[str(sig.get("class"))].update(pixels)

    families: list[dict[str, Any]] = []
    for key, pixels in family_pixels.items():
        item = dict(family_rows[key])
        item["cover_pixels"] = len(pixels)
        item["cover_pct"] = 100.0 * len(pixels) / region_pixels if region_pixels else 0.0
        item["frames"] = sorted(set(item["frames"]))
        item["triangles"] = {
            "count": len(item["triangles"]),
            "min": min(item["triangles"]) if item["triangles"] else None,
            "max": max(item["triangles"]) if item["triangles"] else None,
        }
        families.append(item)
    families.sort(key=lambda item: (-item["cover_pixels"], -item["rows"], json.dumps(item["signature"], sort_keys=True)))

    effect_cover = {
        key: {
            "cover_pixels": len(pixels),
            "cover_pct": 100.0 * len(pixels) / region_pixels if region_pixels else 0.0,
        }
        for key, pixels in sorted(effect_pixels.items(), key=lambda item: (-len(item[1]), item[0]))
    }
    class_cover = {
        key: {
            "cover_pixels": len(pixels),
            "cover_pct": 100.0 * len(pixels) / region_pixels if region_pixels else 0.0,
        }
        for key, pixels in sorted(class_pixels.items(), key=lambda item: (-len(item[1]), item[0]))
    }
    semantics = region_semantics(roi_payload, region_name)
    changed_pct = semantics.get("changed_pct") or 0.0
    all_cover_pct = 100.0 * len(all_pixels) / region_pixels if region_pixels else 0.0

    return {
        "roi": [rx, ry, rw, rh],
        "source_pixels": region_pixels,
        "pixel_semantics_unmasked": semantics,
        "texgen_rows": len(overlapping_rows),
        "effect_counts": count_by(overlapping_rows, "effect"),
        "class_counts": count_by(overlapping_rows, "class"),
        "texgen_bbox_union": {
            "cover_pixels": len(all_pixels),
            "cover_pct": all_cover_pct,
            "changed_minus_cover_pct": changed_pct - all_cover_pct,
        },
        "effect_coverage": effect_cover,
        "class_coverage": class_cover,
        "top_families": families[:top],
    }


def shard_noop_summary(audit: dict[str, Any] | None) -> dict[str, Any] | None:
    if not audit:
        return None
    visual = audit.get("visual") or {}
    shard_oracle = audit.get("shard_pixel_oracle") or {}
    return {
        "status": audit.get("status"),
        "primary_region": audit.get("primary_region"),
        "default_primary_effect_counts": (audit.get("default") or {}).get("primary_effect_counts"),
        "shards_off_primary_rows": (audit.get("shards_off") or {}).get("primary_rows"),
        "default_vs_shards_off_full_changed_pct": visual.get("full_changed_pct"),
        "default_vs_shards_off_primary_changed_pct": visual.get("primary_changed_pct"),
        "shard_mask_coverage_pixels": shard_oracle.get("coverage_pixels"),
        "shard_mask_union_changed_pct": shard_oracle.get("union_changed_pct"),
    }


def build_interpretation(payload: dict[str, Any]) -> list[str]:
    out: list[str] = []
    shard = payload.get("shard_off_control")
    for name, region in payload["regions"].items():
        semantics = region["pixel_semantics_unmasked"]
        changed_pct = semantics.get("changed_pct") or 0.0
        cover_pct = region["texgen_bbox_union"]["cover_pct"]
        out.append(
            f"{name}: changed={changed_pct:.3f}% texgen_bbox_cover={cover_pct:.3f}% "
            f"rows={region['texgen_rows']}"
        )
        if cover_pct + 1.0 < changed_pct:
            out.append(
                f"{name}: texgen bboxes cover far less than changed pixels; "
                "look for background/post-composite or non-texgen contributors"
            )
        if "glass_shards" in region.get("effect_coverage", {}):
            shard_cover = region["effect_coverage"]["glass_shards"]["cover_pct"]
            if shard and shard.get("shard_mask_union_changed_pct") == 0.0:
                out.append(
                    f"{name}: glass_shards bbox cover={shard_cover:.3f}% but shard-off "
                    "framebuffer control is 0.000%, so bbox coverage is not visible ownership"
                )
        if "glass" in region.get("effect_coverage", {}):
            glass_cover = region["effect_coverage"]["glass"]["cover_pct"]
            out.append(
                f"{name}: room glass bbox cover={glass_cover:.3f}% with current room-alpha "
                "A/Bs already proven too weak to promote"
            )
    return out


def compare(args: argparse.Namespace) -> tuple[dict[str, Any], int]:
    failures: list[str] = []
    route = load_json(args.route_json)
    roi_payload = load_json(args.roi_pixel_semantics)
    aligned_size_value = ((roi_payload.get("alignment") or {}).get("aligned_size") or [])
    if len(aligned_size_value) != 2:
        failures.append("roi pixel semantics JSON is missing alignment.aligned_size")
        aligned_size = (0, 0)
    else:
        aligned_size = (int(aligned_size_value[0]), int(aligned_size_value[1]))
    regions = route_regions(route)
    selected_regions = args.region or ["tower_pane", "projected_impact", "impact_side"]
    for name in selected_regions:
        if name not in regions:
            failures.append(f"route has no visual region: {name}")

    for path in args.log:
        if not path.exists():
            failures.append(f"missing log: {path}")
    if not args.roi_pixel_semantics.exists():
        failures.append(f"missing roi pixel semantics JSON: {args.roi_pixel_semantics}")
    audit = load_json(args.texgen_audit) if args.texgen_audit and args.texgen_audit.exists() else None
    if args.texgen_audit and not args.texgen_audit.exists():
        failures.append(f"missing texgen audit JSON: {args.texgen_audit}")

    payload: dict[str, Any] = {
        "status": "fail" if failures else "pass",
        "failures": failures,
        "route": route.get("name"),
        "inputs": {
            "route_json": str(args.route_json),
            "logs": [str(path) for path in args.log],
            "roi_pixel_semantics": str(args.roi_pixel_semantics),
            "texgen_audit": str(args.texgen_audit) if args.texgen_audit else None,
        },
        "aligned_size": list(aligned_size),
        "regions": {},
        "shard_off_control": shard_noop_summary(audit),
        "interpretation": [],
    }
    if failures:
        return payload, 1

    rows = parse_rows(args.log, aligned_size)
    selected_rows, frame_selection = select_rows(rows, args.frame)
    payload["frame_selection"] = frame_selection
    payload["row_counts"] = {
        "parsed": len(rows),
        "selected": len(selected_rows),
    }
    for name in selected_regions:
        payload["regions"][name] = summarize_region(
            selected_rows,
            name,
            regions[name],
            aligned_size,
            roi_payload,
            args.top,
        )
    payload["interpretation"] = build_interpretation(payload)
    return payload, 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", nargs="+", type=Path)
    parser.add_argument("--route-json", type=Path, required=True)
    parser.add_argument("--roi-pixel-semantics", type=Path, required=True)
    parser.add_argument("--texgen-audit", type=Path)
    parser.add_argument("--frame", default="latest", help="'latest', 'all', or an integer native frame")
    parser.add_argument("--region", action="append")
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
