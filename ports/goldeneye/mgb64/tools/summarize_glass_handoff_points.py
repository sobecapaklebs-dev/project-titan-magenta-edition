#!/usr/bin/env python3
"""Summarize multiple glass stock/native pixel handoff JSON reports."""

from __future__ import annotations

import argparse
from collections import Counter
import json
import math
from pathlib import Path
from typing import Any


def luma(rgb: list[Any] | None) -> float | None:
    if not isinstance(rgb, list) or len(rgb) < 3:
        return None
    try:
        r, g, b = (float(rgb[0]), float(rgb[1]), float(rgb[2]))
    except (TypeError, ValueError):
        return None
    return 0.2126 * r + 0.7152 * g + 0.0722 * b


def rgb_delta(a: list[Any] | None, b: list[Any] | None) -> dict[str, Any] | None:
    if not isinstance(a, list) or not isinstance(b, list) or len(a) < 3 or len(b) < 3:
        return None
    try:
        delta = [int(a[index]) - int(b[index]) for index in range(3)]
    except (TypeError, ValueError):
        return None
    return {
        "delta": delta,
        "mean_abs_rgb": sum(abs(item) for item in delta) / 3.0,
        "luma_delta": luma([a[0], a[1], a[2]]) - luma([b[0], b[1], b[2]]),
    }


def float_rgb_delta(a: list[Any] | None, b: list[Any] | None) -> dict[str, Any] | None:
    if not isinstance(a, list) or not isinstance(b, list) or len(a) < 3 or len(b) < 3:
        return None
    try:
        delta = [float(a[index]) - float(b[index]) for index in range(3)]
    except (TypeError, ValueError):
        return None
    return {
        "delta": delta,
        "mean_abs_rgb": sum(abs(item) for item in delta) / 3.0,
        "luma_delta": luma([a[0], a[1], a[2]]) - luma([b[0], b[1], b[2]]),
    }


def required_source_rgb(output: list[Any] | None,
                        underlay: list[Any] | None,
                        alpha: Any) -> list[float] | None:
    if (
        not isinstance(output, list) or
        not isinstance(underlay, list) or
        len(output) < 3 or
        len(underlay) < 3
    ):
        return None
    try:
        alpha_unit = float(alpha) / 255.0
        out = [float(output[index]) for index in range(3)]
        base = [float(underlay[index]) for index in range(3)]
    except (TypeError, ValueError):
        return None
    if not math.isfinite(alpha_unit) or alpha_unit <= 0.0:
        return None
    inv_alpha = 1.0 - alpha_unit
    return [
        (out[index] - base[index] * inv_alpha) / alpha_unit
        for index in range(3)
    ]


