#!/usr/bin/env python3
"""Summarize and compare level-intro camera/setup/Bond animation traces."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any


CAMERA_MODES_BY_NAME = {
    "none": 0,
    "intro": 1,
    "fadeswirl": 2,
    "swirl": 3,
    "fp": 4,
    "death_sp": 5,
    "death_mp": 6,
    "posend": 7,
    "fp_noinput": 8,
    "mp": 9,
    "fade_to_title": 10,
}
CAMERA_MODE_NAMES = {value: key for key, value in CAMERA_MODES_BY_NAME.items()}

PATH_FIELDS = (
    ("cam_pos", "vector"),
    ("cam_target", "vector"),
    ("cam_up", "direction"),
    ("facing", "direction"),
)
STATE_FIELDS = (
    ("cam", "exact"),
    ("cam_after", "exact"),
    ("icam", "exact"),
    ("p_unk", "exact"),
    ("intro.frozen", "exact"),
)
SELECTED_CAMERA_FIELDS = (
    ("intro.selected_camera.present", "exact"),
    ("intro.selected_camera.index", "exact"),
    ("intro.selected_camera.count", "exact"),
    ("intro.selected_camera.pos", "vector"),
    ("intro.selected_camera.yaw", "scalar"),
    ("intro.selected_camera.pitch", "scalar"),
    ("intro.selected_camera.pad", "exact"),
)
SETUP_FIELDS = (
    ("intro.setup.anim_index", "exact"),
    ("intro.setup.swirl.present", "exact"),
    ("intro.setup.swirl.count", "exact"),
    ("intro.setup.swirl.hash", "exact"),
    ("intro.setup.swirl.current.index", "exact"),
    ("intro.setup.swirl.current.flags", "exact"),
    ("intro.setup.swirl.current.pos", "vector"),
    ("intro.setup.swirl.current.curve", "scalar"),
    ("intro.setup.swirl.current.duration", "scalar"),
    ("intro.setup.swirl.current.pad", "exact"),
)
BOND_ANIM_FIELDS = (
    ("intro.bond_present", "exact"),
    ("intro.bond_action", "exact"),
    ("intro.bond_rendered", "exact"),
    ("intro.bond_anim.valid", "exact"),
    ("intro.bond_anim.frames", "exact"),
    ("intro.bond_anim.hash", "exact"),
    ("intro.bond_anim.entry_offset", "exact"),
    ("intro.bond_anim.bits_offset", "exact"),
    ("intro.bond_anim.frame", "anim"),
    ("intro.bond_anim.end", "scalar"),
    ("intro.bond_anim.speed", "scalar"),
    ("intro.bond_anim.abs_speed", "scalar"),
    ("intro.bond_anim.looping", "exact"),
    ("intro.bond_anim.gunhand", "exact"),
    ("intro.bond_held.right.item", "exact"),
)

SELECTED_CAMERA_COMPARE_FIELDS = (
    ("intro.selected_camera.present", "exact"),
    ("intro.selected_camera.pos", "vector"),
    ("intro.selected_camera.yaw", "scalar"),
    ("intro.selected_camera.pitch", "scalar"),
    ("intro.selected_camera.pad", "exact"),
)

BOND_ANIM_COMPARE_FIELDS = (
    ("intro.bond_present", "exact"),
    ("intro.bond_action", "exact"),
    ("intro.bond_anim.valid", "exact"),
    ("intro.bond_anim.frames", "exact"),
    ("intro.bond_anim.hash", "exact"),
    ("intro.bond_anim.entry_offset", "exact"),
    ("intro.bond_anim.bits_offset", "exact"),
    ("intro.bond_anim.frame", "anim"),
    ("intro.bond_anim.end", "scalar"),
    ("intro.bond_anim.speed", "scalar"),
    ("intro.bond_anim.abs_speed", "scalar"),
    ("intro.bond_anim.looping", "exact"),
    ("intro.bond_anim.gunhand", "exact"),
)


@dataclass
class Summary:
    path: Path
    label: str
    records: int = 0
    active_records: int = 0
    first_frame: int | None = None
    last_frame: int | None = None
    first_active_frame: int | None = None
    last_active_frame: int | None = None
    min_intro_timer: float | None = None
    max_intro_timer: float | None = None
    mode_counts: dict[int, int] = field(default_factory=dict)
    timer_duplicates: int = 0
    unique_timers: int = 0
    setup_digest: str = ""
    selected_camera_digest: str = ""
    path_digest: str = ""
    bond_anim_digest: str = ""
    active_digest: str = ""
    bond_present: int = 0
    bond_rendered: int = 0
    bond_anim: int = 0
    first_bond_present_frame: int | None = None
    first_bond_rendered_frame: int | None = None
    min_bond_anim_frame: float | None = None
    max_bond_anim_frame: float | None = None
    action_counts: dict[int, int] = field(default_factory=dict)
    right_item_counts: dict[int, int] = field(default_factory=dict)
    timer_samples: dict[str, dict[str, Any]] = field(default_factory=dict)
    timer_order: list[str] = field(default_factory=list)


def parse_int(value: Any) -> int | None:
    if isinstance(value, bool):
        return int(value)
    if isinstance(value, int):
        return value
    if isinstance(value, float) and value.is_integer():
        return int(value)
    if isinstance(value, str):
        text = value.strip()
        if not text:
            return None
        try:
            return int(text, 0)
        except ValueError:
            return None
    return None


def parse_float(value: Any) -> float | None:
    if isinstance(value, bool):
        return float(int(value))
    if isinstance(value, (int, float)):
        result = float(value)
        return result if math.isfinite(result) else None
    if isinstance(value, str):
        try:
            result = float(value.strip())
        except ValueError:
            return None
        return result if math.isfinite(result) else None
    return None


def get_path(record: dict[str, Any], path: str) -> Any:
    value: Any = record
    for part in path.split("."):
        if not isinstance(value, dict):
            break
        value = value.get(part)
    else:
        if value is not None:
            return value

    if path == "intro.frozen":
        p_unk = parse_int(record.get("p_unk"))
        if p_unk is not None:
            return 1 if p_unk == 1 else 0
    return value


def parse_vector(value: Any) -> tuple[float, float, float] | None:
    if not isinstance(value, list) or len(value) != 3:
        return None
    parsed = [parse_float(item) for item in value]
    if any(item is None for item in parsed):
        return None
    return (parsed[0], parsed[1], parsed[2])  # type: ignore[index]


def rounded_float(value: float | None, digits: int) -> float | None:
    if value is None:
        return None
    return round(value, digits)


def normalized_value(value: Any, kind: str, digits: int) -> Any:
    if kind in ("vector", "direction"):
        vector = parse_vector(value)
        if vector is None:
            return None
        return [round(component, digits) for component in vector]
    if kind in ("scalar", "anim"):
        return rounded_float(parse_float(value), digits)
    if kind == "exact":
        parsed_int = parse_int(value)
        if parsed_int is not None and not isinstance(value, str):
            return parsed_int
        return value
    raise AssertionError(kind)


def timer_key(record: dict[str, Any], timer_digits: int, integer_only: bool) -> str | None:
    if integer_only:
        timer = parse_int(get_path(record, "intro.timer"))
        return str(timer) if timer is not None else None

    timer = parse_float(get_path(record, "intro.timer"))
    if timer is None:
        return None
    rounded = round(timer, timer_digits)
    if rounded.is_integer():
        return str(int(rounded))
    return f"{rounded:.{timer_digits}f}".rstrip("0").rstrip(".")


def stable_digest(value: Any) -> str:
    payload = json.dumps(value, sort_keys=True, separators=(",", ":"))
    return hashlib.sha256(payload.encode("utf-8")).hexdigest()[:16]


def parse_modes(spec: str) -> set[int]:
    modes: set[int] = set()
    for item in spec.split(","):
        key = item.strip().lower().replace("-", "_")
        if not key:
            continue
        if key in CAMERA_MODES_BY_NAME:
            modes.add(CAMERA_MODES_BY_NAME[key])
            continue
        try:
            modes.add(int(key, 0))
        except ValueError:
            raise argparse.ArgumentTypeError(f"unknown camera mode {item!r}") from None
    if not modes:
        raise argparse.ArgumentTypeError("at least one camera mode is required")
    return modes


def load_jsonl(path: Path) -> list[dict[str, Any]]:
    records: list[dict[str, Any]] = []
    skipped = 0
    with path.open("r", encoding="utf-8", errors="replace") as handle:
        for line_no, line in enumerate(handle, start=1):
            line = line.strip()
            if not line:
                continue
            if not line.startswith("{"):
                skipped += 1
                continue
            try:
                record = json.loads(line)
            except json.JSONDecodeError as exc:
                raise SystemExit(f"FAIL: invalid JSON in {path}:{line_no}: {exc}") from None
            if isinstance(record, dict):
                records.append(record)
    if skipped:
        print(f"WARNING: skipped {skipped} non-JSON line(s) in {path}", file=sys.stderr)
    return records


def record_active(
    record: dict[str, Any],
    modes: set[int],
    require_player: bool,
    require_frozen: bool,
    start_intro_timer: float | None,
    end_intro_timer: float | None,
) -> bool:
    cam = parse_int(record.get("cam"))
    if cam not in modes:
        return False
    if require_player and parse_int(record.get("p")) != 1:
        return False
    if require_frozen and parse_int(get_path(record, "intro.frozen")) != 1:
        return False
    intro_timer = parse_float(get_path(record, "intro.timer"))
    if start_intro_timer is not None and (intro_timer is None or intro_timer < start_intro_timer):
        return False
    if end_intro_timer is not None and (intro_timer is None or intro_timer > end_intro_timer):
        return False
    return True


def collect_fields(record: dict[str, Any], specs: tuple[tuple[str, str], ...], digits: int) -> dict[str, Any]:
    return {field: normalized_value(get_path(record, field), kind, digits) for field, kind in specs}


def sample_record(record: dict[str, Any], digits: int) -> dict[str, Any]:
    return {
        "timer": normalized_value(get_path(record, "intro.timer"), "scalar", digits),
        "state": collect_fields(record, STATE_FIELDS, digits),
        "selected_camera": collect_fields(record, SELECTED_CAMERA_FIELDS, digits),
        "setup": collect_fields(record, SETUP_FIELDS, digits),
        "path": collect_fields(record, PATH_FIELDS, digits),
        "bond_anim": collect_fields(record, BOND_ANIM_FIELDS, digits),
    }


def update_range(current_min: float | None, current_max: float | None, value: float) -> tuple[float, float]:
    if current_min is None or current_max is None:
        return value, value
    return min(current_min, value), max(current_max, value)


def summarize_trace(
    path: Path,
    *,
    label: str | None,
    modes: set[int],
    require_player: bool,
    require_frozen: bool,
    start_intro_timer: float | None,
    end_intro_timer: float | None,
    timer_digits: int,
    integer_timer_keys: bool,
    value_digits: int,
) -> Summary:
    records = load_jsonl(path)
    summary = Summary(path=path, label=label or path.stem, records=len(records))
    active_samples: list[dict[str, Any]] = []
    setup_samples: list[dict[str, Any]] = []
    selected_camera_samples: list[dict[str, Any]] = []
    path_samples: list[dict[str, Any]] = []
    bond_samples: list[dict[str, Any]] = []

    for record in records:
        frame = parse_int(record.get("f"))
        if frame is not None:
            if summary.first_frame is None:
                summary.first_frame = frame
            summary.last_frame = frame

        cam = parse_int(record.get("cam"))
        if cam is not None:
            summary.mode_counts[cam] = summary.mode_counts.get(cam, 0) + 1

        if not record_active(
            record,
            modes,
            require_player,
            require_frozen,
            start_intro_timer,
            end_intro_timer,
        ):
            continue

        key = timer_key(record, timer_digits, integer_timer_keys)
        if key is None:
            continue

        sample = sample_record(record, value_digits)
        summary.active_records += 1
        if summary.first_active_frame is None:
            summary.first_active_frame = frame
        summary.last_active_frame = frame

        intro_timer = parse_float(get_path(record, "intro.timer"))
        if intro_timer is not None:
            summary.min_intro_timer, summary.max_intro_timer = update_range(
                summary.min_intro_timer,
                summary.max_intro_timer,
                intro_timer,
            )

        if key in summary.timer_samples:
            summary.timer_duplicates += 1
        else:
            summary.timer_order.append(key)
        summary.timer_samples[key] = sample

        active_samples.append(sample)
        setup_samples.append(sample["setup"])
        selected_camera_samples.append(sample["selected_camera"])
        path_samples.append({"timer": sample["timer"], **sample["path"]})
        bond_samples.append({"timer": sample["timer"], **sample["bond_anim"]})

        if parse_int(get_path(record, "intro.bond_present")):
            summary.bond_present += 1
            if summary.first_bond_present_frame is None:
                summary.first_bond_present_frame = frame
        if parse_int(get_path(record, "intro.bond_rendered")):
            summary.bond_rendered += 1
            if summary.first_bond_rendered_frame is None:
                summary.first_bond_rendered_frame = frame
        if parse_int(get_path(record, "intro.bond_anim.valid")):
            summary.bond_anim += 1
            anim_frame = parse_float(get_path(record, "intro.bond_anim.frame"))
            if anim_frame is not None:
                summary.min_bond_anim_frame, summary.max_bond_anim_frame = update_range(
                    summary.min_bond_anim_frame,
                    summary.max_bond_anim_frame,
                    anim_frame,
                )

        action = parse_int(get_path(record, "intro.bond_action"))
        if action is not None:
            summary.action_counts[action] = summary.action_counts.get(action, 0) + 1
        right_item = parse_int(get_path(record, "intro.bond_held.right.item"))
        if right_item is not None:
            summary.right_item_counts[right_item] = summary.right_item_counts.get(right_item, 0) + 1

    summary.unique_timers = len(summary.timer_samples)
    summary.setup_digest = stable_digest(setup_samples)
    summary.selected_camera_digest = stable_digest(selected_camera_samples)
    summary.path_digest = stable_digest(path_samples)
    summary.bond_anim_digest = stable_digest(bond_samples)
    summary.active_digest = stable_digest(active_samples)
    return summary


def counts_text(counts: dict[int, int], names: dict[int, str] | None = None) -> str:
    if not counts:
        return "none"
    parts = []
    for key, count in sorted(counts.items()):
        label = names.get(key, str(key)) if names else str(key)
        parts.append(f"{label}:{count}")
    return ", ".join(parts)


def range_text(min_value: float | None, max_value: float | None) -> str:
    if min_value is None or max_value is None:
        return "none"
    return f"{min_value:.2f}..{max_value:.2f}"


def frame_text(frame: int | None) -> str:
    return "?" if frame is None else str(frame)


def summary_dict(summary: Summary) -> dict[str, Any]:
    return {
        "label": summary.label,
        "path": str(summary.path),
        "records": summary.records,
        "active_records": summary.active_records,
        "frames": [summary.first_frame, summary.last_frame],
        "active_frames": [summary.first_active_frame, summary.last_active_frame],
        "intro_timer": [summary.min_intro_timer, summary.max_intro_timer],
        "mode_counts": {CAMERA_MODE_NAMES.get(key, str(key)): value for key, value in sorted(summary.mode_counts.items())},
        "unique_timers": summary.unique_timers,
        "timer_duplicates": summary.timer_duplicates,
        "digests": {
            "setup": summary.setup_digest,
            "selected_camera": summary.selected_camera_digest,
            "path": summary.path_digest,
            "bond_anim": summary.bond_anim_digest,
            "active": summary.active_digest,
        },
        "bond": {
            "present": summary.bond_present,
            "rendered": summary.bond_rendered,
            "anim": summary.bond_anim,
            "first_present_frame": summary.first_bond_present_frame,
            "first_rendered_frame": summary.first_bond_rendered_frame,
            "anim_frame": [summary.min_bond_anim_frame, summary.max_bond_anim_frame],
            "action_counts": summary.action_counts,
            "right_item_counts": summary.right_item_counts,
        },
    }


def print_summary(summary: Summary) -> None:
    print(f"intro-summary: {summary.label}")
    print(
        "  "
        f"path={summary.path} records={summary.records} "
        f"frames={frame_text(summary.first_frame)}..{frame_text(summary.last_frame)}"
    )
    print(
        "  "
        f"active={summary.active_records} "
        f"active_frames={frame_text(summary.first_active_frame)}..{frame_text(summary.last_active_frame)} "
        f"intro_timer={range_text(summary.min_intro_timer, summary.max_intro_timer)} "
        f"unique_timers={summary.unique_timers} duplicate_timer_records={summary.timer_duplicates}"
    )
    print(f"  modes={counts_text(summary.mode_counts, CAMERA_MODE_NAMES)}")
    print(
        "  "
        f"digests setup={summary.setup_digest} selected_camera={summary.selected_camera_digest} "
        f"path={summary.path_digest} bond_anim={summary.bond_anim_digest} active={summary.active_digest}"
    )
    print(
        "  "
        f"bond present={summary.bond_present} rendered={summary.bond_rendered} anim={summary.bond_anim} "
        f"first_present={frame_text(summary.first_bond_present_frame)} "
        f"first_rendered={frame_text(summary.first_bond_rendered_frame)} "
        f"anim_frame={range_text(summary.min_bond_anim_frame, summary.max_bond_anim_frame)}"
    )
    print(
        "  "
        f"bond_actions={counts_text(summary.action_counts)} "
        f"right_items={counts_text(summary.right_item_counts)}"
    )


def compare_values(
    field: str,
    kind: str,
    baseline: Any,
    test: Any,
    *,
    key: str,
    vector_tolerance: float,
    direction_tolerance: float,
    scalar_tolerance: float,
    anim_tolerance: float,
) -> str | None:
    if kind in ("vector", "direction"):
        tolerance = direction_tolerance if kind == "direction" else vector_tolerance
        if not isinstance(baseline, list) or not isinstance(test, list) or len(baseline) != 3 or len(test) != 3:
            if baseline != test:
                return f"timer {key}: {field} baseline={baseline!r} test={test!r}"
            return None
        for index, (base_value, test_value) in enumerate(zip(baseline, test, strict=True)):
            delta = abs(float(base_value) - float(test_value))
            if delta > tolerance:
                return (
                    f"timer {key}: {field}[{index}] baseline={base_value:.5f} "
                    f"test={test_value:.5f} delta={delta:.5f} tolerance={tolerance:.5f}"
                )
        return None

    if kind in ("scalar", "anim"):
        tolerance = anim_tolerance if kind == "anim" else scalar_tolerance
        if baseline is None or test is None:
            if baseline != test:
                return f"timer {key}: {field} baseline={baseline!r} test={test!r}"
            return None
        delta = abs(float(baseline) - float(test))
        if delta > tolerance:
            return (
                f"timer {key}: {field} baseline={baseline:.5f} test={test:.5f} "
                f"delta={delta:.5f} tolerance={tolerance:.5f}"
            )
        return None

    if kind == "exact":
        if baseline != test:
            return f"timer {key}: {field} baseline={baseline!r} test={test!r}"
        return None

    raise AssertionError(kind)


def profile_specs(profile: set[str]) -> list[tuple[str, str, str]]:
    specs: list[tuple[str, str, str]] = []
    if "state" in profile:
        specs.extend(("state", field, kind) for field, kind in STATE_FIELDS)
    if "selected-camera" in profile:
        specs.extend(("selected_camera", field, kind) for field, kind in SELECTED_CAMERA_COMPARE_FIELDS)
    if "setup" in profile:
        specs.extend(("setup", field, kind) for field, kind in SETUP_FIELDS)
    if "path" in profile:
        specs.extend(("path", field, kind) for field, kind in PATH_FIELDS)
    if "bond-anim" in profile:
        specs.extend(("bond_anim", field, kind) for field, kind in BOND_ANIM_COMPARE_FIELDS)
    return specs


def display_field_name(section: str, field: str) -> str:
    if section == "selected_camera" and field.startswith("intro.selected_camera."):
        return f"{section}.{field.removeprefix('intro.selected_camera.')}"
    if section == "setup" and field.startswith("intro.setup."):
        return f"{section}.{field.removeprefix('intro.setup.')}"
    if section == "bond_anim" and field.startswith("intro."):
        suffix = field.removeprefix("intro.")
        suffix = suffix.removeprefix("bond_anim.")
        suffix = suffix.removeprefix("bond_held.right.")
        return f"{section}.{suffix}"
    return f"{section}.{field}"


def parse_profile(spec: str) -> set[str]:
    aliases = {
        "all": {"state", "selected-camera", "setup", "path", "bond-anim"},
        "camera": {"state", "selected-camera", "path"},
        "anim": {"bond-anim"},
    }
    result: set[str] = set()
    for item in spec.split(","):
        key = item.strip().lower().replace("_", "-")
        if not key:
            continue
        if key in aliases:
            result.update(aliases[key])
        elif key in {"state", "selected-camera", "setup", "path", "bond-anim"}:
            result.add(key)
        else:
            raise argparse.ArgumentTypeError(f"unknown compare profile entry {item!r}") from None
    if not result:
        raise argparse.ArgumentTypeError("at least one compare profile entry is required")
    return result


def compare_summaries(
    args: argparse.Namespace,
    baseline: Summary,
    test: Summary,
) -> tuple[int, dict[str, Any]]:
    baseline_keys = set(baseline.timer_samples)
    test_keys = set(test.timer_samples)
    matched = sorted(
        baseline_keys & test_keys,
        key=lambda value: parse_float(value) if parse_float(value) is not None else math.inf,
    )
    missing_test = sorted(baseline_keys - test_keys)
    missing_baseline = sorted(test_keys - baseline_keys)
    specs = profile_specs(args.compare_profile)
    divergences: list[str] = []

    if args.min_matched_timers and len(matched) < args.min_matched_timers:
        divergences.append(f"matched intro timers {len(matched)} < required {args.min_matched_timers}")

    if args.require_same_timer_set:
        for key in missing_test[: args.max_divergences]:
            divergences.append(f"timer {key}: present in baseline but missing in test")
        for key in missing_baseline[: args.max_divergences]:
            divergences.append(f"timer {key}: present in test but missing in baseline")

    for key in matched:
        baseline_sample = baseline.timer_samples[key]
        test_sample = test.timer_samples[key]
        for section, field, kind in specs:
            baseline_value = baseline_sample[section].get(field)
            test_value = test_sample[section].get(field)
            failure = compare_values(
                display_field_name(section, field),
                kind,
                baseline_value,
                test_value,
                key=key,
                vector_tolerance=args.vector_tolerance,
                direction_tolerance=args.direction_tolerance,
                scalar_tolerance=args.scalar_tolerance,
                anim_tolerance=args.anim_tolerance,
            )
            if failure is not None:
                divergences.append(failure)
                if len(divergences) >= args.max_divergences:
                    break
        if len(divergences) >= args.max_divergences:
            break

    print(
        f"intro-compare: baseline={baseline.label} test={test.label} "
        f"profile={','.join(sorted(args.compare_profile))}"
    )
    print(
        "  "
        f"baseline_active={baseline.active_records} test_active={test.active_records} "
        f"matched_timers={len(matched)} "
        f"baseline_only={len(missing_test)} test_only={len(missing_baseline)}"
    )
    print(
        "  "
        f"baseline_digests setup={baseline.setup_digest} selected_camera={baseline.selected_camera_digest} "
        f"path={baseline.path_digest} bond_anim={baseline.bond_anim_digest}"
    )
    print(
        "  "
        f"test_digests     setup={test.setup_digest} selected_camera={test.selected_camera_digest} "
        f"path={test.path_digest} bond_anim={test.bond_anim_digest}"
    )

    if divergences:
        print(f"FAIL: intro summary comparison found {len(divergences)} issue(s)")
        for divergence in divergences[: args.max_divergences]:
            print(f"  {divergence}")
        return 1, {
            "status": "fail",
            "profile": sorted(args.compare_profile),
            "matched_timers": len(matched),
            "baseline_only_timers": missing_test,
            "test_only_timers": missing_baseline,
            "divergence_count": len(divergences),
            "divergences": divergences,
        }

    print("PASS: intro summary comparison")
    return 0, {
        "status": "pass",
        "profile": sorted(args.compare_profile),
        "matched_timers": len(matched),
        "baseline_only_timers": missing_test,
        "test_only_timers": missing_baseline,
        "divergence_count": 0,
        "divergences": [],
    }


def parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("trace", nargs="*", help="JSONL trace(s) to summarize")
    p.add_argument("--baseline", help="baseline JSONL trace, usually stock ROM")
    p.add_argument("--test", help="test JSONL trace, usually native")
    p.add_argument("--label", action="append", default=[], help="label for positional summary traces")
    p.add_argument("--baseline-label", default="baseline")
    p.add_argument("--test-label", default="test")
    p.add_argument("--camera-modes", type=parse_modes, default=parse_modes("intro,fadeswirl,swirl"))
    p.add_argument("--no-require-player", action="store_true")
    p.add_argument("--require-frozen", action="store_true")
    p.add_argument("--start-intro-timer", type=float)
    p.add_argument("--end-intro-timer", type=float)
    p.add_argument("--timer-digits", type=int, default=3)
    p.add_argument(
        "--integer-timer-keys",
        action="store_true",
        help="compare/summarize only records whose intro timer is an integer tick",
    )
    p.add_argument("--value-digits", type=int, default=5)
    p.add_argument("--compare-profile", type=parse_profile, default=parse_profile("setup,selected-camera,bond-anim"))
    p.add_argument("--require-same-timer-set", action="store_true")
    p.add_argument("--min-matched-timers", type=int, default=1)
    p.add_argument("--vector-tolerance", type=float, default=0.05)
    p.add_argument("--direction-tolerance", type=float, default=0.005)
    p.add_argument("--scalar-tolerance", type=float, default=0.001)
    p.add_argument("--anim-tolerance", type=float, default=0.03)
    p.add_argument("--max-divergences", type=int, default=20)
    p.add_argument("--json-out", help="write summary/compare metadata as JSON")
    return p


def validate_args(args: argparse.Namespace) -> None:
    if args.timer_digits < 0:
        raise SystemExit("FAIL: --timer-digits must be non-negative")
    if args.value_digits < 0:
        raise SystemExit("FAIL: --value-digits must be non-negative")
    if args.max_divergences < 1:
        raise SystemExit("FAIL: --max-divergences must be positive")
    if args.min_matched_timers < 0:
        raise SystemExit("FAIL: --min-matched-timers must be non-negative")
    for name in ("vector_tolerance", "direction_tolerance", "scalar_tolerance", "anim_tolerance"):
        value = getattr(args, name)
        if value < 0.0 or not math.isfinite(value):
            raise SystemExit(f"FAIL: --{name.replace('_', '-')} must be non-negative and finite")
    if bool(args.baseline) != bool(args.test):
        raise SystemExit("FAIL: --baseline and --test must be provided together")
    if args.baseline and args.trace:
        raise SystemExit("FAIL: positional trace summaries cannot be mixed with --baseline/--test compare mode")
    if not args.baseline and not args.trace:
        raise SystemExit("FAIL: provide at least one trace, or --baseline and --test")


def make_summary(args: argparse.Namespace, path: Path, label: str | None) -> Summary:
    return summarize_trace(
        path,
        label=label,
        modes=args.camera_modes,
        require_player=not args.no_require_player,
        require_frozen=args.require_frozen,
        start_intro_timer=args.start_intro_timer,
        end_intro_timer=args.end_intro_timer,
        timer_digits=args.timer_digits,
        integer_timer_keys=args.integer_timer_keys,
        value_digits=args.value_digits,
    )


def main() -> int:
    args = parser().parse_args()
    validate_args(args)

    if args.baseline and args.test:
        baseline = make_summary(args, Path(args.baseline), args.baseline_label)
        test = make_summary(args, Path(args.test), args.test_label)
        exit_code, comparison = compare_summaries(args, baseline, test)
        payload = {
            "baseline": summary_dict(baseline),
            "test": summary_dict(test),
            "result": "pass" if exit_code == 0 else "fail",
            "comparison": comparison,
        }
    else:
        summaries: list[Summary] = []
        for index, trace in enumerate(args.trace):
            label = args.label[index] if index < len(args.label) else None
            summary = make_summary(args, Path(trace), label)
            summaries.append(summary)
            print_summary(summary)
        payload = {"summaries": [summary_dict(summary) for summary in summaries]}
        exit_code = 0

    if args.json_out:
        Path(args.json_out).write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")

    return exit_code


if __name__ == "__main__":
    raise SystemExit(main())
