#!/usr/bin/env python3
"""Compare stock/native first-person viewmodel projection samples.

The movement oracle traces stock viewmodel matrix samples under
watch.hands.<hand>.vm.model.mtx. Native traces expose the matching sampled
matrices as wr_mtx / wl_mtx. This helper compares those anchors in view space and
after logical-screen projection so foreground mismatches can be separated from
viewmodel pose/projection drift.
"""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
from typing import Any


def load_last_jsonl(path: Path) -> dict[str, Any]:
    last: dict[str, Any] | None = None
    with path.open("r", encoding="utf-8") as handle:
        for line_no, line in enumerate(handle, start=1):
            line = line.strip()
            if not line:
                continue
            try:
                last = json.loads(line)
            except json.JSONDecodeError as exc:
                raise SystemExit(f"FAIL: {path}:{line_no}: invalid JSON: {exc}") from exc
    if last is None:
        raise SystemExit(f"FAIL: {path}: no JSON records")
    return last


def vec_dist(a: list[float], b: list[float]) -> float:
    return math.sqrt(sum((float(x) - float(y)) ** 2 for x, y in zip(a, b)))


def stock_mtx(record: dict[str, Any], hand: str) -> dict[str, Any]:
    try:
        return record["watch"]["hands"][hand]["vm"]["model"]["mtx"]
    except KeyError as exc:
        raise SystemExit(f"FAIL: stock trace missing watch.hands.{hand}.vm.model.mtx") from exc


def native_mtx(record: dict[str, Any], hand: str) -> dict[str, Any]:
    key = "wr_mtx" if hand == "right" else "wl_mtx"
    try:
        return record[key]
    except KeyError as exc:
        raise SystemExit(f"FAIL: native trace missing {key}") from exc


def compare(stock: dict[str, Any], native: dict[str, Any]) -> dict[str, Any]:
    stock_pos = stock.get("pos", [])
    native_pos = native.get("pos", [])
    limit = min(len(stock_pos), len(native_pos))
    rows: list[dict[str, Any]] = []

    for idx in range(limit):
        stock_actual = stock.get("screen", [{}])[idx]
        stock_ref50 = stock.get("screen50", [{}])[idx]
        native_actual = native.get("screen", [{}])[idx]
        native_ref50 = native.get("screen50", [{}])[idx]
        stock_actual_xy = stock_actual.get("xy", [0.0, 0.0])
        stock_ref50_xy = stock_ref50.get("xy", [0.0, 0.0])
        native_actual_xy = native_actual.get("xy", [0.0, 0.0])
        native_ref50_xy = native_ref50.get("xy", [0.0, 0.0])
        rows.append(
            {
                "index": idx,
                "stock_pos": stock_pos[idx],
                "native_pos": native_pos[idx],
                "position_distance": vec_dist(stock_pos[idx], native_pos[idx]),
                "stock_screen_valid": int(stock_actual.get("valid", 0)),
                "native_screen_valid": int(native_actual.get("valid", 0)),
                "stock_screen": stock_actual_xy,
                "native_screen": native_actual_xy,
                "screen_distance": vec_dist(stock_actual_xy, native_actual_xy),
                "stock_screen50": stock_ref50_xy,
                "native_screen50": native_ref50_xy,
                "screen50_distance": vec_dist(stock_ref50_xy, native_actual_xy),
                "ref50_pair_distance": vec_dist(stock_ref50_xy, native_ref50_xy),
            }
        )

    valid_rows = [
        row
        for row in rows
        if row["stock_screen_valid"] and row["native_screen_valid"]
    ]
    ref50_distances = [row["screen50_distance"] for row in valid_rows]
    actual_distances = [row["screen_distance"] for row in valid_rows]
    pos_distances = [row["position_distance"] for row in valid_rows]

    def max_or_zero(values: list[float]) -> float:
        return max(values) if values else 0.0

    def mean_or_zero(values: list[float]) -> float:
        return sum(values) / len(values) if values else 0.0

    return {
        "stock_projection": stock.get("projection", {}),
        "native_projection": native.get("projection", {}),
        "stock_count": stock.get("count", 0),
        "native_count": native.get("count", 0),
        "sampled": limit,
        "valid_pairs": len(valid_rows),
        "summary": {
            "position_distance_max": max_or_zero(pos_distances),
            "position_distance_mean": mean_or_zero(pos_distances),
            "screen_distance_max": max_or_zero(actual_distances),
            "screen_distance_mean": mean_or_zero(actual_distances),
            "screen50_distance_max": max_or_zero(ref50_distances),
            "screen50_distance_mean": mean_or_zero(ref50_distances),
        },
        "rows": rows,
    }


def print_text(result: dict[str, Any]) -> None:
    print(f"stock_projection: {result['stock_projection']}")
    print(f"native_projection: {result['native_projection']}")
    print(
        "summary: "
        f"valid_pairs={result['valid_pairs']} "
        f"pos_max={result['summary']['position_distance_max']:.3f} "
        f"screen_max={result['summary']['screen_distance_max']:.2f} "
        f"screen50_max={result['summary']['screen50_distance_max']:.2f}"
    )
    print("idx  dpos   dscreen  dscreen50  stock_pos -> native_pos")
    for row in result["rows"]:
        print(
            f"{row['index']:>3}  "
            f"{row['position_distance']:>6.3f}  "
            f"{row['screen_distance']:>7.2f}  "
            f"{row['screen50_distance']:>9.2f}  "
            f"{row['stock_pos']} -> {row['native_pos']}"
        )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--stock", required=True, type=Path, help="stock oracle JSONL trace")
    parser.add_argument("--native", required=True, type=Path, help="native JSONL trace")
    parser.add_argument("--hand", choices=("right", "left"), default="right")
    parser.add_argument("--json", action="store_true", help="emit machine-readable JSON")
    args = parser.parse_args()

    result = compare(
        stock_mtx(load_last_jsonl(args.stock), args.hand),
        native_mtx(load_last_jsonl(args.native), args.hand),
    )
    if args.json:
        print(json.dumps(result, indent=2, sort_keys=True))
    else:
        print_text(result)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