def load_json(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def parse_point_spec(spec: str) -> tuple[str, Path]:
    if "=" in spec:
        label, path = spec.split("=", 1)
        label = label.strip()
        if not label:
            raise argparse.ArgumentTypeError("point label must not be empty")
        return label, Path(path)
    path = Path(spec)
    return path.stem, path


def first_rgb_delta(native_pixel: dict[str, Any], native_post: list[Any] | None,
                    stock_rgba: list[Any] | None) -> dict[str, Any] | None:
    recorded = native_pixel.get("selected_post_vs_stock_rgb")
    if isinstance(recorded, dict) and recorded.get("delta") is not None:
        return recorded
    return rgb_delta(native_post, stock_rgba)


def summarize_point(label: str, path: Path, payload: dict[str, Any]) -> dict[str, Any]:
    stock = payload.get("stock_pixel") or {}
    native_pixel = payload.get("native_pixel") or {}
    selected = stock.get("selected_sample") or stock.get("selected_changed_sample") or stock.get("last_sample") or {}
    native_final = native_pixel.get("selected_final") or {}
    framebuffer_input = stock.get("selected_framebuffer_input") or {}
    stock_rgba = selected.get("rgba")
    native_post = native_final.get("post")
    native_pre = native_final.get("pre")
    native_source = native_final.get("shaderL_frag")
    native_alpha = native_source[3] if isinstance(native_source, list) and len(native_source) >= 4 else None
    native_required_source = required_source_rgb(native_post, native_pre, native_alpha)
    stock_required_source = required_source_rgb(
        stock_rgba,
        framebuffer_input.get("rgba"),
        native_alpha,
    )
    delta = first_rgb_delta(native_pixel, native_post, stock_rgba)

    missing: list[str] = []
    if not stock_rgba:
        missing.append("stock_rgba")
    if not native_post:
        missing.append("native_post")

    return {
        "label": label,
        "path": str(path),
        "status": payload.get("status"),
        "missing": missing,
        "stock_target": stock.get("target"),
        "native_target": native_pixel.get("selected_native_target"),
        "native_frame": native_pixel.get("selected_native_frame"),
        "selection_reason": stock.get("selection_reason"),
        "stock_texture": selected.get("texture_image"),
        "stock_raw": selected.get("raw"),
        "stock_hidden": selected.get("hidden"),
        "stock_rgba": stock_rgba,
        "stock_luma": luma(stock_rgba),
        "stock_framebuffer_input_reason": stock.get("selected_framebuffer_input_reason"),
        "stock_framebuffer_input_rgba": framebuffer_input.get("rgba"),
        "stock_framebuffer_input_vs_selected_rgb": stock.get("selected_framebuffer_input_vs_selected_rgb"),
        "stock_hidden_transition": stock.get("selected_hidden_transition"),
        "native_pre": native_pre,
        "native_source_rgba": native_source,
        "native_source_luma": luma(native_source),
        "native_required_source_from_pre_post_alpha": native_required_source,
        "native_required_source_vs_reconstructed_rgb": float_rgb_delta(native_required_source, native_source),
        "native_pre_vs_stock_framebuffer_input_rgb": rgb_delta(native_pre, framebuffer_input.get("rgba")),
        "stock_required_source_from_framebuffer_input_alpha": stock_required_source,
        "stock_required_source_vs_native_source_rgb": float_rgb_delta(stock_required_source, native_source),
        "native_post": native_post,
        "native_post_luma": luma(native_post),
        "native_changed": native_final.get("changed"),
        "native_post_vs_stock_rgb": delta,
        "native_inside_rows": native_pixel.get("inside_rows"),
        "native_changed_inside_rows": native_pixel.get("changed_inside_rows"),
        "warnings": payload.get("warnings") or [],
    }


def build_summary(points: list[dict[str, Any]]) -> dict[str, Any]:
    deltas = [
        point["native_post_vs_stock_rgb"]["mean_abs_rgb"]
        for point in points
        if isinstance(point.get("native_post_vs_stock_rgb"), dict) and
        point["native_post_vs_stock_rgb"].get("mean_abs_rgb") is not None
    ]
    required_source_deltas = [
        point["native_required_source_vs_reconstructed_rgb"]["mean_abs_rgb"]
        for point in points
        if isinstance(point.get("native_required_source_vs_reconstructed_rgb"), dict) and
        point["native_required_source_vs_reconstructed_rgb"].get("mean_abs_rgb") is not None
    ]
    input_gaps = [
        point["native_pre_vs_stock_framebuffer_input_rgb"]["mean_abs_rgb"]
        for point in points
        if isinstance(point.get("native_pre_vs_stock_framebuffer_input_rgb"), dict) and
        point["native_pre_vs_stock_framebuffer_input_rgb"].get("mean_abs_rgb") is not None
    ]
    hidden_transitions = Counter()
    for point in points:
        transition = point.get("stock_hidden_transition") or {}
        before = transition.get("before")
        after = transition.get("after")
        if before is not None or after is not None:
            hidden_transitions[(before, after)] += 1

    return {
        "points": len(points),
        "points_with_stock_and_native": sum(1 for point in points if not point["missing"]),
        "points_with_framebuffer_input": sum(1 for point in points if point.get("stock_framebuffer_input_rgba")),
        "exact_points": sum(1 for value in deltas if value == 0.0),
        "near_points_le_1": sum(1 for value in deltas if value <= 1.0),
        "off_points_gt_1": sum(1 for value in deltas if value > 1.0),
        "mean_abs_rgb": {
            "min": min(deltas) if deltas else None,
            "mean": sum(deltas) / len(deltas) if deltas else None,
            "max": max(deltas) if deltas else None,
        },
        "native_required_source_vs_reconstructed_rgb": {
            "points": len(required_source_deltas),
            "min": min(required_source_deltas) if required_source_deltas else None,
            "mean": sum(required_source_deltas) / len(required_source_deltas) if required_source_deltas else None,
            "max": max(required_source_deltas) if required_source_deltas else None,
        },
        "native_pre_vs_stock_framebuffer_input_rgb": {
            "points": len(input_gaps),
            "min": min(input_gaps) if input_gaps else None,
            "mean": sum(input_gaps) / len(input_gaps) if input_gaps else None,
            "max": max(input_gaps) if input_gaps else None,
        },
        "hidden_transitions": [
            {"before": before, "after": after, "points": count}
            for (before, after), count in hidden_transitions.most_common()
        ],
    }


def build_interpretation(summary: dict[str, Any], points: list[dict[str, Any]]) -> list[str]:
    mean_abs = summary.get("mean_abs_rgb") or {}
    required = summary.get("native_required_source_vs_reconstructed_rgb") or {}
    input_gap = summary.get("native_pre_vs_stock_framebuffer_input_rgb") or {}
    notes = [
        f"{summary['points_with_stock_and_native']}/{summary['points']} points have both stock and native final pixels",
        f"near/exact points <=1 mean_abs_rgb: {summary['near_points_le_1']}; off points >1: {summary['off_points_gt_1']}",
    ]
    if mean_abs.get("max") is not None:
        notes.append(
            "native post versus stock final mean_abs_rgb range: "
            f"min={mean_abs.get('min'):.3f} mean={mean_abs.get('mean'):.3f} max={mean_abs.get('max'):.3f}"
        )
    if required.get("max") is not None:
        notes.append(
            "native observed pre/post requires source-vs-reconstructed mean_abs_rgb: "
            f"min={required.get('min'):.3f} mean={required.get('mean'):.3f} max={required.get('max'):.3f}"
        )
    if input_gap.get("max") is not None:
        notes.append(
            "native pre pixel versus stock framebuffer-input mean_abs_rgb: "
            f"min={input_gap.get('min'):.3f} mean={input_gap.get('mean'):.3f} max={input_gap.get('max'):.3f}"
        )
    missing_input = [point["label"] for point in points if not point.get("stock_framebuffer_input_rgba")]
    if missing_input:
        notes.append(
            "points without same-frame stock framebuffer-input sample: "
            + ", ".join(missing_input)
        )
    return notes


def summarize(specs: list[tuple[str, Path]]) -> dict[str, Any]:
    failures: list[str] = []
    points: list[dict[str, Any]] = []
    for label, path in specs:
        if not path.exists():
            failures.append(f"{label}: missing handoff JSON {path}")
            continue
        point = summarize_point(label, path, load_json(path))
        if point["missing"]:
            failures.append(f"{label}: missing {', '.join(point['missing'])}")
        points.append(point)

    summary = build_summary(points)
    payload = {
        "status": "fail" if failures else "pass",
        "summary": summary,
        "points": points,
        "failures": failures,
    }
    payload["interpretation"] = build_interpretation(summary, points)
    return payload


def print_human(payload: dict[str, Any]) -> None:
    summary = payload["summary"]
    mean_abs = summary.get("mean_abs_rgb") or {}
    print(
        "glass handoff points: "
        f"{summary['points_with_stock_and_native']}/{summary['points']} complete "
        f"near_le_1={summary['near_points_le_1']} off_gt_1={summary['off_points_gt_1']} "
        f"max_mean_abs_rgb={mean_abs.get('max')}"
    )
    for point in payload["points"]:
        delta = point.get("native_post_vs_stock_rgb") or {}
        print(
            f"- {point['label']}: stock={point.get('stock_rgba')} "
            f"native_post={point.get('native_post')} "
            f"mean_abs_rgb={delta.get('mean_abs_rgb')} "
            f"fb_in={point.get('stock_framebuffer_input_rgba')} "
            f"hidden={point.get('stock_hidden_transition')} "
            f"source={point.get('native_source_rgba')} "
            f"required_source={point.get('native_required_source_from_pre_post_alpha')}"
        )
    if payload.get("failures"):
        print("failures:")
        for failure in payload["failures"]:
            print(f"  {failure}")
    print("interpretation:")
    for item in payload.get("interpretation") or []:
        print(f"  {item}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("points", nargs="+", type=parse_point_spec, help="handoff JSON path or LABEL=path")
    parser.add_argument("--json-out", type=Path)
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()

    payload = summarize(args.points)
    if args.json_out:
        args.json_out.parent.mkdir(parents=True, exist_ok=True)
        args.json_out.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    if args.json:
        print(json.dumps(payload, indent=2, sort_keys=True))
    else:
        print_human(payload)
    return 0 if payload["status"] == "pass" else 1


if __name__ == "__main__":
    raise SystemExit(main())
