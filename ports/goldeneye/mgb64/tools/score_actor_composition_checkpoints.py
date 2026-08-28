#!/usr/bin/env python3
"""Score stock/native active-glass checkpoints by full actor composition."""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
from typing import Any


DEFAULT_FIELDS = ("alive", "hidden", "onscreen", "rendered", "action")


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


def actor_position(actor: dict[str, Any] | None) -> list[float] | None:
    if not isinstance(actor, dict):
        return None
    pos = actor.get("pos")
    if (
        not isinstance(pos, list)
        or len(pos) != 3
        or any(not isinstance(value, (int, float)) for value in pos)
    ):
        return None
    return [float(pos[0]), float(pos[1]), float(pos[2])]


def distance3(lhs: list[float], rhs: list[float]) -> float:
    return math.sqrt(sum((a - b) * (a - b) for a, b in zip(lhs, rhs)))


def actor_key(actor: dict[str, Any]) -> int | None:
    return as_int(actor.get("chrnum"))


def actors_by_chr(record: dict[str, Any]) -> dict[int, dict[str, Any]]:
    sample = record.get("actors", {}).get("sample", [])
    if not isinstance(sample, list):
        return {}
    actors: dict[int, dict[str, Any]] = {}
    for actor in sample:
        if not isinstance(actor, dict):
            continue
        chrnum = actor_key(actor)
        if chrnum is None:
            continue
        actors[chrnum] = actor
    return actors


def actor_is_visible(actor: dict[str, Any]) -> bool:
    return bool(as_int(actor.get("onscreen")) or as_int(actor.get("rendered")))


def actor_summary(actor: dict[str, Any]) -> dict[str, Any]:
    return {
        "slot": as_int(actor.get("slot")),
        "chrnum": as_int(actor.get("chrnum")),
        "action": as_int(actor.get("action")),
        "alert": as_int(actor.get("alert")),
        "sleep": as_int(actor.get("sleep")),
        "hidden": as_int(actor.get("hidden")),
        "hidden_bits": as_int(actor.get("hidden_bits")),
        "alive": as_int(actor.get("alive")),
        "onscreen": as_int(actor.get("onscreen")),
        "rendered": as_int(actor.get("rendered")),
        "room": as_int(actor.get("room")),
        "dist": as_float(actor.get("dist")),
        "pos": actor_position(actor),
    }


def glass_summary(record: dict[str, Any]) -> dict[str, Any]:
    glass = record.get("glass")
    if not isinstance(glass, dict):
        return {"active": 0, "first_timer": None, "first_rot_y": None}
    first = glass.get("first")
    if not isinstance(first, dict):
        first = {}
    timer = as_int(first.get("timer"))
    if timer is None:
        timer = as_int(first.get("piece"))
    rot_y = as_float(first.get("rot_y"))
    if rot_y is None:
        rot_y = as_float(first.get("age"))
    return {
        "active": as_int(glass.get("active")) or 0,
        "first_timer": timer,
        "first_rot_y": rot_y,
    }


def health_summary(record: dict[str, Any]) -> dict[str, Any]:
    health = record.get("combat", {}).get("health")
    if not isinstance(health, dict):
        health = {}
    return {
        "bond": as_float(health.get("bond")),
        "armor": as_float(health.get("armor")),
        "damage_show": as_int(health.get("damage_show")),
        "health_show": as_int(health.get("health_show")),
    }


def checkpoint(record: dict[str, Any]) -> dict[str, Any] | None:
    if record.get("p") != 1:
        return None
    actors = actors_by_chr(record)
    visible = sorted(chrnum for chrnum, actor in actors.items() if actor_is_visible(actor))
    move = record.get("move")
    if not isinstance(move, dict):
        move = {}
    return {
        "frame": as_int(record.get("f")),
        "global": as_int(move.get("global")),
        "clock": as_int(move.get("clock")),
        "dt": as_float(move.get("dt")),
        "glass": glass_summary(record),
        "health": health_summary(record),
        "actors": actors,
        "visible": visible,
    }


