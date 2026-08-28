#!/usr/bin/env python3
"""Audit a deterministic Bond-damage capture for visible HUD rings."""

from __future__ import annotations

import argparse
import json
import math
import re
import sys
from pathlib import Path
from typing import Any, Callable


DRAWCLASS_RE = re.compile(
    r"\[DRAWCLASS-TRIS\]\s+frame=(?P<frame>-?\d+).*?\bhud=(?P<hud>-?\d+)"
)


def load_image(path: Path):
    try:
        from PIL import Image
    except ImportError:
        raise SystemExit(
            "FAIL: Pillow is required for damage HUD audits. "
            "Install it with: python3 -m pip install pillow"
        )

    try:
        with Image.open(path) as image:
            return image.convert("RGB")
    except Exception as exc:
        raise SystemExit(f"FAIL: could not read screenshot {path}: {exc}")


def load_jsonl(path: Path) -> list[dict[str, Any]]:
    records: list[dict[str, Any]] = []

    try:
        with path.open("r", encoding="utf-8", errors="replace") as handle:
            for line in handle:
                line = line.strip()
                if not line or not line.startswith("{"):
                    continue
                try:
                    record = json.loads(line)
                except json.JSONDecodeError:
                    continue
                if isinstance(record, dict):
                    records.append(record)
    except FileNotFoundError:
        raise SystemExit(f"FAIL: trace not found: {path}")

    return records


def nested(record: dict[str, Any], *path: str) -> Any:
    value: Any = record
    for part in path:
        if not isinstance(value, dict):
            return None
        value = value.get(part)
    return value


def as_int(value: Any) -> int | None:
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
            return None
    return None


def as_float(value: Any) -> float | None:
    if isinstance(value, bool):
        return float(int(value))
    if isinstance(value, (int, float)):
        parsed = float(value)
        return parsed if math.isfinite(parsed) else None
    if isinstance(value, str):
        try:
            parsed = float(value)
        except ValueError:
            return None
        return parsed if math.isfinite(parsed) else None
    return None


def scaled_box(width: int, height: int, box: tuple[int, int, int, int]) -> tuple[int, int, int, int]:
    x0 = max(0, min(width, round(box[0] * width / 640.0)))
    y0 = max(0, min(height, round(box[1] * height / 480.0)))
    x1 = max(0, min(width, round(box[2] * width / 640.0)))
    y1 = max(0, min(height, round(box[3] * height / 480.0)))
    return x0, y0, x1, y1


def count_region(image, box: tuple[int, int, int, int], predicate: Callable[[int, int, int], bool]) -> int:
    x0, y0, x1, y1 = box
    count = 0
    pixels = image.load()

    for y in range(y0, y1):
        for x in range(x0, x1):
            r, g, b = pixels[x, y]
            if predicate(r, g, b):
                count += 1
    return count


def warm_ring_pixel(r: int, g: int, b: int) -> bool:
    return r >= 175 and 35 <= g <= 235 and b <= 150 and r >= g + 12 and r >= b + 70


def cool_ring_pixel(r: int, g: int, b: int) -> bool:
    return 55 <= r <= 175 and 55 <= g <= 185 and b >= 105 and b >= r + 18 and b >= g + 4


def audit_pixels(args: argparse.Namespace) -> tuple[list[str], dict[str, Any]]:
    image = load_image(Path(args.screenshot))
    width, height = image.size
    failures: list[str] = []

    left_box = scaled_box(width, height, (92, 72, 260, 380))
    right_box = scaled_box(width, height, (380, 72, 555, 380))
    warm_pixels = count_region(image, left_box, warm_ring_pixel)
    cool_pixels = count_region(image, right_box, cool_ring_pixel)

    if (width, height) != tuple(args.expect_size):
        failures.append(
            "screenshot size %dx%d != expected %dx%d"
            % (width, height, args.expect_size[0], args.expect_size[1])
        )
    if warm_pixels < args.min_warm_pixels:
        failures.append(f"warm health-ring pixels {warm_pixels} < {args.min_warm_pixels}")
    if cool_pixels < args.min_cool_pixels:
        failures.append(f"cool armor-ring pixels {cool_pixels} < {args.min_cool_pixels}")

    metrics = {
        "screenshot": args.screenshot,
        "size": {"width": width, "height": height},
        "left_box": left_box,
        "right_box": right_box,
        "warm_pixels": warm_pixels,
        "cool_pixels": cool_pixels,
        "min_warm_pixels": args.min_warm_pixels,
        "min_cool_pixels": args.min_cool_pixels,
    }
    return failures, metrics


