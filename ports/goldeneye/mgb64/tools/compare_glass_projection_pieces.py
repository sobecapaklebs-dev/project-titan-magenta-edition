#!/usr/bin/env python3
"""Compare per-piece stock/native glass shard projection samples."""

from __future__ import annotations

import argparse
import json
import math
import statistics
from pathlib import Path
from typing import Any


def load_records(path: Path) -> list[dict[str, Any]]:
    records: list[dict[str, Any]] = []
    with path.open("r", encoding="utf-8") as handle:
        for line_no, line in enumerate(handle, 1):
            line = line.strip()
            if not line:
                continue
            try:
                records.append(json.loads(line))
            except json.JSONDecodeError as exc:
                raise SystemExit(f"FAIL: {path}:{line_no}: invalid JSON: {exc}") from exc
    return records


def as_int(value: Any, default: int = 0) -> int:
    try:
        return int(value)
    except (TypeError, ValueError):
        return default


def as_float(value: Any, default: float = 0.0) -> float:
    try:
        parsed = float(value)
    except (TypeError, ValueError):
        return default
    return parsed if math.isfinite(parsed) else default


def projection(record: dict[str, Any] | None, variant: str | None = None) -> dict[str, Any]:
    if not isinstance(record, dict):
        return {}
    value = record.get("glass_projection")
    if not isinstance(value, dict):
        return {}
    if variant is None:
        return value
    variants = value.get("variants")
    if not isinstance(variants, dict):
        return {}
    selected = variants.get(variant)
    if not isinstance(selected, dict):
        return {}
    merged = dict(value)
    merged.update(selected)
    merged["source"] = f"{value.get('source', 'unknown')}#{variant}"
    merged["sample"] = selected.get("sample", [])
    return merged


def record_frame(record: dict[str, Any], fallback: int) -> int:
    return as_int(record.get("f"), fallback)


def first_active_record(
    records: list[dict[str, Any]], variant: str | None = None
) -> tuple[int | None, dict[str, Any] | None]:
    for index, record in enumerate(records):
        proj = projection(record, variant)
        if as_int(proj.get("active")) > 0:
            return record_frame(record, index + 1), record
    return None, None


def frame_record(
    records: list[dict[str, Any]],
    frame: int | None,
    variant: str | None = None,
) -> tuple[int | None, dict[str, Any] | None]:
    if frame is None:
        return first_active_record(records, variant)
    for index, record in enumerate(records):
        if record_frame(record, index + 1) == frame:
            return frame, record
    return frame, None


def sample_index(proj: dict[str, Any]) -> dict[int, dict[str, Any]]:
    samples = proj.get("sample")
    if not isinstance(samples, list):
        return {}
    out: dict[int, dict[str, Any]] = {}
    for sample in samples:
        if not isinstance(sample, dict):
            continue
        index = as_int(sample.get("index"), -1)
        if index >= 0:
            out[index] = sample
    return out


def bbox_area(sample: dict[str, Any]) -> float:
    bbox = sample.get("screen_bbox")
    if not isinstance(bbox, list) or len(bbox) != 4:
        return as_float(sample.get("screen_area"))
    min_x, min_y, max_x, max_y = (as_float(value) for value in bbox)
    return max(0.0, max_x - min_x) * max(0.0, max_y - min_y)


def bbox_center(sample: dict[str, Any]) -> tuple[float, float]:
    bbox = sample.get("screen_bbox")
    if not isinstance(bbox, list) or len(bbox) != 4:
        return (0.0, 0.0)
    min_x, min_y, max_x, max_y = (as_float(value) for value in bbox)
    return ((min_x + max_x) * 0.5, (min_y + max_y) * 0.5)


def vector3(value: Any) -> tuple[float, float, float]:
    if not isinstance(value, list) or len(value) < 3:
        return (0.0, 0.0, 0.0)
    return (as_float(value[0]), as_float(value[1]), as_float(value[2]))


def vec_delta(a: tuple[float, float, float], b: tuple[float, float, float]) -> dict[str, float]:
    dx = b[0] - a[0]
    dy = b[1] - a[1]
    dz = b[2] - a[2]
    return {
        "dx": dx,
        "dy": dy,
        "dz": dz,
        "abs": math.sqrt(dx * dx + dy * dy + dz * dz),
        "xz": math.sqrt(dx * dx + dz * dz),
    }


def model_centroid(sample: dict[str, Any]) -> tuple[float, float, float]:
    model = sample.get("model")
    if not isinstance(model, list) or not model:
        return (0.0, 0.0, 0.0)
    points = [vector3(point) for point in model if isinstance(point, list)]
    if not points:
        return (0.0, 0.0, 0.0)
    return (
        sum(point[0] for point in points) / len(points),
        sum(point[1] for point in points) / len(points),
        sum(point[2] for point in points) / len(points),
    )


def mean_clip_w(sample: dict[str, Any]) -> float:
    clip_w = sample.get("clip_w")
    if not isinstance(clip_w, list) or not clip_w:
        return 0.0
    values = [as_float(value) for value in clip_w]
    return sum(values) / len(values)