def checkpoint_records(records: list[dict[str, Any]], require_active: bool) -> list[dict[str, Any]]:
    checkpoints: list[dict[str, Any]] = []
    for record in records:
        candidate = checkpoint(record)
        if candidate is None:
            continue
        if require_active and candidate["glass"]["active"] <= 0:
            continue
        checkpoints.append(candidate)
    return checkpoints


def abs_delta(lhs: int | float | None, rhs: int | float | None) -> float | None:
    if lhs is None or rhs is None:
        return None
    return abs(float(rhs) - float(lhs))


def compare_pair(
    baseline: dict[str, Any],
    test: dict[str, Any],
    *,
    fields: list[str],
    required_chrs: list[int],
    position_tolerance: float,
    weights: dict[str, float],
) -> dict[str, Any]:
    baseline_visible = set(baseline["visible"])
    test_visible = set(test["visible"])
    visible_only_baseline = sorted(baseline_visible - test_visible)
    visible_only_test = sorted(test_visible - baseline_visible)
    visible_union = baseline_visible | test_visible
    compare_chrs = sorted(visible_union | set(required_chrs))

    missing: list[dict[str, Any]] = []
    field_mismatches: list[dict[str, Any]] = []
    position_deltas: list[dict[str, Any]] = []
    position_failures: list[dict[str, Any]] = []

    for chrnum in compare_chrs:
        baseline_actor = baseline["actors"].get(chrnum)
        test_actor = test["actors"].get(chrnum)
        if baseline_actor is None or test_actor is None:
            missing.append(
                {
                    "chrnum": chrnum,
                    "baseline_present": baseline_actor is not None,
                    "test_present": test_actor is not None,
                }
            )
            continue

        for field in fields:
            baseline_value = baseline_actor.get(field)
            test_value = test_actor.get(field)
            if baseline_value != test_value:
                field_mismatches.append(
                    {
                        "chrnum": chrnum,
                        "field": field,
                        "baseline": baseline_value,
                        "test": test_value,
                    }
                )

        baseline_pos = actor_position(baseline_actor)
        test_pos = actor_position(test_actor)
        if baseline_pos is not None and test_pos is not None:
            delta = distance3(baseline_pos, test_pos)
            entry = {
                "chrnum": chrnum,
                "delta": delta,
                "baseline": baseline_pos,
                "test": test_pos,
            }
            position_deltas.append(entry)
            if delta > position_tolerance:
                position_failures.append(entry)

    timer_delta = abs_delta(baseline["glass"]["first_timer"], test["glass"]["first_timer"])
    health_delta = abs_delta(baseline["health"]["bond"], test["health"]["bond"])
    damage_show_delta = abs_delta(baseline["health"]["damage_show"], test["health"]["damage_show"])
    health_show_delta = abs_delta(baseline["health"]["health_show"], test["health"]["health_show"])
    hud_delta = (damage_show_delta or 0.0) + (health_show_delta or 0.0)
    active_delta = abs((test["glass"]["active"] or 0) - (baseline["glass"]["active"] or 0))
    max_position_delta = max((item["delta"] for item in position_deltas), default=0.0)
    total_position_delta = sum(item["delta"] for item in position_deltas)
    visible_set_delta = len(visible_only_baseline) + len(visible_only_test)

    score = (
        visible_set_delta * weights["visible_set"]
        + len(missing) * weights["missing"]
        + len(field_mismatches) * weights["field"]
        + len(position_failures) * weights["position_failure"]
        + total_position_delta * weights["position"]
        + (timer_delta if timer_delta is not None else 1000.0) * weights["timer"]
        + (health_delta if health_delta is not None else 10.0) * weights["health"]
        + hud_delta * weights["hud"]
        + active_delta * weights["active"]
    )

    return {
        "score": score,
        "visible_set_delta": visible_set_delta,
        "visible_only_baseline": visible_only_baseline,
        "visible_only_test": visible_only_test,
        "missing": missing,
        "field_mismatches": field_mismatches,
        "position_deltas": position_deltas,
        "position_failures": position_failures,
        "max_position_delta": max_position_delta,
        "total_position_delta": total_position_delta,
        "timer_delta": timer_delta,
        "health_delta": health_delta,
        "damage_show_delta": damage_show_delta,
        "health_show_delta": health_show_delta,
        "active_delta": active_delta,
        "baseline": baseline,
        "test": test,
    }


