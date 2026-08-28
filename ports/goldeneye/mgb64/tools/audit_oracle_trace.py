#!/usr/bin/env python3
"""Audit ROM/native oracle traces for route-control safety."""

from __future__ import annotations

import argparse
import json
import math
import sys
from pathlib import Path
from typing import Any


START_BUTTON_MASK = 0x1000
INTRO_SKIP_BUTTON_MASK = 0xF000


def load_trace(path: Path) -> list[dict[str, Any]]:
    records: list[dict[str, Any]] = []
    skipped = 0
    with path.open("r", encoding="utf-8", errors="replace") as handle:
        for line in handle:
            line = line.strip()
            if not line or not line.startswith("{"):
                continue
            try:
                records.append(json.loads(line))
            except json.JSONDecodeError:
                skipped += 1
    if skipped:
        print(f"WARNING: skipped {skipped} corrupted JSONL line(s) in {path}", file=sys.stderr)
    return records


def parse_int(value: Any) -> int | None:
    if isinstance(value, bool):
        return int(value)
    if isinstance(value, int):
        return value
    if isinstance(value, str):
        try:
            return int(value, 0)
        except ValueError:
            return None
    return None


def get_path(record: dict[str, Any], path: tuple[str, ...]) -> Any:
    value: Any = record
    for part in path:
        if not isinstance(value, dict) or part not in value:
            return None
        value = value[part]
    return value


def numeric_vector(value: Any, min_len: int) -> list[float] | None:
    if not isinstance(value, list) or len(value) < min_len:
        return None
    vector: list[float] = []
    for item in value[:min_len]:
        if not isinstance(item, (int, float)) or not math.isfinite(float(item)):
            return None
        vector.append(float(item))
    return vector


def input_state(record: dict[str, Any]) -> dict[str, Any]:
    oracle = record.get("oracle")
    if not isinstance(oracle, dict):
        return {}
    value = oracle.get("input")
    return value if isinstance(value, dict) else {}


def input_buttons(record: dict[str, Any]) -> int:
    value = parse_int(input_state(record).get("buttons"))
    return value if value is not None else 0


def input_count(record: dict[str, Any], name: str) -> int:
    value = parse_int(input_state(record).get(name))
    return value if value is not None else 0


def force_state(record: dict[str, Any]) -> dict[str, Any]:
    oracle = record.get("oracle")
    if not isinstance(oracle, dict):
        return {}
    value = oracle.get("force")
    return value if isinstance(value, dict) else {}


def force_count(record: dict[str, Any], name: str) -> int:
    value = parse_int(force_state(record).get(name))
    return value if value is not None else 0


def target_stage_matches(record: dict[str, Any], stage: int | None) -> bool:
    if stage is None:
        return True
    front_stage = parse_int(get_path(record, ("front", "active_stage")))
    if front_stage is not None:
        return front_stage == stage
    oracle_stage = parse_int(get_path(record, ("oracle", "stage")))
    if oracle_stage is not None:
        return oracle_stage == stage
    return False


def frame_label(record: dict[str, Any]) -> str:
    frame = record.get("f", "?")
    inp = input_state(record)
    input_frame = inp.get("frame")
    gameplay_frame = inp.get("gameplay_frame")
    return f"frame={frame} input_frame={input_frame} gameplay_frame={gameplay_frame}"


def record_input_frame(record: dict[str, Any]) -> int | None:
    inp = input_state(record)
    value = parse_int(inp.get("frame"))
    if value is not None:
        return value
    return parse_int(record.get("f"))


def first_record_summary(record: dict[str, Any] | None) -> str:
    if record is None:
        return "none"
    move = record.get("move") if isinstance(record.get("move"), dict) else {}
    global_timer = move.get("global") if isinstance(move, dict) else None
    return f"{frame_label(record)} global={global_timer}"


def record_summary(record: dict[str, Any] | None) -> dict[str, Any] | None:
    if record is None:
        return None
    move = record.get("move") if isinstance(record.get("move"), dict) else {}
    inp = input_state(record)
    return {
        "frame": parse_int(record.get("f")),
        "input_frame": parse_int(inp.get("frame")),
        "gameplay_frame": parse_int(inp.get("gameplay_frame")),
        "global": parse_int(move.get("global")) if isinstance(move, dict) else None,
    }


