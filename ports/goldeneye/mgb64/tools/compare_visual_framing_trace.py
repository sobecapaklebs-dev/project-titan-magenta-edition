#!/usr/bin/env python3
"""Compare stock/native visual-checkpoint camera framing in trace JSONL files."""

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


def as_int(value: Any) -> int | None:
    if isinstance(value, bool):
        return int(value)
    if isinstance(value, int):
        return value
    if isinstance(value, float) and math.isfinite(value):
        return int(value)
    if isinstance(value, str):
        try:
            return int(value, 0)
        except ValueError:
            return None
    return None


def as_float(value: Any) -> float | None:
    if isinstance(value, (int, float)) and not isinstance(value, bool):
        parsed = float(value)
        return parsed if math.isfinite(parsed) else None
    return None


def number_list(value: Any, length: int | None = None) -> list[float] | None:
    if not isinstance(value, list):
        return None
    if length is not None and len(value) != length:
        return None
    result: list[float] = []
    for item in value:
        parsed = as_float(item)
        if parsed is None:
            return None
        result.append(parsed)
    return result


def nested(record: dict[str, Any], *path: str) -> Any:
    value: Any = record
    for part in path:
        if not isinstance(value, dict):
            return None
        value = value.get(part)
    return value


def record_frame(record: dict[str, Any]) -> int:
    return as_int(record.get("f")) or 0


def move_global(record: dict[str, Any]) -> int | None:
    return as_int(nested(record, "move", "global"))


def select_record(records: list[dict[str, Any]], *, frame: int | None, global_timer: int | None) -> dict[str, Any] | None:
    if not records:
        return None
    if global_timer is not None:
        with_global = [record for record in records if move_global(record) is not None]
        if not with_global:
            return None
        before = [record for record in with_global if (move_global(record) or -1) <= global_timer]
        if before:
            return max(before, key=lambda record: (move_global(record) or -1, record_frame(record)))
        return min(with_global, key=lambda record: abs((move_global(record) or 0) - global_timer))
    if frame is not None:
        before = [record for record in records if record_frame(record) <= frame]
        if before:
            return max(before, key=record_frame)
        return min(records, key=lambda record: abs(record_frame(record) - frame))
    return records[-1]


def distance(lhs: list[float] | None, rhs: list[float] | None) -> float | None:
    if lhs is None or rhs is None or len(lhs) != len(rhs):
        return None
    return math.sqrt(sum((a - b) * (a - b) for a, b in zip(lhs, rhs)))


def max_abs_delta(lhs: list[float] | None, rhs: list[float] | None) -> float | None:
    if lhs is None or rhs is None or len(lhs) != len(rhs):
        return None
    return max(abs(a - b) for a, b in zip(lhs, rhs)) if lhs else 0.0


def room_set(record: dict[str, Any]) -> list[int]:
    rooms = record.get("rooms")
    if not isinstance(rooms, dict):
        return []
    vis = rooms.get("vis")
    if isinstance(vis, dict):
        for key in ("draw_sample", "rendered_sample", "sample"):
            values = vis.get(key)
            if isinstance(values, list):
                parsed = sorted({value for value in (as_int(item) for item in values) if value is not None and value >= 0})
                if parsed:
                    return parsed
    dl_rooms: list[int] = []
    dl = rooms.get("dl")
    if isinstance(dl, list):
        for item in dl:
            if not isinstance(item, dict) or not as_int(item.get("rendered")):
                continue
            room = as_int(item.get("room"))
            if room is not None and room >= 0:
                dl_rooms.append(room)
    return sorted(set(dl_rooms))


