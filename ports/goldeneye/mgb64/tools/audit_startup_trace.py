#!/usr/bin/env python3
"""Audit native boot/title progression in a JSONL state trace."""

from __future__ import annotations

import argparse
import json
import math
from collections import Counter
from pathlib import Path
from typing import Any


def parse_int_list(value: str) -> list[int]:
    items: list[int] = []
    for raw in value.split(","):
        text = raw.strip()
        if not text:
            continue
        try:
            items.append(int(text, 0))
        except ValueError:
            raise argparse.ArgumentTypeError(f"expected integer list, got {value!r}") from None
    return items


def finite_float(value: str) -> float:
    try:
        parsed = float(value)
    except ValueError:
        raise argparse.ArgumentTypeError(f"expected a finite number, got {value!r}") from None
    if not math.isfinite(parsed):
        raise argparse.ArgumentTypeError(f"expected a finite number, got {value!r}")
    return parsed


def load_jsonl(path: Path) -> list[dict[str, Any]]:
    records: list[dict[str, Any]] = []
    with path.open("r", encoding="utf-8", errors="replace") as handle:
        for line_no, line in enumerate(handle, start=1):
            line = line.strip()
            if not line:
                continue
            try:
                value = json.loads(line)
            except json.JSONDecodeError as exc:
                raise SystemExit(f"FAIL: invalid JSON in {path}:{line_no}: {exc}") from None
            if isinstance(value, dict):
                records.append(value)
    return records


def number_values(records: list[dict[str, Any]], section: str, field: str) -> list[float]:
    values: list[float] = []
    for record in records:
        parent = record.get(section)
        if not isinstance(parent, dict):
            continue
        value = parent.get(field)
        if isinstance(value, bool):
            values.append(float(int(value)))
        elif isinstance(value, (int, float)) and math.isfinite(float(value)):
            values.append(float(value))
    return values


def int_values(records: list[dict[str, Any]], section: str, field: str) -> list[int]:
    values: list[int] = []
    for value in number_values(records, section, field):
        if value.is_integer():
            values.append(int(value))
    return values


def value_range(values: list[float]) -> float:
    if not values:
        return 0.0
    return max(values) - min(values)


def ordered_changes(values: list[int]) -> list[int]:
    changes: list[int] = []
    previous: int | None = None
    for value in values:
        if previous is None or value != previous:
            changes.append(value)
            previous = value
    return changes


def has_ordered_subsequence(sequence: list[int], required: list[int]) -> bool:
    if not required:
        return True
    index = 0
    for value in sequence:
        if value == required[index]:
            index += 1
            if index == len(required):
                return True
    return False


