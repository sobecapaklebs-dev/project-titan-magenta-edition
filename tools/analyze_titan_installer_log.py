#!/usr/bin/env python3
"""Summarize Project Titan Installer INSTALLER.LOG without external deps."""
from __future__ import annotations

import argparse
import collections
import re
from pathlib import Path

KV_RE = re.compile(r"([A-Za-z0-9_]+)=([^ ]+)")


def parse_kv(line: str) -> dict[str, str]:
    return dict(KV_RE.findall(line))


def as_int(value: str | None, default: int = 0) -> int:
    if value is None:
        return default
    try:
        return int(value, 0)
    except ValueError:
        return default


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("log", type=Path, help="INSTALLER.LOG")
    args = ap.parse_args()
    text = args.log.read_text(errors="replace")
    lines = text.splitlines()

    individual_causes: collections.Counter[str] = collections.Counter()
    aggregate_causes: collections.Counter[str] = collections.Counter()
    worst_frame = (0, "")
    worst_blocking_io = (0, "")
    worst_async_io = (0, "")
    perf_rates: list[int] = []
    errors: list[str] = []
    last_state = ""
    dropped = 0

    for line in lines:
        kv = parse_kv(line)
        if "STUTTER_AGGREGATE" in line:
            aggregate_causes[kv.get("cause", "UNKNOWN")] += as_int(kv.get("count"), 1)
            gap = as_int(kv.get("max_ms"))
            if gap > worst_frame[0]:
                worst_frame = (gap, line)
        elif "WAVE_STUTTER" in line:
            cause = kv.get("cause", "UNKNOWN")
            individual_causes[cause] += 1
            gap = as_int(kv.get("gap_ms"))
            if gap > worst_frame[0]:
                worst_frame = (gap, line)

        if "ASYNC_IO_LATENCY" in line:
            elapsed = as_int(kv.get("elapsed_ms"))
            if elapsed > worst_async_io[0]:
                worst_async_io = (elapsed, line)
        elif "SLOW_IO" in line or "IO_FAILURE" in line:
            elapsed = as_int(kv.get("elapsed_ms"))
            if elapsed > worst_blocking_io[0]:
                worst_blocking_io = (elapsed, line)

        if "PERF_WINDOW" in line:
            perf_rates.append(as_int(kv.get("io_kib_s")))
        if " ERROR " in line or "ROLLBACK_BEGIN" in line or "INSTALL_TRANSACTION_FAILED" in line:
            errors.append(line)
        if "INSTALL_STATE" in line:
            last_state = line
        if line.startswith("ring_dropped_lines="):
            dropped = as_int(line.split("=", 1)[1])

    causes = aggregate_causes if aggregate_causes else individual_causes
    print("Titan Installer diagnostic summary")
    print(f"log: {args.log}")
    print(f"lines: {len(lines)}")
    print(f"wave stutters: {sum(causes.values())}")
    for cause, count in causes.most_common():
        print(f"  {cause}: {count}")
    if worst_frame[0]:
        print(f"worst frame: {worst_frame[0]} ms")
        print(f"  {worst_frame[1]}")
    if worst_blocking_io[0]:
        print(f"worst frame-blocking I/O: {worst_blocking_io[0]} ms")
        print(f"  {worst_blocking_io[1]}")
    if worst_async_io[0]:
        print(f"worst async I/O service latency: {worst_async_io[0]} ms")
        print(f"  {worst_async_io[1]}")
    if perf_rates:
        print(f"I/O throughput windows: min={min(perf_rates)} KiB/s avg={sum(perf_rates)//len(perf_rates)} KiB/s max={max(perf_rates)} KiB/s")
    if dropped:
        print(f"ring dropped lines: {dropped}")
    if last_state:
        print("last install state:")
        print(f"  {last_state}")
    if errors:
        print("last errors:")
        for line in errors[-8:]:
            print(f"  {line}")
    else:
        print("last errors: none recorded")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
