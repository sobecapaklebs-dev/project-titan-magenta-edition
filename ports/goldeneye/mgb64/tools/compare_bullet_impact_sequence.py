#!/usr/bin/env python3
"""Compare sampled bullet-impact sequences between two oracle traces.

This is a diagnostic guard for visual fixtures where the first impact sample can
match while later samples differ and still contribute visible pixels.
"""

from __future__ import annotations

import argparse
import json
import math
import sys
from collections import Counter
from pathlib import Path
from typing import Any


def load_frame(path: Path, frame: int) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as handle:
        for line_no, line in enumerate(handle, start=1):
            if not line.strip():
                continue
            try:
                record = json.loads(line)
            except json.JSONDecodeError as exc:
                raise ValueError(f"{path}:{line_no}: invalid JSON: {exc}") from exc
            if record.get("f") == frame:
                return record
    raise ValueError(f"frame {frame} not found in {path}")


def normalize_int(value: Any) -> int | None:
    if isinstance(value, bool):
        return int(value)
    if isinstance(value, int):
        return value
    if isinstance(value, float) and math.isfinite(value) and value.is_integer():
        return int(value)
    if isinstance(value, str):
        try:
            return int(value, 0)
        except ValueError:
            return None
    return None


def numeric_vector(value: Any, length: int | None = None) -> list[float] | None:
    if not isinstance(value, list):
        return None
    if length is not None and len(value) != length:
        return None
    output: list[float] = []
    for item in value:
        if not isinstance(item, (int, float)) or not math.isfinite(float(item)):
            return None
        output.append(float(item))
    return output


def numeric_bbox(value: Any) -> list[float] | None:
    bbox = numeric_vector(value, 4)
    if bbox is None:
        return None
    if bbox[2] <= bbox[0] or bbox[3] <= bbox[1]:
        return None
    return bbox


def bbox_center(bbox: list[float]) -> list[float]:
    return [(bbox[0] + bbox[2]) * 0.5, (bbox[1] + bbox[3]) * 0.5]


def distance(a: list[float] | None, b: list[float] | None) -> float | None:
    if a is None or b is None or len(a) != len(b):
        return None
    return math.sqrt(sum((a[index] - b[index]) ** 2 for index in range(len(a))))


def extract_samples(record: dict[str, Any], scope: str) -> tuple[dict[str, Any], list[dict[str, Any]]]:
    state = record.get("impact_state")
    if not isinstance(state, dict):
        return {}, []
    sample = state.get("sample")
    if not isinstance(sample, list):
        return state, []

    output: list[dict[str, Any]] = []
    for item in sample:
        if not isinstance(item, dict):
            continue
        is_world = bool(normalize_int(item.get("world")))
        if scope == "world" and not is_world:
            continue
        if scope == "prop" and is_world:
            continue
        output.append(item)
    output.sort(key=lambda item: normalize_int(item.get("index")) if normalize_int(item.get("index")) is not None else 10**9)
    return state, output


def sample_identity(sample: dict[str, Any]) -> dict[str, Any]:
    return {
        "index": normalize_int(sample.get("index")),
        "world": normalize_int(sample.get("world")),
        "impact": normalize_int(sample.get("impact")),
        "room": normalize_int(sample.get("room")),
        "prop": normalize_int(sample.get("prop")),
        "prop_pad": normalize_int(sample.get("prop_pad")),
        "model_pos": normalize_int(sample.get("model_pos")),
    }


def projection_summary(sample: dict[str, Any]) -> dict[str, Any]:
    projection = sample.get("projection")
    if not isinstance(projection, dict):
        return {"valid": None, "screen_bbox": None, "screen_center": None, "screen_area": None}
    bbox = numeric_bbox(projection.get("screen_bbox"))
    return {
        "valid": normalize_int(projection.get("valid")),
        "onscreen": normalize_int(projection.get("onscreen")),
        "behind": normalize_int(projection.get("behind")),
        "source": projection.get("source"),
        "screen_bbox": bbox,
        "screen_center": bbox_center(bbox) if bbox is not None else None,
        "screen_area": projection.get("screen_area") if isinstance(projection.get("screen_area"), (int, float)) else None,
    }


