#!/usr/bin/env python3
"""Compare stock/native glass shard state from trace JSONL files."""

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


def first_position(glass: dict[str, Any]) -> list[float] | None:
    first = glass.get("first", {})
    if not isinstance(first, dict):
        return None
    pos = first.get("pos")
    if (
        not isinstance(pos, list)
        or len(pos) != 3
        or any(not isinstance(value, (int, float)) for value in pos)
    ):
        return None
    return [float(pos[0]), float(pos[1]), float(pos[2])]


def first_field(glass: dict[str, Any], key: str) -> Any:
    first = glass.get("first", {})
    if not isinstance(first, dict):
        return None
    return first.get(key)


def prop_position(prop: dict[str, Any] | None) -> list[float] | None:
    if not isinstance(prop, dict):
        return None
    pos = prop.get("pos")
    if (
        not isinstance(pos, list)
        or len(pos) != 3
        or any(not isinstance(value, (int, float)) for value in pos)
    ):
        return None
    return [float(pos[0]), float(pos[1]), float(pos[2])]


def distance3(a: list[float], b: list[float]) -> float:
    return math.sqrt(sum((lhs - rhs) * (lhs - rhs) for lhs, rhs in zip(a, b)))


def position_delta_components(a: list[float], b: list[float]) -> dict[str, float]:
    dx = float(b[0]) - float(a[0])
    dy = float(b[1]) - float(a[1])
    dz = float(b[2]) - float(a[2])
    return {
        "dx": dx,
        "dy": dy,
        "dz": dz,
        "abs_dx": abs(dx),
        "abs_dy": abs(dy),
        "abs_dz": abs(dz),
        "xz": math.sqrt((dx * dx) + (dz * dz)),
    }


def parse_impact_position_points(value: str) -> list[str]:
    allowed = {"center", "v0", "v1", "v2", "v3"}
    points = [point.strip() for point in value.split(",") if point.strip()]
    if not points:
        raise argparse.ArgumentTypeError("must name at least one impact point")
    invalid = [point for point in points if point not in allowed]
    if invalid:
        raise argparse.ArgumentTypeError(
            "invalid impact point(s): "
            + ", ".join(invalid)
            + "; expected comma-separated center,v0,v1,v2,v3"
        )
    return points


def parse_actor_fields(fields: str | None) -> list[str]:
    if fields is None or fields.strip() == "":
        return ["alive", "hidden", "hidden_bits", "onscreen", "rendered"]
    return [field.strip() for field in fields.split(",") if field.strip()]


def record_frame(record: dict[str, Any], fallback: int) -> int:
    try:
        return int(record.get("f", fallback))
    except (TypeError, ValueError):
        return fallback


def record_for_frame(records: list[dict[str, Any]], frame: int | None) -> dict[str, Any] | None:
    if frame is None:
        return None
    for index, record in enumerate(records):
        if record_frame(record, index + 1) == frame:
            return record
    return None


def record_global(record: dict[str, Any]) -> int | None:
    move = record.get("move")
    if not isinstance(move, dict):
        return None
    try:
        return int(move.get("global"))
    except (TypeError, ValueError):
        return None


def record_for_global(records: list[dict[str, Any]], global_timer: int | None) -> dict[str, Any] | None:
    if global_timer is None:
        return None
    for record in records:
        if record_global(record) == global_timer:
            return record
    return None


def actor_from_record(record: dict[str, Any] | None, chrnum: int) -> dict[str, Any] | None:
    if not isinstance(record, dict):
        return None
    actors = record.get("actors")
    if not isinstance(actors, dict):
        return None
    sample = actors.get("sample")
    if not isinstance(sample, list):
        return None
    for actor in sample:
        if not isinstance(actor, dict):
            continue
        try:
            actor_chrnum = int(actor.get("chrnum"))
        except (TypeError, ValueError):
            continue
        if actor_chrnum == chrnum:
            return actor
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


def actor_compare_frame(summary: dict[str, Any], mode: str) -> int | None:
    if mode == "first-active":
        return summary.get("first_active")
    if mode == "last-active":
        return summary.get("last_active")
    if mode == "screenshot":
        return None
    raise ValueError(f"unsupported actor compare frame mode: {mode}")


def actor_compare_record(
    records: list[dict[str, Any]],
    summary: dict[str, Any],
    mode: str,
    explicit_frame: int | None,
    explicit_global: int | None,
) -> tuple[int | None, int | None, dict[str, Any] | None, str]:
    if explicit_global is not None:
        record = record_for_global(records, explicit_global)
        frame = record_frame(record, 0) if record is not None else None
        return frame, explicit_global, record, "global"

    if explicit_frame is not None:
        record = record_for_frame(records, explicit_frame)
        return explicit_frame, record_global(record) if record is not None else None, record, "frame"

    if mode == "screenshot":
        return None, None, None, "screenshot"

    frame = actor_compare_frame(summary, mode)
    record = record_for_frame(records, frame)
    return frame, record_global(record) if record is not None else None, record, mode