def summarize(record: dict[str, Any] | None, label: str) -> dict[str, Any]:
    if record is None:
        return {"label": label, "present": False}
    view = number_list(record.get("view"), 4)
    vi_view = number_list(record.get("vi_view"), 4)
    view_basis = record.get("view_basis") if isinstance(record.get("view_basis"), dict) else {}
    return {
        "label": label,
        "present": True,
        "frame": record_frame(record),
        "global": move_global(record),
        "gameplay_frame": as_int(nested(record, "oracle", "gameplay_frame")),
        "pos": number_list(record.get("pos"), 3),
        "cam_pos": number_list(record.get("cam_pos"), 3),
        "cam_target": number_list(record.get("cam_target"), 3),
        "render_cam_pos": number_list(record.get("render_cam_pos"), 3),
        "render_cam_target": number_list(record.get("render_cam_target"), 3),
        "cam_up": number_list(record.get("cam_up"), 3),
        "facing": number_list(record.get("facing"), 3),
        "room_basis": number_list(record.get("room_basis"), 3),
        "view": view,
        "vi_view": vi_view,
        "effective_view": vi_view or view,
        "theta": as_float(record.get("theta")),
        "cam": as_int(record.get("cam")),
        "icam": as_int(record.get("icam")),
        "view_basis": {
            "vv_verta": as_float(view_basis.get("vv_verta")),
            "vv_verta360": as_float(view_basis.get("vv_verta360")),
            "vv_cosverta": as_float(view_basis.get("vv_cosverta")),
            "vv_sinverta": as_float(view_basis.get("vv_sinverta")),
            "headlook": number_list(view_basis.get("headlook"), 3),
            "headup": number_list(view_basis.get("headup"), 3),
        },
        "rooms": {
            "tile": as_int(nested(record, "rooms", "tile")),
            "portal": as_int(nested(record, "rooms", "portal")),
            "prop": as_int(nested(record, "rooms", "prop")),
            "cur": as_int(nested(record, "rooms", "cur")),
            "render": as_int(nested(record, "rooms", "render")),
            "rendered_set": room_set(record),
            "raw_vis": nested(record, "rooms", "vis"),
        },
    }


def scalar_delta(lhs: float | None, rhs: float | None) -> float | None:
    if lhs is None or rhs is None:
        return None
    return rhs - lhs


def abs_scalar_delta(lhs: float | None, rhs: float | None) -> float | None:
    delta = scalar_delta(lhs, rhs)
    return abs(delta) if delta is not None else None


