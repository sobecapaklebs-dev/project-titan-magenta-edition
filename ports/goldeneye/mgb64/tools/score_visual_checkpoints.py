#!/usr/bin/env python3
"""Score stock/native visual checkpoint pairs from route JSONL traces."""

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


def as_float(value: Any) -> float | None:
    if isinstance(value, (int, float)) and not isinstance(value, bool):
        value = float(value)
        return value if math.isfinite(value) else None
    return None


def as_int(value: Any) -> int | None:
    if isinstance(value, bool):
        return int(value)
    if isinstance(value, int):
        return value
    if isinstance(value, float) and math.isfinite(value):
        return int(value)
    return None


def distance3(a: list[float], b: list[float]) -> float:
    return math.sqrt(sum((lhs - rhs) * (lhs - rhs) for lhs, rhs in zip(a, b)))


def xz_distance(a: list[float], b: list[float]) -> float:
    return math.sqrt((a[0] - b[0]) * (a[0] - b[0]) + (a[2] - b[2]) * (a[2] - b[2]))


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


def actor_for_chr(record: dict[str, Any], chrnum: int) -> dict[str, Any] | None:
    sample = record.get("actors", {}).get("sample", [])
    if not isinstance(sample, list):
        return None
    for actor in sample:
        if not isinstance(actor, dict):
            continue
        if as_int(actor.get("chrnum")) == chrnum:
            return actor
    return None


def visible_actor_sample(record: dict[str, Any]) -> list[dict[str, Any]]:
    sample = record.get("actors", {}).get("sample", [])
    if not isinstance(sample, list):
        return []
    actors: list[dict[str, Any]] = []
    for actor in sample:
        if not isinstance(actor, dict):
            continue
        if as_int(actor.get("onscreen")) or as_int(actor.get("rendered")):
            actors.append(
                {
                    "chrnum": as_int(actor.get("chrnum")),
                    "action": as_int(actor.get("action")),
                    "onscreen": as_int(actor.get("onscreen")),
                    "rendered": as_int(actor.get("rendered")),
                    "pos": actor_position(actor),
                }
            )
    return actors


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


def checkpoint(record: dict[str, Any], actor_chrnum: int) -> dict[str, Any] | None:
    if record.get("p") != 1:
        return None
    actor = actor_for_chr(record, actor_chrnum)
    actor_pos = actor_position(actor)
    glass = glass_summary(record)
    health = health_summary(record)
    if actor is None or actor_pos is None:
        return None
    return {
        "frame": as_int(record.get("f")),
        "global": as_int(record.get("move", {}).get("global")),
        "clock": as_int(record.get("move", {}).get("clock")),
        "dt": as_float(record.get("move", {}).get("dt")),
        "glass": glass,
        "health": health,
        "actor": actor,
        "visible_actors": visible_actor_sample(record),
    }


def checkpoint_records(records: list[dict[str, Any]], actor_chrnum: int) -> list[dict[str, Any]]:
    checkpoints: list[dict[str, Any]] = []
    for record in records:
        candidate = checkpoint(record, actor_chrnum)
        if candidate is not None:
            checkpoints.append(candidate)
    return checkpoints


def numeric_delta(lhs: float | int | None, rhs: float | int | None) -> float | None:
    if lhs is None or rhs is None:
        return None
    return float(rhs) - float(lhs)


def abs_delta(lhs: float | int | None, rhs: float | int | None) -> float | None:
    delta = numeric_delta(lhs, rhs)
    return abs(delta) if delta is not None else None


def score_pair(
    baseline: dict[str, Any],
    test: dict[str, Any],
    *,
    weights: dict[str, float],
) -> dict[str, Any]:
    baseline_pos = actor_position(baseline["actor"])
    test_pos = actor_position(test["actor"])
    assert baseline_pos is not None and test_pos is not None
    actor_delta = distance3(baseline_pos, test_pos)
    actor_xz = xz_distance(baseline_pos, test_pos)
    timer_delta = abs_delta(baseline["glass"]["first_timer"], test["glass"]["first_timer"])
    health_delta = abs_delta(baseline["health"]["bond"], test["health"]["bond"])
    damage_show_delta = abs_delta(baseline["health"]["damage_show"], test["health"]["damage_show"])
    health_show_delta = abs_delta(baseline["health"]["health_show"], test["health"]["health_show"])
    hud_delta = (damage_show_delta or 0.0) + (health_show_delta or 0.0)
    visible_count_delta = abs(len(test["visible_actors"]) - len(baseline["visible_actors"]))
    active_delta = abs((test["glass"]["active"] or 0) - (baseline["glass"]["active"] or 0))

    score = (
        actor_delta * weights["actor"]
        + (timer_delta if timer_delta is not None else 1000.0) * weights["timer"]
        + (health_delta if health_delta is not None else 10.0) * weights["health"]
        + hud_delta * weights["hud"]
        + visible_count_delta * weights["visible_count"]
        + active_delta * weights["active"]
    )

    field_mismatches: list[str] = []
    for field in ("alive", "hidden", "onscreen", "action"):
        if baseline["actor"].get(field) != test["actor"].get(field):
            field_mismatches.append(field)

    return {
        "score": score,
        "actor_position_delta": actor_delta,
        "actor_xz_delta": actor_xz,
        "timer_delta": timer_delta,
        "health_delta": health_delta,
        "damage_show_delta": damage_show_delta,
        "health_show_delta": health_show_delta,
        "visible_count_delta": visible_count_delta,
        "active_delta": active_delta,
        "actor_field_mismatches": field_mismatches,
        "baseline": baseline,
        "test": test,
    }