def compare_actor_state(
    baseline_records: list[dict[str, Any]],
    test_records: list[dict[str, Any]],
    baseline_summary: dict[str, Any],
    test_summary: dict[str, Any],
    chrnum: int,
    fields: list[str],
    frame_mode: str,
    position_tolerance: float | None,
    baseline_frame_override: int | None,
    test_frame_override: int | None,
    baseline_global_override: int | None,
    test_global_override: int | None,
) -> dict[str, Any]:
    baseline_frame, baseline_global, baseline_record, baseline_source = actor_compare_record(
        baseline_records,
        baseline_summary,
        frame_mode,
        baseline_frame_override,
        baseline_global_override,
    )
    test_frame, test_global, test_record, test_source = actor_compare_record(
        test_records,
        test_summary,
        frame_mode,
        test_frame_override,
        test_global_override,
    )
    baseline_actor = actor_from_record(baseline_record, chrnum)
    test_actor = actor_from_record(test_record, chrnum)
    failures: list[str] = []
    field_results: dict[str, Any] = {}

    if baseline_record is None:
        failures.append(
            f"chr {chrnum}: missing baseline actor comparison record "
            f"for {baseline_source} checkpoint"
        )
    if test_record is None:
        failures.append(
            f"chr {chrnum}: missing test actor comparison record "
            f"for {test_source} checkpoint"
        )
    if baseline_actor is None:
        failures.append(f"chr {chrnum}: missing in baseline actor sample at frame {baseline_frame}")
    if test_actor is None:
        failures.append(f"chr {chrnum}: missing in test actor sample at frame {test_frame}")

    if baseline_actor is not None and test_actor is not None:
        for field in fields:
            baseline_value = baseline_actor.get(field)
            test_value = test_actor.get(field)
            match = baseline_value == test_value
            field_results[field] = {
                "baseline": baseline_value,
                "test": test_value,
                "match": match,
            }
            if not match:
                failures.append(
                    f"chr {chrnum}: field {field} mismatch at {frame_mode}: "
                    f"{baseline_value!r} != {test_value!r}"
                )

    position_delta = None
    position_components = None
    baseline_pos = actor_position(baseline_actor)
    test_pos = actor_position(test_actor)
    if baseline_pos is not None and test_pos is not None:
        position_delta = distance3(baseline_pos, test_pos)
        position_components = position_delta_components(baseline_pos, test_pos)

    if position_tolerance is not None:
        if position_delta is None or position_components is None:
            failures.append(f"chr {chrnum}: missing actor position for required position match")
        elif position_delta > position_tolerance:
            failures.append(
                f"chr {chrnum}: position delta {position_delta:.3f} exceeds "
                f"tolerance {position_tolerance:.3f} "
                f"(dx={position_components['dx']:.3f}, "
                f"dy={position_components['dy']:.3f}, "
                f"dz={position_components['dz']:.3f}, "
                f"xz={position_components['xz']:.3f})"
            )

    return {
        "chrnum": chrnum,
        "frame_mode": frame_mode,
        "baseline_frame": baseline_frame,
        "test_frame": test_frame,
        "baseline_global": baseline_global,
        "test_global": test_global,
        "baseline_source": baseline_source,
        "test_source": test_source,
        "baseline": baseline_actor,
        "test": test_actor,
        "fields": field_results,
        "position_delta": position_delta,
        "position_delta_components": position_components,
        "status": "fail" if failures else "pass",
        "failures": failures,
    }


def parse_rng(record: dict[str, Any] | None) -> dict[str, Any] | None:
    if not isinstance(record, dict):
        return None
    rng = record.get("rng")
    if not isinstance(rng, dict):
        return None
    seed = rng.get("seed")
    if not isinstance(seed, str):
        return None
    try:
        seed_int = int(seed, 16)
    except ValueError:
        return None
    out = dict(rng)
    out["seed_int"] = seed_int
    return out


def first_sample(summary: dict[str, Any]) -> list[Any] | None:
    sample_frames = summary.get("sample")
    if not isinstance(sample_frames, list) or not sample_frames:
        return None
    frame = sample_frames[0]
    if not isinstance(frame, dict):
        return None
    sample = frame.get("sample")
    if not isinstance(sample, list):
        return None
    return sample


def compare_sample_values(
    baseline_value: Any,
    test_value: Any,
    path: str,
    mismatches: list[str],
) -> float:
    if isinstance(baseline_value, (int, float)) and isinstance(test_value, (int, float)):
        return abs(float(test_value) - float(baseline_value))

    if isinstance(baseline_value, list) and isinstance(test_value, list):
        max_delta = 0.0
        if len(baseline_value) != len(test_value):
            mismatches.append(f"{path}: length {len(baseline_value)} != {len(test_value)}")
        for index, (baseline_item, test_item) in enumerate(zip(baseline_value, test_value)):
            max_delta = max(
                max_delta,
                compare_sample_values(
                    baseline_item,
                    test_item,
                    f"{path}[{index}]",
                    mismatches,
                ),
            )
        return max_delta

    if isinstance(baseline_value, dict) and isinstance(test_value, dict):
        max_delta = 0.0
        baseline_keys = set(baseline_value)
        test_keys = set(test_value)
        for key in sorted(baseline_keys - test_keys):
            mismatches.append(f"{path}.{key}: missing in test")
        for key in sorted(test_keys - baseline_keys):
            mismatches.append(f"{path}.{key}: missing in baseline")
        for key in sorted(baseline_keys & test_keys):
            max_delta = max(
                max_delta,
                compare_sample_values(
                    baseline_value[key],
                    test_value[key],
                    f"{path}.{key}",
                    mismatches,
                ),
            )
        return max_delta

    if baseline_value != test_value:
        mismatches.append(f"{path}: {baseline_value!r} != {test_value!r}")
    return 0.0


