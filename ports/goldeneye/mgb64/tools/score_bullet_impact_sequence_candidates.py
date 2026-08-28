#!/usr/bin/env python3
"""Rank test trace frames by sampled bullet-impact sequence parity.

This is a route-cleanup helper for visual fixtures where a single selected
impact matches but later impact samples pollute the screenshot.
"""

from __future__ import annotations

import argparse
import json
import math
import sys
from pathlib import Path
from typing import Any

sys.path.insert(0, str(Path(__file__).resolve().parent))
import compare_bullet_impact_sequence as impact_sequence  # noqa: E402


def load_records(path: Path) -> list[dict[str, Any]]:
    records: list[dict[str, Any]] = []
    with path.open("r", encoding="utf-8", errors="replace") as handle:
        for line_no, raw in enumerate(handle, start=1):
            line = raw.strip()
            if not line:
                continue
            try:
                record = json.loads(line)
            except json.JSONDecodeError as exc:
                raise SystemExit(f"FAIL: {path}:{line_no}: invalid JSONL: {exc}") from exc
            if isinstance(record, dict):
                records.append(record)
    return records


def as_int(value: Any) -> int | None:
    if isinstance(value, bool):
        return int(value)
    if isinstance(value, int):
        return value
    if isinstance(value, float) and math.isfinite(value):
        return int(value)
    return None


def glass_summary(record: dict[str, Any]) -> dict[str, Any]:
    glass = record.get("glass")
    if not isinstance(glass, dict):
        glass = {}
    first = glass.get("first")
    if not isinstance(first, dict):
        first = {}
    timer = as_int(first.get("timer"))
    if timer is None:
        timer = as_int(first.get("piece"))
    return {
        "active": as_int(glass.get("active")) or 0,
        "first_timer": timer,
        "first_rot_y": first.get("rot_y") if isinstance(first.get("rot_y"), (int, float)) else None,
    }


def sequence_counts(samples: list[dict[str, Any]]) -> dict[str, list[Any]]:
    return {
        "index": impact_sequence.sequence_of(samples, "index"),
        "impact": impact_sequence.sequence_of(samples, "impact"),
        "room": impact_sequence.sequence_of(samples, "room"),
        "model_pos": impact_sequence.sequence_of(samples, "model_pos"),
        "prop": impact_sequence.sequence_of(samples, "prop"),
        "prop_pad": impact_sequence.sequence_of(samples, "prop_pad"),
        "world": impact_sequence.sequence_of(samples, "world"),
    }


def sequence_penalty(baseline: list[Any], test: list[Any], *, weight: float) -> float:
    penalty = abs(len(test) - len(baseline)) * weight
    for index in range(min(len(baseline), len(test))):
        if baseline[index] != test[index]:
            penalty += weight
    return penalty


def numeric_penalty(values: list[float | None], *, missing: float, scale: float) -> float:
    total = 0.0
    for value in values:
        if value is None:
            total += missing
        else:
            total += abs(float(value)) * scale
    return total


