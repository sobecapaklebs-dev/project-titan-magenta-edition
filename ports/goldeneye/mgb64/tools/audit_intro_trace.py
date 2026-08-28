#!/usr/bin/env python3
"""Audit native intro actor/render fields in a JSONL trace."""

from __future__ import annotations

import argparse
import json
import math
import statistics
import sys
from dataclasses import dataclass, field
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


@dataclass
class Counts:
    active: int = 0
    present: int = 0
    onscreen: int = 0
    seen_onscreen: int = 0
    model_mtx: int = 0
    rendered: int = 0
    anim: int = 0
    right_item_match: int = 0
    right_item_mismatch: int = 0
    first_present_frame: int | None = None
    first_render_frame: int | None = None
    first_anim_frame: int | None = None
    min_anim_frame: float | None = None
    max_anim_frame: float | None = None
    anim_hash: int = 0
    anim_hash_values: set[str] = field(default_factory=set)
    max_render_count: int = 0
    last_render_count: int = 0
    render_count_regressions: int = 0
    bond_body_frames: int = 0
    min_render_pos_count: int | None = None
    grounding_offsets: list[float] = field(default_factory=list)


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
        text = value.strip()
        if not text:
            return None
        try:
            result = float(text)
        except ValueError:
            return None
        return result if math.isfinite(result) else None
    return None


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


def intro_value(record: dict[str, Any], field: str) -> int:
    intro = record.get("intro")
    if not isinstance(intro, dict):
        return 0
    value = parse_int(intro.get(field))
    return 0 if value is None else value


def intro_nested(record: dict[str, Any], *path: str) -> Any:
    value: Any = record.get("intro")
    for part in path:
        if not isinstance(value, dict):
            return None
        value = value.get(part)
    return value


def record_frame(record: dict[str, Any]) -> int | None:
    return parse_int(record.get("f"))


# --- H17: first-swirl-tick applied_view must not still be the degenerate
# (1,0,0) seed. src/platform/port_trace.c:6495-6522 emits top-level
# "cam_pos"/"cam_target" arrays; during CAMERAMODE_SWIRL (cam==3) these come
# from the *live* (non-frozen) branch where cam_target = cam_pos +
# applied_view, so applied_view is recoverable exactly as
# cam_target - cam_pos without any new traced field (per the task brief:
# don't extend port_trace.c for this).
SWIRL_CAMERA_MODE = CAMERA_MODES["swirl"]
DEGENERATE_APPLIED_VIEW = (1.0, 0.0, 0.0)
DEGENERATE_APPLIED_VIEW_TOLERANCE = 1e-4


def record_vec3(record: dict[str, Any], field: str) -> tuple[float, float, float] | None:
    value = record.get(field)
    if not isinstance(value, list) or len(value) != 3:
        return None
    parsed = [parse_float(v) for v in value]
    if any(v is None for v in parsed):
        return None
    return (parsed[0], parsed[1], parsed[2])  # type: ignore[return-value]


def applied_view_from_record(record: dict[str, Any]) -> tuple[float, float, float] | None:
    cam_pos = record_vec3(record, "cam_pos")
    cam_target = record_vec3(record, "cam_target")
    if cam_pos is None or cam_target is None:
        return None
    return (cam_target[0] - cam_pos[0], cam_target[1] - cam_pos[1], cam_target[2] - cam_pos[2])


def is_degenerate_applied_view(applied_view: tuple[float, float, float]) -> bool:
    return all(
        math.isclose(a, b, rel_tol=0.0, abs_tol=DEGENERATE_APPLIED_VIEW_TOLERANCE)
        for a, b in zip(applied_view, DEGENERATE_APPLIED_VIEW)
    )


