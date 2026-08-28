#!/usr/bin/env python3
"""Compare glass shard projection coverage from stock/native trace JSONL files."""

from __future__ import annotations

import argparse
import json
import math
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


def record_frame(record: dict[str, Any], fallback: int) -> int:
    return as_int(record.get("f"), fallback)


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


def first_projected_record(records: list[dict[str, Any]]) -> tuple[int | None, dict[str, Any] | None]:
    for index, record in enumerate(records):
        proj = projection(record)
        if as_int(proj.get("active")) > 0:
            return record_frame(record, index + 1), record
    return None, None


def frame_record(records: list[dict[str, Any]], frame: int | None) -> tuple[int | None, dict[str, Any] | None]:
    if frame is None:
        return first_projected_record(records)
    for index, record in enumerate(records):
        if record_frame(record, index + 1) == frame:
            return frame, record
    return frame, None


def bbox_area(bbox: Any) -> float:
    if not isinstance(bbox, list) or len(bbox) != 4:
        return 0.0
    min_x, min_y, max_x, max_y = (as_float(value) for value in bbox)
    area = max(0.0, max_x - min_x) * max(0.0, max_y - min_y)
    return area if math.isfinite(area) else 0.0


def viewport_area(proj: dict[str, Any]) -> float:
    viewport = proj.get("viewport")
    if not isinstance(viewport, list) or len(viewport) != 4:
        return 320.0 * 240.0
    width = as_float(viewport[2], 320.0)
    height = as_float(viewport[3], 240.0)
    area = max(0.0, width) * max(0.0, height)
    return area if area > 0.0 else 320.0 * 240.0


def summarize(
    label: str,
    frame: int | None,
    record: dict[str, Any] | None,
    variant: str | None = None,
) -> dict[str, Any]:
    proj = projection(record, variant)
    vp_area = viewport_area(proj)
    max_area = as_float(proj.get("max_screen_area"))
    union_area = bbox_area(proj.get("union_screen_bbox"))
    return {
        "label": label,
        "frame": frame,
        "present": as_int(proj.get("present")),
        "source": proj.get("source"),
        "scale_mode": proj.get("scale_mode"),
        "emit_enabled": as_int(proj.get("emit_enabled")),
        "projection_valid": as_int(proj.get("projection_valid")),
        "projection_float": as_int(proj.get("projection_float")),
        "active": as_int(proj.get("active")),
        "projected": as_int(proj.get("projected")),
        "onscreen": as_int(proj.get("onscreen")),
        "behind": as_int(proj.get("behind")),
        "viewport": proj.get("viewport"),
        "viewport_area": vp_area,
        "max_screen_area": max_area,
        "max_screen_area_pct": 100.0 * max_area / vp_area if vp_area else 0.0,
        "union_screen_bbox": proj.get("union_screen_bbox"),
        "union_screen_area": union_area,
        "union_screen_area_pct": 100.0 * union_area / vp_area if vp_area else 0.0,
        "sample_count": as_int(proj.get("sample_count")),
        "max_piece": proj.get("max_piece") if isinstance(proj.get("max_piece"), dict) else {},
        "sample": proj.get("sample") if isinstance(proj.get("sample"), list) else [],
    }