def screen_points(sample: dict[str, Any]) -> list[tuple[float, float]]:
    screen = sample.get("screen")
    if not isinstance(screen, list):
        return []
    points: list[tuple[float, float]] = []
    for point in screen:
        if isinstance(point, list) and len(point) >= 2:
            points.append((as_float(point[0]), as_float(point[1])))
    return points


def screen_point_rms(a: dict[str, Any], b: dict[str, Any]) -> float:
    pa = screen_points(a)
    pb = screen_points(b)
    if len(pa) != len(pb) or not pa:
        return 0.0
    total = 0.0
    for left, right in zip(pa, pb):
        dx = right[0] - left[0]
        dy = right[1] - left[1]
        total += dx * dx + dy * dy
    return math.sqrt(total / len(pa))


def transition(left: dict[str, Any], right: dict[str, Any]) -> str:
    a = 1 if as_int(left.get("onscreen")) else 0
    b = 1 if as_int(right.get("onscreen")) else 0
    return f"{a}->{b}"


def percentile(values: list[float], pct: float) -> float:
    if not values:
        return 0.0
    ordered = sorted(values)
    if len(ordered) == 1:
        return ordered[0]
    pos = (len(ordered) - 1) * pct
    lo = math.floor(pos)
    hi = math.ceil(pos)
    if lo == hi:
        return ordered[lo]
    frac = pos - lo
    return ordered[lo] * (1.0 - frac) + ordered[hi] * frac


def summarize_values(values: list[float]) -> dict[str, float]:
    if not values:
        return {"max": 0.0, "mean": 0.0, "median": 0.0, "p90": 0.0}
    return {
        "max": max(values),
        "mean": statistics.fmean(values),
        "median": statistics.median(values),
        "p90": percentile(values, 0.90),
    }


def compare_piece(index: int, baseline: dict[str, Any], test: dict[str, Any]) -> dict[str, Any]:
    b_area = bbox_area(baseline)
    t_area = bbox_area(test)
    b_center = bbox_center(baseline)
    t_center = bbox_center(test)
    center_dx = t_center[0] - b_center[0]
    center_dy = t_center[1] - b_center[1]
    b_clip = mean_clip_w(baseline)
    t_clip = mean_clip_w(test)
    world_delta = vec_delta(vector3(baseline.get("world")), vector3(test.get("world")))
    model_delta = vec_delta(model_centroid(baseline), model_centroid(test))
    return {
        "index": index,
        "timer": [as_int(baseline.get("timer")), as_int(test.get("timer"))],
        "transition": transition(baseline, test),
        "world_delta": world_delta,
        "model_centroid_delta": model_delta,
        "screen_center_delta": {
            "dx": center_dx,
            "dy": center_dy,
            "abs": math.sqrt(center_dx * center_dx + center_dy * center_dy),
        },
        "screen_point_rms": screen_point_rms(baseline, test),
        "area": {
            "baseline": b_area,
            "test": t_area,
            "delta": t_area - b_area,
            "ratio": t_area / b_area if b_area > 0.0 else 0.0,
        },
        "mean_clip_w": {
            "baseline": b_clip,
            "test": t_clip,
            "delta": t_clip - b_clip,
            "ratio": t_clip / b_clip if abs(b_clip) > 0.000001 else 0.0,
        },
        "baseline": {
            "world": baseline.get("world"),
            "model_centroid": list(model_centroid(baseline)),
            "screen_bbox": baseline.get("screen_bbox"),
            "clip_w": baseline.get("clip_w"),
        },
        "test": {
            "world": test.get("world"),
            "model_centroid": list(model_centroid(test)),
            "screen_bbox": test.get("screen_bbox"),
            "clip_w": test.get("clip_w"),
        },
    }


