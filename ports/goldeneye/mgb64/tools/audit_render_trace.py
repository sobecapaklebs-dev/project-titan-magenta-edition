#!/usr/bin/env python3
"""Audit native render-health counters in a JSONL state trace."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any


DL_COUNTERS = (
    "mtx_fail",
    "vtx_fail",
    "dl_fail",
    "movemem_fail",
    "texture_fail",
    "settimg_fail",
    "non_dl_skip_pc",
    "non_dl_skip_n64",
    "unregistered_skip",
    "dyn_overflow",
    "hud_image_fault",
)


def load_trace(path: Path) -> list[dict[str, Any]]:
    records: list[dict[str, Any]] = []
    skipped = 0
    with path.open("r", encoding="utf-8", errors="replace") as handle:
        for line in handle:
            line = line.strip()
            if not line or not line.startswith("{"):
                continue
            try:
                record = json.loads(line)
            except json.JSONDecodeError:
                skipped += 1
                continue
            if isinstance(record, dict):
                records.append(record)
    if skipped:
        print(f"WARNING: skipped {skipped} corrupted JSONL line(s) in {path}", file=sys.stderr)
    return records


def parse_int(value: Any) -> int | None:
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


def nested(record: dict[str, Any], *path: str) -> Any:
    value: Any = record
    for part in path:
        if not isinstance(value, dict):
            return None
        value = value.get(part)
    return value


def frame_label(record: dict[str, Any] | None) -> str:
    if record is None:
        return "none"
    return f"frame={record.get('f', '?')}"


def frame_number(record: dict[str, Any] | None) -> int | None:
    if record is None:
        return None
    return parse_int(record.get("f"))


def write_json_metrics(path: str | None, metrics: dict[str, Any]) -> None:
    if not path:
        return
    Path(path).write_text(json.dumps(metrics, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def audit(args: argparse.Namespace) -> int:
    records = load_trace(Path(args.trace))
    if not records:
        print(f"FAIL: {args.label}: no JSON records in {args.trace}")
        write_json_metrics(
            args.json_out,
            {
                "label": args.label,
                "trace": args.trace,
                "status": "fail",
                "records": 0,
                "failures": ["no JSON records"],
            },
        )
        return 2

    missing_fields: set[str] = set()
    failures: list[str] = []
    max_bad_cmds = 0
    max_crashes = 0
    max_nan = 0
    max_fallback_total = 0
    fallback_records = 0
    dl_max: dict[str, int] = {name: 0 for name in DL_COUNTERS}
    first_bad_cmds: dict[str, Any] | None = None
    first_crash: dict[str, Any] | None = None
    first_nan: dict[str, Any] | None = None
    first_fallback: dict[str, Any] | None = None
    first_dl: dict[str, dict[str, Any]] = {}

    def read_counter(record: dict[str, Any], field: str, *path: str) -> int:
        value = parse_int(nested(record, *path))
        if value is None:
            missing_fields.add(field)
            return 0
        return value

    for record in records:
        bad_cmds = read_counter(record, "bad_cmds", "bad_cmds")
        crashes = read_counter(record, "crashes", "crashes")
        nan_count = read_counter(record, "nan", "nan")
        fallback_active = read_counter(record, "rooms.fallback.active", "rooms", "fallback", "active")
        fallback_total = read_counter(record, "rooms.fallback.total", "rooms", "fallback", "total")

        if bad_cmds > max_bad_cmds:
            max_bad_cmds = bad_cmds
            first_bad_cmds = first_bad_cmds or record
        if crashes > max_crashes:
            max_crashes = crashes
            first_crash = first_crash or record
        if nan_count > max_nan:
            max_nan = nan_count
            first_nan = first_nan or record
        if fallback_active > 0:
            fallback_records += 1
            first_fallback = first_fallback or record
        max_fallback_total = max(max_fallback_total, fallback_total)

        for name in DL_COUNTERS:
            value = read_counter(record, f"dl.{name}", "dl", name)
            if value > dl_max[name]:
                dl_max[name] = value
                first_dl.setdefault(name, record)

    print(f"audit: {args.label}")
    print(f"  records={len(records)}")
    print(f"  bad_cmds_max={max_bad_cmds} crashes_max={max_crashes} nan_max={max_nan}")
    print(f"  room_fallback_records={fallback_records} room_fallback_total_max={max_fallback_total}")
    print(
        "  dl_max="
        + ", ".join(f"{name}:{dl_max[name]}" for name in DL_COUNTERS)
    )

    if missing_fields and not args.allow_missing_fields:
        failures.append("missing render-health fields: " + ", ".join(sorted(missing_fields)))
    if max_bad_cmds > args.max_bad_cmds:
        failures.append(f"bad_cmds above threshold: {max_bad_cmds} > {args.max_bad_cmds} at {frame_label(first_bad_cmds)}")
    if max_crashes > args.max_crashes:
        failures.append(f"crashes above threshold: {max_crashes} > {args.max_crashes} at {frame_label(first_crash)}")
    if max_nan > args.max_nan:
        failures.append(f"nan above threshold: {max_nan} > {args.max_nan} at {frame_label(first_nan)}")
    if not args.allow_room_fallback and (fallback_records > 0 or max_fallback_total > 0):
        failures.append(
            "room render fallback observed: "
            f"records={fallback_records} total_max={max_fallback_total} at {frame_label(first_fallback)}"
        )
    for name, value in dl_max.items():
        if value > args.max_dl_counter:
            failures.append(
                f"dl.{name} above threshold: {value} > {args.max_dl_counter} "
                f"at {frame_label(first_dl.get(name))}"
            )

    metrics = {
        "label": args.label,
        "trace": args.trace,
        "status": "fail" if failures else "pass",
        "records": len(records),
        "missing_fields": sorted(missing_fields),
        "bad_cmds_max": max_bad_cmds,
        "crashes_max": max_crashes,
        "nan_max": max_nan,
        "room_fallback_records": fallback_records,
        "room_fallback_total_max": max_fallback_total,
        "dl_max": dl_max,
        "first_frames": {
            "bad_cmds": frame_number(first_bad_cmds),
            "crash": frame_number(first_crash),
            "nan": frame_number(first_nan),
            "room_fallback": frame_number(first_fallback),
            "dl": {name: frame_number(record) for name, record in first_dl.items()},
        },
        "thresholds": {
            "max_bad_cmds": args.max_bad_cmds,
            "max_crashes": args.max_crashes,
            "max_nan": args.max_nan,
            "max_dl_counter": args.max_dl_counter,
            "allow_room_fallback": args.allow_room_fallback,
            "allow_missing_fields": args.allow_missing_fields,
        },
        "failures": failures,
    }
    write_json_metrics(args.json_out, metrics)

    if failures:
        print(f"FAIL: {args.label}: render trace audit found {len(failures)} issue(s)")
        for failure in failures[: args.max_failures]:
            print(f"  {failure}")
        if len(failures) > args.max_failures:
            print("  ...")
        return 1

    print(f"PASS: {args.label}: render trace health audit")
    return 0


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace", help="JSONL trace to audit")
    parser.add_argument("--label", default="trace")
    parser.add_argument("--max-bad-cmds", type=int, default=0)
    parser.add_argument("--max-crashes", type=int, default=0)
    parser.add_argument("--max-nan", type=int, default=0)
    parser.add_argument("--max-dl-counter", type=int, default=0)
    parser.add_argument("--allow-room-fallback", action="store_true")
    parser.add_argument("--allow-missing-fields", action="store_true")
    parser.add_argument("--max-failures", type=int, default=8)
    parser.add_argument("--json-out", help="write render-health metrics as JSON")
    return parser.parse_args()


def main() -> int:
    return audit(parse_args())


if __name__ == "__main__":
    raise SystemExit(main())