def audit_trace(args: argparse.Namespace) -> tuple[list[str], dict[str, Any]]:
    records = load_jsonl(Path(args.trace))
    failures: list[str] = []
    active: dict[str, Any] | None = None

    for record in records:
        frame = as_int(record.get("f"))
        if frame is None or frame < args.damage_frame or frame > args.screenshot_frame:
            continue

        damage_show = as_int(nested(record, "combat", "health", "damage_show"))
        health_show = as_int(nested(record, "combat", "health", "health_show"))
        actual_h = as_float(nested(record, "combat", "health", "actual_h"))
        bond_h = as_float(nested(record, "combat", "health", "bond"))

        if damage_show is None or health_show is None or actual_h is None or bond_h is None:
            continue

        if (
            damage_show >= args.min_damage_show
            and health_show >= args.min_health_show
            and (actual_h <= args.max_actual_health or bond_h <= args.max_bond_health)
        ):
            active = {
                "frame": frame,
                "damage_show": damage_show,
                "health_show": health_show,
                "actual_h": actual_h,
                "bond": bond_h,
            }

    if not records:
        failures.append("trace has no JSON records")
    if active is None:
        failures.append(
            "no active damage HUD state between frames %d and %d"
            % (args.damage_frame, args.screenshot_frame)
        )

    metrics = {
        "trace": args.trace,
        "records": len(records),
        "active_damage_record": active,
        "damage_frame": args.damage_frame,
        "screenshot_frame": args.screenshot_frame,
        "min_damage_show": args.min_damage_show,
        "min_health_show": args.min_health_show,
        "max_actual_health": args.max_actual_health,
        "max_bond_health": args.max_bond_health,
    }
    return failures, metrics


def audit_log(args: argparse.Namespace) -> tuple[list[str], dict[str, Any]]:
    failures: list[str] = []
    max_hud_tris = -1
    max_hud_frame: int | None = None
    first_frame = max(0, args.screenshot_frame - args.drawclass_frame_slop)

    try:
        lines = Path(args.log).read_text(encoding="utf-8", errors="replace").splitlines()
    except FileNotFoundError:
        raise SystemExit(f"FAIL: log not found: {args.log}")

    for line in lines:
        match = DRAWCLASS_RE.search(line)
        if not match:
            continue
        frame = int(match.group("frame"))
        hud = int(match.group("hud"))
        if frame < first_frame or frame > args.screenshot_frame:
            continue
        if hud > max_hud_tris:
            max_hud_tris = hud
            max_hud_frame = frame

    if args.min_hud_tris > 0 and max_hud_tris < args.min_hud_tris:
        failures.append(
            "HUD-class triangles %d < %d near screenshot frame"
            % (max_hud_tris, args.min_hud_tris)
        )

    metrics = {
        "log": args.log,
        "max_hud_tris": max_hud_tris,
        "max_hud_frame": max_hud_frame,
        "min_hud_tris": args.min_hud_tris,
        "drawclass_first_frame": first_frame,
        "drawclass_last_frame": args.screenshot_frame,
    }
    return failures, metrics


def parse_size(value: str) -> tuple[int, int]:
    parts = value.lower().split("x")
    if len(parts) != 2:
        raise argparse.ArgumentTypeError("expected WIDTHxHEIGHT")
    try:
        width = int(parts[0])
        height = int(parts[1])
    except ValueError:
        raise argparse.ArgumentTypeError("expected WIDTHxHEIGHT")
    if width <= 0 or height <= 0:
        raise argparse.ArgumentTypeError("width and height must be positive")
    return width, height


def write_json(path: str | None, metrics: dict[str, Any]) -> None:
    if path:
        Path(path).write_text(json.dumps(metrics, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--label", default="damage HUD capture")
    parser.add_argument("--screenshot", required=True)
    parser.add_argument("--trace", required=True)
    parser.add_argument("--log", required=True)
    parser.add_argument("--json-out")
    parser.add_argument("--expect-size", type=parse_size, default=(640, 480))
    parser.add_argument("--damage-frame", type=int, default=300)
    parser.add_argument("--screenshot-frame", type=int, default=306)
    parser.add_argument("--drawclass-frame-slop", type=int, default=2)
    parser.add_argument("--min-damage-show", type=int, default=1)
    parser.add_argument("--min-health-show", type=int, default=1)
    parser.add_argument("--max-actual-health", type=float, default=1.01)
    parser.add_argument("--max-bond-health", type=float, default=0.99)
    parser.add_argument("--min-hud-tris", type=int, default=0)
    parser.add_argument("--min-warm-pixels", type=int, default=500)
    parser.add_argument("--min-cool-pixels", type=int, default=220)
    args = parser.parse_args(argv)

    failures: list[str] = []
    metrics: dict[str, Any] = {
        "label": args.label,
        "status": "pass",
    }

    for key, audit_fn in (
        ("pixels", audit_pixels),
        ("trace", audit_trace),
        ("drawclass", audit_log),
    ):
        audit_failures, audit_metrics = audit_fn(args)
        metrics[key] = audit_metrics
        failures.extend(audit_failures)

    if failures:
        metrics["status"] = "fail"
        metrics["failures"] = failures
        write_json(args.json_out, metrics)
        print(f"FAIL: {args.label}: {len(failures)} issue(s)")
        for failure in failures:
            print(f"  - {failure}")
        return 1

    write_json(args.json_out, metrics)
    print(
        "PASS: %s: warm=%d cool=%d hud_tris=%d active_frame=%s"
        % (
            args.label,
            metrics["pixels"]["warm_pixels"],
            metrics["pixels"]["cool_pixels"],
            metrics["drawclass"]["max_hud_tris"],
            metrics["trace"]["active_damage_record"]["frame"],
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