def first_swirl_applied_view(
    records: list[dict[str, Any]],
    swirl_mode: int = SWIRL_CAMERA_MODE,
    require_player: bool = False,
    require_frozen: bool = False,
) -> tuple[int | None, tuple[float, float, float] | None]:
    """Returns (frame, applied_view) for the first record whose `cam` field
    is the swirl mode AND which passes the same require_player/require_frozen
    eligibility filtering as every other counted metric (see `is_active`),
    or (None, None) if no such record has a decodable applied_view.
    `applied_view` is None (with a frame number) if an eligible swirl record
    was found but cam_pos/cam_target were missing/malformed.

    Routes that enable H17 (native_intro_require_h17_swirl_facing) also set
    native_intro_require_player/native_intro_require_frozen, so this must
    honor the same flags rather than looking at the raw, unfiltered trace."""
    swirl_modes = {swirl_mode}
    for record in records:
        if not is_active(record, swirl_modes, require_player, require_frozen):
            continue
        return record_frame(record), applied_view_from_record(record)
    return None, None


def is_active(record: dict[str, Any], modes: set[int], require_player: bool, require_frozen: bool) -> bool:
    cam = parse_int(record.get("cam"))
    if cam not in modes:
        return False
    if require_player and parse_int(record.get("p")) != 1:
        return False
    if require_frozen and parse_int(record.get("p_unk")) != 1:
        return False
    return True


def audit(
    records: list[dict[str, Any]],
    modes: set[int],
    require_player: bool,
    require_frozen: bool,
    required_right_item: int | None,
) -> Counts:
    counts = Counts()

    for record in records:
        if not is_active(record, modes, require_player, require_frozen):
            continue

        frame = record_frame(record)
        render_count = intro_value(record, "bond_render_count")
        anim_frame = parse_float(intro_nested(record, "bond_anim", "frame"))
        anim_hash = intro_nested(record, "bond_anim", "hash")
        right_item = parse_int(intro_nested(record, "bond_held", "right", "item"))

        counts.active += 1
        if intro_value(record, "bond_present"):
            counts.present += 1
            if counts.first_present_frame is None:
                counts.first_present_frame = frame
        if intro_value(record, "bond_onscreen"):
            counts.onscreen += 1
        if intro_value(record, "bond_seen_onscreen"):
            counts.seen_onscreen += 1
        if intro_value(record, "bond_model_mtx"):
            counts.model_mtx += 1
        if intro_value(record, "bond_rendered"):
            counts.rendered += 1
            if counts.first_render_frame is None:
                counts.first_render_frame = frame
        if intro_value_from_nested(record, "bond_anim", "valid"):
            counts.anim += 1
            if counts.first_anim_frame is None:
                counts.first_anim_frame = frame
        if anim_frame is not None and intro_value_from_nested(record, "bond_anim", "valid"):
            counts.min_anim_frame = anim_frame if counts.min_anim_frame is None else min(counts.min_anim_frame, anim_frame)
            counts.max_anim_frame = anim_frame if counts.max_anim_frame is None else max(counts.max_anim_frame, anim_frame)
        if intro_value_from_nested(record, "bond_anim", "valid") and isinstance(anim_hash, str):
            if anim_hash and anim_hash != "0x0000000000000000":
                counts.anim_hash += 1
                counts.anim_hash_values.add(anim_hash)
        if required_right_item is not None and intro_value(record, "bond_present"):
            if right_item == required_right_item:
                counts.right_item_match += 1
            else:
                counts.right_item_mismatch += 1

        if render_count < counts.last_render_count:
            counts.render_count_regressions += 1
        counts.last_render_count = render_count
        counts.max_render_count = max(counts.max_render_count, render_count)

        # M1.4/R8: projected viewer-body geometry (emitted as intro.bond_body).
        body = intro_nested(record, "bond_body")
        if isinstance(body, dict) and parse_int(body.get("projected")) == 1:
            counts.bond_body_frames += 1
            rpc = parse_int(body.get("render_pos_count"))
            if rpc is not None:
                counts.min_render_pos_count = (
                    rpc if counts.min_render_pos_count is None
                    else min(counts.min_render_pos_count, rpc)
                )
            world_root = body.get("world_root")
            floor_y = parse_float(body.get("floor_y"))
            if (parse_int(body.get("floor_valid")) == 1 and floor_y is not None
                    and isinstance(world_root, list) and len(world_root) == 3):
                root_y = parse_float(world_root[1])
                if root_y is not None:
                    counts.grounding_offsets.append(root_y - floor_y)

    return counts