def compare_first_samples(
    baseline: dict[str, Any],
    test: dict[str, Any],
) -> dict[str, Any] | None:
    baseline_sample = first_sample(baseline)
    test_sample = first_sample(test)
    if baseline_sample is None and test_sample is None:
        return None

    mismatches: list[str] = []
    if baseline_sample is None:
        mismatches.append("baseline: missing first active sample")
        return {
            "match": False,
            "max_numeric_delta": None,
            "mismatch_count": len(mismatches),
            "mismatches": mismatches,
        }
    if test_sample is None:
        mismatches.append("test: missing first active sample")
        return {
            "match": False,
            "max_numeric_delta": None,
            "mismatch_count": len(mismatches),
            "mismatches": mismatches,
        }

    max_delta = compare_sample_values(baseline_sample, test_sample, "sample", mismatches)
    return {
        "match": not mismatches and max_delta == 0.0,
        "max_numeric_delta": max_delta,
        "mismatch_count": len(mismatches),
        "mismatches": mismatches[:32],
    }


def rng_next(seed: int) -> int:
    mask = (1 << 64) - 1
    seed &= mask
    value = ((seed << 63) & mask) >> 31
    value |= ((seed << 31) & mask) >> 32
    value ^= ((seed << 44) & mask) >> 32
    return (value ^ ((value >> 20) & 0xFFF)) & mask


def rng_draw_distance(start: int | None, end: int | None, limit: int = 10000) -> int | None:
    if start is None or end is None:
        return None
    seed = start
    for count in range(1, limit + 1):
        seed = rng_next(seed)
        if seed == end:
            return count
    return None


def glass_summary(records: list[dict[str, Any]], path: Path) -> dict[str, Any]:
    frames = []
    first_active = None
    first_pre_rng = None
    first_rng = None
    first_rng_draws = None
    max_active = 0
    active_frames = 0
    last_active = None
    first_hash = None
    max_buffer_len = 0
    present_records = 0
    first_pos = None
    first_piece = None
    first_timer = None
    first_rot_y = None
    last_piece = None
    last_timer = None
    last_rot_y = None

    previous_record = None
    for index, record in enumerate(records):
        glass = record.get("glass")
        if not isinstance(glass, dict):
            previous_record = record
            continue
        present_records += 1
        frame = int(record.get("f", index + 1))
        active = int(glass.get("active", 0) or 0)
        buffer_len = int(glass.get("buffer_len", 0) or 0)
        if buffer_len > max_buffer_len:
            max_buffer_len = buffer_len
        if active <= 0:
            previous_record = record
            continue
        active_frames += 1
        if active > max_active:
            max_active = active
        if first_active is None:
            first_active = frame
            first_hash = str(glass.get("hash", ""))
            first_pos = first_position(glass)
            first_piece = first_field(glass, "piece")
            first_timer = first_field(glass, "timer")
            first_rot_y = first_field(glass, "rot_y")
            if first_timer is None:
                first_timer = first_piece
            if first_rot_y is None:
                first_rot_y = first_field(glass, "age")
            first_pre_rng = parse_rng(previous_record)
            first_rng = parse_rng(record)
            if first_pre_rng is not None and first_rng is not None:
                first_rng_draws = rng_draw_distance(
                    first_pre_rng.get("seed_int"),
                    first_rng.get("seed_int"),
                )
        last_piece = first_field(glass, "piece")
        last_timer = first_field(glass, "timer")
        last_rot_y = first_field(glass, "rot_y")
        if last_timer is None:
            last_timer = last_piece
        if last_rot_y is None:
            last_rot_y = first_field(glass, "age")
        last_active = frame
        frames.append(
            {
                "frame": frame,
                "active": active,
                "next": int(glass.get("next", 0) or 0),
                "hash": str(glass.get("hash", "")),
                "first": glass.get("first", {}),
                "sample": glass.get("sample", []),
                "rng": record.get("rng", {}),
            }
        )
        previous_record = record

    return {
        "path": str(path),
        "records": len(records),
        "glass_records": present_records,
        "first_active": first_active,
        "last_active": last_active,
        "active_frames": active_frames,
        "max_active": max_active,
        "max_buffer_len": max_buffer_len,
        "first_hash": first_hash,
        "first_pos": first_pos,
        "first_piece": first_piece,
        "first_timer": first_timer,
        "first_rot_y": first_rot_y,
        "last_piece": last_piece,
        "last_timer": last_timer,
        "last_rot_y": last_rot_y,
        "first_pre_rng": first_pre_rng,
        "first_rng": first_rng,
        "first_rng_draws": first_rng_draws,
        "sample": frames[:8],
    }