def score_candidate(
    baseline_record: dict[str, Any],
    baseline_samples: list[dict[str, Any]],
    test_record: dict[str, Any],
    test_samples: list[dict[str, Any]],
    *,
    scope: str,
) -> dict[str, Any]:
    baseline_seq = sequence_counts(baseline_samples)
    test_seq = sequence_counts(test_samples)
    paired = [
        impact_sequence.compare_pair(baseline, test)
        for baseline, test in zip(baseline_samples, test_samples)
    ]
    first_pair = paired[0] if paired else {}
    baseline_glass = glass_summary(baseline_record)
    test_glass = glass_summary(test_record)

    impact_sequence_match = baseline_seq["impact"] == test_seq["impact"]
    full_sequence_match = all(baseline_seq[key] == test_seq[key] for key in baseline_seq)
    active_delta = abs(float(test_glass["active"]) - float(baseline_glass["active"]))
    timer_delta = (
        abs(float(test_glass["first_timer"]) - float(baseline_glass["first_timer"]))
        if test_glass["first_timer"] is not None and baseline_glass["first_timer"] is not None
        else None
    )

    projection_center_deltas = [
        item.get("projection_center_delta")
        for item in paired
        if isinstance(item, dict)
    ]
    world_center_deltas = [
        item.get("world_center_delta")
        for item in paired
        if isinstance(item, dict)
    ]

    score = (
        sequence_penalty(baseline_seq["impact"], test_seq["impact"], weight=10000.0)
        + sequence_penalty(baseline_seq["index"], test_seq["index"], weight=3000.0)
        + sequence_penalty(baseline_seq["room"], test_seq["room"], weight=2000.0)
        + sequence_penalty(baseline_seq["model_pos"], test_seq["model_pos"], weight=1000.0)
        + active_delta * 100.0
        + (timer_delta if timer_delta is not None else 100.0) * 25.0
        + numeric_penalty(world_center_deltas, missing=500.0, scale=2.0)
        + numeric_penalty(projection_center_deltas, missing=250.0, scale=20.0)
    )
    if first_pair.get("identity_match") is not True:
        score += 2500.0
    if not impact_sequence_match:
        score += 5000.0

    strict = (
        full_sequence_match
        and impact_sequence_match
        and first_pair.get("identity_match") is True
        and active_delta == 0.0
        and timer_delta == 0.0
    )

    move = test_record.get("move")
    if not isinstance(move, dict):
        move = {}

    return {
        "score": score,
        "strict": strict,
        "scope": scope,
        "frame": as_int(test_record.get("f")),
        "global": as_int(move.get("global")),
        "glass": test_glass,
        "impact_sequence_match": impact_sequence_match,
        "full_sequence_match": full_sequence_match,
        "first_pair_identity_match": bool(first_pair.get("identity_match")) if first_pair else False,
        "active_delta": active_delta,
        "timer_delta": timer_delta,
        "sequence": {
            key: {
                "baseline": baseline_seq[key],
                "test": test_seq[key],
                "match": baseline_seq[key] == test_seq[key],
            }
            for key in baseline_seq
        },
        "paired_samples": paired,
    }


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline-frame", type=int, required=True)
    parser.add_argument("--scope", choices=("world", "prop", "all"), default="world")
    parser.add_argument("--require-active", action="store_true")
    parser.add_argument("--top", type=int, default=10)
    parser.add_argument("--json-out", type=Path)
    parser.add_argument("baseline_trace", type=Path)
    parser.add_argument("test_trace", type=Path)
    args = parser.parse_args()
    if args.top <= 0:
        parser.error("--top must be positive")
    for path in (args.baseline_trace, args.test_trace):
        if not path.exists():
            parser.error(f"trace not found: {path}")
    return args


def main() -> int:
    args = parse_args()
    try:
        baseline_record = impact_sequence.load_frame(args.baseline_trace, args.baseline_frame)
    except (OSError, ValueError) as exc:
        raise SystemExit(f"FAIL: {exc}") from exc
    _, baseline_samples = impact_sequence.extract_samples(baseline_record, args.scope)
    if not baseline_samples:
        raise SystemExit(f"FAIL: baseline frame {args.baseline_frame} has no {args.scope} impact samples")

    candidates: list[dict[str, Any]] = []
    for record in load_records(args.test_trace):
        if record.get("p") != 1:
            continue
        if args.require_active and glass_summary(record)["active"] <= 0:
            continue
        _, test_samples = impact_sequence.extract_samples(record, args.scope)
        if not test_samples:
            continue
        candidates.append(
            score_candidate(
                baseline_record,
                baseline_samples,
                record,
                test_samples,
                scope=args.scope,
            )
        )

    candidates.sort(key=lambda item: item["score"])
    strict = [item for item in candidates if item["strict"]]
    payload = {
        "status": "pass",
        "baseline_trace": str(args.baseline_trace),
        "test_trace": str(args.test_trace),
        "baseline_frame": args.baseline_frame,
        "scope": args.scope,
        "require_active": args.require_active,
        "candidate_counts": {
            "test": len(candidates),
            "strict": len(strict),
        },
        "baseline": {
            "glass": glass_summary(baseline_record),
            "sequence": sequence_counts(baseline_samples),
            "samples": [impact_sequence.sample_summary(item) for item in baseline_samples],
        },
        "best": candidates[: args.top],
        "best_strict": strict[: args.top],
    }
    if args.json_out:
        args.json_out.parent.mkdir(parents=True, exist_ok=True)
        args.json_out.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")

    print(
        "impact-sequence candidates: test={test} strict={strict}".format(
            **payload["candidate_counts"]
        )
    )
    if candidates:
        best = candidates[0]
        impact_seq = best["sequence"]["impact"]
        print(
            "best: frame={frame} global={global_} score={score:.3f} strict={strict} "
            "impact_match={impact_match} first_pair={first_pair} active={active} timer={timer}".format(
                frame=best["frame"],
                global_=best["global"],
                score=best["score"],
                strict=best["strict"],
                impact_match=best["impact_sequence_match"],
                first_pair=best["first_pair_identity_match"],
                active=best["glass"]["active"],
                timer=best["glass"]["first_timer"],
            )
        )
        print(f"  impact baseline={impact_seq['baseline']} test={impact_seq['test']}")
    else:
        print("best: none")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
