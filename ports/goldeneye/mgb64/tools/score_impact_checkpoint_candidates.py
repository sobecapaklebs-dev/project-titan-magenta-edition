#!/usr/bin/env python3
"""Score stock/native glass-impact checkpoints for actor-clean visual candidates."""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
from typing import Any


DEFAULT_FIELDS = ("alive", "hidden", "hidden_bits", "onscreen", "rendered", "action", "room")


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
    if isinstance(value, bool):
        return None
    if isinstance(value, (int, float)):
        value = float(value)
        return value if math.isfinite(value) else None
    return None


def number_list(value: Any, length: int) -> list[float] | None:
    if (
        not isinstance(value, list)
        or len(value) != length
        or any(not isinstance(item, (int, float)) or isinstance(item, bool) for item in value)
    ):
        return None
    result = [float(item) for item in value]
    if any(not math.isfinite(item) for item in result):
        return None
    return result


def distance3(lhs: Any, rhs: Any) -> float | None:
    left = number_list(lhs, 3)
    right = number_list(rhs, 3)
    if left is None or right is None:
        return None
    return math.sqrt(sum((a - b) * (a - b) for a, b in zip(left, right)))


def bbox_center(value: Any) -> list[float] | None:
    bbox = number_list(value, 4)
    if bbox is None or bbox[2] <= bbox[0] or bbox[3] <= bbox[1]:
        return None
    return [(bbox[0] + bbox[2]) * 0.5, (bbox[1] + bbox[3]) * 0.5]


def distance2(lhs: Any, rhs: Any) -> float | None:
    left = number_list(lhs, 2)
    right = number_list(rhs, 2)
    if left is None or right is None:
        return None
    return math.sqrt((left[0] - right[0]) ** 2 + (left[1] - right[1]) ** 2)


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


def actor_position(actor: dict[str, Any] | None) -> list[float] | None:
    if not isinstance(actor, dict):
        return None
    return number_list(actor.get("pos"), 3)


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


def actors_by_chr(record: dict[str, Any]) -> dict[int, dict[str, Any]]:
    sample = record.get("actors", {}).get("sample", [])
    if not isinstance(sample, list):
        return {}
    result: dict[int, dict[str, Any]] = {}
    for actor in sample:
        if not isinstance(actor, dict):
            continue
        chrnum = as_int(actor.get("chrnum"))
        if chrnum is None:
            continue
        result[chrnum] = actor
    return result


def visible_chrnums(actors: dict[int, dict[str, Any]]) -> list[int]:
    return sorted(chrnum for chrnum, actor in actors.items() if actor_is_visible(actor))


def first_world_impact(record: dict[str, Any]) -> dict[str, Any]:
    state = record.get("impact_state")
    if not isinstance(state, dict):
        return {}
    sample = state.get("sample")
    if not isinstance(sample, list):
        return {}
    for item in sample:
        if isinstance(item, dict) and item.get("world"):
            return item
    return {}


def impact_identity(sample: dict[str, Any]) -> dict[str, Any]:
    return {
        "index": sample.get("index"),
        "room": sample.get("room"),
        "impact": sample.get("impact"),
        "prop": sample.get("prop"),
        "prop_pad": sample.get("prop_pad"),
        "world_center": sample.get("world_center"),
    }


def projection_center(sample: dict[str, Any]) -> list[float] | None:
    projection = sample.get("projection")
    if not isinstance(projection, dict) or projection.get("valid") != 1:
        return None
    return bbox_center(projection.get("screen_bbox"))


def checkpoint(record: dict[str, Any], require_active: bool) -> dict[str, Any] | None:
    if record.get("p") != 1:
        return None
    glass = glass_summary(record)
    if require_active and glass["active"] <= 0:
        return None
    impact = first_world_impact(record)
    if not impact:
        return None
    actors = actors_by_chr(record)
    move = record.get("move")
    if not isinstance(move, dict):
        move = {}
    return {
        "frame": as_int(record.get("f")),
        "global": as_int(move.get("global")),
        "clock": as_int(move.get("clock")),
        "dt": as_float(move.get("dt")),
        "glass": glass,
        "health": health_summary(record),
        "impact": impact,
        "impact_identity": impact_identity(impact),
        "projection_center": projection_center(impact),
        "actors": actors,
        "visible": visible_chrnums(actors),
    }


