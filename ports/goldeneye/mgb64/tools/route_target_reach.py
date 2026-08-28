#!/usr/bin/env python3
"""Summarize route trace proximity to setup-dump targets."""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
from typing import Any


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("artifacts", nargs="+", help="Route artifact directories")
    parser.add_argument(
        "--target",
        action="append",
        default=[],
        metavar="KEY=VALUE",
        help="Setup row matcher; repeat for exact target fields",
    )
    parser.add_argument(
        "--same-floor-y",
        type=float,
        default=35.0,
        help="Maximum |Y delta| for same-floor closest-hit reporting",
    )
    parser.add_argument(
        "--setup-dump",
        type=Path,
        help="Shared stage_pads.jsonl to use when an artifact has no setup dump",
    )
    parser.add_argument("--json", action="store_true", help="Write JSON instead of a table")
    return parser.parse_args()


def parse_scalar(value: str) -> Any:
    text = value.strip()
    if not text:
        return ""
    try:
        return int(text, 0)
    except ValueError:
        pass
    try:
        parsed = float(text)
    except ValueError:
        return text
    return parsed if math.isfinite(parsed) else text


def parse_target(items: list[str]) -> dict[str, Any]:
    target: dict[str, Any] = {}
    for item in items:
        key, sep, value = item.partition("=")
        if not sep or not key:
            raise SystemExit(f"FAIL: malformed --target {item!r}; expected KEY=VALUE")
        target[key] = parse_scalar(value)
    if not target:
        raise SystemExit("FAIL: at least one --target KEY=VALUE is required")
    return target


def load_jsonl(path: Path) -> list[dict[str, Any]]:
    records: list[dict[str, Any]] = []
    with path.open("r", encoding="utf-8", errors="replace") as handle:
        for raw in handle:
            raw = raw.strip()
            if not raw:
                continue
            data = json.loads(raw)
            if isinstance(data, dict):
                records.append(data)
    return records


def number(value: Any) -> float | None:
    if isinstance(value, bool):
        return None
    if isinstance(value, (int, float)):
        result = float(value)
        return result if math.isfinite(result) else None
    if isinstance(value, str):
        try:
            if value.lower().startswith("0x"):
                return float(int(value, 16))
            result = float(value)
        except ValueError:
            return None
        return result if math.isfinite(result) else None
    return None


def values_equal(actual: Any, expected: Any) -> bool:
    actual_num = number(actual)
    expected_num = number(expected)
    if actual_num is not None and expected_num is not None:
        return math.isclose(actual_num, expected_num, rel_tol=0.0, abs_tol=0.0001)
    return actual == expected


def row_matches(row: dict[str, Any], target: dict[str, Any]) -> bool:
    return all(key in row and values_equal(row[key], value) for key, value in target.items())


def position(record: dict[str, Any]) -> tuple[float, float, float] | None:
    raw = record.get("pos")
    if not isinstance(raw, list) or len(raw) < 3:
        return None
    try:
        x, y, z = float(raw[0]), float(raw[1]), float(raw[2])
    except (TypeError, ValueError):
        return None
    if not (math.isfinite(x) and math.isfinite(y) and math.isfinite(z)):
        return None
    return x, y, z