def glass_props_summary(records: list[dict[str, Any]], path: Path) -> dict[str, Any]:
    present_records = 0
    max_count = 0
    max_destroyed = 0
    max_remove = 0
    first_removed = None
    first_destroyed = None
    first_break = None
    first_break_pos = None

    for index, record in enumerate(records):
        props = record.get("glass_props")
        if not isinstance(props, dict):
            continue
        present_records += 1
        frame = int(record.get("f", index + 1))
        count = int(props.get("count", 0) or 0)
        destroyed = int(props.get("destroyed", 0) or 0)
        remove = int(props.get("remove", 0) or 0)
        max_count = max(max_count, count)
        max_destroyed = max(max_destroyed, destroyed)
        max_remove = max(max_remove, remove)

        if first_removed is None and remove > 0:
            first_removed = {
                "frame": frame,
                "prop": props.get("first_removed", {}),
                "hash": str(props.get("hash", "")),
            }
        if first_destroyed is None and destroyed > 0:
            first_destroyed = {
                "frame": frame,
                "prop": props.get("first_destroyed", {}),
                "hash": str(props.get("hash", "")),
            }
        if first_break is None and (remove > 0 or destroyed > 0):
            prop = props.get("first_removed" if remove > 0 else "first_destroyed", {})
            first_break = {
                "frame": frame,
                "prop": prop,
                "hash": str(props.get("hash", "")),
            }
            first_break_pos = prop_position(prop)

    return {
        "path": str(path),
        "glass_prop_records": present_records,
        "max_count": max_count,
        "max_destroyed": max_destroyed,
        "max_remove": max_remove,
        "first_removed": first_removed,
        "first_destroyed": first_destroyed,
        "first_break": first_break,
        "first_break_pos": first_break_pos,
    }


def impact_state_summary(records: list[dict[str, Any]], path: Path) -> dict[str, Any]:
    present_records = 0
    max_occupied = 0
    first_active = None
    last_active = None
    first_hash = None
    first_impact = None
    first_sample = None
    first_world_active = None
    first_world_sample = None
    max_buffer_len = 0

    for index, record in enumerate(records):
        state = record.get("impact_state")
        if not isinstance(state, dict):
            continue
        present_records += 1
        frame = int(record.get("f", index + 1))
        occupied = int(state.get("occupied", 0) or 0)
        buffer_len = int(state.get("buffer_len", 0) or 0)
        max_buffer_len = max(max_buffer_len, buffer_len)
        if occupied <= 0:
            continue
        max_occupied = max(max_occupied, occupied)
        if first_active is None:
            first_active = frame
            first_hash = str(state.get("hash", ""))
            first_impact = state.get("first", {})
            sample = state.get("sample")
            if isinstance(sample, list) and sample:
                first_sample = sample[0]
        sample = state.get("sample")
        if first_world_sample is None and isinstance(sample, list):
            for item in sample:
                if isinstance(item, dict) and item.get("world"):
                    first_world_active = frame
                    first_world_sample = item
                    break
        last_active = frame

    return {
        "path": str(path),
        "impact_state_records": present_records,
        "first_active": first_active,
        "last_active": last_active,
        "max_occupied": max_occupied,
        "max_buffer_len": max_buffer_len,
        "first_hash": first_hash,
        "first": first_impact,
        "first_sample": first_sample,
        "first_world_active": first_world_active,
        "first_world_sample": first_world_sample,
    }


def impact_numeric_list(value: Any, length: int) -> list[float] | None:
    if (
        not isinstance(value, list)
        or len(value) != length
        or any(not isinstance(item, (int, float)) for item in value)
    ):
        return None
    return [float(item) for item in value]


def impact_sample_point(
    sample: dict[str, Any] | None,
    key: str,
    *,
    use_world: bool = False,
) -> list[float] | None:
    if not isinstance(sample, dict):
        return None
    if key == "center":
        return impact_numeric_list(sample.get("world_center" if use_world else "center"), 3)
    if key.startswith("v"):
        try:
            index = int(key[1:])
        except ValueError:
            return None
        vertices = sample.get("world_v" if use_world else "v")
        if not isinstance(vertices, list) or index < 0 or index >= len(vertices):
            return None
        return impact_numeric_list(vertices[index], 3)
    return None


