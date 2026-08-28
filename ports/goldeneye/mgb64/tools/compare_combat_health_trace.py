#!/usr/bin/env python3
"""Compare combat health/HUD state at stock/native visual checkpoints."""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
from typing import Any


def load_records(path: Path) -> list[dict[str, Any]]:
    records: list[dict[str, Any]] = []
    with path.open("r", encoding="utf-8", errors="replace") as handle:
        for line_no, raw in enumerate(handle, 1):
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


def health(record: dict[str, Any]) -> dict[str, Any] | None:
    value = record.get("combat", {}).get("health")
    return value if isinstance(value, dict) else None


def valid_health_records(records: list[dict[str, Any]]) -> list[dict[str, Any]]:
    return [record for record in records if record.get("p") == 1 and health(record) is not None]


def as_int(value: Any) -> int | None:
    if isinstance(value, bool):
        return int(value)
    if isinstance(value, int):
        return value
    if isinstance(value, float) and math.isfinite(value):
        return int(value)
    return None


def as_float(value: Any) -> float | None:
    if isinstance(value, (int, float)) and not isinstance(value, bool):
        value = float(value)
        return value if math.isfinite(value) else None
    return None


def move_global(record: dict[str, Any]) -> int | None:
    return as_int(record.get("move", {}).get("global"))


def move_clock(record: dict[str, Any]) -> int | None:
    return as_int(record.get("move", {}).get("clock"))


def move_dt(record: dict[str, Any]) -> float | None:
    return as_float(record.get("move", {}).get("dt"))


def gameplay_frame(record: dict[str, Any]) -> int | None:
    return as_int(record.get("oracle", {}).get("gameplay_frame"))


def record_frame(record: dict[str, Any]) -> int:
    return int(record.get("f", 0) or 0)


def select_checkpoint(
    records: list[dict[str, Any]], *, frame: int | None, global_timer: int | None
) -> dict[str, Any] | None:
    candidates = valid_health_records(records)
    if not candidates:
        return None

    if global_timer is not None:
        before = [record for record in candidates if (move_global(record) is not None and move_global(record) <= global_timer)]
        if before:
            return max(before, key=lambda record: (move_global(record) or -1, record_frame(record)))
        return min(candidates, key=lambda record: abs((move_global(record) or 0) - global_timer))

    if frame is not None:
        before = [record for record in candidates if record_frame(record) <= frame]
        if before:
            return max(before, key=record_frame)
        return min(candidates, key=lambda record: abs(record_frame(record) - frame))

    return candidates[-1]


def first_glass_active(records: list[dict[str, Any]]) -> dict[str, Any] | None:
    for record in valid_health_records(records):
        glass = record.get("glass")
        if isinstance(glass, dict) and as_int(glass.get("active")) and as_int(glass.get("active")) > 0:
            return record
    return None


def first_health_drop(records: list[dict[str, Any]]) -> dict[str, Any] | None:
    previous: float | None = None
    for record in valid_health_records(records):
        current = as_float((health(record) or {}).get("bond"))
        if current is None:
            continue
        if previous is not None and current < previous - 0.0001:
            return record
        previous = current
    return None


def summarize_record(record: dict[str, Any] | None) -> dict[str, Any] | None:
    if record is None:
        return None
    state = health(record) or {}
    glass = record.get("glass")
    first_glass = glass.get("first") if isinstance(glass, dict) else None
    glass_summary = None
    if isinstance(glass, dict):
        timer = first_glass.get("timer") if isinstance(first_glass, dict) else None
        piece = first_glass.get("piece") if isinstance(first_glass, dict) else None
        rot_y = first_glass.get("rot_y") if isinstance(first_glass, dict) else None
        if timer is None:
            timer = piece
        if rot_y is None and isinstance(first_glass, dict):
            rot_y = first_glass.get("age")
        glass_summary = {
            "active": as_int(glass.get("active")),
            "first_piece": as_int(piece),
            "first_timer": as_int(timer),
            "first_rot_y": as_float(rot_y),
        }
    return {
        "frame": record_frame(record),
        "global": move_global(record),
        "clock": move_clock(record),
        "dt": move_dt(record),
        "gameplay_frame": gameplay_frame(record),
        "pos": record.get("pos"),
        "glass": glass_summary,
        "health": {
            "bond": as_float(state.get("bond")),
            "armor": as_float(state.get("armor")),
            "actual_h": as_float(state.get("actual_h")),
            "actual_a": as_float(state.get("actual_a")),
            "damage_show": as_int(state.get("damage_show")),
            "health_show": as_int(state.get("health_show")),
            "damage_type": as_int(state.get("damage_type")),
            "health_type": as_int(state.get("health_type")),
            "fade_rgba": state.get("fade_rgba"),
        },
    }


def active_offset(event: dict[str, Any] | None, origin: dict[str, Any] | None) -> int | None:
    if event is None or origin is None:
        return None
    return record_frame(event) - record_frame(origin)


def global_offset(event: dict[str, Any] | None, origin: dict[str, Any] | None) -> int | None:
    if event is None or origin is None:
        return None
    event_global = move_global(event)
    origin_global = move_global(origin)
    if event_global is None or origin_global is None:
        return None
    return event_global - origin_global