def sample_summary(sample: dict[str, Any]) -> dict[str, Any]:
    output = sample_identity(sample)
    output.update(
        {
            "center": numeric_vector(sample.get("center"), 3),
            "world_center": numeric_vector(sample.get("world_center"), 3),
            "tc": sample.get("tc") if isinstance(sample.get("tc"), list) else None,
            "rgba": sample.get("rgba") if isinstance(sample.get("rgba"), list) else None,
            "projection": projection_summary(sample),
        }
    )
    return output


def sequence_of(samples: list[dict[str, Any]], key: str) -> list[Any]:
    return [sample_identity(item).get(key) for item in samples]


def counter_dict(values: list[Any]) -> dict[str, int]:
    return {str(key): count for key, count in sorted(Counter(values).items(), key=lambda item: str(item[0]))}


def compare_pair(baseline: dict[str, Any], test: dict[str, Any]) -> dict[str, Any]:
    baseline_summary = sample_summary(baseline)
    test_summary = sample_summary(test)
    base_projection = baseline_summary["projection"]
    test_projection = test_summary["projection"]
    base_bbox = base_projection.get("screen_bbox")
    test_bbox = test_projection.get("screen_bbox")
    base_center = base_projection.get("screen_center")
    test_center = test_projection.get("screen_center")
    base_world_center = baseline_summary.get("world_center")
    test_world_center = test_summary.get("world_center")
    return {
        "baseline": baseline_summary,
        "test": test_summary,
        "identity_match": sample_identity(baseline) == sample_identity(test),
        "impact_type_match": normalize_int(baseline.get("impact")) == normalize_int(test.get("impact")),
        "index_match": normalize_int(baseline.get("index")) == normalize_int(test.get("index")),
        "room_match": normalize_int(baseline.get("room")) == normalize_int(test.get("room")),
        "model_pos_match": normalize_int(baseline.get("model_pos")) == normalize_int(test.get("model_pos")),
        "world_center_delta": distance(base_world_center, test_world_center),
        "projection_center_delta": distance(base_center, test_center),
        "projection_bbox_delta": (
            [test_bbox[index] - base_bbox[index] for index in range(4)]
            if base_bbox is not None and test_bbox is not None
            else None
        ),
        "projection_area_delta": (
            float(test_projection["screen_area"]) - float(base_projection["screen_area"])
            if isinstance(base_projection.get("screen_area"), (int, float))
            and isinstance(test_projection.get("screen_area"), (int, float))
            else None
        ),
    }


