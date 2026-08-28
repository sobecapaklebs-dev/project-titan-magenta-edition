#!/usr/bin/env python3
"""Compare level-intro camera paths from native and ROM-oracle JSONL traces."""

from __future__ import annotations

import argparse
import json
import math
import sys
from collections import Counter
from collections.abc import Iterable
from dataclasses import dataclass
from pathlib import Path
from typing import Any


CAMERA_MODES = {
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

SKIP_BUTTON_MASK = 0x8000 | 0x4000 | 0x2000 | 0x1000 | 0x0020 | 0x0010

PATH_FIELDS = (
    ("cam_pos", "vector"),
    ("cam_target", "vector"),
    ("cam_up", "direction"),
    ("cam_floor", "vector"),
    ("cam_delta", "vector"),
    ("facing", "direction"),
)

SCALAR_FIELDS = (
    ("theta", "scalar"),
    ("floor", "scalar"),
    ("stan_h", "scalar"),
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

# The fields absorbed inside the phase-3 onset window when
# --bond-anim-onset-tolerance is active (DAM_PARITY_DEEP_DIVE 2026-07-17 §3.3):
# everything that flips 1:1 with the ACT_STAND -> ACT_ANIM transition.
BOND_ONSET_WINDOW_FIELDS = frozenset(field for field, _ in BOND_ANIM_FIELDS)


def load_jsonl(path: str) -> list[dict[str, Any]]:
    records: list[dict[str, Any]] = []
    with open(path, "r", encoding="utf-8") as handle:
        for line_no, line in enumerate(handle, start=1):
            line = line.strip()
            if not line:
                continue
            try:
                record = json.loads(line)
            except json.JSONDecodeError as exc:
                raise SystemExit(f"FAIL: invalid JSON in {path}:{line_no}: {exc}") from None
            if isinstance(record, dict):
                records.append(record)
    return records


def write_json_metrics(path: str | None, metrics: dict[str, Any]) -> None:
    if not path:
        return
    Path(path).write_text(json.dumps(metrics, indent=2, sort_keys=True) + "\n", encoding="utf-8")


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
            result = float(value)
        except ValueError:
            return None
        return result if math.isfinite(result) else None
    return None


def parse_vector(value: Any) -> tuple[float, float, float] | None:
    if not isinstance(value, list) or len(value) != 3:
        return None
    parsed = [parse_float(item) for item in value]
    if any(item is None for item in parsed):
        return None
    return (parsed[0], parsed[1], parsed[2])  # type: ignore[index]


def parse_modes(spec: str) -> set[int]:
    modes: set[int] = set()
    for item in spec.split(","):
        key = item.strip().lower().replace("-", "_")
        if not key:
            continue
        if key in CAMERA_MODES:
            modes.add(CAMERA_MODES[key])
            continue
        try:
            modes.add(int(key, 0))
        except ValueError:
            raise argparse.ArgumentTypeError(f"unknown camera mode {item!r}") from None
    if not modes:
        raise argparse.ArgumentTypeError("at least one camera mode is required")
    return modes


@dataclass
class Divergence:
    """One evaluated failure: a per-field mismatch on an aligned pair, or a
    mode-duration assertion miss. `waiver_candidates()` lists the scope keys
    (most-specific first) that a `compare_waivers` entry may match against."""

    message: str
    kind: str = "field"  # "field" | "duration"
    field: str | None = None
    mode: int | None = None
    delta: float | None = None

    def waiver_candidates(self) -> list[str]:
        if self.kind == "duration":
            return ["mode_durations"]
        candidates: list[str] = []
        if self.mode is not None:
            candidates.append(f"field:{self.field}:mode{self.mode}")
        candidates.append(f"field:{self.field}")
        return candidates


@dataclass
class WaivedGroup:
    scope: str
    ledger_id: str
    count: int = 0
    max_delta: float | None = None


def parse_mode_durations(spec: str) -> dict[int, tuple[int, int]]:
    expectations: dict[int, tuple[int, int]] = {}
    if not spec:
        return expectations
    for item in spec.split(","):
        item = item.strip()
        if not item:
            continue
        parts = item.split(":")
        if len(parts) != 3:
            raise argparse.ArgumentTypeError(
                f"invalid --expect-mode-durations entry {item!r} (expected mode:expected:tolerance)"
            )
        mode_text, expected_text, tolerance_text = parts
        try:
            mode = int(mode_text, 0)
            expected = int(expected_text, 0)
            tolerance = int(tolerance_text, 0)
        except ValueError:
            raise argparse.ArgumentTypeError(
                f"invalid --expect-mode-durations entry {item!r}"
            ) from None
        if tolerance < 0:
            raise argparse.ArgumentTypeError(
                f"--expect-mode-durations tolerance must be non-negative: {item!r}"
            )
        expectations[mode] = (expected, tolerance)
    return expectations


def mode_duration_divergences(
    counts: dict[int, int],
    expectations: dict[int, tuple[int, int]],
) -> list[Divergence]:
    findings: list[Divergence] = []
    for mode in sorted(expectations):
        expected, tolerance = expectations[mode]
        actual = counts.get(mode, 0)
        delta = abs(actual - expected)
        if delta > tolerance:
            findings.append(
                Divergence(
                    message=(
                        f"mode_durations: mode {mode} test duration {actual} record(s), "
                        f"expected {expected} tolerance {tolerance} delta {delta}"
                    ),
                    kind="duration",
                    delta=float(delta),
                )
            )
    return findings


def parse_waivers(spec: str) -> dict[str, str]:
    if not spec:
        return {}
    try:
        data = json.loads(spec)
    except json.JSONDecodeError as exc:
        raise argparse.ArgumentTypeError(f"invalid --waivers JSON: {exc}") from None
    if not isinstance(data, dict):
        raise argparse.ArgumentTypeError("--waivers must be a JSON object")
    for scope, ledger_id in data.items():
        if not isinstance(scope, str) or not isinstance(ledger_id, str):
            raise argparse.ArgumentTypeError("--waivers keys and values must be strings")
    return data


def apply_waivers(
    divergences: list[Divergence],
    waivers: dict[str, str],
) -> tuple[list[Divergence], list[WaivedGroup]]:
    remaining: list[Divergence] = []
    groups: dict[tuple[str, str], WaivedGroup] = {}
    order: list[tuple[str, str]] = []
    for divergence in divergences:
        matched_scope = None
        ledger_id = None
        for candidate in divergence.waiver_candidates():
            if candidate in waivers:
                matched_scope = candidate
                ledger_id = waivers[candidate]
                break
        if matched_scope is None:
            remaining.append(divergence)
            continue
        key = (matched_scope, ledger_id)
        if key not in groups:
            groups[key] = WaivedGroup(scope=matched_scope, ledger_id=ledger_id)
            order.append(key)
        group = groups[key]
        group.count += 1
        if divergence.delta is not None:
            if group.max_delta is None or divergence.delta > group.max_delta:
                group.max_delta = divergence.delta
    return remaining, [groups[key] for key in order]


def input_buttons(record: dict[str, Any]) -> int | None:
    oracle = record.get("oracle")
    if not isinstance(oracle, dict):
        return None
    state = oracle.get("input")
    if not isinstance(state, dict):
        return None
    return parse_int(state.get("buttons"))


def validate_controls(records: Iterable[dict[str, Any]], label: str, modes: set[int], allow_skip_input: bool) -> list[str]:
    if allow_skip_input:
        return []
    failures: list[str] = []
    for record in records:
        cam = parse_int(record.get("cam"))
        if cam not in modes:
            continue
        buttons = input_buttons(record)
        if buttons is not None and (buttons & SKIP_BUTTON_MASK):
            failures.append(
                f"{label}: skip-capable input during intro at frame {record.get('f')} "
                f"(cam={cam}, buttons=0x{buttons:04x})"
            )
    return failures


def active_intro_records(
    records: Iterable[dict[str, Any]],
    modes: set[int],
    require_player: bool,
    require_frozen: bool,
    start_global: int | None,
    end_global: int | None,
    start_intro_timer: float | None,
    end_intro_timer: float | None,
) -> list[dict[str, Any]]:
    active: list[dict[str, Any]] = []
    for record in records:
        if require_player and parse_int(record.get("p")) != 1:
            continue
        cam = parse_int(record.get("cam"))
        if cam not in modes:
            continue
        if require_frozen and parse_int(record.get("p_unk")) != 1 and parse_int(get_path(record, "intro.frozen")) != 1:
            continue
        global_timer = parse_int(get_path(record, "move.global"))
        if start_global is not None and (global_timer is None or global_timer < start_global):
            continue
        if end_global is not None and (global_timer is None or global_timer > end_global):
            continue
        intro_timer = parse_float(get_path(record, "intro.timer"))
        if start_intro_timer is not None and (intro_timer is None or intro_timer < start_intro_timer):
            continue
        if end_intro_timer is not None and (intro_timer is None or intro_timer > end_intro_timer):
            continue
        active.append(record)
    return active


def align_by_index(
    baseline: list[dict[str, Any]],
    test: list[dict[str, Any]],
    start_active_frame: int,
    sample_step: int,
    max_aligned: int | None,
) -> list[tuple[int, dict[str, Any], dict[str, Any]]]:
    if start_active_frame < 1:
        raise ValueError("start_active_frame must be positive")
    pairs: list[tuple[int, dict[str, Any], dict[str, Any]]] = []
    base_index = start_active_frame - 1
    test_index = start_active_frame - 1
    active_frame = start_active_frame
    while base_index < len(baseline) and test_index < len(test):
        pairs.append((active_frame, baseline[base_index], test[test_index]))
        if max_aligned is not None and len(pairs) >= max_aligned:
            break
        base_index += sample_step
        test_index += sample_step
        active_frame += sample_step
    return pairs


def align_by_key(
    baseline: list[dict[str, Any]],
    test: list[dict[str, Any]],
    key_path: str,
    max_aligned: int | None,
) -> list[tuple[int, dict[str, Any], dict[str, Any]]]:
    test_by_key: dict[int, dict[str, Any]] = {}
    for record in test:
        key = parse_int(get_path(record, key_path))
        if key is None:
            continue
        test_by_key[key] = record

    baseline_order: list[int] = []
    baseline_by_key: dict[int, dict[str, Any]] = {}
    for record in baseline:
        key = parse_int(get_path(record, key_path))
        if key is None:
            continue
        if key not in baseline_by_key:
            baseline_order.append(key)
        baseline_by_key[key] = record

    pairs: list[tuple[int, dict[str, Any], dict[str, Any]]] = []
    for key in baseline_order:
        record = baseline_by_key[key]
        other = test_by_key.get(key)
        if other is None:
            continue
        pairs.append((key, record, other))
        if max_aligned is not None and len(pairs) >= max_aligned:
            break
    return pairs


def segment_by_mode(
    records: list[dict[str, Any]],
    modes: set[int],
) -> dict[int, list[dict[str, Any]]]:
    segments: dict[int, list[dict[str, Any]]] = {mode: [] for mode in modes}
    for record in records:
        cam = parse_int(record.get("cam"))
        if cam in segments:
            segments[cam].append(record)
    return segments


def align_mode_segment(
    mode: int,
    baseline_segment: list[dict[str, Any]],
    test_segment: list[dict[str, Any]],
) -> list[tuple[str, dict[str, Any], dict[str, Any]]]:
    """Align one camera-mode segment: key by `intro.timer` (deduped to the last
    record per timer value per side) when both sides have a usable timer AND
    that keying actually produces overlap; otherwise fall back to
    segment-relative index (timer absent entirely, or the two sides' timer
    domains don't intersect -- e.g. a mode that freezes the timer at a
    different value per side). Only overlapping keys are paired -- unmatched
    tail records (mode-duration skew) are not divergences.

    The swirl mode chains multiple authored control points, each with its own
    `intro.timer` that restarts near 0 (measured: Dam mode 3 resets ~6 times).
    A raw-timer key would collide across control points and re-pair unrelated
    spline segments, fabricating divergences of the same kind per-mode
    alignment exists to eliminate -- so the key also carries
    `intro.setup.swirl.current.index` (the authored control-point index) when
    present, disambiguating resets. It is a no-op for modes without a swirl
    segment (the field is absent, so every record shares the same index)."""

    def timer_key_of(record: dict[str, Any]) -> tuple[int, float] | None:
        timer = parse_float(get_path(record, "intro.timer"))
        if timer is None:
            return None
        segment = parse_int(get_path(record, "intro.setup.swirl.current.index"))
        return (segment if segment is not None else 0, timer)

    base_timers_usable = any(timer_key_of(record) is not None for record in baseline_segment)
    test_timers_usable = any(timer_key_of(record) is not None for record in test_segment)

    if base_timers_usable and test_timers_usable:
        base_by_timer: dict[tuple[int, float], dict[str, Any]] = {}
        base_order: list[tuple[int, float]] = []
        for record in baseline_segment:
            key = timer_key_of(record)
            if key is None:
                continue
            if key not in base_by_timer:
                base_order.append(key)
            # The ares harness records all four controller polls for a VI.
            # The first poll can carry the timer's pre-tick actor state while
            # later polls carry the settled state (D41: stale intro animation
            # frame at 11 otherwise-isolated keys). The last record is the
            # completed state for that authored timer and matches native's
            # post-tick trace point.
            base_by_timer[key] = record
        test_by_timer: dict[tuple[int, float], dict[str, Any]] = {}
        for record in test_segment:
            key = timer_key_of(record)
            if key is None:
                continue
            test_by_timer[key] = record
        timer_pairs: list[tuple[str, dict[str, Any], dict[str, Any]]] = []
        for key in base_order:
            other = test_by_timer.get(key)
            if other is not None:
                segment, timer = key
                label = f"mode{mode}:s{segment}:t{timer:g}" if segment else f"mode{mode}:t{timer:g}"
                timer_pairs.append((label, base_by_timer[key], other))
        if timer_pairs:
            return timer_pairs
        # Timer present on both sides but the value domains don't intersect
        # (e.g. a freeze-frame mode pinned at a different accumulated timer
        # value per side) -- fall through to index alignment rather than
        # silently reporting zero coverage for this mode.

    pairs: list[tuple[str, dict[str, Any], dict[str, Any]]] = []
    for index, (base_record, test_record) in enumerate(zip(baseline_segment, test_segment)):
        pairs.append((f"mode{mode}:i{index}", base_record, test_record))
    return pairs


def align_per_mode(
    baseline: list[dict[str, Any]],
    test: list[dict[str, Any]],
    modes: set[int],
    max_aligned: int | None,
) -> list[tuple[str, dict[str, Any], dict[str, Any]]]:
    baseline_segments = segment_by_mode(baseline, modes)
    test_segments = segment_by_mode(test, modes)
    pairs: list[tuple[str, dict[str, Any], dict[str, Any]]] = []
    for mode in sorted(modes):
        pairs.extend(
            align_mode_segment(mode, baseline_segments.get(mode, []), test_segments.get(mode, []))
        )
        if max_aligned is not None and len(pairs) >= max_aligned:
            return pairs[:max_aligned]
    return pairs


def field_specs(
    profile: str,
    compare_state: bool,
    compare_selected_camera: bool,
    compare_setup: bool,
    compare_bond_anim: bool,
    exclude_fields: set[str],
) -> list[tuple[str, str]]:
    specs: list[tuple[str, str]] = []
    if profile in ("path", "full"):
        specs.extend(PATH_FIELDS)
    if profile in ("scalar", "full"):
        specs.extend(SCALAR_FIELDS)
    if profile == "state" or compare_state or profile == "full":
        specs.extend(STATE_FIELDS)
    if compare_selected_camera:
        specs.extend(SELECTED_CAMERA_FIELDS)
    if compare_setup:
        specs.extend(SETUP_FIELDS)
    if compare_bond_anim:
        specs.extend(BOND_ANIM_FIELDS)
    if exclude_fields:
        specs = [(field, kind) for field, kind in specs if field not in exclude_fields]
    return specs


def parse_exclude_fields(spec: str) -> set[str]:
    fields: set[str] = set()
    for item in spec.split(","):
        field = item.strip()
        if field:
            fields.add(field)
    return fields


def apply_bond_idle_onset_alignment(
    pairs: list[tuple[Any, dict[str, Any], dict[str, Any]]],
    tolerance: float,
) -> tuple[set[int], dict[str, Any], list[Divergence]]:
    """Absorb only the ACT_BONDINTRO -> ACT_STAND sampling boundary.

    Retail advances the intro actor in three-tick batches. Depending on the
    controller-poll lattice, its last ACT_BONDINTRO record and first ACT_STAND
    record can be three authored timer units later than native's per-tick
    trace even though both execute the same transition. Numeric animation
    phase remains an ordinary comparison; this alignment only prevents that
    one boundary record from looking like an animation identity regression.
    """
    def action_of(record: dict[str, Any]) -> int | None:
        return parse_int(get_path(record, "intro.bond_action"))

    base_idx = next(
        (index for index, (_key, base, _test) in enumerate(pairs) if action_of(base) == 1),
        None,
    )
    test_idx = next(
        (index for index, (_key, _base, test) in enumerate(pairs) if action_of(test) == 1),
        None,
    )
    metrics: dict[str, Any] = {"tolerance": tolerance}
    divergences: list[Divergence] = []
    absorbed: set[int] = set()

    if base_idx is None or test_idx is None:
        metrics["delta"] = None
        metrics["baseline_onset"] = None if base_idx is None else str(pairs[base_idx][0])
        metrics["test_onset"] = None if test_idx is None else str(pairs[test_idx][0])
        return absorbed, metrics, divergences

    low, high = min(base_idx, test_idx), max(base_idx, test_idx)
    for index in range(low, high):
        base_action = action_of(pairs[index][1])
        test_action = action_of(pairs[index][2])
        if base_action != test_action and {base_action, test_action} <= {1, 23}:
            absorbed.add(index)

    def onset_info(index: int, record: dict[str, Any]) -> dict[str, Any]:
        return {
            "key": str(pairs[index][0]),
            "segment": parse_int(get_path(record, "intro.setup.swirl.current.index")),
            "timer": parse_float(get_path(record, "intro.timer")),
        }

    base_info = onset_info(base_idx, pairs[base_idx][1])
    test_info = onset_info(test_idx, pairs[test_idx][2])
    metrics["baseline_onset"] = base_info
    metrics["test_onset"] = test_info
    metrics["absorbed_pairs"] = len(absorbed)
    mode = parse_int(pairs[base_idx][1].get("cam"))

    if base_info["segment"] != test_info["segment"]:
        metrics["delta"] = None
        divergences.append(
            Divergence(
                message=(
                    f"bond_anim idle onset segments differ: baseline {base_info['key']}"
                    f" vs test {test_info['key']}"
                ),
                field="intro.bond_anim.idle_onset",
                mode=mode,
            )
        )
    elif base_info["timer"] is None or test_info["timer"] is None:
        metrics["delta"] = None
        divergences.append(
            Divergence(
                message=(
                    f"bond_anim idle onset timer missing: baseline {base_info['key']}"
                    f" vs test {test_info['key']}"
                ),
                field="intro.bond_anim.idle_onset",
                mode=mode,
            )
        )
    else:
        delta = abs(base_info["timer"] - test_info["timer"])
        metrics["delta"] = delta
        if delta > tolerance:
            divergences.append(
                Divergence(
                    message=(
                        f"bond_anim idle onset delta {delta:.2f} > tolerance"
                        f" {tolerance:.2f} (baseline {base_info['key']},"
                        f" test {test_info['key']})"
                    ),
                    field="intro.bond_anim.idle_onset",
                    mode=mode,
                    delta=delta,
                )
            )

    return absorbed, metrics, divergences


def apply_bond_anim_onset_alignment(
    pairs: list[tuple[Any, dict[str, Any], dict[str, Any]]],
    baseline_records: list[dict[str, Any]],
    test_records: list[dict[str, Any]],
    tolerance: float,
) -> tuple[
    set[int],
    list[tuple[str, dict[str, Any], dict[str, Any]]],
    dict[str, Any],
    list[Divergence],
]:
    """Phase-3 onset event alignment (DAM_PARITY_DEEP_DIVE 2026-07-17 §3.3).

    Retail fires the intro phase-3 animation from an RNG-jittered AI
    sleep-wake boundary; native (D43) fires the SAME animation at a fixed
    swirl timer. The camera path stays tick-exact, so per-timer alignment is
    right for every other field — but the bond-anim family flips at each
    side's own onset, so comparing phase-3 animation frames at the same
    camera timer remains wrong even after both sides have started: the side
    that started first keeps its scheduling lead. This helper therefore:

    * keeps ordinary per-timer Bond comparison before the earlier onset;
    * gates the onset timer delta by `tolerance` (same swirl segment); and
    * re-pairs phase-3 records by animation frame, so the animation identity,
      speed, end, hand, and frame coverage are judged at equal animation age.

    This is deliberately narrower than ignoring phase 3. Missing internal
    stock frame samples and a shortened/frozen phase-3 range are explicit
    divergences, while one leading sample of capture skew is tolerated.
    """
    def action_of(record: dict[str, Any]) -> int | None:
        return parse_int(get_path(record, "intro.bond_action"))

    base_idx: int | None = None
    test_idx: int | None = None
    for index, (_key, base, test) in enumerate(pairs):
        if base_idx is None and action_of(base) == 3:
            base_idx = index
        if test_idx is None and action_of(test) == 3:
            test_idx = index
        if base_idx is not None and test_idx is not None:
            break

    metrics: dict[str, Any] = {"tolerance": tolerance}
    divergences: list[Divergence] = []
    absorbed: set[int] = set()
    phase3_pairs: list[tuple[str, dict[str, Any], dict[str, Any]]] = []

    if base_idx is None or test_idx is None:
        # Zero or one side reaches phase 3 inside the compared window: nothing
        # to event-align; the ordinary field comparison judges the mismatch.
        metrics["delta"] = None
        metrics["baseline_onset"] = None if base_idx is None else str(pairs[base_idx][0])
        metrics["test_onset"] = None if test_idx is None else str(pairs[test_idx][0])
        return absorbed, phase3_pairs, metrics, divergences

    low, high = min(base_idx, test_idx), max(base_idx, test_idx)
    # Once either side reaches phase 3, compare the Bond family by animation
    # age below. Camera/path fields remain compared on these ordinary timer
    # pairs, and pre-onset idle animation (including D41) stays timer-aligned.
    absorbed.update(range(low, len(pairs)))

    def onset_info(index: int, record: dict[str, Any]) -> dict[str, Any]:
        return {
            "key": str(pairs[index][0]),
            "segment": parse_int(get_path(record, "intro.setup.swirl.current.index")),
            "timer": parse_float(get_path(record, "intro.timer")),
        }

    base_info = onset_info(base_idx, pairs[base_idx][1])
    test_info = onset_info(test_idx, pairs[test_idx][2])
    metrics["baseline_onset"] = base_info
    metrics["test_onset"] = test_info
    metrics["onset_window_pairs"] = high - low
    metrics["timer_pairs_replaced"] = len(absorbed)

    mode = parse_int(pairs[base_idx][1].get("cam"))
    if base_info["segment"] != test_info["segment"]:
        metrics["delta"] = None
        divergences.append(
            Divergence(
                message=(
                    f"bond_anim phase-3 onset segments differ: baseline {base_info['key']}"
                    f" vs test {test_info['key']}"
                ),
                field="intro.bond_anim.onset",
                mode=mode,
            )
        )
    elif base_info["timer"] is None or test_info["timer"] is None:
        metrics["delta"] = None
        divergences.append(
            Divergence(
                message=(
                    f"bond_anim phase-3 onset timer missing: baseline {base_info['key']}"
                    f" vs test {test_info['key']}"
                ),
                field="intro.bond_anim.onset",
                mode=mode,
            )
        )
    else:
        delta = abs(base_info["timer"] - test_info["timer"])
        metrics["delta"] = delta
        if delta > tolerance:
            divergences.append(
                Divergence(
                    message=(
                        f"bond_anim phase-3 onset delta {delta:.2f} > tolerance"
                        f" {tolerance:.2f} (baseline {base_info['key']},"
                        f" test {test_info['key']})"
                    ),
                    field="intro.bond_anim.onset",
                    mode=mode,
                    delta=delta,
                )
            )

    def phase3_by_frame(records: list[dict[str, Any]]) -> dict[float, dict[str, Any]]:
        by_frame: dict[float, dict[str, Any]] = {}
        for record in records:
            if action_of(record) != 3:
                continue
            frame = parse_float(get_path(record, "intro.bond_anim.frame"))
            if frame is None:
                continue
            # Trace output is decimal and authored animation frames advance in
            # quarter/half-frame units. Rounding only canonicalizes formatting.
            by_frame[round(frame, 3)] = record
        return by_frame

    base_by_frame = phase3_by_frame(baseline_records)
    test_by_frame = phase3_by_frame(test_records)
    shared_frames = sorted(base_by_frame.keys() & test_by_frame.keys())
    phase3_pairs = [
        (f"bond-frame:{frame:g}", base_by_frame[frame], test_by_frame[frame])
        for frame in shared_frames
    ]

    base_frames = sorted(base_by_frame)
    test_frames = sorted(test_by_frame)
    metrics["phase3_frame_alignment"] = {
        "baseline_unique": len(base_frames),
        "test_unique": len(test_frames),
        "aligned": len(shared_frames),
        "baseline_range": None if not base_frames else [base_frames[0], base_frames[-1]],
        "test_range": None if not test_frames else [test_frames[0], test_frames[-1]],
    }

    if not phase3_pairs:
        divergences.append(
            Divergence(
                message="bond_anim phase-3 has no shared animation-frame samples",
                field="intro.bond_anim.phase_coverage",
                mode=mode,
            )
        )
    elif base_frames and test_frames:
        # Ares may first observe the three-tick retail batch one frame before
        # native's per-tick trace point. That leading edge is sampling skew;
        # holes after both captures are active or an early endpoint are not.
        coverage_slack = 1.0
        start_delta = abs(base_frames[0] - test_frames[0])
        metrics["phase3_frame_alignment"]["start_delta"] = start_delta
        if start_delta > coverage_slack:
            divergences.append(
                Divergence(
                    message=(
                        f"bond_anim phase-3 start delta {start_delta:.2f} > "
                        f"sampling slack {coverage_slack:.2f}"
                    ),
                    field="intro.bond_anim.phase_coverage",
                    mode=mode,
                    delta=start_delta,
                )
            )

        internal_missing = [
            frame
            for frame in base_frames
            if frame >= test_frames[0] and frame not in test_by_frame
        ]
        metrics["phase3_frame_alignment"]["missing_baseline_internal"] = internal_missing
        if internal_missing:
            divergences.append(
                Divergence(
                    message=(
                        "bond_anim phase-3 native trace misses stock animation-frame "
                        f"sample(s): {internal_missing[:8]}"
                    ),
                    field="intro.bond_anim.phase_coverage",
                    mode=mode,
                )
            )

        end_delta = abs(base_frames[-1] - test_frames[-1])
        metrics["phase3_frame_alignment"]["end_delta"] = end_delta
        if end_delta > coverage_slack:
            divergences.append(
                Divergence(
                    message=(
                        f"bond_anim phase-3 endpoint delta {end_delta:.2f} > "
                        f"sampling slack {coverage_slack:.2f}"
                    ),
                    field="intro.bond_anim.phase_coverage",
                    mode=mode,
                    delta=end_delta,
                )
            )

    return absorbed, phase3_pairs, metrics, divergences


def compare_pairs(
    pairs: list[tuple[Any, dict[str, Any], dict[str, Any]]],
    specs: list[tuple[str, str]],
    vector_tolerance: float,
    direction_tolerance: float,
    scalar_tolerance: float,
    anim_tolerance: float,
    absorbed_indices: set[int] | None = None,
    absorbed_fields: frozenset[str] = BOND_ONSET_WINDOW_FIELDS,
) -> tuple[list[Divergence], dict[str, float]]:
    """Evaluate every aligned pair against every field spec -- full window,
    no early return. `--max-divergences` is a reporting cap applied by the
    caller, not an evaluation cap. Pairs listed in `absorbed_indices` skip the
    `absorbed_fields` family (phase-3 onset-window event alignment)."""
    divergences: list[Divergence] = []
    max_abs: dict[str, float] = {}

    for pair_index, (key, base, test) in enumerate(pairs):
        mode = parse_int(base.get("cam"))
        pair_absorbed = absorbed_indices is not None and pair_index in absorbed_indices
        for field, kind in specs:
            if pair_absorbed and field in absorbed_fields:
                continue
            if kind in ("vector", "direction"):
                base_vec = parse_vector(get_path(base, field))
                test_vec = parse_vector(get_path(test, field))
                if base_vec is None or test_vec is None:
                    divergences.append(
                        Divergence(
                            message=(
                                f"key {key}: missing vector field {field} "
                                f"(baseline={get_path(base, field)!r}, test={get_path(test, field)!r})"
                            ),
                            field=field,
                            mode=mode,
                        )
                    )
                    continue
                tolerance = direction_tolerance if kind == "direction" else vector_tolerance
                for index, (base_value, test_value) in enumerate(zip(base_vec, test_vec, strict=True)):
                    delta = abs(base_value - test_value)
                    stat_key = f"{field}[{index}]"
                    max_abs[stat_key] = max(max_abs.get(stat_key, 0.0), delta)
                    if delta > tolerance:
                        divergences.append(
                            Divergence(
                                message=(
                                    f"key {key}: {stat_key} baseline={base_value:.5f} "
                                    f"test={test_value:.5f} delta={delta:.5f} tolerance={tolerance:.5f}"
                                ),
                                field=stat_key,
                                mode=mode,
                                delta=delta,
                            )
                        )
            elif kind == "scalar":
                base_value = parse_float(get_path(base, field))
                test_value = parse_float(get_path(test, field))
                if base_value is None or test_value is None:
                    divergences.append(
                        Divergence(
                            message=(
                                f"key {key}: missing scalar field {field} "
                                f"(baseline={get_path(base, field)!r}, test={get_path(test, field)!r})"
                            ),
                            field=field,
                            mode=mode,
                        )
                    )
                    continue
                delta = abs(base_value - test_value)
                max_abs[field] = max(max_abs.get(field, 0.0), delta)
                if delta > scalar_tolerance:
                    divergences.append(
                        Divergence(
                            message=(
                                f"key {key}: {field} baseline={base_value:.5f} "
                                f"test={test_value:.5f} delta={delta:.5f} tolerance={scalar_tolerance:.5f}"
                            ),
                            field=field,
                            mode=mode,
                            delta=delta,
                        )
                    )
            elif kind == "anim":
                base_value = parse_float(get_path(base, field))
                test_value = parse_float(get_path(test, field))
                if base_value is None or test_value is None:
                    divergences.append(
                        Divergence(
                            message=(
                                f"key {key}: missing animation field {field} "
                                f"(baseline={get_path(base, field)!r}, test={get_path(test, field)!r})"
                            ),
                            field=field,
                            mode=mode,
                        )
                    )
                    continue
                delta = abs(base_value - test_value)
                max_abs[field] = max(max_abs.get(field, 0.0), delta)
                if delta > anim_tolerance:
                    divergences.append(
                        Divergence(
                            message=(
                                f"key {key}: {field} baseline={base_value:.5f} "
                                f"test={test_value:.5f} delta={delta:.5f} tolerance={anim_tolerance:.5f}"
                            ),
                            field=field,
                            mode=mode,
                            delta=delta,
                        )
                    )
            elif kind == "exact":
                base_value = get_path(base, field)
                test_value = get_path(test, field)
                if base_value != test_value:
                    divergences.append(
                        Divergence(
                            message=f"key {key}: {field} baseline={base_value!r} test={test_value!r}",
                            field=field,
                            mode=mode,
                        )
                    )
            else:
                raise AssertionError(kind)

    return divergences, max_abs


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("baseline", help="baseline JSONL trace, usually ROM/emulator")
    parser.add_argument("test", help="test JSONL trace, usually native")
    parser.add_argument(
        "--align",
        choices=["active-index", "global", "frame", "intro-timer", "per-mode"],
        default="active-index",
    )
    parser.add_argument("--profile", choices=["path", "scalar", "state", "full"], default="path")
    parser.add_argument("--camera-modes", type=parse_modes, default=parse_modes("intro,fadeswirl,swirl"))
    parser.add_argument("--start-active-frame", type=int, default=1)
    parser.add_argument("--sample-step", type=int, default=1)
    parser.add_argument("--max-aligned", type=int)
    parser.add_argument("--start-global", type=int)
    parser.add_argument("--end-global", type=int)
    parser.add_argument("--start-intro-timer", type=float)
    parser.add_argument("--end-intro-timer", type=float)
    parser.add_argument("--vector-tolerance", type=float, default=0.05)
    parser.add_argument("--direction-tolerance", type=float, default=0.005)
    parser.add_argument("--scalar-tolerance", type=float, default=0.05)
    parser.add_argument("--anim-tolerance", type=float, default=0.02)
    parser.add_argument(
        "--bond-idle-onset-tolerance",
        type=float,
        default=None,
        help=(
            "event-align the ACT_BONDINTRO-to-ACT_STAND boundary by absorbing "
            "Bond-family identity mismatches only between the two first idle "
            "records, and fail if their authored timer delta exceeds this value"
        ),
    )
    parser.add_argument(
        "--bond-anim-onset-tolerance",
        type=float,
        default=None,
        help=(
            "event-align the phase-3 bond animation onset: absorb bond-anim "
            "mismatches between the two sides' first ACT_ANIM records and fail "
            "only if the onset timer delta exceeds this tolerance (retail fires "
            "phase 3 from an RNG-jittered AI wake, so the onset is scheduling-"
            "dependent; DAM_PARITY_DEEP_DIVE 2026-07-17 §3.3)"
        ),
    )
    parser.add_argument("--compare-state", action="store_true")
    parser.add_argument("--compare-selected-camera", action="store_true")
    parser.add_argument("--compare-setup", action="store_true")
    parser.add_argument("--compare-bond-anim", action="store_true")
    parser.add_argument("--exclude-fields", default="")
    parser.add_argument("--no-require-player", action="store_true")
    parser.add_argument("--require-frozen", action="store_true")
    parser.add_argument("--allow-skip-input", action="store_true")
    parser.add_argument(
        "--max-divergences",
        type=int,
        default=20,
        help="reporting cap only: how many divergence lines to print (evaluation is always full-window)",
    )
    parser.add_argument("--min-aligned", type=int)
    parser.add_argument(
        "--expect-mode-durations",
        type=parse_mode_durations,
        default=parse_mode_durations(""),
        help="mode:expected:tolerance[,mode:expected:tolerance...] asserted against the test trace's per-mode active-record counts",
    )
    parser.add_argument(
        "--waivers",
        type=parse_waivers,
        default=parse_waivers(""),
        help='JSON object mapping waiver scope ("field:<name>", "field:<name>:mode<N>", or "mode_durations") to a ledger ID',
    )
    parser.add_argument("--json-out", help="write comparison metrics as JSON")
    args = parser.parse_args()

    if args.sample_step < 1:
        raise SystemExit("FAIL: --sample-step must be positive")
    if args.max_aligned is not None and args.max_aligned < 1:
        raise SystemExit("FAIL: --max-aligned must be positive when set")
    if args.max_divergences < 1:
        raise SystemExit("FAIL: --max-divergences must be positive")
    if args.min_aligned is not None and args.min_aligned < 1:
        raise SystemExit("FAIL: --min-aligned must be positive when set")
    if args.anim_tolerance < 0.0:
        raise SystemExit("FAIL: --anim-tolerance must be non-negative")
    if args.bond_idle_onset_tolerance is not None and args.bond_idle_onset_tolerance < 0.0:
        raise SystemExit("FAIL: --bond-idle-onset-tolerance must be non-negative")

    baseline_all = load_jsonl(args.baseline)
    test_all = load_jsonl(args.test)
    common_metrics: dict[str, Any] = {
        "baseline": args.baseline,
        "test": args.test,
        "align": args.align,
        "profile": args.profile,
        "camera_modes": sorted(args.camera_modes),
        "filters": {
            "require_player": not args.no_require_player,
            "require_frozen": args.require_frozen,
            "start_global": args.start_global,
            "end_global": args.end_global,
            "start_intro_timer": args.start_intro_timer,
            "end_intro_timer": args.end_intro_timer,
            "start_active_frame": args.start_active_frame,
            "sample_step": args.sample_step,
            "max_aligned": args.max_aligned,
            "min_aligned": args.min_aligned,
        },
        "compare": {
            "compare_state": args.compare_state,
            "compare_selected_camera": args.compare_selected_camera,
            "compare_setup": args.compare_setup,
            "compare_bond_anim": args.compare_bond_anim,
            "exclude_fields": sorted(parse_exclude_fields(args.exclude_fields)),
        },
        "tolerances": {
            "vector": args.vector_tolerance,
            "direction": args.direction_tolerance,
            "scalar": args.scalar_tolerance,
            "anim": args.anim_tolerance,
        },
        "record_counts": {
            "baseline": len(baseline_all),
            "test": len(test_all),
        },
        "expect_mode_durations": {
            str(mode): {"expected": expected, "tolerance": tolerance}
            for mode, (expected, tolerance) in sorted(args.expect_mode_durations.items())
        },
        "waivers": dict(args.waivers),
    }
    degenerate_metrics = {"verdict": "fail", "per_mode_aligned_counts": {}, "waived": []}

    control_failures = validate_controls(
        baseline_all, "baseline", args.camera_modes, args.allow_skip_input
    ) + validate_controls(test_all, "test", args.camera_modes, args.allow_skip_input)
    if control_failures:
        write_json_metrics(
            args.json_out,
            {
                **common_metrics,
                **degenerate_metrics,
                "status": "fail",
                "failure_kind": "control",
                "failures": control_failures,
            },
        )
        print("FAIL: intro capture controls became invalid")
        for failure in control_failures[: args.max_divergences]:
            print(f"  {failure}")
        return 1

    baseline = active_intro_records(
        baseline_all,
        args.camera_modes,
        not args.no_require_player,
        args.require_frozen,
        args.start_global,
        args.end_global,
        args.start_intro_timer,
        args.end_intro_timer,
    )
    test = active_intro_records(
        test_all,
        args.camera_modes,
        not args.no_require_player,
        args.require_frozen,
        args.start_global,
        args.end_global,
        args.start_intro_timer,
        args.end_intro_timer,
    )
    if not baseline:
        write_json_metrics(
            args.json_out,
            {
                **common_metrics,
                **degenerate_metrics,
                "status": "fail",
                "failure_kind": "filter",
                "active_counts": {"baseline": 0, "test": len(test)},
                "failures": ["no baseline intro records matched the requested filters"],
            },
        )
        raise SystemExit("FAIL: no baseline intro records matched the requested filters")
    if not test:
        write_json_metrics(
            args.json_out,
            {
                **common_metrics,
                **degenerate_metrics,
                "status": "fail",
                "failure_kind": "filter",
                "active_counts": {"baseline": len(baseline), "test": 0},
                "failures": ["no test intro records matched the requested filters"],
            },
        )
        raise SystemExit("FAIL: no test intro records matched the requested filters")

    if args.align == "active-index":
        pairs = align_by_index(baseline, test, args.start_active_frame, args.sample_step, args.max_aligned)
    elif args.align == "global":
        pairs = align_by_key(baseline, test, "move.global", args.max_aligned)
    elif args.align == "frame":
        pairs = align_by_key(baseline, test, "f", args.max_aligned)
    elif args.align == "intro-timer":
        pairs = align_by_key(baseline, test, "intro.timer", args.max_aligned)
    elif args.align == "per-mode":
        pairs = align_per_mode(baseline, test, args.camera_modes, args.max_aligned)
    else:
        raise AssertionError(args.align)

    if not pairs:
        write_json_metrics(
            args.json_out,
            {
                **common_metrics,
                **degenerate_metrics,
                "status": "fail",
                "failure_kind": "alignment",
                "active_counts": {"baseline": len(baseline), "test": len(test)},
                "aligned_count": 0,
                "failures": ["no aligned intro records"],
            },
        )
        raise SystemExit("FAIL: no aligned intro records")
    if args.min_aligned is not None and len(pairs) < args.min_aligned:
        write_json_metrics(
            args.json_out,
            {
                **common_metrics,
                **degenerate_metrics,
                "status": "fail",
                "failure_kind": "alignment",
                "active_counts": {"baseline": len(baseline), "test": len(test)},
                "aligned_count": len(pairs),
                "failures": [f"aligned intro records {len(pairs)} < required {args.min_aligned}"],
            },
        )
        raise SystemExit(
            f"FAIL: aligned intro records {len(pairs)} < required {args.min_aligned}"
        )

    specs = field_specs(
        args.profile,
        args.compare_state,
        args.compare_selected_camera,
        args.compare_setup,
        args.compare_bond_anim,
        parse_exclude_fields(args.exclude_fields),
    )
    absorbed_indices: set[int] | None = None
    idle_onset_metrics: dict[str, Any] | None = None
    idle_onset_divergences: list[Divergence] = []
    if args.bond_idle_onset_tolerance is not None:
        absorbed_indices, idle_onset_metrics, idle_onset_divergences = (
            apply_bond_idle_onset_alignment(pairs, args.bond_idle_onset_tolerance)
        )
    phase3_pairs: list[tuple[str, dict[str, Any], dict[str, Any]]] = []
    onset_metrics: dict[str, Any] | None = None
    onset_divergences: list[Divergence] = []
    if args.bond_anim_onset_tolerance is not None:
        phase3_absorbed, phase3_pairs, onset_metrics, onset_divergences = (
            apply_bond_anim_onset_alignment(
                pairs, baseline, test, args.bond_anim_onset_tolerance
            )
        )
        if absorbed_indices is None:
            absorbed_indices = phase3_absorbed
        else:
            absorbed_indices.update(phase3_absorbed)

    divergences, max_abs = compare_pairs(
        pairs,
        specs,
        args.vector_tolerance,
        args.direction_tolerance,
        args.scalar_tolerance,
        args.anim_tolerance,
        absorbed_indices=absorbed_indices,
    )
    if phase3_pairs:
        phase3_specs = [spec for spec in specs if spec[0] in BOND_ONSET_WINDOW_FIELDS]
        phase3_divergences, phase3_max_abs = compare_pairs(
            phase3_pairs,
            phase3_specs,
            args.vector_tolerance,
            args.direction_tolerance,
            args.scalar_tolerance,
            args.anim_tolerance,
        )
        divergences.extend(phase3_divergences)
        for field, delta in phase3_max_abs.items():
            max_abs[field] = max(max_abs.get(field, 0.0), delta)
    divergences.extend(idle_onset_divergences)
    if idle_onset_metrics is not None:
        common_metrics["bond_idle_onset"] = idle_onset_metrics
    divergences.extend(onset_divergences)
    if onset_metrics is not None:
        common_metrics["bond_anim_onset"] = onset_metrics

    test_mode_counts = Counter(parse_int(record.get("cam")) for record in test)
    divergences.extend(mode_duration_divergences(dict(test_mode_counts), args.expect_mode_durations))

    unwaived, waived_groups = apply_waivers(divergences, args.waivers)

    per_mode_aligned_counts = Counter(parse_int(base.get("cam")) for _key, base, _test in pairs)

    total = len(divergences)
    waived_total = total - len(unwaived)

    metrics = {
        **common_metrics,
        "verdict": "fail" if unwaived else "pass",
        "status": "fail" if unwaived else "pass",
        "active_counts": {"baseline": len(baseline), "test": len(test)},
        "aligned_count": len(pairs),
        "aligned_keys": [key for key, _base, _test in pairs],
        "per_mode_aligned_counts": {str(mode): count for mode, count in sorted(per_mode_aligned_counts.items())},
        "per_mode_test_counts": {str(mode): count for mode, count in sorted(test_mode_counts.items())},
        "field_count": len(specs),
        "max_abs": max_abs,
        "divergence_count": total,
        "divergences": [d.message for d in divergences],
        "unwaived_divergence_count": len(unwaived),
        "unwaived_divergences": [d.message for d in unwaived],
        "waived": [
            {
                "scope": group.scope,
                "ledger_id": group.ledger_id,
                "count": group.count,
                "max_delta": group.max_delta,
            }
            for group in waived_groups
        ],
    }
    write_json_metrics(args.json_out, metrics)

    for group in waived_groups:
        max_delta_text = f"{group.max_delta:.5f}" if group.max_delta is not None else "n/a"
        print(f"WAIVED ({group.ledger_id}): {group.scope} -- {group.count} divergence(s), max delta={max_delta_text}")

    if unwaived:
        if waived_total:
            print(f"FAIL: {len(unwaived)} intro divergence(s) found ({total} total, {waived_total} waived)")
        else:
            print(f"FAIL: {total} intro divergence(s) found")
        for divergence in unwaived[: args.max_divergences]:
            print(f"  {divergence.message}")
        print(
            f"Compared {len(pairs)} aligned intro record(s) "
            f"(profile={args.profile}, align={args.align})."
        )
        for key in sorted(max_abs):
            print(f"  max_abs {key}: {max_abs[key]:.5f}")
        return 1

    print(
        f"MATCH: {len(pairs)} aligned intro record(s) "
        f"(profile={args.profile}, align={args.align}, baseline_active={len(baseline)}, test_active={len(test)})"
        + (f" [{waived_total} divergence(s) waived]" if waived_total else "")
    )
    for key in sorted(max_abs):
        print(f"  max_abs {key}: {max_abs[key]:.5f}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
