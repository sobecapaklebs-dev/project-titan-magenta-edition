#!/usr/bin/env python3
"""Audit native minimap cache, snapshot, objective-pin, and overlay dumps."""

from __future__ import annotations

import argparse
import json
import math
import sys
from pathlib import Path
from typing import Any


OBJECTIVE_OBJECT_TYPES = {
    "objective_destroy_object",
    "objective_collect_object",
    "objective_deposit_object",
}
OBJECTIVE_ROOM_TYPES = {
    "objective_enter_room",
    "objective_deposit_object_in_room",
}


def load_json(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as handle:
        data = json.load(handle)
    if not isinstance(data, dict):
        raise ValueError(f"{path} did not contain a JSON object")
    return data


def load_jsonl(path: Path | None) -> list[dict[str, Any]]:
    if path is None:
        return []
    records: list[dict[str, Any]] = []
    with path.open("r", encoding="utf-8", errors="replace") as handle:
        for line in handle:
            line = line.strip()
            if not line:
                continue
            data = json.loads(line)
            if isinstance(data, dict):
                records.append(data)
    return records


def as_int(value: Any, default: int = 0) -> int:
    if isinstance(value, bool):
        return int(value)
    if isinstance(value, int):
        return value
    if isinstance(value, float) and value.is_integer():
        return int(value)
    if isinstance(value, str):
        try:
            return int(value, 0)
        except ValueError:
            return default
    return default


def finite_number(value: Any) -> bool:
    return isinstance(value, (int, float)) and math.isfinite(float(value))


def pin_is_finite(pin: dict[str, Any]) -> bool:
    return all(finite_number(pin.get(key)) for key in ("x", "y", "z"))


def pad_is_resolvable(pad_id: int, pads: set[int], boundpads: set[int]) -> bool:
    if pad_id < 0:
        return False
    if pad_id < 10000:
        return pad_id in pads
    return (pad_id - 10000) in boundpads


def expected_objective_icons(stage_records: list[dict[str, Any]]) -> dict[int, list[str]]:
    stage_meta = next((r for r in stage_records if r.get("scope") == "meta" and r.get("kind") == "stage"), {})
    difficulty = as_int(stage_meta.get("difficulty"), 0)
    objectives = {
        as_int(row.get("objective"), -1): row
        for row in stage_records
        if row.get("kind") == "objective"
    }
    tags = {
        as_int(row.get("tag"), -1): row
        for row in stage_records
        if row.get("kind") == "tag"
    }
    objects = {
        as_int(row.get("obj"), -1): row
        for row in stage_records
        if row.get("kind") == "object"
    }
    pads = {
        as_int(row.get("pad"), -1)
        for row in stage_records
        if row.get("kind") == "pad"
    }
    boundpads = {
        as_int(row.get("pad"), -1)
        for row in stage_records
        if row.get("kind") == "boundpad"
    }

    expected: dict[int, list[str]] = {}
    for row in stage_records:
        if row.get("kind") != "objective_criterion":
            continue
        objective = as_int(row.get("objective"), -1)
        if objective < 0:
            continue

        objective_row = objectives.get(objective)
        if objective_row is None:
            continue
        if as_int(objective_row.get("difficulty"), 99) > difficulty:
            continue
        if as_int(objective_row.get("status"), 0) == 1:
            continue

        type_name = str(row.get("type_name", ""))
        reasons = expected.setdefault(objective, [])

        if type_name in OBJECTIVE_OBJECT_TYPES:
            tag_id = as_int(row.get("ref"), -1)
            tag = tags.get(tag_id)
            obj = objects.get(as_int(tag.get("target_obj"), -1)) if tag else None
            if obj is not None and as_int(obj.get("has_pos"), 0):
                reasons.append(f"{type_name}:tag{tag_id}")
        elif type_name == "objective_photograph":
            if as_int(row.get("flag"), 0) == 0:
                tag_id = as_int(row.get("tag"), -1)
                tag = tags.get(tag_id)
                obj = objects.get(as_int(tag.get("target_obj"), -1)) if tag else None
                if obj is not None and as_int(obj.get("has_pos"), 0):
                    reasons.append(f"{type_name}:tag{tag_id}")
        elif type_name == "objective_enter_room":
            if as_int(row.get("status"), 0) == 0:
                pad_id = as_int(row.get("pad"), -1)
                if pad_is_resolvable(pad_id, pads, boundpads):
                    reasons.append(f"{type_name}:pad{pad_id}")
        elif type_name == "objective_deposit_object_in_room":
            if as_int(row.get("flag"), 0) == 0:
                pad_id = as_int(row.get("pad"), -1)
                if pad_is_resolvable(pad_id, pads, boundpads):
                    reasons.append(f"{type_name}:pad{pad_id}")

    return {key: value for key, value in expected.items() if value}


def compare_reference(minimap: dict[str, Any], reference: dict[str, Any]) -> list[str]:
    failures: list[str] = []
    for key in ("stage", "ready", "setup_ready", "poly_count", "overflow_count"):
        if minimap.get(key) != reference.get(key):
            failures.append(f"reference mismatch for {key}: {minimap.get(key)!r} != {reference.get(key)!r}")

    rooms = minimap.get("rooms")
    ref_rooms = reference.get("rooms")
    if isinstance(rooms, list) and isinstance(ref_rooms, list) and len(rooms) != len(ref_rooms):
        failures.append(f"reference room count mismatch: {len(rooms)} != {len(ref_rooms)}")

    bbox = minimap.get("bbox")
    ref_bbox = reference.get("bbox")
    if isinstance(bbox, dict) and isinstance(ref_bbox, dict):
        for key in ("x_min", "z_min", "x_max", "z_max", "y_min", "y_max"):
            value = bbox.get(key)
            ref_value = ref_bbox.get(key)
            if finite_number(value) and finite_number(ref_value):
                if abs(float(value) - float(ref_value)) > 0.01:
                    failures.append(f"reference bbox mismatch for {key}: {value!r} != {ref_value!r}")

    return failures


def audit(args: argparse.Namespace) -> tuple[int, dict[str, Any]]:
    minimap = load_json(Path(args.minimap_dump))
    stage_records = load_jsonl(Path(args.stage_dump) if args.stage_dump else None)
    overlay = load_json(Path(args.overlay_dump)) if args.overlay_dump else None
    reference = load_json(Path(args.reference_minimap)) if args.reference_minimap else None
    failures: list[str] = []

    rooms = minimap.get("rooms") if isinstance(minimap.get("rooms"), list) else []
    objectives = minimap.get("objectives") if isinstance(minimap.get("objectives"), list) else []
    enemies = minimap.get("enemies") if isinstance(minimap.get("enemies"), list) else []
    player = minimap.get("player")
    expected_icons = expected_objective_icons(stage_records)
    actual_icons = {
        as_int(pin.get("icon"), -1)
        for pin in objectives
        if isinstance(pin, dict) and pin.get("kind") == 0
    }

    if args.expect_stage is not None and as_int(minimap.get("stage"), -9999) != args.expect_stage:
        failures.append(f"stage mismatch: {minimap.get('stage')!r} != {args.expect_stage}")
    if args.require_ready and as_int(minimap.get("ready"), 0) != 1:
        failures.append("minimap cache was not ready")
    if args.require_ready and as_int(minimap.get("setup_ready"), 0) != 1:
        failures.append("minimap setup was not ready")
    if as_int(minimap.get("poly_count"), 0) < args.min_polys:
        failures.append(f"poly_count below threshold: {minimap.get('poly_count')} < {args.min_polys}")
    if len(rooms) < args.min_rooms:
        failures.append(f"room count below threshold: {len(rooms)} < {args.min_rooms}")

    bbox = minimap.get("bbox")
    if not isinstance(bbox, dict) or not all(finite_number(bbox.get(k)) for k in ("x_min", "z_min", "x_max", "z_max")):
        failures.append("minimap bbox is missing or non-finite")
    elif not (float(bbox["x_max"]) > float(bbox["x_min"]) and float(bbox["z_max"]) > float(bbox["z_min"])):
        failures.append("minimap bbox is degenerate")

    if args.expect_snapshot:
        if as_int(minimap.get("snapshot_count"), 0) < 1:
            failures.append("expected at least one minimap snapshot")
        if not isinstance(player, dict):
            failures.append("expected minimap player snapshot")
    if args.expect_no_snapshot:
        if as_int(minimap.get("snapshot_count"), 0) != 0:
            failures.append(f"expected no minimap snapshots, found {minimap.get('snapshot_count')}")
        if player is not None:
            failures.append("expected no player snapshot when minimap is disabled")

    if isinstance(player, dict):
        if not all(finite_number(player.get(key)) for key in ("x", "y", "z", "theta_deg")):
            failures.append("player snapshot has non-finite coordinates")
        if as_int(player.get("tile_valid"), 0) != 1:
            failures.append("player snapshot did not resolve a STAN tile")

    for index, pin in enumerate(objectives):
        if not isinstance(pin, dict) or not pin_is_finite(pin):
            failures.append(f"objective pin {index} is missing or non-finite")
    for index, pin in enumerate(enemies):
        if not isinstance(pin, dict) or not pin_is_finite(pin):
            failures.append(f"enemy pin {index} is missing or non-finite")

    if args.require_objective_pins_from_stage:
        missing = sorted(set(expected_icons) - actual_icons)
        if missing:
            failures.append(
                "missing objective pins for resolvable setup objectives: "
                + ", ".join(str(item) for item in missing)
            )

    if overlay is not None:
        if args.require_overlay_drawn:
            if overlay.get("status") != "drawn":
                failures.append(f"overlay status was not drawn: {overlay.get('status')!r}")
            if as_int(overlay.get("queued_frames"), 0) < 1:
                failures.append("overlay did not observe queued frames")
            if as_int(overlay.get("drawn_frames"), 0) < 1:
                failures.append("overlay did not draw a frame")
            if as_int(overlay.get("vertices_flushed"), 0) < args.min_overlay_vertices:
                failures.append(
                    f"overlay vertices below threshold: {overlay.get('vertices_flushed')} < {args.min_overlay_vertices}"
                )
        if args.expect_overlay_no_queue:
            if overlay.get("status") != "no_queue":
                failures.append(f"expected overlay no_queue, got {overlay.get('status')!r}")
            if as_int(overlay.get("queued_frames"), 0) != 0:
                failures.append(f"expected no overlay queue, found {overlay.get('queued_frames')}")

    if reference is not None:
        failures.extend(compare_reference(minimap, reference))

    metrics = {
        "label": args.label,
        "status": "fail" if failures else "pass",
        "minimap_dump": args.minimap_dump,
        "stage_dump": args.stage_dump,
        "overlay_dump": args.overlay_dump,
        "stage": minimap.get("stage"),
        "ready": minimap.get("ready"),
        "setup_ready": minimap.get("setup_ready"),
        "poly_count": minimap.get("poly_count"),
        "overflow_count": minimap.get("overflow_count"),
        "room_count": len(rooms),
        "snapshot_count": minimap.get("snapshot_count"),
        "objective_count": minimap.get("objective_count"),
        "enemy_count": minimap.get("enemy_count"),
        "expected_objective_icons": {str(key): value for key, value in sorted(expected_icons.items())},
        "actual_objective_icons": sorted(icon for icon in actual_icons if icon >= 0),
        "overlay": overlay,
        "failures": failures,
    }
    return (1 if failures else 0), metrics


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--label", default="minimap dump")
    parser.add_argument("--minimap-dump", required=True)
    parser.add_argument("--stage-dump")
    parser.add_argument("--overlay-dump")
    parser.add_argument("--reference-minimap")
    parser.add_argument("--expect-stage", type=int)
    parser.add_argument("--require-ready", action="store_true")
    parser.add_argument("--expect-snapshot", action="store_true")
    parser.add_argument("--expect-no-snapshot", action="store_true")
    parser.add_argument("--require-objective-pins-from-stage", action="store_true")
    parser.add_argument("--require-overlay-drawn", action="store_true")
    parser.add_argument("--expect-overlay-no-queue", action="store_true")
    parser.add_argument("--min-polys", type=int, default=1)
    parser.add_argument("--min-rooms", type=int, default=1)
    parser.add_argument("--min-overlay-vertices", type=int, default=32)
    parser.add_argument("--json-out")
    args = parser.parse_args(argv)

    if args.expect_snapshot and args.expect_no_snapshot:
        parser.error("--expect-snapshot and --expect-no-snapshot are mutually exclusive")
    if args.require_overlay_drawn and args.expect_overlay_no_queue:
        parser.error("--require-overlay-drawn and --expect-overlay-no-queue are mutually exclusive")

    code, metrics = audit(args)
    if args.json_out:
        Path(args.json_out).write_text(json.dumps(metrics, indent=2, sort_keys=True) + "\n", encoding="utf-8")

    print(f"audit: {args.label}")
    print(
        "  stage={stage} ready={ready} setup_ready={setup_ready} "
        "polys={polys} rooms={rooms} snapshots={snapshots} objectives={objectives}".format(
            stage=metrics["stage"],
            ready=metrics["ready"],
            setup_ready=metrics["setup_ready"],
            polys=metrics["poly_count"],
            rooms=metrics["room_count"],
            snapshots=metrics["snapshot_count"],
            objectives=metrics["objective_count"],
        )
    )
    if metrics.get("overlay") is not None:
        overlay = metrics["overlay"]
        print(
            "  overlay status={status} queued={queued} drawn={drawn} vertices={vertices}".format(
                status=overlay.get("status"),
                queued=overlay.get("queued_frames"),
                drawn=overlay.get("drawn_frames"),
                vertices=overlay.get("vertices_flushed"),
            )
        )
    if metrics["failures"]:
        print(f"FAIL: {args.label}: {len(metrics['failures'])} issue(s)")
        for failure in metrics["failures"]:
            print(f"  {failure}")
    else:
        print(f"PASS: {args.label}")
    return code


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