def summarize_pair(pair: dict[str, Any]) -> dict[str, Any]:
    baseline = pair["baseline"]
    test = pair["test"]
    return {
        "score": pair["score"],
        "actor_position_delta": pair["actor_position_delta"],
        "actor_xz_delta": pair["actor_xz_delta"],
        "timer_delta": pair["timer_delta"],
        "health_delta": pair["health_delta"],
        "damage_show_delta": pair["damage_show_delta"],
        "health_show_delta": pair["health_show_delta"],
        "visible_count_delta": pair["visible_count_delta"],
        "active_delta": pair["active_delta"],
        "actor_field_mismatches": pair["actor_field_mismatches"],
        "baseline": {
            "frame": baseline["frame"],
            "global": baseline["global"],
            "clock": baseline["clock"],
            "dt": baseline["dt"],
            "glass": baseline["glass"],
            "health": baseline["health"],
            "actor": {
                "chrnum": as_int(baseline["actor"].get("chrnum")),
                "action": as_int(baseline["actor"].get("action")),
                "alive": as_int(baseline["actor"].get("alive")),
                "hidden": as_int(baseline["actor"].get("hidden")),
                "onscreen": as_int(baseline["actor"].get("onscreen")),
                "rendered": as_int(baseline["actor"].get("rendered")),
                "pos": actor_position(baseline["actor"]),
            },
            "visible_actors": baseline["visible_actors"],
        },
        "test": {
            "frame": test["frame"],
            "global": test["global"],
            "clock": test["clock"],
            "dt": test["dt"],
            "glass": test["glass"],
            "health": test["health"],
            "actor": {
                "chrnum": as_int(test["actor"].get("chrnum")),
                "action": as_int(test["actor"].get("action")),
                "alive": as_int(test["actor"].get("alive")),
                "hidden": as_int(test["actor"].get("hidden")),
                "onscreen": as_int(test["actor"].get("onscreen")),
                "rendered": as_int(test["actor"].get("rendered")),
                "pos": actor_position(test["actor"]),
            },
            "visible_actors": test["visible_actors"],
        },
    }


