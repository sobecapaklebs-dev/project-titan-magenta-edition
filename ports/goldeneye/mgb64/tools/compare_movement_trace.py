#!/usr/bin/env python3
"""Compare player movement fields from native and ROM-oracle JSONL traces."""

from __future__ import annotations

import argparse
import json
import math
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any


@dataclass(frozen=True)
class FieldSpec:
    path: tuple[str | int, ...]
    tolerance_name: str
    exact: bool = False


TIMING_FIELDS = [
    FieldSpec(("move", "clock"), "clock", exact=True),
    FieldSpec(("move", "dt"), "dt"),
    FieldSpec(("move", "max_t"), "clock", exact=True),
]

SCALAR_SPEED_FIELDS = [
    FieldSpec(("move", "speed", 0), "speed"),
    FieldSpec(("move", "speed", 1), "speed"),
    FieldSpec(("move", "raw", 0), "speed"),
    FieldSpec(("move", "raw", 1), "speed"),
]

DYNAMICS_FIELDS = SCALAR_SPEED_FIELDS + [
    FieldSpec(("move", "boost"), "speed"),
    FieldSpec(("move", "turn"), "speed"),
    FieldSpec(("move", "pitch"), "speed"),
]

POSITION_FIELDS = [
    FieldSpec(("move", "head", 0), "head"),
    FieldSpec(("move", "head", 1), "head"),
    FieldSpec(("move", "head", 2), "head"),
    FieldSpec(("move", "prev", 0), "position"),
    FieldSpec(("move", "prev", 1), "position"),
    FieldSpec(("move", "prev", 2), "position"),
    FieldSpec(("pos", 0), "position"),
    FieldSpec(("pos", 1), "position"),
    FieldSpec(("pos", 2), "position"),
]

FIELDS = TIMING_FIELDS + DYNAMICS_FIELDS + POSITION_FIELDS

COLLISION_FIELDS = [
    FieldSpec(("col", 0), "position"),
    FieldSpec(("col", 1), "position"),
    FieldSpec(("col", 2), "position"),
]

POSITION_PATHS = {
    ("move", "prev", 0),
    ("move", "prev", 1),
    ("move", "prev", 2),
    ("pos", 0),
    ("pos", 1),
    ("pos", 2),
}

START_BUTTON_MASK = 0x1000


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