def intro_value_from_nested(record: dict[str, Any], *path: str) -> int:
    value = parse_int(intro_nested(record, *path))
    return 0 if value is None else value


def require_min(errors: list[str], label: str, name: str, actual: int, expected: int | None) -> None:
    if expected is not None and actual < expected:
        errors.append(f"{label}: {name} {actual} < required {expected}")


def require_first_at_most(
    errors: list[str],
    label: str,
    name: str,
    actual: int | None,
    expected: int | None,
) -> None:
    if expected is None:
        return
    if actual is None:
        errors.append(f"{label}: {name} was never observed")
    elif actual > expected:
        errors.append(f"{label}: {name} first observed at frame {actual}, after required {expected}")


def require_anim_advance(
    errors: list[str],
    label: str,
    minimum: float | None,
    min_frame: float | None,
    max_frame: float | None,
) -> None:
    if minimum is None:
        return
    if min_frame is None or max_frame is None:
        errors.append(f"{label}: animation frame range was never observed")
        return
    advance = max_frame - min_frame
    if advance < minimum:
        errors.append(f"{label}: animation frame advance {advance:.2f} < required {minimum:.2f}")


def counts_dict(counts: Counts) -> dict[str, Any]:
    return {
        "active": counts.active,
        "present": counts.present,
        "onscreen": counts.onscreen,
        "seen_onscreen": counts.seen_onscreen,
        "model_mtx": counts.model_mtx,
        "rendered": counts.rendered,
        "anim": counts.anim,
        "right_item_match": counts.right_item_match,
        "right_item_mismatch": counts.right_item_mismatch,
        "first_present_frame": counts.first_present_frame,
        "first_render_frame": counts.first_render_frame,
        "first_anim_frame": counts.first_anim_frame,
        "min_anim_frame": counts.min_anim_frame,
        "max_anim_frame": counts.max_anim_frame,
        "anim_hash": counts.anim_hash,
        "anim_hash_values": sorted(counts.anim_hash_values),
        "max_render_count": counts.max_render_count,
        "last_render_count": counts.last_render_count,
        "render_count_regressions": counts.render_count_regressions,
    }