def summarize_checkpoint(checkpoint_: dict[str, Any]) -> dict[str, Any]:
    actor_chrs = sorted(checkpoint_["actors"])
    return {
        "frame": checkpoint_["frame"],
        "global": checkpoint_["global"],
        "clock": checkpoint_["clock"],
        "dt": checkpoint_["dt"],
        "glass": checkpoint_["glass"],
        "health": checkpoint_["health"],
        "visible": checkpoint_["visible"],
        "actors": {
            str(chrnum): actor_summary(checkpoint_["actors"][chrnum])
            for chrnum in actor_chrs
        },
    }


def summarize_pair(pair: dict[str, Any]) -> dict[str, Any]:
    return {
        "score": pair["score"],
        "visible_set_delta": pair["visible_set_delta"],
        "visible_only_baseline": pair["visible_only_baseline"],
        "visible_only_test": pair["visible_only_test"],
        "missing": pair["missing"],
        "field_mismatches": pair["field_mismatches"],
        "position_deltas": pair["position_deltas"],
        "position_failures": pair["position_failures"],
        "max_position_delta": pair["max_position_delta"],
        "total_position_delta": pair["total_position_delta"],
        "timer_delta": pair["timer_delta"],
        "health_delta": pair["health_delta"],
        "damage_show_delta": pair["damage_show_delta"],
        "health_show_delta": pair["health_show_delta"],
        "active_delta": pair["active_delta"],
        "baseline": summarize_checkpoint(pair["baseline"]),
        "test": summarize_checkpoint(pair["test"]),
    }


def print_pair(label: str, pair: dict[str, Any]) -> None:
    baseline = pair["baseline"]
    test = pair["test"]
    print(
        f"{label}: score={pair['score']:.3f} visible_delta={pair['visible_set_delta']} "
        f"field_mismatches={len(pair['field_mismatches'])} "
        f"missing={len(pair['missing'])} max_pos={pair['max_position_delta']:.3f} "
        f"timer_delta={pair['timer_delta']} health_delta={pair['health_delta']} "
        f"hud_delta={(pair['damage_show_delta'] or 0) + (pair['health_show_delta'] or 0)} "
        f"active_delta={pair['active_delta']}"
    )
    print(
        "  baseline: "
        f"frame={baseline['frame']} global={baseline['global']} "
        f"timer={baseline['glass']['first_timer']} active={baseline['glass']['active']} "
        f"visible={baseline['visible']}"
    )
    print(
        "  test:     "
        f"frame={test['frame']} global={test['global']} "
        f"timer={test['glass']['first_timer']} active={test['glass']['active']} "
        f"visible={test['visible']}"
    )
    if pair["visible_only_baseline"] or pair["visible_only_test"]:
        print(
            "  visible_diff: "
            f"baseline_only={pair['visible_only_baseline']} test_only={pair['visible_only_test']}"
        )
    if pair["field_mismatches"]:
        preview = pair["field_mismatches"][:6]
        print("  field_mismatches: " + json.dumps(preview, sort_keys=True))
    if pair["position_failures"]:
        preview = pair["position_failures"][:6]
        print("  position_failures: " + json.dumps(preview, sort_keys=True))