def fail_if_vector_delta(
    failures: list[str],
    deltas: dict[str, Any],
    key: str,
    label: str,
    limit: float | None,
) -> None:
    value = deltas.get(key)
    if limit is None:
        return
    if value is None:
        failures.append(f"{label} delta is unavailable")
    elif value > limit:
        failures.append(f"{label} delta {value:.6f} exceeds {limit:.6f}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("baseline", type=Path)
    parser.add_argument("test", type=Path)
    parser.add_argument("--baseline-label", default="baseline")
    parser.add_argument("--test-label", default="test")
    parser.add_argument("--baseline-frame", type=int)
    parser.add_argument("--test-frame", type=int)
    parser.add_argument("--baseline-global", type=int)
    parser.add_argument("--test-global", type=int)
    parser.add_argument("--max-camera-pos-delta", type=float)
    parser.add_argument("--max-camera-target-delta", type=float)
    parser.add_argument("--max-render-camera-pos-delta", type=float)
    parser.add_argument("--max-render-camera-target-delta", type=float)
    parser.add_argument("--max-cam-up-delta", type=float)
    parser.add_argument("--max-facing-delta", type=float)
    parser.add_argument("--max-room-basis-delta", type=float)
    parser.add_argument("--max-view-delta", type=float)
    parser.add_argument("--max-vv-verta-delta", type=float)
    parser.add_argument("--require-room-set-match", action="store_true")
    parser.add_argument("--json-out", type=Path)
    args = parser.parse_args()

    baseline = summarize(
        select_record(load_records(args.baseline), frame=args.baseline_frame, global_timer=args.baseline_global),
        args.baseline_label,
    )
    test = summarize(
        select_record(load_records(args.test), frame=args.test_frame, global_timer=args.test_global),
        args.test_label,
    )

    failures: list[str] = []
    if not baseline.get("present"):
        failures.append("baseline checkpoint not found")
    if not test.get("present"):
        failures.append("test checkpoint not found")

    room_base = set(baseline.get("rooms", {}).get("rendered_set", []))
    room_test = set(test.get("rooms", {}).get("rendered_set", []))
    room_delta = {
        "baseline_only": sorted(room_base - room_test),
        "test_only": sorted(room_test - room_base),
        "matches": sorted(room_base & room_test),
    }

    deltas = {
        "pos": distance(baseline.get("pos"), test.get("pos")),
        "cam_pos": distance(baseline.get("cam_pos"), test.get("cam_pos")),
        "cam_target": distance(baseline.get("cam_target"), test.get("cam_target")),
        "render_cam_pos": distance(baseline.get("render_cam_pos"), test.get("render_cam_pos")),
        "render_cam_target": distance(baseline.get("render_cam_target"), test.get("render_cam_target")),
        "cam_up": distance(baseline.get("cam_up"), test.get("cam_up")),
        "facing": distance(baseline.get("facing"), test.get("facing")),
        "room_basis": distance(baseline.get("room_basis"), test.get("room_basis")),
        "view": max_abs_delta(baseline.get("effective_view"), test.get("effective_view")),
        "vv_verta": abs_scalar_delta(
            baseline.get("view_basis", {}).get("vv_verta"),
            test.get("view_basis", {}).get("vv_verta"),
        ),
        "theta": scalar_delta(baseline.get("theta"), test.get("theta")),
        "room_set": room_delta,
    }

    fail_if_vector_delta(failures, deltas, "cam_pos", "camera position", args.max_camera_pos_delta)
    fail_if_vector_delta(failures, deltas, "cam_target", "camera target", args.max_camera_target_delta)
    fail_if_vector_delta(failures, deltas, "render_cam_pos", "render camera position", args.max_render_camera_pos_delta)
    fail_if_vector_delta(failures, deltas, "render_cam_target", "render camera target", args.max_render_camera_target_delta)
    fail_if_vector_delta(failures, deltas, "cam_up", "camera up", args.max_cam_up_delta)
    fail_if_vector_delta(failures, deltas, "facing", "facing", args.max_facing_delta)
    fail_if_vector_delta(failures, deltas, "room_basis", "room basis", args.max_room_basis_delta)
    fail_if_vector_delta(failures, deltas, "view", "viewport", args.max_view_delta)
    fail_if_vector_delta(failures, deltas, "vv_verta", "view pitch", args.max_vv_verta_delta)

    if args.require_room_set_match and (room_delta["baseline_only"] or room_delta["test_only"]):
        failures.append(
            "rendered room sets differ: "
            f"baseline_only={room_delta['baseline_only']} test_only={room_delta['test_only']}"
        )

    payload = {
        "status": "fail" if failures else "pass",
        "failures": failures,
        "baseline": baseline,
        "test": test,
        "deltas": deltas,
        "thresholds": {
            "max_camera_pos_delta": args.max_camera_pos_delta,
            "max_camera_target_delta": args.max_camera_target_delta,
            "max_render_camera_pos_delta": args.max_render_camera_pos_delta,
            "max_render_camera_target_delta": args.max_render_camera_target_delta,
            "max_cam_up_delta": args.max_cam_up_delta,
            "max_facing_delta": args.max_facing_delta,
            "max_room_basis_delta": args.max_room_basis_delta,
            "max_view_delta": args.max_view_delta,
            "max_vv_verta_delta": args.max_vv_verta_delta,
            "require_room_set_match": args.require_room_set_match,
        },
    }
    if args.json_out:
        args.json_out.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")

    print(
        "=== visual framing: "
        f"{args.baseline_label} frame={baseline.get('frame')} vs {args.test_label} frame={test.get('frame')} ==="
    )
    print(
        "  camera: "
        f"pos_delta={deltas['cam_pos']} target_delta={deltas['cam_target']} "
        f"render_pos_delta={deltas['render_cam_pos']} render_target_delta={deltas['render_cam_target']} "
        f"up_delta={deltas['cam_up']} facing_delta={deltas['facing']} pitch_delta={deltas['vv_verta']}"
    )
    print(
        "  room/view: "
        f"basis_delta={deltas['room_basis']} view_delta={deltas['view']} "
        f"rooms baseline={sorted(room_base)} test={sorted(room_test)} "
        f"baseline_only={room_delta['baseline_only']} test_only={room_delta['test_only']}"
    )
    if failures:
        print("FAIL: visual framing comparison failed")
        for failure in failures:
            print(f"  - {failure}")
        return 1
    print("PASS: visual framing comparison")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