def write_json_metrics(path: str | None, metrics: dict[str, Any]) -> None:
    if not path:
        return
    Path(path).write_text(json.dumps(metrics, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def get_path(record: dict[str, Any], path: tuple[str | int, ...]) -> Any:
    value: Any = record
    for part in path:
        if isinstance(part, int):
            if not isinstance(value, list) or part >= len(value):
                return None
            value = value[part]
        else:
            if not isinstance(value, dict) or part not in value:
                return None
            value = value[part]
    return value


def path_label(path: tuple[str | int, ...]) -> str:
    out = ""
    for part in path:
        if isinstance(part, int):
            out += f"[{part}]"
        else:
            out += f".{part}" if out else part
    return out


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


def target_stage_matches(record: dict[str, Any], stage: int | None) -> bool:
    if stage is None:
        return True
    for path in (
        ("front", "active_stage"),
        ("front", "loaded_stage"),
        ("front", "stage"),
        ("oracle", "stage"),
    ):
        value = parse_int(get_path(record, path))
        if value is not None:
            return value == stage
    return False


def movement_records(records: list[dict[str, Any]], stage: int | None) -> list[dict[str, Any]]:
    return [
        record
        for record in records
        if record.get("p") == 1
        and isinstance(record.get("move"), dict)
        and target_stage_matches(record, stage)
    ]


def validate_movement_capture_controls(
    label: str,
    records: list[dict[str, Any]],
    *,
    allow_gameplay_start: bool,
    allow_menu_after_player: bool,
    allow_watch: bool,
    max_report: int,
) -> list[str]:
    failures: list[str] = []
    for record in records:
        frame = record.get("f", "?")
        oracle = record.get("oracle")
        input_state = oracle.get("input") if isinstance(oracle, dict) else None

        if isinstance(input_state, dict) and not allow_gameplay_start:
            gameplay_frame = parse_int(input_state.get("gameplay_frame"))
            buttons = parse_int(input_state.get("buttons"))
            if gameplay_frame is not None and gameplay_frame > 0 and buttons is not None:
                if buttons & START_BUTTON_MASK:
                    failures.append(
                        f"{label}: START injected during gameplay at frame {frame} "
                        f"(gameplay_frame={gameplay_frame}, buttons=0x{buttons:04x})"
                    )

        if isinstance(input_state, dict) and not allow_menu_after_player:
            menu_events = parse_int(input_state.get("menu_events"))
            if record.get("p") == 1 and menu_events is not None and menu_events > 0:
                failures.append(
                    f"{label}: menu input injected after target player entry at frame {frame} "
                    f"(menu_events={menu_events})"
                )
            gameplay_frame = parse_int(input_state.get("gameplay_frame"))
            if (
                gameplay_frame is not None
                and gameplay_frame > 0
                and menu_events is not None
                and menu_events > 0
            ):
                failures.append(
                    f"{label}: menu input injected after gameplay timeline start at frame {frame} "
                    f"(gameplay_frame={gameplay_frame}, menu_events={menu_events})"
                )

        if not allow_watch:
            watch_state = parse_int(get_path(record, ("watch", "state")))
            if watch_state is not None and watch_state != 0:
                failures.append(f"{label}: watch/pause active at frame {frame} (state={watch_state})")

        if len(failures) >= max_report:
            break
    return failures


def parse_gameplay_window(spec: str) -> tuple[int, int]:
    try:
        start_text, length_text = spec.split(":", 1)
        start = int(start_text, 10)
        length = int(length_text, 10)
    except ValueError:
        raise argparse.ArgumentTypeError(f"invalid gameplay window {spec!r}; expected START:LEN") from None
    if start < 1 or length < 1:
        raise argparse.ArgumentTypeError(f"invalid gameplay window {spec!r}; START and LEN must be positive")
    return start, start + length - 1


def gameplay_frame_key(record: dict[str, Any]) -> int | None:
    oracle = record.get("oracle")
    if isinstance(oracle, dict):
        gameplay_frame = oracle.get("gameplay_frame")
        if isinstance(gameplay_frame, int) and gameplay_frame > 0:
            return gameplay_frame
    frame = record.get("f")
    return frame if isinstance(frame, int) and frame > 0 else None


def filter_gameplay_windows(
    records: list[dict[str, Any]],
    windows: list[tuple[int, int]],
) -> list[dict[str, Any]]:
    if not windows:
        return records
    filtered = []
    for record in records:
        key = gameplay_frame_key(record)
        if key is None:
            continue
        if any(start <= key <= end for start, end in windows):
            filtered.append(record)
    return filtered


def state_key(record: dict[str, Any]) -> tuple[Any, ...]:
    move = record.get("move", {})
    return (
        record.get("p"),
        tuple(record.get("pos", [])),
        tuple(record.get("col", [])),
        move.get("clock"),
        move.get("dt"),
        tuple(move.get("speed", [])),
        tuple(move.get("raw", [])),
        move.get("boost"),
        move.get("turn"),
        move.get("pitch"),
        move.get("max_t"),
        tuple(move.get("head", [])),
        tuple(move.get("prev", [])),
    )


def dedupe_consecutive_states(records: list[dict[str, Any]]) -> list[dict[str, Any]]:
    deduped: list[dict[str, Any]] = []
    previous_key: tuple[Any, ...] | None = None
    for record in records:
        key = state_key(record)
        if key == previous_key:
            continue
        deduped.append(record)
        previous_key = key
    return deduped


def first_moving_index(records: list[dict[str, Any]], threshold: float) -> int:
    for index, record in enumerate(records):
        values = [
            get_path(record, ("move", "speed", 0)),
            get_path(record, ("move", "speed", 1)),
            get_path(record, ("move", "raw", 0)),
            get_path(record, ("move", "raw", 1)),
        ]
        for value in values:
            if isinstance(value, (int, float)) and abs(float(value)) > threshold:
                return index
    return 0


def align_records(
    baseline: list[dict[str, Any]],
    test: list[dict[str, Any]],
    mode: str,
    motion_threshold: float,
) -> list[tuple[Any, dict[str, Any], dict[str, Any]]]:
    if mode == "index":
        count = min(len(baseline), len(test))
        return [(i, baseline[i], test[i]) for i in range(count)]
    if mode == "move":
        baseline_start = first_moving_index(baseline, motion_threshold)
        test_start = first_moving_index(test, motion_threshold)
        count = min(len(baseline) - baseline_start, len(test) - test_start)
        return [
            (i, baseline[baseline_start + i], test[test_start + i])
            for i in range(max(0, count))
        ]
    if mode == "move-global":
        # Onset-rebased sim-tick pairing (DAM_PARITY_DEEP_DIVE 2026-07-17 §3.1;
        # movement-lane twin of compare_combat_trace --align tick / FID-0062).
        # The two emitters run at different record cadences (native: one record
        # per rendered frame, move.global advancing by speedframes; stock: one
        # or more records per tick), so index pairing from onset skews the
        # timelines. Instead: keep the LAST record per move.global (end-of-tick
        # state), rebase each side's global to its first MOVING record (the
        # stock counter includes the menu boot), and pair on the rebased tick.
        def rebased_by_tick(records: list[dict[str, Any]]) -> dict[int, dict[str, Any]]:
            by_global: dict[int, dict[str, Any]] = {}
            for record in records:
                tick = record.get("move", {}).get("global")
                if isinstance(tick, int):
                    by_global[tick] = record
            ordered = [by_global[tick] for tick in sorted(by_global)]
            start = first_moving_index(ordered, motion_threshold)
            if not ordered:
                return {}
            onset = ordered[start].get("move", {}).get("global", 0)
            return {
                tick - onset: record
                for tick, record in by_global.items()
                if tick >= onset
            }

        baseline_by_tick = rebased_by_tick(baseline)
        test_by_tick = rebased_by_tick(test)
        shared = sorted(set(baseline_by_tick) & set(test_by_tick))
        return [(tick, baseline_by_tick[tick], test_by_tick[tick]) for tick in shared]

    def key_for(record: dict[str, Any]) -> Any:
        move = record.get("move", {})
        if mode == "global":
            return move.get("global")
        if mode == "frame":
            return record.get("f")
        if mode == "gameplay-frame":
            oracle = record.get("oracle")
            if isinstance(oracle, dict):
                gameplay_frame = oracle.get("gameplay_frame")
                if isinstance(gameplay_frame, int) and gameplay_frame > 0:
                    return gameplay_frame
            return record.get("f")
        raise AssertionError(mode)

    def records_by_key(records: list[dict[str, Any]]) -> dict[Any, dict[str, Any]]:
        by_key: dict[Any, dict[str, Any]] = {}
        for record in records:
            key = key_for(record)
            if key is None:
                continue
            if mode == "gameplay-frame" and key in by_key:
                continue
            by_key[key] = record
        return by_key

    baseline_by_key = records_by_key(baseline)
    test_by_key = records_by_key(test)
    keys = sorted(set(baseline_by_key) & set(test_by_key))
    return [(key, baseline_by_key[key], test_by_key[key]) for key in keys]


def horizontal_distance(a: dict[str, Any], b: dict[str, Any]) -> float | None:
    apos = get_path(a, ("pos",))
    bpos = get_path(b, ("pos",))
    if not (isinstance(apos, list) and isinstance(bpos, list) and len(apos) >= 3 and len(bpos) >= 3):
        return None
    try:
        dx = float(bpos[0]) - float(apos[0])
        dz = float(bpos[2]) - float(apos[2])
    except (TypeError, ValueError):
        return None
    return math.hypot(dx, dz)


def summarize(records: list[dict[str, Any]]) -> dict[str, Any]:
    distance = 0.0
    max_step = 0.0
    moving_frames = 0
    previous: dict[str, Any] | None = None
    for record in records:
        speed = get_path(record, ("move", "speed", 0))
        side = get_path(record, ("move", "speed", 1))
        if isinstance(speed, (int, float)) and isinstance(side, (int, float)):
            if abs(speed) > 0.0001 or abs(side) > 0.0001:
                moving_frames += 1
        if previous is not None:
            step = horizontal_distance(previous, record)
            if step is not None:
                distance += step
                max_step = max(max_step, step)
        previous = record
    return {
        "frames": len(records),
        "moving_frames": moving_frames,
        "distance": distance,
        "max_step": max_step,
    }


def compare(args: argparse.Namespace) -> int:
    if args.max_aligned is not None and args.max_aligned < 1:
        print("FAIL: --max-aligned must be positive when set")
        return 2
    if args.min_aligned is not None and args.min_aligned < 1:
        print("FAIL: --min-aligned must be positive when set")
        return 2

    baseline_path = Path(args.baseline)
    test_path = Path(args.test)
    baseline_all = load_trace(baseline_path)
    test_all = load_trace(test_path)
    common_metrics: dict[str, Any] = {
        "baseline": str(baseline_path),
        "test": str(test_path),
        "align": args.align,
        "profile": args.profile,
        "filters": {
            "baseline_stage": args.baseline_stage,
            "test_stage": args.test_stage,
            "gameplay_windows": [
                {"start": start, "end": end, "length": end - start + 1}
                for start, end in args.gameplay_window
            ],
            "start_global": args.start_global,
            "end_global": args.end_global,
            "dedupe_state": args.dedupe_state,
            "normalize_position": args.normalize_position,
            "max_aligned": args.max_aligned,
            "min_aligned": args.min_aligned,
        },
        "tolerances": {
            "speed": args.speed_tolerance,
            "position": args.position_tolerance,
            "head": args.head_tolerance,
            "dt": args.dt_tolerance,
            "clock": 0.0,
        },
        "controls": {
            "allow_gameplay_start": args.allow_gameplay_start,
            "allow_menu_after_player": args.allow_menu_after_player,
            "allow_watch": args.allow_watch,
        },
        "record_counts": {
            "baseline": len(baseline_all),
            "test": len(test_all),
        },
    }

    control_failures = []
    control_failures.extend(
        validate_movement_capture_controls(
            "baseline",
            baseline_all,
            allow_gameplay_start=args.allow_gameplay_start,
            allow_menu_after_player=args.allow_menu_after_player,
            allow_watch=args.allow_watch,
            max_report=args.max_control_failures,
        )
    )
    control_failures.extend(
        validate_movement_capture_controls(
            "test",
            test_all,
            allow_gameplay_start=args.allow_gameplay_start,
            allow_menu_after_player=args.allow_menu_after_player,
            allow_watch=args.allow_watch,
            max_report=args.max_control_failures,
        )
    )
    if control_failures:
        write_json_metrics(
            args.json_out,
            {
                **common_metrics,
                "status": "fail",
                "failure_kind": "control",
                "failure_count": len(control_failures),
                "failures": control_failures,
            },
        )
        print("FAIL: movement capture controls became invalid")
        for failure in control_failures[: args.max_control_failures]:
            print(f"  {failure}")
        if len(control_failures) > args.max_control_failures:
            print("  ...")
        return 2

    baseline = filter_gameplay_windows(
        movement_records(baseline_all, args.baseline_stage),
        args.gameplay_window,
    )
    test = filter_gameplay_windows(
        movement_records(test_all, args.test_stage),
        args.gameplay_window,
    )

    if args.start_global is not None or args.end_global is not None:
        def in_global_window(record: dict[str, Any]) -> bool:
            value = get_path(record, ("move", "global"))
            if not isinstance(value, int):
                return False
            if args.start_global is not None and value < args.start_global:
                return False
            if args.end_global is not None and value > args.end_global:
                return False
            return True
        baseline = [record for record in baseline if in_global_window(record)]
        test = [record for record in test if in_global_window(record)]

    if args.dedupe_state:
        baseline = dedupe_consecutive_states(baseline)
        test = dedupe_consecutive_states(test)

    aligned = align_records(baseline, test, args.align, args.motion_threshold)
    if args.max_aligned is not None:
        aligned = aligned[: args.max_aligned]
    if not aligned:
        write_json_metrics(
            args.json_out,
            {
                **common_metrics,
                "status": "fail",
                "failure_kind": "alignment",
                "active_counts": {"baseline": len(baseline), "test": len(test)},
                "aligned_count": 0,
                "failures": ["no aligned movement frames"],
            },
        )
        print("FAIL: no aligned movement frames")
        print(
            "  baseline movement frames:"
            f" {len(baseline)}"
            f" (stage={args.baseline_stage if args.baseline_stage is not None else 'any'})"
        )
        print(
            "  test movement frames:    "
            f" {len(test)}"
            f" (stage={args.test_stage if args.test_stage is not None else 'any'})"
        )
        return 2
    if args.min_aligned is not None and len(aligned) < args.min_aligned:
        write_json_metrics(
            args.json_out,
            {
                **common_metrics,
                "status": "fail",
                "failure_kind": "alignment",
                "active_counts": {"baseline": len(baseline), "test": len(test)},
                "aligned_count": len(aligned),
                "aligned_keys": [key for key, _left, _right in aligned],
                "failures": [f"aligned movement frames {len(aligned)} < required {args.min_aligned}"],
            },
        )
        print(f"FAIL: aligned movement frames {len(aligned)} < required {args.min_aligned}")
        return 2

    tolerances = {
        "speed": args.speed_tolerance,
        "position": args.position_tolerance,
        "head": args.head_tolerance,
        "dt": args.dt_tolerance,
        "clock": 0.0,
    }

    max_abs: dict[str, float] = {}
    divergences = 0
    divergence_details: list[dict[str, Any]] = []
    profile_fields = {
        "full": FIELDS,
        "dynamics": DYNAMICS_FIELDS,
        "scalar-speed": SCALAR_SPEED_FIELDS,
        "timing": TIMING_FIELDS,
    }[args.profile]
    fields = profile_fields + (COLLISION_FIELDS if args.compare_collision else [])
    left_base_pos = get_path(aligned[0][1], ("pos",)) if args.normalize_position else None
    right_base_pos = get_path(aligned[0][2], ("pos",)) if args.normalize_position else None

    def comparable_value(record: dict[str, Any], path: tuple[str | int, ...], base_pos: Any) -> Any:
        value = get_path(record, path)
        if not args.normalize_position or path not in POSITION_PATHS:
            return value
        axis = path[-1]
        if not (
            isinstance(axis, int)
            and isinstance(value, (int, float))
            and isinstance(base_pos, list)
            and axis < len(base_pos)
            and isinstance(base_pos[axis], (int, float))
        ):
            return value
        return float(value) - float(base_pos[axis])

    for key, left, right in aligned:
        diffs: list[str] = []
        for spec in fields:
            left_value = comparable_value(left, spec.path, left_base_pos)
            right_value = comparable_value(right, spec.path, right_base_pos)
            if left_value is None and right_value is None:
                continue
            label = path_label(spec.path)
            if left_value is None or right_value is None:
                diffs.append(f"{label}: {left_value!r} -> {right_value!r}")
                continue
            if spec.exact:
                if left_value != right_value:
                    diffs.append(f"{label}: {left_value!r} -> {right_value!r}")
                continue
            if not isinstance(left_value, (int, float)) or not isinstance(right_value, (int, float)):
                if left_value != right_value:
                    diffs.append(f"{label}: {left_value!r} -> {right_value!r}")
                continue
            delta = abs(float(left_value) - float(right_value))
            max_abs[label] = max(max_abs.get(label, 0.0), delta)
            if delta > tolerances[spec.tolerance_name]:
                diffs.append(f"{label}: {left_value:.5f} -> {right_value:.5f} (delta={delta:.5f})")

        if diffs:
            divergences += 1
            if len(divergence_details) < args.max_divergences:
                divergence_details.append(
                    {
                        "key": key,
                        "baseline_frame": left.get("f"),
                        "test_frame": right.get("f"),
                        "diffs": diffs,
                    }
                )
            if divergences <= args.max_divergences:
                left_frame = left.get("f", "?")
                right_frame = right.get("f", "?")
                print(f"DIVERGENCE at {args.align}={key} frames {left_frame}->{right_frame}:")
                for diff in diffs:
                    print(f"  {diff}")
            if divergences == args.max_divergences:
                print("  ...")

    baseline_summary = summarize([left for _, left, _ in aligned])
    test_summary = summarize([right for _, _, right in aligned])
    print("summary:")
    print(
        "  baseline:"
        f" frames={baseline_summary['frames']}"
        f" moving={baseline_summary['moving_frames']}"
        f" distance={baseline_summary['distance']:.3f}"
        f" max_step={baseline_summary['max_step']:.3f}"
    )
    print(
        "  test:    "
        f" frames={test_summary['frames']}"
        f" moving={test_summary['moving_frames']}"
        f" distance={test_summary['distance']:.3f}"
        f" max_step={test_summary['max_step']:.3f}"
    )
    if max_abs:
        worst = sorted(max_abs.items(), key=lambda item: item[1], reverse=True)[:8]
        print("  max_abs:", ", ".join(f"{name}={value:.5f}" for name, value in worst))

    metrics = {
        **common_metrics,
        "status": "fail" if divergences else "pass",
        "failure_kind": "divergence" if divergences else None,
        "active_counts": {"baseline": len(baseline), "test": len(test)},
        "aligned_count": len(aligned),
        "aligned_keys": [key for key, _left, _right in aligned],
        "field_count": len(fields),
        "summaries": {
            "baseline": baseline_summary,
            "test": test_summary,
        },
        "max_abs": max_abs,
        "divergence_count": divergences,
        "divergences": divergence_details,
    }
    write_json_metrics(args.json_out, metrics)

    if divergences:
        print(f"FAIL: {divergences} divergent aligned movement frame(s) out of {len(aligned)}")
        return 1

    print(
        "MATCH:"
        f" {len(aligned)} aligned movement frames"
        f" (profile={args.profile}, align={args.align}, speed_tol={args.speed_tolerance},"
        f" pos_tol={args.position_tolerance}, head_tol={args.head_tolerance})"
    )
    return 0


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("baseline", help="baseline JSONL trace, usually ROM/emulator")
    parser.add_argument("test", help="test JSONL trace, usually native")
    parser.add_argument(
        "--align",
        choices=["global", "frame", "index", "move", "move-global", "gameplay-frame"],
        default="global",
    )
    parser.add_argument(
        "--profile",
        choices=["full", "dynamics", "scalar-speed", "timing"],
        default="full",
        help="field set to compare",
    )
    parser.add_argument("--motion-threshold", type=float, default=0.0001)
    parser.add_argument("--max-aligned", type=int, default=None, help="compare at most this many aligned records")
    parser.add_argument("--min-aligned", type=int, default=None, help="require at least this many aligned records")
    parser.add_argument(
        "--gameplay-window",
        action="append",
        type=parse_gameplay_window,
        default=[],
        metavar="START:LEN",
        help="only compare records whose gameplay frame/native frame is inside this window; may repeat",
    )
    parser.add_argument("--no-dedupe-state", dest="dedupe_state", action="store_false")
    parser.set_defaults(dedupe_state=True)
    parser.add_argument("--normalize-position", action="store_true")
    parser.add_argument("--speed-tolerance", type=float, default=0.005)
    parser.add_argument("--position-tolerance", type=float, default=0.05)
    parser.add_argument("--head-tolerance", type=float, default=0.005)
    parser.add_argument("--dt-tolerance", type=float, default=0.001)
    parser.add_argument("--compare-collision", action="store_true")
    parser.add_argument(
        "--baseline-stage",
        type=int,
        default=None,
        help="only compare baseline records for this raw LEVELID",
    )
    parser.add_argument(
        "--test-stage",
        type=int,
        default=None,
        help="only compare test records for this raw LEVELID",
    )
    parser.add_argument(
        "--allow-gameplay-start",
        action="store_true",
        help="do not fail when instrumented input reports START during gameplay",
    )
    parser.add_argument(
        "--allow-watch",
        action="store_true",
        help="do not fail when traces report an active watch/pause state",
    )
    parser.add_argument(
        "--allow-menu-after-player",
        action="store_true",
        help="do not fail when instrumented stock input reports menu events after target player entry",
    )
    parser.add_argument("--start-global", type=int, default=None)
    parser.add_argument("--end-global", type=int, default=None)
    parser.add_argument("--max-divergences", type=int, default=3)
    parser.add_argument("--max-control-failures", type=int, default=5)
    parser.add_argument("--json-out", help="write comparison metrics as JSON")
    return parser.parse_args()


def main() -> int:
    return compare(parse_args())


if __name__ == "__main__":
    raise SystemExit(main())