def checkpoint_records(records: list[dict[str, Any]], require_active: bool) -> list[dict[str, Any]]:
    checkpoints: list[dict[str, Any]] = []
    for record in records:
        candidate = checkpoint(record, require_active)
        if candidate is not None:
            checkpoints.append(candidate)
    return checkpoints


def abs_delta(lhs: int | float | None, rhs: int | float | None) -> float | None:
    if lhs is None or rhs is None:
        return None
    return abs(float(rhs) - float(lhs))


def impact_identity_matches(lhs: dict[str, Any], rhs: dict[str, Any]) -> bool:
    return (
        lhs.get("room") == rhs.get("room")
        and lhs.get("impact") == rhs.get("impact")
        and lhs.get("prop") == rhs.get("prop")
        and lhs.get("prop_pad") == rhs.get("prop_pad")
    )


def compare_pair(
    baseline: dict[str, Any],
    test: dict[str, Any],
    *,
    fields: list[str],
    required_chrs: list[int],
    position_tolerance: float,
    max_active_delta: float,
    max_timer_delta: float,
    max_health_delta: float,
    max_hud_delta: float,
    max_impact_center_delta: float,
    max_projection_center_delta: float,
) -> dict[str, Any]:
    baseline_visible = set(baseline["visible"])
    test_visible = set(test["visible"])
    visible_only_baseline = sorted(baseline_visible - test_visible)
    visible_only_test = sorted(test_visible - baseline_visible)
    compare_chrs = sorted(baseline_visible | test_visible | set(required_chrs))

    missing: list[dict[str, Any]] = []
    field_mismatches: list[dict[str, Any]] = []
    position_failures: list[dict[str, Any]] = []
    position_deltas: list[dict[str, Any]] = []

    for chrnum in compare_chrs:
        baseline_actor = baseline["actors"].get(chrnum)
        test_actor = test["actors"].get(chrnum)
        if baseline_actor is None or test_actor is None:
            missing.append({
                "chrnum": chrnum,
                "baseline_present": baseline_actor is not None,
                "test_present": test_actor is not None,
            })
            continue
        for field in fields:
            if baseline_actor.get(field) != test_actor.get(field):
                field_mismatches.append({
                    "chrnum": chrnum,
                    "field": field,
                    "baseline": baseline_actor.get(field),
                    "test": test_actor.get(field),
                })
        delta = distance3(actor_position(baseline_actor), actor_position(test_actor))
        if delta is not None:
            entry = {
                "chrnum": chrnum,
                "delta": delta,
                "baseline": actor_position(baseline_actor),
                "test": actor_position(test_actor),
            }
            position_deltas.append(entry)
            if delta > position_tolerance:
                position_failures.append(entry)

    active_delta = abs(float(test["glass"]["active"]) - float(baseline["glass"]["active"]))
    timer_delta = abs_delta(baseline["glass"]["first_timer"], test["glass"]["first_timer"])
    health_delta = abs_delta(baseline["health"]["bond"], test["health"]["bond"])
    damage_show_delta = abs_delta(baseline["health"]["damage_show"], test["health"]["damage_show"]) or 0.0
    health_show_delta = abs_delta(baseline["health"]["health_show"], test["health"]["health_show"]) or 0.0
    hud_delta = damage_show_delta + health_show_delta
    impact_center_delta = distance3(
        baseline["impact"].get("world_center"),
        test["impact"].get("world_center"),
    )
    projection_center_delta = distance2(baseline.get("projection_center"), test.get("projection_center"))
    identity_match = impact_identity_matches(baseline["impact_identity"], test["impact_identity"])

    visible_delta = len(visible_only_baseline) + len(visible_only_test)
    total_position_delta = sum(item["delta"] for item in position_deltas)
    max_position_delta = max((item["delta"] for item in position_deltas), default=0.0)

    strict = (
        identity_match
        and not visible_only_baseline
        and not visible_only_test
        and not missing
        and not field_mismatches
        and not position_failures
        and active_delta <= max_active_delta
        and timer_delta is not None and timer_delta <= max_timer_delta
        and health_delta is not None and health_delta <= max_health_delta
        and hud_delta <= max_hud_delta
        and impact_center_delta is not None and impact_center_delta <= max_impact_center_delta
        and projection_center_delta is not None and projection_center_delta <= max_projection_center_delta
    )

    score = (
        visible_delta * 500.0
        + len(missing) * 400.0
        + len(field_mismatches) * 120.0
        + len(position_failures) * 250.0
        + total_position_delta
        + active_delta * 100.0
        + (timer_delta if timer_delta is not None else 1000.0) * 25.0
        + (health_delta if health_delta is not None else 10.0) * 1000.0
        + hud_delta * 25.0
        + (impact_center_delta if impact_center_delta is not None else 1000.0) * 20.0
        + (projection_center_delta if projection_center_delta is not None else 1000.0) * 20.0
        + (0.0 if identity_match else 1000.0)
    )

    return {
        "score": score,
        "strict": strict,
        "visible_delta": visible_delta,
        "visible_only_baseline": visible_only_baseline,
        "visible_only_test": visible_only_test,
        "missing": missing,
        "field_mismatches": field_mismatches,
        "position_tolerance": position_tolerance,
        "position_failures": position_failures,
        "position_deltas": position_deltas,
        "max_position_delta": max_position_delta,
        "active_delta": active_delta,
        "timer_delta": timer_delta,
        "health_delta": health_delta,
        "hud_delta": hud_delta,
        "impact_identity_match": identity_match,
        "impact_center_delta": impact_center_delta,
        "projection_center_delta": projection_center_delta,
        "baseline": summarize_checkpoint(baseline),
        "test": summarize_checkpoint(test),
    }