def compare(args: argparse.Namespace) -> tuple[dict[str, Any], int]:
    failures: list[str] = []
    try:
        baseline_record = load_frame(args.baseline_trace, args.baseline_frame)
    except (OSError, ValueError) as exc:
        baseline_record = {}
        failures.append(str(exc))
    try:
        test_record = load_frame(args.test_trace, args.test_frame)
    except (OSError, ValueError) as exc:
        test_record = {}
        failures.append(str(exc))

    baseline_state, baseline_samples = extract_samples(baseline_record, args.scope)
    test_state, test_samples = extract_samples(test_record, args.scope)

    mismatches: list[dict[str, Any]] = []
    field_sequences: dict[str, dict[str, Any]] = {}
    for field in ("index", "impact", "room", "prop", "prop_pad", "model_pos", "world"):
        baseline_sequence = sequence_of(baseline_samples, field)
        test_sequence = sequence_of(test_samples, field)
        match = baseline_sequence == test_sequence
        field_sequences[field] = {
            "match": match,
            "baseline": baseline_sequence,
            "test": test_sequence,
        }
        if not match:
            mismatches.append(
                {
                    "kind": f"{field}_sequence_mismatch",
                    "baseline": baseline_sequence,
                    "test": test_sequence,
                }
            )

    if len(baseline_samples) != len(test_samples):
        mismatches.append(
            {
                "kind": "sample_count_mismatch",
                "baseline": len(baseline_samples),
                "test": len(test_samples),
            }
        )

    paired = [
        compare_pair(baseline, test)
        for baseline, test in zip(baseline_samples, test_samples)
    ]
    first_pair = paired[0] if paired else {}
    match = not mismatches
    if args.require_match and not match:
        failures.append(f"{args.scope} bullet-impact sequence mismatch")

    baseline_impact_types = sequence_of(baseline_samples, "impact")
    test_impact_types = sequence_of(test_samples, "impact")
    baseline_type_set = {item for item in baseline_impact_types if item is not None}
    test_type_set = {item for item in test_impact_types if item is not None}
    interpretation: list[str] = []
    if first_pair.get("identity_match") is True and not match:
        interpretation.append(
            "the first sampled impact matches, but later sampled impacts diverge; first-impact visual gates are insufficient"
        )
    if baseline_impact_types != test_impact_types:
        interpretation.append(
            f"{args.scope} impact type sequence differs: baseline {baseline_impact_types} vs test {test_impact_types}"
        )
    test_only = sorted(test_type_set - baseline_type_set)
    baseline_only = sorted(baseline_type_set - test_type_set)
    if test_only:
        interpretation.append(f"test-only {args.scope} impact types: {test_only}")
    if baseline_only:
        interpretation.append(f"baseline-only {args.scope} impact types: {baseline_only}")
    if not interpretation and match:
        interpretation.append(f"{args.scope} bullet-impact sequence matches at the selected frames")

    result = {
        "status": "fail" if failures else "pass",
        "failures": failures,
        "match": match,
        "scope": args.scope,
        "require_match": args.require_match,
        "baseline": {
            "label": args.baseline_label,
            "trace": str(args.baseline_trace),
            "frame": args.baseline_frame,
            "impact_state": {
                "present": baseline_state.get("present"),
                "occupied": baseline_state.get("occupied"),
                "current_slot": baseline_state.get("current_slot"),
                "buffer_len": baseline_state.get("buffer_len"),
                "hash": baseline_state.get("hash"),
                "projection": baseline_state.get("projection"),
            },
            "samples": [sample_summary(item) for item in baseline_samples],
            "impact_type_counts": counter_dict(baseline_impact_types),
        },
        "test": {
            "label": args.test_label,
            "trace": str(args.test_trace),
            "frame": args.test_frame,
            "impact_state": {
                "present": test_state.get("present"),
                "occupied": test_state.get("occupied"),
                "current_slot": test_state.get("current_slot"),
                "buffer_len": test_state.get("buffer_len"),
                "hash": test_state.get("hash"),
                "projection": test_state.get("projection"),
            },
            "samples": [sample_summary(item) for item in test_samples],
            "impact_type_counts": counter_dict(test_impact_types),
        },
        "sequence": field_sequences,
        "mismatches": mismatches,
        "paired_samples": paired,
        "first_pair_identity_match": bool(first_pair.get("identity_match")) if first_pair else False,
        "interpretation": interpretation,
    }
    return result, 1 if failures else 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline-frame", type=int, required=True)
    parser.add_argument("--test-frame", type=int, required=True)
    parser.add_argument("--baseline-label", default="baseline")
    parser.add_argument("--test-label", default="test")
    parser.add_argument("--scope", choices=("world", "prop", "all"), default="world")
    parser.add_argument("--require-match", action="store_true")
    parser.add_argument("--json-out", type=Path)
    parser.add_argument("baseline_trace", type=Path)
    parser.add_argument("test_trace", type=Path)
    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    for path in (args.baseline_trace, args.test_trace):
        if not path.exists():
            parser.error(f"trace not found: {path}")
    result, code = compare(args)
    text = json.dumps(result, indent=2, sort_keys=True)
    if args.json_out:
        args.json_out.parent.mkdir(parents=True, exist_ok=True)
        args.json_out.write_text(text + "\n", encoding="utf-8")
    print(text)
    return code


if __name__ == "__main__":
    sys.exit(main())
