#!/usr/bin/env python3
"""Fail if any trace frame reports non-zero stub hit counters."""

import json
import sys
from pathlib import Path


def fail(msg: str) -> int:
    print(f"FAIL: {msg}", file=sys.stderr)
    return 1


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        return fail("Usage: assert_stub_hits_zero.py TRACE.jsonl")

    trace_path = Path(argv[1])
    if not trace_path.is_file():
        return fail(f"trace not found: {trace_path}")

    hit_frame = None
    hit_values: dict[str, int] = {}

    with trace_path.open("r", encoding="utf-8", errors="replace") as handle:
        for line_no, raw in enumerate(handle, start=1):
            raw = raw.strip()
            if not raw:
                continue
            try:
                rec = json.loads(raw)
            except json.JSONDecodeError:
                continue
            stub = rec.get("stub_hits") or {}
            snd_items = {
                key: int(value)
                for key, value in stub.items()
                if key.startswith("snd_")
            }
            if any(value != 0 for value in snd_items.values()):
                hit_frame = int(rec.get("f", line_no))
                hit_values = snd_items
                break

    if hit_frame is not None:
        details = ", ".join(f"{k}={v}" for k, v in sorted(hit_values.items()) if v != 0)
        return fail(
            f"stub hits non-zero at frame {hit_frame} ({details})"
        )

    print("PASS: trace reported zero snd stub hits")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