def summarize_checkpoint(checkpoint: dict[str, Any]) -> dict[str, Any]:
    return {
        "frame": checkpoint["frame"],
        "global": checkpoint["global"],
        "clock": checkpoint["clock"],
        "dt": checkpoint["dt"],
        "glass": checkpoint["glass"],
        "health": checkpoint["health"],
        "visible": checkpoint["visible"],
        "impact": checkpoint["impact_identity"],
        "projection_center": checkpoint["projection_center"],
        "visible_actors": [
            actor_summary(checkpoint["actors"][chrnum])
            for chrnum in checkpoint["visible"]
            if chrnum in checkpoint["actors"]
        ],
    }


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--require-active", action="store_true")
    parser.add_argument("--actor-chrnum", action="append", type=int, default=[])
    parser.add_argument("--actor-field", action="append", default=[])
    parser.add_argument("--position-tolerance", type=float, default=25.0)
    parser.add_argument("--max-active-delta", type=float, default=0.0)
    parser.add_argument("--max-timer-delta", type=float, default=0.0)
    parser.add_argument("--max-health-delta", type=float, default=0.001)
    parser.add_argument("--max-hud-delta", type=float, default=1.0)
    parser.add_argument("--max-impact-center-delta", type=float, default=5.0)
    parser.add_argument("--max-projection-center-delta", type=float, default=1.0)
    parser.add_argument("--top", type=int, default=10)
    parser.add_argument("--json-out", type=Path)
    parser.add_argument("baseline_trace", type=Path)
    parser.add_argument("test_trace", type=Path)
    args = parser.parse_args()
    if args.top <= 0:
        parser.error("--top must be positive")
    for name in (
        "position_tolerance",
        "max_active_delta",
        "max_timer_delta",
        "max_health_delta",
        "max_hud_delta",
        "max_impact_center_delta",
        "max_projection_center_delta",
    ):
        value = getattr(args, name)
        if value < 0.0 or not math.isfinite(value):
            parser.error(f"--{name.replace('_', '-')} must be a non-negative finite number")
    return args