def parse_fields(value: str) -> list[str]:
    fields = [item.strip() for item in value.split(",") if item.strip()]
    if not fields:
        raise argparse.ArgumentTypeError("field list must not be empty")
    return fields


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("baseline", type=Path, help="baseline JSONL trace, usually stock ares")
    parser.add_argument("test", type=Path, help="test JSONL trace, usually native")
    parser.add_argument("--top", type=int, default=20)
    parser.add_argument("--json-out", type=Path)
    parser.add_argument("--require-active", action="store_true")
    parser.add_argument("--actor-chrnum", type=int, action="append", default=[])
    parser.add_argument("--fields", type=parse_fields, default=list(DEFAULT_FIELDS))
    parser.add_argument("--health-tolerance", type=float, default=0.001)
    parser.add_argument("--timer-tolerance", type=float, default=0.0)
    parser.add_argument("--position-tolerance", type=float, default=25.0)
    parser.add_argument("--weight-visible-set", type=float, default=500.0)
    parser.add_argument("--weight-missing", type=float, default=1000.0)
    parser.add_argument("--weight-field", type=float, default=100.0)
    parser.add_argument("--weight-position-failure", type=float, default=250.0)
    parser.add_argument("--weight-position", type=float, default=1.0)
    parser.add_argument("--weight-timer", type=float, default=10.0)
    parser.add_argument("--weight-health", type=float, default=1000.0)
    parser.add_argument("--weight-hud", type=float, default=1.0)
    parser.add_argument("--weight-active", type=float, default=10.0)
    args = parser.parse_args()

    if args.top < 1:
        parser.error("--top must be positive")
    for name in (
        "health_tolerance",
        "timer_tolerance",
        "position_tolerance",
        "weight_visible_set",
        "weight_missing",
        "weight_field",
        "weight_position_failure",
        "weight_position",
        "weight_timer",
        "weight_health",
        "weight_hud",
        "weight_active",
    ):
        if getattr(args, name) < 0:
            parser.error(f"--{name.replace('_', '-')} must be non-negative")

    baseline_records = checkpoint_records(load_records(args.baseline), args.require_active)
    test_records = checkpoint_records(load_records(args.test), args.require_active)
    weights = {
        "visible_set": args.weight_visible_set,
        "missing": args.weight_missing,
        "field": args.weight_field,
        "position_failure": args.weight_position_failure,
        "position": args.weight_position,
        "timer": args.weight_timer,
        "health": args.weight_health,
        "hud": args.weight_hud,
        "active": args.weight_active,
    }
    scored = [
        compare_pair(
            baseline,
            test,
            fields=args.fields,
            required_chrs=args.actor_chrnum,
            position_tolerance=args.position_tolerance,
            weights=weights,
        )
        for baseline in baseline_records
        for test in test_records
    ]
    scored.sort(key=lambda pair: pair["score"])

    strict = [
        pair
        for pair in scored
        if (
            pair["health_delta"] is not None
            and pair["health_delta"] <= args.health_tolerance
            and pair["timer_delta"] is not None
            and pair["timer_delta"] <= args.timer_tolerance
            and pair["active_delta"] == 0
            and pair["visible_set_delta"] == 0
            and not pair["missing"]
            and not pair["field_mismatches"]
            and not pair["position_failures"]
        )
    ]

    payload = {
        "baseline": str(args.baseline),
        "test": str(args.test),
        "baseline_candidates": len(baseline_records),
        "test_candidates": len(test_records),
        "pair_count": len(scored),
        "filters": {
            "require_active": args.require_active,
            "actor_chrnums": args.actor_chrnum,
            "fields": args.fields,
            "health_tolerance": args.health_tolerance,
            "timer_tolerance": args.timer_tolerance,
            "position_tolerance": args.position_tolerance,
        },
        "weights": weights,
        "best": [summarize_pair(pair) for pair in scored[: args.top]],
        "best_strict": [summarize_pair(pair) for pair in strict[: args.top]],
    }
    if args.json_out:
        args.json_out.parent.mkdir(parents=True, exist_ok=True)
        args.json_out.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")

    print(
        f"candidates: baseline={len(baseline_records)} test={len(test_records)} "
        f"pairs={len(scored)} require_active={args.require_active}"
    )
    if scored:
        print_pair("best", scored[0])
    if strict:
        print_pair("best_strict", strict[0])
    else:
        print("best_strict: none")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