def write_json_metrics(path: str | None, metrics: dict[str, Any]) -> None:
    if not path:
        return
    Path(path).write_text(json.dumps(metrics, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def audit(args: argparse.Namespace) -> int:
    if args.min_gameplay_input_records < 0:
        print("FAIL: --min-gameplay-input-records must be non-negative")
        return 2
    if args.min_moving_records < 0:
        print("FAIL: --min-moving-records must be non-negative")
        return 2
    if args.max_suppressed_menu_records is not None and args.max_suppressed_menu_records < 0:
        print("FAIL: --max-suppressed-menu-records must be non-negative")
        return 2
    if args.min_menu_to_gameplay_gap < 0:
        print("FAIL: --min-menu-to-gameplay-gap must be non-negative")
        return 2
    if args.min_horizontal_delta < 0.0:
        print("FAIL: --min-horizontal-delta must be non-negative")
        return 2
    if args.min_force_player_applies < 0:
        print("FAIL: --min-force-player-applies must be non-negative")
        return 2
    if args.min_force_player_stan_applies < 0:
        print("FAIL: --min-force-player-stan-applies must be non-negative")
        return 2

    records = load_trace(Path(args.trace))
    if not records:
        print(f"FAIL: {args.label}: no JSON records in {args.trace}")
        return 2

    stage = args.require_stage
    failures: list[str] = []
    failure_count = 0
    first_target_player: dict[str, Any] | None = None
    first_gameplay_timeline: dict[str, Any] | None = None
    first_gameplay_input: dict[str, Any] | None = None
    first_moving_record: dict[str, Any] | None = None
    last_menu_input: dict[str, Any] | None = None
    total_menu_events = 0
    total_gameplay_events = 0
    total_suppressed_menu = 0
    target_player_records = 0
    gameplay_input_records = 0
    moving_records = 0
    max_force_player_applies = 0
    max_force_player_stan_applies = 0
    first_position_record: dict[str, Any] | None = None
    last_position_record: dict[str, Any] | None = None
    first_position: list[float] | None = None
    max_horizontal_delta = 0.0

    def fail(message: str) -> None:
        nonlocal failure_count
        failure_count += 1
        if len(failures) < args.max_failures:
            failures.append(message)

    for record in records:
        inp = input_state(record)
        gameplay_frame = parse_int(inp.get("gameplay_frame"))
        menu_events = input_count(record, "menu_events")
        gameplay_events = input_count(record, "gameplay_events")
        suppressed_menu = input_count(record, "suppressed_menu_events")
        buttons = input_buttons(record)
        target_player = record.get("p") == 1 and target_stage_matches(record, stage)
        force = force_state(record)
        force_applies = force_count(record, "applies")
        max_force_player_applies = max(max_force_player_applies, force_applies)
        last_force_stan = parse_int(force.get("last_stan"))
        if last_force_stan:
            max_force_player_stan_applies = max(max_force_player_stan_applies, force_applies)

        total_menu_events += menu_events
        total_gameplay_events += gameplay_events
        total_suppressed_menu += suppressed_menu

        if target_player:
            target_player_records += 1
            if first_target_player is None:
                first_target_player = record
            pos = numeric_vector(record.get("pos"), 3)
            if pos is not None:
                if first_position is None:
                    first_position = pos
                    first_position_record = record
                last_position_record = record
                dx = pos[0] - first_position[0]
                dz = pos[2] - first_position[2]
                max_horizontal_delta = max(max_horizontal_delta, math.hypot(dx, dz))

        if gameplay_frame is not None and gameplay_frame > 0 and first_gameplay_timeline is None:
            first_gameplay_timeline = record

        if menu_events > 0:
            last_menu_input = record
            if target_player and not args.allow_menu_after_player:
                fail(
                    f"menu input after target-player entry at {frame_label(record)} "
                    f"events={menu_events} buttons=0x{buttons:04x}"
                )
            if (
                gameplay_frame is not None
                and gameplay_frame > 0
                and not args.allow_menu_after_player
            ):
                fail(
                    f"menu input after gameplay timeline start at {frame_label(record)} "
                    f"events={menu_events} buttons=0x{buttons:04x}"
                )

        if gameplay_events > 0:
            gameplay_input_records += 1
            if first_gameplay_input is None:
                first_gameplay_input = record
            if buttons & START_BUTTON_MASK and not args.allow_gameplay_start:
                fail(
                    f"START input during gameplay route at {frame_label(record)} "
                    f"buttons=0x{buttons:04x}"
                )

        if args.kind == "intro" and not args.allow_intro_skip:
            intro_frozen = parse_int(get_path(record, ("intro", "frozen")))
            if target_player and intro_frozen == 1 and (buttons & INTRO_SKIP_BUTTON_MASK):
                fail(
                    f"skip-capable input during active intro at {frame_label(record)} "
                    f"buttons=0x{buttons:04x}"
                )

        if not args.allow_watch:
            watch_state = parse_int(get_path(record, ("watch", "state")))
            if watch_state is not None and watch_state != 0:
                fail(f"watch/pause active at {frame_label(record)} state={watch_state}")

        if target_player:
            speed = get_path(record, ("move", "speed"))
            if (
                (speed_vector := numeric_vector(speed, 2)) is not None
                and any(abs(value) > 0.0001 for value in speed_vector[:2])
            ):
                moving_records += 1
                if first_moving_record is None:
                    first_moving_record = record

    menu_to_gameplay_gap: int | None = None
    if last_menu_input is not None and first_gameplay_input is not None:
        last_menu_frame = record_input_frame(last_menu_input)
        first_gameplay_frame = record_input_frame(first_gameplay_input)
        if last_menu_frame is not None and first_gameplay_frame is not None:
            menu_to_gameplay_gap = first_gameplay_frame - last_menu_frame

    print(f"audit: {args.label}")
    print(f"  records={len(records)} target_player_records={target_player_records} moving_records={moving_records}")
    print(f"  first_target_player:    {first_record_summary(first_target_player)}")
    print(f"  first_gameplay_timeline:{first_record_summary(first_gameplay_timeline)}")
    print(f"  first_gameplay_input:   {first_record_summary(first_gameplay_input)}")
    print(f"  first_moving_record:    {first_record_summary(first_moving_record)}")
    print(f"  first_position_record:  {first_record_summary(first_position_record)}")
    print(f"  last_position_record:   {first_record_summary(last_position_record)}")
    print(f"  max_horizontal_delta:   {max_horizontal_delta:.6f}")
    print(f"  last_menu_input:        {first_record_summary(last_menu_input)}")
    print(
        "  menu_to_gameplay_gap:  "
        f"{menu_to_gameplay_gap if menu_to_gameplay_gap is not None else 'n/a'}"
    )
    print(
        "  input_events:"
        f" menu={total_menu_events}"
        f" gameplay={total_gameplay_events}"
        f" suppressed_menu={total_suppressed_menu}"
    )
    print(f"  input_records: gameplay={gameplay_input_records}")
    print(f"  force_player_applies: {max_force_player_applies}")
    print(f"  force_player_stan_applies: {max_force_player_stan_applies}")

    if args.require_target_player and first_target_player is None:
        fail("target-stage player was never observed")
    if args.require_gameplay_input and first_gameplay_input is None:
        fail("gameplay input events were never applied")
    if args.min_gameplay_input_records and gameplay_input_records < args.min_gameplay_input_records:
        fail(
            "gameplay input records below threshold: "
            f"{gameplay_input_records} < {args.min_gameplay_input_records}"
        )
    if args.min_moving_records and moving_records < args.min_moving_records:
        fail(
            f"moving records below threshold: {moving_records} < {args.min_moving_records}"
        )
    if args.min_horizontal_delta:
        if first_position is None:
            fail("target-stage player position was never observed")
        elif max_horizontal_delta < args.min_horizontal_delta:
            fail(
                "horizontal position delta below threshold: "
                f"{max_horizontal_delta:.6f} < {args.min_horizontal_delta:.6f}"
            )
    if args.min_force_player_applies and max_force_player_applies < args.min_force_player_applies:
        fail(
            "force-player applies below threshold: "
            f"{max_force_player_applies} < {args.min_force_player_applies}"
        )
    if (
        args.min_force_player_stan_applies
        and max_force_player_stan_applies < args.min_force_player_stan_applies
    ):
        fail(
            "force-player resolved-stan applies below threshold: "
            f"{max_force_player_stan_applies} < {args.min_force_player_stan_applies}"
        )
    if (
        args.max_suppressed_menu_records is not None
        and total_suppressed_menu > args.max_suppressed_menu_records
    ):
        fail(
            "suppressed menu records above threshold: "
            f"{total_suppressed_menu} > {args.max_suppressed_menu_records}"
        )
    if args.min_menu_to_gameplay_gap:
        if last_menu_input is None:
            pass
        elif first_gameplay_input is None:
            fail("cannot verify menu-to-gameplay gap because gameplay input never started")
        elif menu_to_gameplay_gap is None:
            fail("cannot verify menu-to-gameplay gap because frame counters are missing")
        elif menu_to_gameplay_gap < args.min_menu_to_gameplay_gap:
            fail(
                "menu-to-gameplay gap below threshold: "
                f"{menu_to_gameplay_gap} < {args.min_menu_to_gameplay_gap}"
            )
    if args.require_first_gameplay_global is not None:
        if first_gameplay_timeline is None:
            fail("cannot verify first gameplay global because gameplay never started")
        else:
            first_global = parse_int(get_path(first_gameplay_timeline, ("move", "global")))
            if first_global != args.require_first_gameplay_global:
                fail(
                    "first gameplay global mismatch: "
                    f"{first_global} != {args.require_first_gameplay_global}"
                )

    metrics = {
        "label": args.label,
        "trace": args.trace,
        "kind": args.kind,
        "status": "fail" if failure_count else "pass",
        "records": len(records),
        "target_player_records": target_player_records,
        "moving_records": moving_records,
        "gameplay_input_records": gameplay_input_records,
        "input_events": {
            "menu": total_menu_events,
            "gameplay": total_gameplay_events,
            "suppressed_menu": total_suppressed_menu,
        },
        "max_horizontal_delta": max_horizontal_delta,
        "max_force_player_applies": max_force_player_applies,
        "max_force_player_stan_applies": max_force_player_stan_applies,
        "menu_to_gameplay_gap": menu_to_gameplay_gap,
        "first_target_player": record_summary(first_target_player),
        "first_gameplay_timeline": record_summary(first_gameplay_timeline),
        "first_gameplay_input": record_summary(first_gameplay_input),
        "first_moving_record": record_summary(first_moving_record),
        "first_position_record": record_summary(first_position_record),
        "last_position_record": record_summary(last_position_record),
        "last_menu_input": record_summary(last_menu_input),
        "failure_count": failure_count,
        "failures": failures,
        "thresholds": {
            "min_gameplay_input_records": args.min_gameplay_input_records,
            "min_moving_records": args.min_moving_records,
            "min_horizontal_delta": args.min_horizontal_delta,
            "min_force_player_applies": args.min_force_player_applies,
            "min_force_player_stan_applies": args.min_force_player_stan_applies,
            "max_suppressed_menu_records": args.max_suppressed_menu_records,
            "min_menu_to_gameplay_gap": args.min_menu_to_gameplay_gap,
            "require_first_gameplay_global": args.require_first_gameplay_global,
        },
    }
    write_json_metrics(args.json_out, metrics)

    if failure_count:
        print(f"FAIL: {args.label}: oracle trace audit found {failure_count} issue(s)")
        for failure in failures[: args.max_failures]:
            print(f"  {failure}")
        if failure_count > args.max_failures:
            print("  ...")
        return 1

    print(f"PASS: {args.label}: oracle trace control audit")
    return 0


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace", help="JSONL trace to audit")
    parser.add_argument("--label", default="trace")
    parser.add_argument("--kind", choices=["movement", "intro"], default="movement")
    parser.add_argument("--require-stage", type=int, default=None)
    parser.add_argument("--require-target-player", action="store_true")
    parser.add_argument("--require-gameplay-input", action="store_true")
    parser.add_argument("--min-gameplay-input-records", type=int, default=0)
    parser.add_argument("--min-moving-records", type=int, default=0)
    parser.add_argument("--min-horizontal-delta", type=float, default=0.0)
    parser.add_argument("--min-force-player-applies", type=int, default=0)
    parser.add_argument("--min-force-player-stan-applies", type=int, default=0)
    parser.add_argument("--require-first-gameplay-global", type=int, default=None)
    parser.add_argument("--max-suppressed-menu-records", type=int, default=None)
    parser.add_argument("--min-menu-to-gameplay-gap", type=int, default=0)
    parser.add_argument("--allow-menu-after-player", action="store_true")
    parser.add_argument("--allow-gameplay-start", action="store_true")
    parser.add_argument("--allow-watch", action="store_true")
    parser.add_argument("--allow-intro-skip", action="store_true")
    parser.add_argument("--max-failures", type=int, default=5)
    parser.add_argument("--json-out", help="write audit metrics as JSON")
    return parser.parse_args()


def main() -> int:
    return audit(parse_args())


if __name__ == "__main__":
    raise SystemExit(main())