def compare_health(
    baseline: dict[str, Any] | None,
    test: dict[str, Any] | None,
    *,
    health_tolerance: float,
    damage_show_tolerance: int,
) -> tuple[dict[str, Any], list[str]]:
    warnings: list[str] = []
    result: dict[str, Any] = {"health_delta": None, "damage_show_delta": None, "health_show_delta": None}
    if baseline is None or test is None:
        warnings.append("missing checkpoint health data")
        return result, warnings

    base_health = health(baseline) or {}
    test_health = health(test) or {}
    base_bond = as_float(base_health.get("bond"))
    test_bond = as_float(test_health.get("bond"))
    if base_bond is not None and test_bond is not None:
        delta = test_bond - base_bond
        result["health_delta"] = delta
        if abs(delta) > health_tolerance:
            warnings.append(f"bond health differs by {delta:+.4f}")

    for key, label in (("damage_show", "damage-show timer"), ("health_show", "health-show timer")):
        base_value = as_int(base_health.get(key))
        test_value = as_int(test_health.get(key))
        if base_value is None or test_value is None:
            continue
        delta = test_value - base_value
        result[f"{key}_delta"] = delta
        if abs(delta) > damage_show_tolerance:
            warnings.append(f"{label} differs by {delta:+d} frame(s)")

    return result, warnings


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("baseline", type=Path)
    parser.add_argument("test", type=Path)
    parser.add_argument("--baseline-label", default="baseline")
    parser.add_argument("--test-label", default="test")
    parser.add_argument("--baseline-frame", type=int)
    parser.add_argument("--test-frame", type=int)
    parser.add_argument("--baseline-global", type=int)
    parser.add_argument("--test-global", type=int)
    parser.add_argument("--health-tolerance", type=float, default=0.001)
    parser.add_argument("--damage-show-tolerance", type=int, default=4)
    parser.add_argument("--json-out", type=Path)
    parser.add_argument("--require-match", action="store_true")
    args = parser.parse_args()

    baseline_records = load_records(args.baseline)
    test_records = load_records(args.test)
    baseline_checkpoint = select_checkpoint(
        baseline_records, frame=args.baseline_frame, global_timer=args.baseline_global
    )
    test_checkpoint = select_checkpoint(test_records, frame=args.test_frame, global_timer=args.test_global)
    baseline_glass = first_glass_active(baseline_records)
    test_glass = first_glass_active(test_records)
    baseline_damage = first_health_drop(baseline_records)
    test_damage = first_health_drop(test_records)

    comparison, warnings = compare_health(
        baseline_checkpoint,
        test_checkpoint,
        health_tolerance=args.health_tolerance,
        damage_show_tolerance=args.damage_show_tolerance,
    )

    baseline_summary = summarize_record(baseline_checkpoint)
    test_summary = summarize_record(test_checkpoint)
    status = "pass"
    if warnings:
        status = "fail" if args.require_match else "warn"

    payload = {
        "status": status,
        "baseline_label": args.baseline_label,
        "test_label": args.test_label,
        "baseline_checkpoint": baseline_summary,
        "test_checkpoint": test_summary,
        "baseline_first_glass_active": summarize_record(baseline_glass),
        "test_first_glass_active": summarize_record(test_glass),
        "baseline_first_health_drop": summarize_record(baseline_damage),
        "test_first_health_drop": summarize_record(test_damage),
        "baseline_damage_after_glass_frames": active_offset(baseline_damage, baseline_glass),
        "test_damage_after_glass_frames": active_offset(test_damage, test_glass),
        "baseline_damage_after_glass_global_delta": global_offset(baseline_damage, baseline_glass),
        "test_damage_after_glass_global_delta": global_offset(test_damage, test_glass),
        "comparison": comparison,
        "warnings": warnings,
    }

    if args.json_out:
        args.json_out.parent.mkdir(parents=True, exist_ok=True)
        with args.json_out.open("w", encoding="utf-8") as handle:
            json.dump(payload, handle, indent=2, sort_keys=True)
            handle.write("\n")

    print(f"=== combat health checkpoint: {args.baseline_label} vs {args.test_label} ===")
    for label, summary in ((args.baseline_label, baseline_summary), (args.test_label, test_summary)):
        if summary is None:
            print(f"  {label}: missing")
            continue
        state = summary["health"]
        dt_text = "unknown" if summary.get("dt") is None else f"{summary['dt']:.3f}"
        print(
            f"  {label}: frame={summary['frame']} global={summary['global']} "
            f"clock={summary['clock']} dt={dt_text} "
            f"bond={state['bond']:.4f} armor={state['armor']:.4f} "
            f"damage_show={state['damage_show']} health_show={state['health_show']}"
        )
        glass = summary.get("glass")
        if isinstance(glass, dict) and glass.get("active"):
            rot_y = glass.get("first_rot_y")
            rot_y_text = "unknown" if rot_y is None else f"{rot_y:.2f}"
            print(
                f"    glass: active={glass['active']} "
                f"first_timer={glass['first_timer']} rot_y={rot_y_text}"
            )
    if baseline_damage is not None or test_damage is not None:
        baseline_frames = payload["baseline_damage_after_glass_frames"]
        test_frames = payload["test_damage_after_glass_frames"]
        baseline_global_delta = payload["baseline_damage_after_glass_global_delta"]
        test_global_delta = payload["test_damage_after_glass_global_delta"]
        print(
            "  first health drop after glass: "
            f"{args.baseline_label}={baseline_frames} frame(s), "
            f"{baseline_global_delta} global tick(s); "
            f"{args.test_label}={test_frames} frame(s), "
            f"{test_global_delta} global tick(s)"
        )
    if warnings:
        print("WARN: combat health phase differs at checkpoint")
        for warning in warnings:
            print(f"  {warning}")
        return 1 if args.require_match else 0

    print("PASS: combat health phase matches configured tolerances")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