def compare_impact_state(
    baseline: dict[str, Any],
    test: dict[str, Any],
    require_active: bool,
    require_match: bool,
    position_tolerance: float | None,
    position_points: list[str],
) -> dict[str, Any]:
    failures: list[str] = []
    field_results: dict[str, Any] = {}
    position_results: dict[str, Any] = {}
    position_space = "local"
    baseline_sample = baseline.get("first_sample")
    test_sample = test.get("first_sample")
    baseline_world_sample = baseline.get("first_world_sample")
    test_world_sample = test.get("first_world_sample")
    have_world_samples = isinstance(baseline_world_sample, dict) and isinstance(
        test_world_sample,
        dict,
    )
    baseline_compare_sample = (
        baseline_world_sample if have_world_samples
        else baseline_sample
    )
    test_compare_sample = (
        test_world_sample if have_world_samples
        else test_sample
    )
    sample_selector = "first_world_sample" if have_world_samples else "first_sample"

    if require_active:
        for side, summary in (("baseline", baseline), ("test", test)):
            if summary.get("impact_state_records", 0) == 0:
                failures.append(f"{side}: no impact_state telemetry records")
            if summary.get("max_occupied", 0) <= 0:
                failures.append(f"{side}: no occupied bullet-impact state")

    if require_match:
        if not isinstance(baseline_compare_sample, dict):
            failures.append(f"baseline: missing {sample_selector} bullet-impact sample")
        if not isinstance(test_compare_sample, dict):
            failures.append(f"test: missing {sample_selector} bullet-impact sample")
        if isinstance(baseline_compare_sample, dict) and isinstance(test_compare_sample, dict):
            for field in ("impact", "room", "model_pos", "clear", "prop"):
                baseline_value = baseline_compare_sample.get(field)
                test_value = test_compare_sample.get(field)
                match = baseline_value == test_value
                field_results[field] = {
                    "baseline": baseline_value,
                    "test": test_value,
                    "match": match,
                }
                if not match:
                    failures.append(
                        f"impact {sample_selector} field {field} mismatch: "
                        f"{baseline_value!r} != {test_value!r}"
                    )

    if position_tolerance is not None:
        if not isinstance(baseline_compare_sample, dict):
            failures.append(f"baseline: missing {sample_selector} bullet-impact sample")
        if not isinstance(test_compare_sample, dict):
            failures.append(f"test: missing {sample_selector} bullet-impact sample")
        if isinstance(baseline_compare_sample, dict) and isinstance(test_compare_sample, dict):
            use_world = bool(baseline_compare_sample.get("world")) and bool(test_compare_sample.get("world"))
            position_space = "world" if use_world else "local"
            for key in position_points:
                baseline_pos = impact_sample_point(baseline_compare_sample, key, use_world=use_world)
                test_pos = impact_sample_point(test_compare_sample, key, use_world=use_world)
                if baseline_pos is None or test_pos is None:
                    failures.append(f"impact {sample_selector} missing {position_space} {key} point")
                    continue
                delta = distance3(baseline_pos, test_pos)
                position_results[key] = delta
                if delta > position_tolerance:
                    failures.append(
                        f"impact {sample_selector} {position_space} {key} delta {delta:.3f} exceeds "
                        f"tolerance {position_tolerance:.3f}"
                    )

    return {
        "status": "fail" if failures else "pass",
        "require_active": require_active,
        "require_match": require_match,
        "position_tolerance": position_tolerance,
        "position_points": position_points,
        "position_space": position_space,
        "sample_selector": sample_selector,
        "fields": field_results,
        "position_deltas": position_results,
        "failures": failures,
    }