def write_json_metrics(path: str | None, metrics: dict[str, Any]) -> None:
    if not path:
        return
    Path(path).write_text(json.dumps(metrics, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace", help="native JSONL trace")
    parser.add_argument("--label", default="intro trace")
    parser.add_argument("--camera-modes", default="intro,fadeswirl,swirl", type=parse_modes)
    parser.add_argument("--require-player", action="store_true")
    parser.add_argument("--require-frozen", action="store_true")
    parser.add_argument("--require-bond-present", action="store_true")
    parser.add_argument("--require-bond-onscreen", action="store_true")
    parser.add_argument("--require-bond-model-mtx", action="store_true")
    parser.add_argument("--require-bond-rendered", action="store_true")
    parser.add_argument("--require-bond-anim", action="store_true")
    parser.add_argument("--require-bond-anim-hash", action="store_true")
    parser.add_argument("--require-right-item", type=lambda text: int(text, 0))
    parser.add_argument("--min-active-records", type=int)
    parser.add_argument("--min-present-frames", type=int)
    parser.add_argument("--min-onscreen-frames", type=int)
    parser.add_argument("--min-model-mtx-frames", type=int)
    parser.add_argument("--min-rendered-frames", type=int)
    parser.add_argument("--min-render-count", type=int)
    parser.add_argument("--min-anim-frames", type=int)
    parser.add_argument("--min-anim-hash-frames", type=int)
    parser.add_argument("--min-right-item-frames", type=int)
    parser.add_argument("--min-anim-advance", type=float)
    parser.add_argument("--max-first-present-frame", type=int)
    parser.add_argument("--max-first-render-frame", type=int)
    parser.add_argument("--allow-render-count-regression", action="store_true")
    parser.add_argument("--min-body-render-pos-count", type=int,
                        help="min intro.bond_body.render_pos_count across projected frames")
    parser.add_argument("--max-grounding-offset", type=float,
                        help="max |median(root_y - floor_y)| from intro.bond_body")
    parser.add_argument(
        "--require-h17-swirl-facing",
        action="store_true",
        help=(
            "H17 invariant: at the first mode-3 (swirl) record, the "
            "applied_view derived from cam_target-cam_pos must not still be "
            "the degenerate (1,0,0) seed."
        ),
    )
    parser.add_argument("--json-out", help="write intro actor/render audit metrics as JSON")
    args = parser.parse_args()

    records = load_jsonl(args.trace)
    counts = audit(
        records,
        args.camera_modes,
        args.require_player,
        args.require_frozen,
        args.require_right_item,
    )
    errors: list[str] = []

    require_min(errors, args.label, "active intro records", counts.active, args.min_active_records)

    if args.require_bond_present:
        require_min(errors, args.label, "Bond-present frames", counts.present, 1)
    if args.require_bond_onscreen:
        require_min(errors, args.label, "Bond-onscreen frames", counts.onscreen, 1)
    if args.require_bond_model_mtx:
        require_min(errors, args.label, "Bond model-matrix frames", counts.model_mtx, 1)
    if args.require_bond_rendered:
        require_min(errors, args.label, "Bond-rendered frames", counts.rendered, 1)
        require_min(errors, args.label, "Bond render count", counts.max_render_count, 1)
    if args.require_bond_anim:
        require_min(errors, args.label, "Bond animation frames", counts.anim, 1)
    if args.require_bond_anim_hash:
        require_min(errors, args.label, "Bond animation hash frames", counts.anim_hash, 1)
    if args.require_right_item is not None:
        require_min(errors, args.label, f"right-item {args.require_right_item} frames", counts.right_item_match, 1)
        if counts.right_item_mismatch:
            errors.append(
                f"{args.label}: right item mismatched expected {args.require_right_item} "
                f"for {counts.right_item_mismatch} active record(s)"
            )

    require_min(errors, args.label, "Bond-present frames", counts.present, args.min_present_frames)
    require_min(errors, args.label, "Bond-onscreen frames", counts.onscreen, args.min_onscreen_frames)
    require_min(errors, args.label, "Bond model-matrix frames", counts.model_mtx, args.min_model_mtx_frames)
    require_min(errors, args.label, "Bond-rendered frames", counts.rendered, args.min_rendered_frames)
    require_min(errors, args.label, "Bond render count", counts.max_render_count, args.min_render_count)
    require_min(errors, args.label, "Bond animation frames", counts.anim, args.min_anim_frames)
    require_min(errors, args.label, "Bond animation hash frames", counts.anim_hash, args.min_anim_hash_frames)
    require_min(errors, args.label, "right-item match frames", counts.right_item_match, args.min_right_item_frames)
    require_anim_advance(
        errors,
        args.label,
        args.min_anim_advance,
        counts.min_anim_frame,
        counts.max_anim_frame,
    )

    require_first_at_most(
        errors,
        args.label,
        "Bond present",
        counts.first_present_frame,
        args.max_first_present_frame,
    )
    require_first_at_most(
        errors,
        args.label,
        "Bond rendered",
        counts.first_render_frame,
        args.max_first_render_frame,
    )

    if counts.render_count_regressions and not args.allow_render_count_regression:
        errors.append(f"{args.label}: render count regressed {counts.render_count_regressions} time(s)")

    if args.min_body_render_pos_count is not None:
        if counts.bond_body_frames == 0:
            errors.append(f"{args.label}: no projected intro.bond_body frames")
        elif counts.min_render_pos_count is not None and counts.min_render_pos_count < args.min_body_render_pos_count:
            errors.append(
                f"{args.label}: body render_pos_count {counts.min_render_pos_count} "
                f"< required {args.min_body_render_pos_count}")
    if args.max_grounding_offset is not None:
        if not counts.grounding_offsets:
            errors.append(f"{args.label}: no floor-valid intro.bond_body frames for grounding")
        else:
            ground_med = statistics.median(counts.grounding_offsets)
            if abs(ground_med) > args.max_grounding_offset:
                errors.append(
                    f"{args.label}: grounding offset median {ground_med:.1f} "
                    f"> {args.max_grounding_offset:.1f} (Bond floating)")

    h17_frame: int | None = None
    h17_applied_view: tuple[float, float, float] | None = None
    if args.require_h17_swirl_facing:
        h17_frame, h17_applied_view = first_swirl_applied_view(
            records, require_player=args.require_player, require_frozen=args.require_frozen
        )
        if h17_frame is None:
            errors.append(f"{args.label}: H17: no swirl-mode (cam==3) record found in trace")
        elif h17_applied_view is None:
            errors.append(f"{args.label}: H17: swirl record at frame {h17_frame} has no decodable cam_pos/cam_target")
        elif is_degenerate_applied_view(h17_applied_view):
            errors.append(
                f"{args.label}: H17: first-swirl-tick applied_view at frame {h17_frame} "
                f"is still the degenerate seed {h17_applied_view} == (1,0,0)"
            )

    print(
        f"audit: {args.label}\n"
        f"  records={len(records)} active_intro_records={counts.active}\n"
        f"  bond_present={counts.present} first_present_frame={counts.first_present_frame}\n"
        f"  bond_onscreen={counts.onscreen} seen_onscreen={counts.seen_onscreen} "
        f"model_mtx={counts.model_mtx}\n"
        f"  bond_rendered={counts.rendered} first_render_frame={counts.first_render_frame} "
        f"max_render_count={counts.max_render_count}\n"
        f"  bond_anim={counts.anim} first_anim_frame={counts.first_anim_frame} "
        f"frame_range={counts.min_anim_frame}..{counts.max_anim_frame} "
        f"hash_frames={counts.anim_hash} unique_hashes={len(counts.anim_hash_values)}\n"
        f"  right_item_match={counts.right_item_match} "
        f"right_item_mismatch={counts.right_item_mismatch}"
        + (f"\n  h17_swirl_frame={h17_frame} h17_applied_view={h17_applied_view}" if args.require_h17_swirl_facing else "")
    )

    metrics = {
        "label": args.label,
        "trace": args.trace,
        "status": "fail" if errors else "pass",
        "records": len(records),
        "camera_modes": sorted(args.camera_modes),
        "counts": counts_dict(counts),
        "thresholds": {
            "require_player": args.require_player,
            "require_frozen": args.require_frozen,
            "require_bond_present": args.require_bond_present,
            "require_bond_onscreen": args.require_bond_onscreen,
            "require_bond_model_mtx": args.require_bond_model_mtx,
            "require_bond_rendered": args.require_bond_rendered,
            "require_bond_anim": args.require_bond_anim,
            "require_bond_anim_hash": args.require_bond_anim_hash,
            "require_right_item": args.require_right_item,
            "min_active_records": args.min_active_records,
            "min_present_frames": args.min_present_frames,
            "min_onscreen_frames": args.min_onscreen_frames,
            "min_model_mtx_frames": args.min_model_mtx_frames,
            "min_rendered_frames": args.min_rendered_frames,
            "min_render_count": args.min_render_count,
            "min_anim_frames": args.min_anim_frames,
            "min_anim_hash_frames": args.min_anim_hash_frames,
            "min_right_item_frames": args.min_right_item_frames,
            "min_anim_advance": args.min_anim_advance,
            "max_first_present_frame": args.max_first_present_frame,
            "max_first_render_frame": args.max_first_render_frame,
            "allow_render_count_regression": args.allow_render_count_regression,
            "require_h17_swirl_facing": args.require_h17_swirl_facing,
        },
        "h17": {
            "swirl_frame": h17_frame,
            "applied_view": list(h17_applied_view) if h17_applied_view is not None else None,
        },
        "failures": errors,
    }
    write_json_metrics(args.json_out, metrics)

    if errors:
        for error in errors:
            print(f"FAIL: {error}", file=sys.stderr)
        return 1

    print(f"PASS: {args.label}: intro actor/render trace audit")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