def main() -> int:
    args = parse_args()
    fields = args.actor_field or list(DEFAULT_FIELDS)
    baseline = checkpoint_records(load_records(args.baseline_trace), args.require_active)
    test = checkpoint_records(load_records(args.test_trace), args.require_active)

    pairs: list[dict[str, Any]] = []
    for baseline_checkpoint in baseline:
        for test_checkpoint in test:
            pairs.append(compare_pair(
                baseline_checkpoint,
                test_checkpoint,
                fields=fields,
                required_chrs=args.actor_chrnum,
                position_tolerance=args.position_tolerance,
                max_active_delta=args.max_active_delta,
                max_timer_delta=args.max_timer_delta,
                max_health_delta=args.max_health_delta,
                max_hud_delta=args.max_hud_delta,
                max_impact_center_delta=args.max_impact_center_delta,
                max_projection_center_delta=args.max_projection_center_delta,
            ))

    pairs.sort(key=lambda item: item["score"])
    best = pairs[:args.top]
    strict = [pair for pair in pairs if pair["strict"]]
    payload = {
        "status": "pass",
        "baseline_trace": str(args.baseline_trace),
        "test_trace": str(args.test_trace),
        "require_active": args.require_active,
        "candidate_counts": {
            "baseline": len(baseline),
            "test": len(test),
            "pairs": len(pairs),
            "strict": len(strict),
        },
        "thresholds": {
            "position_tolerance": args.position_tolerance,
            "max_active_delta": args.max_active_delta,
            "max_timer_delta": args.max_timer_delta,
            "max_health_delta": args.max_health_delta,
            "max_hud_delta": args.max_hud_delta,
            "max_impact_center_delta": args.max_impact_center_delta,
            "max_projection_center_delta": args.max_projection_center_delta,
            "fields": fields,
            "required_chrs": args.actor_chrnum,
        },
        "best": best,
        "best_strict": strict[:args.top],
    }

    if args.json_out:
        args.json_out.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")

    print(
        "candidates: baseline={baseline} test={test} pairs={pairs} strict={strict}".format(
            **payload["candidate_counts"]
        )
    )
    if best:
        pair = best[0]
        print(
            "best: score={score:.3f} strict={strict} visible_delta={visible_delta} "
            "fields={fields} max_pos={max_pos:.3f} timer_delta={timer} "
            "impact_delta={impact} projection_delta={projection}".format(
                score=pair["score"],
                strict=pair["strict"],
                visible_delta=pair["visible_delta"],
                fields=len(pair["field_mismatches"]),
                max_pos=pair["max_position_delta"],
                timer=pair["timer_delta"],
                impact=pair["impact_center_delta"],
                projection=pair["projection_center_delta"],
            )
        )
        print(
            "  baseline: frame={frame} global={global_} timer={timer} active={active} visible={visible}".format(
                frame=pair["baseline"]["frame"],
                global_=pair["baseline"]["global"],
                timer=pair["baseline"]["glass"]["first_timer"],
                active=pair["baseline"]["glass"]["active"],
                visible=pair["baseline"]["visible"],
            )
        )
        print(
            "  test:     frame={frame} global={global_} timer={timer} active={active} visible={visible}".format(
                frame=pair["test"]["frame"],
                global_=pair["test"]["global"],
                timer=pair["test"]["glass"]["first_timer"],
                active=pair["test"]["glass"]["active"],
                visible=pair["test"]["visible"],
            )
        )
    if strict:
        print(f"best_strict: score={strict[0]['score']:.3f}")
    else:
        print("best_strict: none")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