def counter_dict(counter: Counter[int]) -> dict[str, int]:
    return {str(key): counter[key] for key in sorted(counter)}


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace", type=Path, help="native --trace-state JSONL path")
    parser.add_argument("--label", default="startup trace", help="label used in output")
    parser.add_argument("--min-records", type=int, default=1500)
    parser.add_argument(
        "--require-gunbarrel-modes",
        type=parse_int_list,
        default=parse_int_list("0,2,3,4,5,6,7,8,9"),
        help="comma-separated mode subsequence required in order",
    )
    parser.add_argument(
        "--require-front-menus",
        type=parse_int_list,
        default=parse_int_list("0,1,2,3"),
        help="comma-separated front menu ids that must be observed",
    )
    parser.add_argument(
        "--require-blood-state",
        type=int,
        default=1,
        help="blood_state value that must be observed; negative disables",
    )
    parser.add_argument("--min-eye-counter-max", type=finite_float, default=250.0)
    parser.add_argument("--min-rare-rotation-range", type=finite_float, default=500.0)
    parser.add_argument("--min-nintendo-rotation-range", type=finite_float, default=8.0)
    parser.add_argument("--min-nintendo-scale-max", type=finite_float, default=1.0)
    parser.add_argument("--max-nan-records", type=int, default=0)
    parser.add_argument("--json-out", type=Path, help="write machine-readable summary")
    args = parser.parse_args(argv)

    records = load_jsonl(args.trace)
    title_records = [record for record in records if isinstance(record.get("title"), dict)]
    modes = int_values(title_records, "title", "gunbarrel_mode")
    eye_counter = number_values(title_records, "title", "eye_counter")
    blood_states = int_values(title_records, "title", "blood_state")
    rare_rotation = number_values(title_records, "title", "rare_rotation")
    nintendo_rotation = number_values(title_records, "title", "nintendo_rotation")
    nintendo_scale = number_values(title_records, "title", "nintendo_scale")
    front_menus = int_values(records, "front", "menu")
    nan_records = sum(1 for record in records if int(record.get("nan") or 0) != 0)

    mode_changes = ordered_changes(modes)
    mode_counts = Counter(modes)
    menu_counts = Counter(front_menus)

    metrics: dict[str, Any] = {
        "trace": str(args.trace),
        "records": len(records),
        "title_records": len(title_records),
        "first_frame": records[0].get("f") if records else None,
        "last_frame": records[-1].get("f") if records else None,
        "gunbarrel_modes": counter_dict(mode_counts),
        "gunbarrel_mode_changes": mode_changes,
        "front_menus": counter_dict(menu_counts),
        "eye_counter": {
            "min": min(eye_counter) if eye_counter else None,
            "max": max(eye_counter) if eye_counter else None,
            "unique": len(set(eye_counter)),
        },
        "blood_states": counter_dict(Counter(blood_states)),
        "rare_rotation_range": value_range(rare_rotation),
        "nintendo_rotation_range": value_range(nintendo_rotation),
        "nintendo_scale_max": max(nintendo_scale) if nintendo_scale else None,
        "nan_records": nan_records,
    }

    failures: list[str] = []
    if len(records) < args.min_records:
        failures.append(f"records {len(records)} < required {args.min_records}")
    if len(title_records) < args.min_records:
        failures.append(f"title records {len(title_records)} < required {args.min_records}")
    if not has_ordered_subsequence(mode_changes, args.require_gunbarrel_modes):
        failures.append(
            "gunbarrel modes do not contain required ordered subsequence "
            f"{args.require_gunbarrel_modes}; observed changes {mode_changes}"
        )
    for menu in args.require_front_menus:
        if menu not in menu_counts:
            failures.append(f"front menu {menu} was not observed")
    if args.require_blood_state >= 0 and args.require_blood_state not in blood_states:
        failures.append(f"blood_state {args.require_blood_state} was not observed")
    if not eye_counter or max(eye_counter) < args.min_eye_counter_max:
        failures.append(
            f"eye_counter max {max(eye_counter) if eye_counter else 'missing'} "
            f"< required {args.min_eye_counter_max:g}"
        )
    if value_range(rare_rotation) < args.min_rare_rotation_range:
        failures.append(
            f"rare_rotation range {value_range(rare_rotation):.2f} "
            f"< required {args.min_rare_rotation_range:.2f}"
        )
    if value_range(nintendo_rotation) < args.min_nintendo_rotation_range:
        failures.append(
            f"nintendo_rotation range {value_range(nintendo_rotation):.4f} "
            f"< required {args.min_nintendo_rotation_range:.4f}"
        )
    if not nintendo_scale or max(nintendo_scale) < args.min_nintendo_scale_max:
        failures.append(
            f"nintendo_scale max {max(nintendo_scale) if nintendo_scale else 'missing'} "
            f"< required {args.min_nintendo_scale_max:g}"
        )
    if nan_records > args.max_nan_records:
        failures.append(f"nan records {nan_records} > allowed {args.max_nan_records}")

    payload = {
        "status": "fail" if failures else "pass",
        "label": args.label,
        "failures": failures,
        "metrics": metrics,
    }

    print(f"=== {args.label} ===")
    print(
        "records=%d title_records=%d frames=%s..%s"
        % (
            metrics["records"],
            metrics["title_records"],
            metrics["first_frame"],
            metrics["last_frame"],
        )
    )
    print(f"gunbarrel changes={mode_changes}")
    print(f"front menus={dict(sorted(menu_counts.items()))}")
    print(
        "rare_range=%.2f nintendo_range=%.4f nintendo_scale_max=%s eye_max=%s"
        % (
            metrics["rare_rotation_range"],
            metrics["nintendo_rotation_range"],
            metrics["nintendo_scale_max"],
            metrics["eye_counter"]["max"],
        )
    )

    if args.json_out:
        args.json_out.parent.mkdir(parents=True, exist_ok=True)
        args.json_out.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")

    if failures:
        print("FAIL:")
        for failure in failures:
            print(f"  - {failure}")
        return 1

    print("PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
