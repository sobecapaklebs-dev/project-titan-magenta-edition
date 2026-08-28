#!/usr/bin/env python3
"""Embed a binary as an aligned C byte array.

Project Titan uses this for VU microprograms so the EE linker never needs to
interpret a DVP .vu section. The generated object is ordinary C data and can be
passed directly to packet2_vif_add_micro_program().
"""

from __future__ import annotations

import argparse
from pathlib import Path
import re
import sys


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("symbol")
    return parser.parse_args()


def validate_symbol(symbol: str) -> None:
    if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", symbol) is None:
        raise ValueError(f"invalid C symbol: {symbol!r}")


def main() -> int:
    args = parse_args()
    validate_symbol(args.symbol)

    payload = args.input.read_bytes()
    if not payload:
        raise ValueError(f"input is empty: {args.input}")
    if len(payload) % 8 != 0:
        raise ValueError(
            f"VU microprogram size must be a multiple of 8 bytes, got {len(payload)}"
        )

    guard = f"PROJECT_TITAN_GENERATED_{args.symbol.upper()}_H"
    lines = [
        f"#ifndef {guard}",
        f"#define {guard}",
        "",
        f"static unsigned char {args.symbol}[] __attribute__((aligned(16))) =",
        "{",
    ]

    for offset in range(0, len(payload), 12):
        chunk = payload[offset : offset + 12]
        encoded = ", ".join(f"0x{byte:02x}" for byte in chunk)
        lines.append(f"    {encoded},")

    lines.extend(
        [
            "};",
            "",
            f"static const unsigned int {args.symbol}_size = {len(payload)}u;",
            "",
            f"#endif /* {guard} */",
            "",
        ]
    )

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text("\n".join(lines), encoding="utf-8")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError) as exc:
        print(f"embed_aligned_binary.py: {exc}", file=sys.stderr)
        raise SystemExit(1)