def compact_row(row: dict[str, Any]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key in ("kind", "type_name", "obj", "pad", "room", "index", "tag", "pos"):
        if key in row:
            result[key] = row[key]
    return result


def hit_summary(record: dict[str, Any],
                target_row: dict[str, Any],
                metric: tuple[float, float, float]) -> dict[str, Any]:
    pos = position(record)
    target_pos = position(target_row)
    if pos is None or target_pos is None:
        return {}
    dx = pos[0] - target_pos[0]
    dy = pos[1] - target_pos[1]
    dz = pos[2] - target_pos[2]
    return {
        "frame": record.get("f"),
        "pos": [round(pos[0], 2), round(pos[1], 2), round(pos[2], 2)],
        "target": compact_row(target_row),
        "delta": {
            "x": round(dx, 2),
            "y": round(dy, 2),
            "z": round(dz, 2),
            "horizontal": round(math.hypot(dx, dz), 2),
            "distance": round(math.sqrt(dx * dx + dy * dy + dz * dz), 2),
        },
        "sort_metric": [round(value, 4) for value in metric],
    }


def best_hit(records: list[dict[str, Any]],
             target_rows: list[dict[str, Any]],
             mode: str,
             same_floor_y: float) -> dict[str, Any] | None:
    best: tuple[float, float, float, dict[str, Any], dict[str, Any]] | None = None
    for record in records:
        rec_pos = position(record)
        if rec_pos is None:
            continue
        for row in target_rows:
            target_pos = position(row)
            if target_pos is None:
                continue
            dx = rec_pos[0] - target_pos[0]
            dy = rec_pos[1] - target_pos[1]
            dz = rec_pos[2] - target_pos[2]
            horizontal = math.hypot(dx, dz)
            abs_y = abs(dy)
            distance = math.sqrt(dx * dx + dy * dy + dz * dz)
            if mode == "horizontal":
                metric = (horizontal, abs_y, distance)
            elif mode == "distance":
                metric = (distance, horizontal, abs_y)
            elif mode == "same_floor":
                if abs_y > same_floor_y:
                    continue
                metric = (horizontal, distance, abs_y)
            elif mode == "vertical":
                metric = (abs_y, horizontal, distance)
            else:
                raise AssertionError(mode)
            candidate = (*metric, record, row)
            if best is None or candidate[:3] < best[:3]:
                best = candidate
    if best is None:
        return None
    return hit_summary(best[3], best[4], best[:3])


def route_name(artifact: Path) -> str:
    summary_path = artifact / "summary.json"
    if summary_path.is_file():
        try:
            data = json.loads(summary_path.read_text(encoding="utf-8"))
        except json.JSONDecodeError:
            data = {}
        if isinstance(data, dict) and isinstance(data.get("route"), str):
            return data["route"]
    return artifact.name


def analyze_artifact(artifact: Path,
                     target: dict[str, Any],
                     same_floor_y: float,
                     shared_setup_dump: Path | None) -> dict[str, Any]:
    trace_path = artifact / "trace.jsonl"
    setup_path = artifact / "stage_pads.jsonl"
    result: dict[str, Any] = {
        "artifact": str(artifact),
        "route": route_name(artifact),
        "status": "ok",
    }
    if not setup_path.is_file() and shared_setup_dump is not None:
        setup_path = shared_setup_dump
    if not setup_path.is_file():
        result["status"] = "missing_setup_dump"
        return result
    if not trace_path.is_file():
        result["status"] = "missing_trace"
        return result

    setup_records = load_jsonl(setup_path)
    trace_records = load_jsonl(trace_path)
    target_rows = [row for row in setup_records if row_matches(row, target) and position(row) is not None]
    result["target_rows"] = [compact_row(row) for row in target_rows]
    result["setup_dump"] = str(setup_path)
    result["records"] = len(trace_records)
    if not target_rows:
        result["status"] = "target_not_found"
        return result
    result["best_horizontal"] = best_hit(trace_records, target_rows, "horizontal", same_floor_y)
    result["best_distance"] = best_hit(trace_records, target_rows, "distance", same_floor_y)
    result["best_same_floor"] = best_hit(trace_records, target_rows, "same_floor", same_floor_y)
    result["best_vertical"] = best_hit(trace_records, target_rows, "vertical", same_floor_y)
    return result


def format_hit(hit: dict[str, Any] | None) -> str:
    if not hit:
        return "-"
    delta = hit.get("delta") if isinstance(hit.get("delta"), dict) else {}
    return "f={frame} h={h:.2f} dy={dy:.2f} d={d:.2f}".format(
        frame=hit.get("frame"),
        h=float(delta.get("horizontal", math.nan)),
        dy=float(delta.get("y", math.nan)),
        d=float(delta.get("distance", math.nan)),
    )


def print_table(results: list[dict[str, Any]]) -> None:
    print("route\tstatus\ttarget_rows\tbest_horizontal\tbest_same_floor\tbest_vertical")
    for result in results:
        print(
            "{route}\t{status}\t{target_rows}\t{best_h}\t{best_floor}\t{best_y}".format(
                route=result.get("route"),
                status=result.get("status"),
                target_rows=len(result.get("target_rows", [])),
                best_h=format_hit(result.get("best_horizontal")),
                best_floor=format_hit(result.get("best_same_floor")),
                best_y=format_hit(result.get("best_vertical")),
            )
        )


def main() -> int:
    args = parse_args()
    target = parse_target(args.target)
    results = [
        analyze_artifact(
            Path(artifact).resolve(),
            target,
            args.same_floor_y,
            args.setup_dump.resolve() if args.setup_dump else None,
        )
        for artifact in args.artifacts
    ]
    if args.json:
        print(json.dumps({"target": target, "results": results}, indent=2, sort_keys=True))
    else:
        print_table(results)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