def print_pair(prefix: str, pair: dict[str, Any]) -> None:
    baseline = pair["baseline"]
    test = pair["test"]
    print(
        f"{prefix}: score={pair['score']:.3f} "
        f"actor={pair['actor_position_delta']:.3f} xz={pair['actor_xz_delta']:.3f} "
        f"timer_delta={pair['timer_delta']} health_delta={pair['health_delta']} "
        f"hud_delta={(pair['damage_show_delta'] or 0) + (pair['health_show_delta'] or 0)} "
        f"visible_delta={pair['visible_count_delta']} active_delta={pair['active_delta']}"
    )
    print(
        "  baseline: "
        f"frame={baseline['frame']} global={baseline['global']} "
        f"timer={baseline['glass']['first_timer']} active={baseline['glass']['active']} "
        f"bond={baseline['health']['bond']} damage_show={baseline['health']['damage_show']} "
        f"health_show={baseline['health']['health_show']} action={baseline['actor'].get('action')} "
        f"visible={len(baseline['visible_actors'])}"
    )
    print(
        "  test:     "
        f"frame={test['frame']} global={test['global']} "
        f"timer={test['glass']['first_timer']} active={test['glass']['active']} "
        f"bond={test['health']['bond']} damage_show={test['health']['damage_show']} "
        f"health_show={test['health']['health_show']} action={test['actor'].get('action')} "
        f"visible={len(test['visible_actors'])}"
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("baseline", type=Path, help="baseline JSONL trace, usually stock ares")
    parser.add_argument("test", type=Path, help="test JSONL trace, usually native")
    parser.add_argument("--actor-chrnum", type=int, default=12)
    parser.add_argument("--top", type=int, default=20)
    parser.add_argument("--json-out", type=Path)
    parser.add_argument("--require-active", action="store_true", help="only score pairs with active glass on both sides")
    parser.add_argument("--health-tolerance", type=float, default=0.001)
    parser.add_argument("--actor-position-tolerance", type=float, default=25.0)
    parser.add_argument("--timer-tolerance", type=float, default=0.0)
    parser.add_argument("--weight-actor", type=float, default=1.0)
    parser.add_argument("--weight-timer", type=float, default=2.0)
    parser.add_argument("--weight-health", type=float, default=200.0)
    parser.add_argument("--weight-hud", type=float, default=0.5)
    parser.add_argument("--weight-visible-count", type=float, default=25.0)
    parser.add_argument("--weight-active", type=float, default=5.0)
    args = parser.parse_args()

    if args.top < 1:
        parser.error("--top must be positive")
    for name in (
        "health_tolerance",
        "actor_position_tolerance",
        "timer_tolerance",
        "weight_actor",
        "weight_timer",
        "weight_health",
        "weight_hud",
        "weight_visible_count",
        "weight_active",
    ):
        if getattr(args, name) < 0:
            parser.error(f"--{name.replace('_', '-')} must be non-negative")

    baseline_records = checkpoint_records(load_records(args.baseline), args.actor_chrnum)
    test_records = checkpoint_records(load_records(args.test), args.actor_chrnum)
    if args.require_active:
        baseline_records = [record for record in baseline_records if record["glass"]["active"] > 0]
        test_records = [record for record in test_records if record["glass"]["active"] > 0]

    weights = {
        "actor": args.weight_actor,
        "timer": args.weight_timer,
        "health": args.weight_health,
        "hud": args.weight_hud,
        "visible_count": args.weight_visible_count,
        "active": args.weight_active,
    }
    scored = [
        score_pair(baseline, test, weights=weights)
        for baseline in baseline_records
        for test in test_records
    ]
    scored.sort(key=lambda pair: pair["score"])

    health_matched = [
        pair for pair in scored
        if pair["health_delta"] is not None and pair["health_delta"] <= args.health_tolerance
    ]
    actor_matched = [
        pair for pair in scored
        if pair["actor_position_delta"] <= args.actor_position_tolerance
    ]
    timer_matched = [
        pair for pair in scored
        if pair["timer_delta"] is not None and pair["timer_delta"] <= args.timer_tolerance
    ]
    strict_matched = [
        pair for pair in scored
        if (
            pair["health_delta"] is not None
            and pair["health_delta"] <= args.health_tolerance
            and pair["actor_position_delta"] <= args.actor_position_tolerance
            and pair["timer_delta"] is not None
            and pair["timer_delta"] <= args.timer_tolerance
            and not pair["actor_field_mismatches"]
        )
    ]

    payload = {
        "baseline": str(args.baseline),
        "test": str(args.test),
        "actor_chrnum": args.actor_chrnum,
        "baseline_candidates": len(baseline_records),
        "test_candidates": len(test_records),
        "pair_count": len(scored),
        "filters": {
            "require_active": args.require_active,
            "health_tolerance": args.health_tolerance,
            "actor_position_tolerance": args.actor_position_tolerance,
            "timer_tolerance": args.timer_tolerance,
        },
        "weights": weights,
        "best": [summarize_pair(pair) for pair in scored[: args.top]],
        "best_health_matched": [summarize_pair(pair) for pair in health_matched[: args.top]],
        "best_actor_matched": [summarize_pair(pair) for pair in actor_matched[: args.top]],
        "best_timer_matched": [summarize_pair(pair) for pair in timer_matched[: args.top]],
        "best_strict_matched": [summarize_pair(pair) for pair in strict_matched[: args.top]],
    }

    if args.json_out:
        args.json_out.parent.mkdir(parents=True, exist_ok=True)
        with args.json_out.open("w", encoding="utf-8") as handle:
            json.dump(payload, handle, indent=2, sort_keys=True)
            handle.write("\n")

    print(
        f"candidates: baseline={len(baseline_records)} test={len(test_records)} "
        f"pairs={len(scored)} require_active={args.require_active}"
    )
    if scored:
        print_pair("best", scored[0])
    if health_matched:
        print_pair("best_health_matched", health_matched[0])
    if actor_matched:
        print_pair("best_actor_matched", actor_matched[0])
    if timer_matched:
        print_pair("best_timer_matched", timer_matched[0])
    if strict_matched:
        print_pair("best_strict_matched", strict_matched[0])
    else:
        print("best_strict_matched: none")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