def compare(args: argparse.Namespace) -> dict[str, Any]:
    baseline_path = Path(args.baseline)
    test_path = Path(args.test)
    baseline_records = load_records(baseline_path)
    test_records = load_records(test_path)
    baseline = glass_summary(baseline_records, baseline_path)
    test = glass_summary(test_records, test_path)
    baseline_props = glass_props_summary(baseline_records, baseline_path)
    test_props = glass_props_summary(test_records, test_path)
    baseline_impact = impact_state_summary(baseline_records, baseline_path)
    test_impact = impact_state_summary(test_records, test_path)
    failures: list[str] = []
    actor_results: list[dict[str, Any]] = []

    for side, summary in (("baseline", baseline), ("test", test)):
        if summary["records"] == 0:
            failures.append(f"{side}: no records")
        if summary["glass_records"] == 0:
            failures.append(f"{side}: no glass telemetry records")
        if args.require_active and summary["first_active"] is None:
            failures.append(f"{side}: no active glass shard frames")
        if summary["max_buffer_len"] > args.max_buffer_len:
            failures.append(
                f"{side}: buffer_len {summary['max_buffer_len']} exceeds {args.max_buffer_len}"
            )

    first_delta = None
    max_active_delta = None
    first_position_delta = None
    prop_position_delta = None
    rng_pre_seed_match = None
    rng_first_seed_match = None
    rng_draw_delta = None
    first_sample_comparison = compare_first_samples(baseline, test)
    impact_comparison = compare_impact_state(
        baseline_impact,
        test_impact,
        args.require_impact_active,
        args.require_impact_match,
        args.impact_position_tolerance,
        args.impact_position_points,
    )
    failures.extend(impact_comparison["failures"])
    if baseline["first_active"] is not None and test["first_active"] is not None:
        first_delta = int(test["first_active"]) - int(baseline["first_active"])
        if (
            args.first_active_tolerance is not None
            and abs(first_delta) > args.first_active_tolerance
        ):
            failures.append(
                "first active frame delta "
                f"{first_delta} exceeds tolerance {args.first_active_tolerance}"
            )
    if baseline["max_active"] or test["max_active"]:
        max_active_delta = int(test["max_active"]) - int(baseline["max_active"])
        if abs(max_active_delta) > args.max_active_tolerance:
            failures.append(
                f"max active shard delta {max_active_delta} exceeds tolerance "
                f"{args.max_active_tolerance}"
            )
    if args.first_position_tolerance is not None:
        if not baseline["first_pos"] or not test["first_pos"]:
            failures.append("missing first active shard position for required position match")
        else:
            first_position_delta = distance3(baseline["first_pos"], test["first_pos"])
            if first_position_delta > args.first_position_tolerance:
                failures.append(
                    "first active shard position delta "
                    f"{first_position_delta:.3f} exceeds tolerance "
                    f"{args.first_position_tolerance}"
                )
    if args.require_hash_match:
        if not baseline["first_hash"] or not test["first_hash"]:
            failures.append("missing first active hash for required hash match")
        elif baseline["first_hash"] != test["first_hash"]:
            failures.append(
                f"first active hash mismatch: {baseline['first_hash']} != {test['first_hash']}"
            )
    if args.first_sample_tolerance is not None:
        if first_sample_comparison is None:
            failures.append("missing first active shard sample for required sample match")
        else:
            max_delta = first_sample_comparison.get("max_numeric_delta")
            if first_sample_comparison.get("mismatch_count", 0):
                failures.append(
                    "first active sample structural/value mismatch: "
                    f"{first_sample_comparison['mismatch_count']} field(s)"
                )
            if max_delta is None:
                failures.append("missing first active sample numeric delta")
            elif max_delta > args.first_sample_tolerance:
                failures.append(
                    "first active sample numeric delta "
                    f"{max_delta:.3f} exceeds tolerance {args.first_sample_tolerance}"
                )
    baseline_pre_rng = baseline.get("first_pre_rng")
    test_pre_rng = test.get("first_pre_rng")
    baseline_rng = baseline.get("first_rng")
    test_rng = test.get("first_rng")
    if isinstance(baseline_pre_rng, dict) and isinstance(test_pre_rng, dict):
        rng_pre_seed_match = baseline_pre_rng.get("seed") == test_pre_rng.get("seed")
    if isinstance(baseline_rng, dict) and isinstance(test_rng, dict):
        rng_first_seed_match = baseline_rng.get("seed") == test_rng.get("seed")
    if baseline.get("first_rng_draws") is not None and test.get("first_rng_draws") is not None:
        rng_draw_delta = int(test["first_rng_draws"]) - int(baseline["first_rng_draws"])
    if args.require_prop_destroyed:
        for side, summary in (("baseline", baseline_props), ("test", test_props)):
            if summary["glass_prop_records"] == 0:
                failures.append(f"{side}: no glass prop lifecycle telemetry records")
            if summary["max_destroyed"] <= 0:
                failures.append(f"{side}: no destroyed glass prop lifecycle state")
            if summary["max_remove"] <= 0:
                failures.append(f"{side}: no remove-pending glass prop lifecycle state")
    if args.prop_position_tolerance is not None:
        if not baseline_props["first_break_pos"] or not test_props["first_break_pos"]:
            failures.append("missing first broken glass prop position for required position match")
        else:
            prop_position_delta = distance3(
                baseline_props["first_break_pos"],
                test_props["first_break_pos"],
            )
            if prop_position_delta > args.prop_position_tolerance:
                failures.append(
                    "first broken glass prop position delta "
                    f"{prop_position_delta:.3f} exceeds tolerance "
                    f"{args.prop_position_tolerance}"
                )

    actor_fields = parse_actor_fields(args.actor_fields)
    for chrnum in args.require_actor_match:
        result = compare_actor_state(
            baseline_records,
            test_records,
            baseline,
            test,
            chrnum,
            actor_fields,
            args.actor_frame,
            args.actor_position_tolerance,
            args.actor_baseline_frame,
            args.actor_test_frame,
            args.actor_baseline_global,
            args.actor_test_global,
        )
        actor_results.append(result)
        failures.extend(result["failures"])

    return {
        "status": "fail" if failures else "pass",
        "failures": failures,
        "first_active_delta": first_delta,
        "max_active_delta": max_active_delta,
        "first_position_delta": first_position_delta,
        "prop_position_delta": prop_position_delta,
        "rng_pre_seed_match": rng_pre_seed_match,
        "rng_first_seed_match": rng_first_seed_match,
        "rng_draw_delta": rng_draw_delta,
        "first_sample": first_sample_comparison,
        "baseline": baseline,
        "test": test,
        "baseline_props": baseline_props,
        "test_props": test_props,
        "baseline_impact": baseline_impact,
        "test_impact": test_impact,
        "impact": impact_comparison,
        "actors": actor_results,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("baseline", help="baseline JSONL trace, usually stock ares")
    parser.add_argument("test", help="test JSONL trace, usually native")
    parser.add_argument("--json-out", help="write comparison metrics as JSON")
    parser.add_argument("--require-active", action="store_true", help="fail unless shards become active")
    parser.add_argument(
        "--require-hash-match",
        action="store_true",
        help="require the first active glass state hash to match exactly",
    )
    parser.add_argument(
        "--require-sample-match",
        action="store_const",
        const=0.0,
        dest="first_sample_tolerance",
        help="require the first active sampled shard fields to match exactly",
    )
    parser.add_argument(
        "--first-sample-tolerance",
        type=float,
        default=None,
        help="optional tolerance for numeric fields in the first active shard sample",
    )
    parser.add_argument(
        "--first-active-tolerance",
        type=int,
        default=None,
        help="optional absolute-frame tolerance for the first active shard record",
    )
    parser.add_argument("--max-active-tolerance", type=int, default=0)
    parser.add_argument(
        "--first-position-tolerance",
        type=float,
        default=None,
        help="optional world-space tolerance for the first active shard sample position",
    )
    parser.add_argument(
        "--require-prop-destroyed",
        action="store_true",
        help="fail unless a glass prop reaches destroyed and remove-pending lifecycle state",
    )
    parser.add_argument(
        "--require-impact-active",
        action="store_true",
        help="fail unless both traces report occupied bullet-impact state",
    )
    parser.add_argument(
        "--require-impact-match",
        action="store_true",
        help="require first occupied bullet-impact type/room/model/clear/prop fields to match",
    )
    parser.add_argument(
        "--impact-position-tolerance",
        type=float,
        default=None,
        help="optional world-space tolerance for first occupied bullet-impact center and vertices",
    )
    parser.add_argument(
        "--impact-position-points",
        type=parse_impact_position_points,
        default=["center", "v0", "v1", "v2", "v3"],
        help="comma-separated impact points checked by --impact-position-tolerance",
    )
    parser.add_argument(
        "--prop-position-tolerance",
        type=float,
        default=None,
        help="optional world-space tolerance for the first broken glass prop position",
    )
    parser.add_argument("--max-buffer-len", type=int, default=512)
    parser.add_argument(
        "--require-actor-match",
        type=int,
        action="append",
        default=[],
        metavar="CHRNUM",
        help="require sampled actor state for CHRNUM to match between stock/native",
    )
    parser.add_argument(
        "--actor-fields",
        default=None,
        help=(
            "comma-separated actor fields to compare when --require-actor-match is used "
            "(default: alive,hidden,hidden_bits,onscreen,rendered)"
        ),
    )
    parser.add_argument(
        "--actor-frame",
        choices=("first-active", "last-active", "screenshot"),
        default="first-active",
        help=(
            "which frame to use for actor-state comparison; screenshot requires "
            "explicit --actor-baseline-frame/--actor-test-frame or global timers"
        ),
    )
    parser.add_argument(
        "--actor-baseline-frame",
        type=int,
        default=None,
        help="explicit baseline trace frame for actor-state comparison",
    )
    parser.add_argument(
        "--actor-test-frame",
        type=int,
        default=None,
        help="explicit test trace frame for actor-state comparison",
    )
    parser.add_argument(
        "--actor-baseline-global",
        type=int,
        default=None,
        help="explicit baseline move.global timer for actor-state comparison",
    )
    parser.add_argument(
        "--actor-test-global",
        type=int,
        default=None,
        help="explicit test move.global timer for actor-state comparison",
    )
    parser.add_argument(
        "--actor-position-tolerance",
        type=float,
        default=None,
        help="optional world-space tolerance for sampled actor position",
    )
    args = parser.parse_args()
    if args.first_active_tolerance is not None and args.first_active_tolerance < 0:
        parser.error("--first-active-tolerance must be non-negative")
    if args.max_active_tolerance < 0:
        parser.error("--max-active-tolerance must be non-negative")
    if args.first_position_tolerance is not None and args.first_position_tolerance < 0.0:
        parser.error("--first-position-tolerance must be non-negative")
    if args.prop_position_tolerance is not None and args.prop_position_tolerance < 0.0:
        parser.error("--prop-position-tolerance must be non-negative")
    if args.impact_position_tolerance is not None and args.impact_position_tolerance < 0.0:
        parser.error("--impact-position-tolerance must be non-negative")
    if args.first_sample_tolerance is not None and args.first_sample_tolerance < 0.0:
        parser.error("--first-sample-tolerance must be non-negative")
    if args.max_buffer_len < 1:
        parser.error("--max-buffer-len must be positive")
    if args.actor_position_tolerance is not None and args.actor_position_tolerance < 0.0:
        parser.error("--actor-position-tolerance must be non-negative")
    for arg_name in (
        "actor_baseline_frame",
        "actor_test_frame",
        "actor_baseline_global",
        "actor_test_global",
    ):
        value = getattr(args, arg_name)
        if value is not None and value < 0:
            parser.error(f"--{arg_name.replace('_', '-')} must be non-negative")
    if args.actor_baseline_frame is not None and args.actor_baseline_global is not None:
        parser.error("--actor-baseline-frame and --actor-baseline-global are mutually exclusive")
    if args.actor_test_frame is not None and args.actor_test_global is not None:
        parser.error("--actor-test-frame and --actor-test-global are mutually exclusive")
    if args.actor_frame == "screenshot" and args.require_actor_match:
        if args.actor_baseline_frame is None and args.actor_baseline_global is None:
            parser.error("--actor-frame screenshot requires --actor-baseline-frame or --actor-baseline-global")
        if args.actor_test_frame is None and args.actor_test_global is None:
            parser.error("--actor-frame screenshot requires --actor-test-frame or --actor-test-global")

    metrics = compare(args)
    if args.json_out:
        Path(args.json_out).write_text(
            json.dumps(metrics, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )

    if metrics["status"] != "pass":
        print("FAIL: glass trace comparison")
        for failure in metrics["failures"]:
            print(f"  - {failure}")
        return 1

    print("PASS: glass trace comparison")
    print(
        "  first_active: "
        f"{metrics['baseline']['first_active']} -> {metrics['test']['first_active']} "
        f"(delta={metrics['first_active_delta']})"
    )
    print(
        "  max_active: "
        f"{metrics['baseline']['max_active']} -> {metrics['test']['max_active']} "
        f"(delta={metrics['max_active_delta']})"
    )
    print(
        "  first_shard_timer: "
        f"{metrics['baseline']['first_timer']} -> {metrics['test']['first_timer']} "
        f"rot_y={metrics['baseline']['first_rot_y']}->{metrics['test']['first_rot_y']}"
    )
    print(
        "  last_shard_timer: "
        f"{metrics['baseline']['last_timer']} -> {metrics['test']['last_timer']} "
        f"rot_y={metrics['baseline']['last_rot_y']}->{metrics['test']['last_rot_y']}"
    )
    if metrics["first_position_delta"] is not None:
        print(f"  first_position_delta: {metrics['first_position_delta']:.3f}")
    for label, summary in (("baseline", metrics["baseline"]), ("test", metrics["test"])):
        pre_rng = summary.get("first_pre_rng")
        first_rng = summary.get("first_rng")
        if isinstance(pre_rng, dict) and isinstance(first_rng, dict):
            draws = summary.get("first_rng_draws")
            draw_text = "unknown" if draws is None else str(draws)
            print(
                f"  {label}_rng: pre={pre_rng.get('seed')} "
                f"first={first_rng.get('seed')} draws={draw_text}"
            )
    if metrics["rng_pre_seed_match"] is not None:
        print(f"  rng_pre_seed_match: {metrics['rng_pre_seed_match']}")
    if metrics["rng_first_seed_match"] is not None:
        print(f"  rng_first_seed_match: {metrics['rng_first_seed_match']}")
    if metrics["rng_draw_delta"] is not None:
        print(f"  rng_draw_delta: {metrics['rng_draw_delta']}")
    if metrics["first_sample"] is not None:
        sample = metrics["first_sample"]
        max_delta = sample.get("max_numeric_delta")
        if max_delta is None:
            delta_text = "unknown"
        else:
            delta_text = f"{max_delta:.3f}"
        print(
            "  first_sample_match: "
            f"{sample.get('match')} max_delta={delta_text} "
            f"mismatches={sample.get('mismatch_count')}"
        )
    if (
        metrics["baseline_props"]["glass_prop_records"] > 0
        or metrics["test_props"]["glass_prop_records"] > 0
    ):
        print(
            "  glass_props: "
            f"destroyed {metrics['baseline_props']['max_destroyed']} -> "
            f"{metrics['test_props']['max_destroyed']}, "
            f"remove {metrics['baseline_props']['max_remove']} -> "
            f"{metrics['test_props']['max_remove']}"
        )
    if metrics["prop_position_delta"] is not None:
        print(f"  prop_position_delta: {metrics['prop_position_delta']:.3f}")
    if (
        metrics["baseline_impact"]["impact_state_records"] > 0
        or metrics["test_impact"]["impact_state_records"] > 0
    ):
        print(
            "  impact_state: "
            f"occupied {metrics['baseline_impact']['max_occupied']} -> "
            f"{metrics['test_impact']['max_occupied']}, "
            f"first {metrics['baseline_impact']['first_active']} -> "
            f"{metrics['test_impact']['first_active']}, "
            f"first_world {metrics['baseline_impact']['first_world_active']} -> "
            f"{metrics['test_impact']['first_world_active']}"
        )
        impact_position_deltas = metrics["impact"].get("position_deltas", {})
        if impact_position_deltas:
            center_delta = impact_position_deltas.get("center")
            if center_delta is not None:
                position_space = metrics["impact"].get("position_space", "local")
                print(f"  impact_center_delta: {center_delta:.3f} ({position_space})")
    for actor in metrics["actors"]:
        fields = actor.get("fields", {})
        field_text = ", ".join(
            f"{name}={values.get('baseline')!r}->{values.get('test')!r}"
            for name, values in fields.items()
        )
        print(
            "  actor_match: "
            f"chr={actor.get('chrnum')} frame={actor.get('baseline_frame')}->"
            f"{actor.get('test_frame')} {field_text}"
        )
        if actor.get("position_delta") is not None:
            print(f"    actor_position_delta: {actor['position_delta']:.3f}")
            components = actor.get("position_delta_components")
            if isinstance(components, dict):
                print(
                    "    actor_position_components: "
                    f"dx={components.get('dx', 0.0):.3f} "
                    f"dy={components.get('dy', 0.0):.3f} "
                    f"dz={components.get('dz', 0.0):.3f} "
                    f"xz={components.get('xz', 0.0):.3f}"
                )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