def projection_summary(label: str, frame: int | None, proj: dict[str, Any]) -> dict[str, Any]:
    return {
        "label": label,
        "frame": frame,
        "present": as_int(proj.get("present")),
        "source": proj.get("source"),
        "scale_mode": proj.get("scale_mode"),
        "active": as_int(proj.get("active")),
        "projected": as_int(proj.get("projected")),
        "onscreen": as_int(proj.get("onscreen")),
        "behind": as_int(proj.get("behind")),
        "sample_all": as_int(proj.get("sample_all")),
        "sample_limit": as_int(proj.get("sample_limit")),
        "sample_count": as_int(proj.get("sample_count")),
        "sample_truncated": as_int(proj.get("sample_truncated")),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("baseline", type=Path)
    parser.add_argument("test", type=Path)
    parser.add_argument("--baseline-label", default="baseline")
    parser.add_argument("--test-label", default="test")
    parser.add_argument("--baseline-frame", type=int)
    parser.add_argument("--test-frame", type=int)
    parser.add_argument("--baseline-variant")
    parser.add_argument("--test-variant")
    parser.add_argument("--require-full-sample", action="store_true")
    parser.add_argument("--require-same-indices", action="store_true")
    parser.add_argument("--top", type=int, default=12)
    parser.add_argument("--json-out", type=Path)
    args = parser.parse_args()

    baseline_frame, baseline_record = frame_record(
        load_records(args.baseline), args.baseline_frame, args.baseline_variant
    )
    test_frame, test_record = frame_record(load_records(args.test), args.test_frame, args.test_variant)
    baseline_proj = projection(baseline_record, args.baseline_variant)
    test_proj = projection(test_record, args.test_variant)
    baseline_samples = sample_index(baseline_proj)
    test_samples = sample_index(test_proj)

    failures: list[str] = []
    for label, proj, samples in (
        ("baseline", baseline_proj, baseline_samples),
        ("test", test_proj, test_samples),
    ):
        if as_int(proj.get("present")) != 1:
            failures.append(f"{label} glass_projection is not present")
        if as_int(proj.get("active")) <= 0:
            failures.append(f"{label} glass_projection has no active shards")
        if args.require_full_sample:
            if as_int(proj.get("sample_truncated")):
                failures.append(f"{label} projection sample is truncated")
            if len(samples) < as_int(proj.get("active")):
                failures.append(
                    f"{label} has {len(samples)} sampled pieces for {as_int(proj.get('active'))} active shards"
                )

    baseline_indices = set(baseline_samples)
    test_indices = set(test_samples)
    common_indices = sorted(baseline_indices & test_indices)
    missing_from_test = sorted(baseline_indices - test_indices)
    extra_in_test = sorted(test_indices - baseline_indices)
    if args.require_same_indices and (missing_from_test or extra_in_test):
        failures.append(
            f"sample index sets differ: missing_from_test={missing_from_test[:20]} "
            f"extra_in_test={extra_in_test[:20]}"
        )
    if not common_indices:
        failures.append("no common sampled piece indices")

    pieces = [
        compare_piece(index, baseline_samples[index], test_samples[index])
        for index in common_indices
    ]
    transitions: dict[str, int] = {}
    for piece in pieces:
        transitions[piece["transition"]] = transitions.get(piece["transition"], 0) + 1

    center_abs = [piece["screen_center_delta"]["abs"] for piece in pieces]
    point_rms = [piece["screen_point_rms"] for piece in pieces]
    area_abs = [abs(piece["area"]["delta"]) for piece in pieces]
    clip_ratio = [piece["mean_clip_w"]["ratio"] for piece in pieces]
    model_abs = [piece["model_centroid_delta"]["abs"] for piece in pieces]
    world_abs = [piece["world_delta"]["abs"] for piece in pieces]

    top_center = sorted(pieces, key=lambda piece: piece["screen_center_delta"]["abs"], reverse=True)[
        : max(0, args.top)
    ]
    top_area = sorted(pieces, key=lambda piece: abs(piece["area"]["delta"]), reverse=True)[
        : max(0, args.top)
    ]
    top_model = sorted(pieces, key=lambda piece: piece["model_centroid_delta"]["abs"], reverse=True)[
        : max(0, args.top)
    ]

    payload = {
        "status": "fail" if failures else "pass",
        "failures": failures,
        "baseline": projection_summary(args.baseline_label, baseline_frame, baseline_proj),
        "test": projection_summary(args.test_label, test_frame, test_proj),
        "common_count": len(common_indices),
        "missing_from_test": missing_from_test,
        "extra_in_test": extra_in_test,
        "transitions": transitions,
        "metrics": {
            "screen_center_delta": summarize_values(center_abs),
            "screen_point_rms": summarize_values(point_rms),
            "abs_area_delta": summarize_values(area_abs),
            "clip_w_ratio": summarize_values(clip_ratio),
            "model_centroid_delta": summarize_values(model_abs),
            "world_delta": summarize_values(world_abs),
        },
        "top_screen_center_delta": top_center,
        "top_abs_area_delta": top_area,
        "top_model_centroid_delta": top_model,
    }

    if args.json_out:
        args.json_out.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")

    print(
        "=== glass projection pieces: "
        f"{args.baseline_label} frame={baseline_frame} vs {args.test_label} frame={test_frame} ==="
    )
    print(
        f"  samples: common={len(common_indices)} "
        f"baseline={len(baseline_samples)}/{as_int(baseline_proj.get('active'))} "
        f"test={len(test_samples)}/{as_int(test_proj.get('active'))}"
    )
    print(f"  transitions: {transitions}")
    print(
        "  screen_center_delta: "
        f"mean={payload['metrics']['screen_center_delta']['mean']:.2f} "
        f"p90={payload['metrics']['screen_center_delta']['p90']:.2f} "
        f"max={payload['metrics']['screen_center_delta']['max']:.2f}"
    )
    print(
        "  model_centroid_delta: "
        f"mean={payload['metrics']['model_centroid_delta']['mean']:.2f} "
        f"max={payload['metrics']['model_centroid_delta']['max']:.2f}"
    )
    print(
        "  clip_w_ratio: "
        f"median={payload['metrics']['clip_w_ratio']['median']:.4f} "
        f"p90={payload['metrics']['clip_w_ratio']['p90']:.4f}"
    )
    if failures:
        print("FAIL: glass projection piece comparison failed")
        for failure in failures:
            print(f"  - {failure}")
        return 1
    print("PASS: glass projection piece comparison")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