def pct_delta(a: float, b: float) -> float:
    return b - a


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("baseline", type=Path)
    parser.add_argument("test", type=Path)
    parser.add_argument("--baseline-label", default="baseline")
    parser.add_argument("--test-label", default="test")
    parser.add_argument("--baseline-variant")
    parser.add_argument("--test-variant")
    parser.add_argument("--baseline-frame", type=int)
    parser.add_argument("--test-frame", type=int)
    parser.add_argument("--require-present", action="store_true")
    parser.add_argument("--max-active-delta", type=int)
    parser.add_argument("--max-projected-delta", type=int)
    parser.add_argument("--max-onscreen-delta", type=int)
    parser.add_argument("--max-behind-delta", type=int)
    parser.add_argument("--max-test-max-area-pct", type=float)
    parser.add_argument("--max-test-union-area-pct", type=float)
    parser.add_argument("--max-max-area-pct-delta", type=float)
    parser.add_argument("--max-union-area-pct-delta", type=float)
    parser.add_argument("--json-out", type=Path)
    args = parser.parse_args()

    baseline_frame, baseline_record = frame_record(load_records(args.baseline), args.baseline_frame)
    test_frame, test_record = frame_record(load_records(args.test), args.test_frame)
    baseline = summarize(args.baseline_label, baseline_frame, baseline_record, args.baseline_variant)
    test = summarize(args.test_label, test_frame, test_record, args.test_variant)

    failures: list[str] = []
    if args.require_present:
        for side, summary in (("baseline", baseline), ("test", test)):
            if summary["present"] != 1:
                failures.append(f"{side} glass_projection is not present")
            if summary["projection_valid"] != 1:
                failures.append(f"{side} glass_projection projection matrix is not valid")
            if summary["active"] <= 0:
                failures.append(f"{side} glass_projection has no active shards")

    deltas = {
        "active": test["active"] - baseline["active"],
        "projected": test["projected"] - baseline["projected"],
        "onscreen": test["onscreen"] - baseline["onscreen"],
        "behind": test["behind"] - baseline["behind"],
        "max_screen_area_pct": pct_delta(
            baseline["max_screen_area_pct"], test["max_screen_area_pct"]
        ),
        "union_screen_area_pct": pct_delta(
            baseline["union_screen_area_pct"], test["union_screen_area_pct"]
        ),
    }

    checks = [
        ("active", args.max_active_delta, "active shard count"),
        ("projected", args.max_projected_delta, "projected shard count"),
        ("onscreen", args.max_onscreen_delta, "onscreen shard count"),
        ("behind", args.max_behind_delta, "behind shard count"),
    ]
    for key, limit, label in checks:
        if limit is not None and abs(deltas[key]) > limit:
            failures.append(f"{label} delta {deltas[key]} exceeds {limit}")

    if (
        args.max_test_max_area_pct is not None
        and test["max_screen_area_pct"] > args.max_test_max_area_pct
    ):
        failures.append(
            f"test max shard bbox area {test['max_screen_area_pct']:.3f}% exceeds "
            f"{args.max_test_max_area_pct:.3f}%"
        )
    if (
        args.max_test_union_area_pct is not None
        and test["union_screen_area_pct"] > args.max_test_union_area_pct
    ):
        failures.append(
            f"test union bbox area {test['union_screen_area_pct']:.3f}% exceeds "
            f"{args.max_test_union_area_pct:.3f}%"
        )
    if (
        args.max_max_area_pct_delta is not None
        and abs(deltas["max_screen_area_pct"]) > args.max_max_area_pct_delta
    ):
        failures.append(
            f"max shard bbox area delta {deltas['max_screen_area_pct']:.3f}% exceeds "
            f"{args.max_max_area_pct_delta:.3f}%"
        )
    if (
        args.max_union_area_pct_delta is not None
        and abs(deltas["union_screen_area_pct"]) > args.max_union_area_pct_delta
    ):
        failures.append(
            f"union bbox area delta {deltas['union_screen_area_pct']:.3f}% exceeds "
            f"{args.max_union_area_pct_delta:.3f}%"
        )

    payload = {
        "status": "fail" if failures else "pass",
        "failures": failures,
        "baseline": baseline,
        "test": test,
        "deltas": deltas,
    }
    if args.json_out:
        args.json_out.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")

    print(
        "=== glass projection: "
        f"{args.baseline_label} frame={baseline_frame} vs {args.test_label} frame={test_frame} ==="
    )
    for name, summary in (("baseline", baseline), ("test", test)):
        print(
            f"  {name}: present={summary['present']} active={summary['active']} "
            f"projected={summary['projected']} onscreen={summary['onscreen']} "
            f"behind={summary['behind']} max_area={summary['max_screen_area_pct']:.3f}% "
            f"union={summary['union_screen_area_pct']:.3f}% source={summary['source']} "
            f"scale={summary['scale_mode']}"
        )
    print(
        "  deltas: "
        f"active={deltas['active']} projected={deltas['projected']} "
        f"onscreen={deltas['onscreen']} behind={deltas['behind']} "
        f"max_area={deltas['max_screen_area_pct']:.3f}% "
        f"union={deltas['union_screen_area_pct']:.3f}%"
    )
    if failures:
        print("FAIL: glass projection comparison failed")
        for failure in failures:
            print(f"  - {failure}")
        return 1
    print("PASS: glass projection comparison")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
