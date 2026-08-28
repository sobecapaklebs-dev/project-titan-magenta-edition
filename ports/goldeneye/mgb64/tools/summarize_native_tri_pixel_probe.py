#!/usr/bin/env python3
"""Summarize native TRI-PIXEL probe owner chains."""

from __future__ import annotations

import argparse
from collections import Counter
import json
from pathlib import Path
import re
from typing import Any


TRI_PIXEL_RE = re.compile(r"\[TRI-PIXEL\]\s+(?P<payload>\{.*\})")


def load_rows(path: Path) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    with path.open("r", encoding="utf-8", errors="replace") as handle:
        for line_no, line in enumerate(handle, start=1):
            match = TRI_PIXEL_RE.search(line)
            if not match:
                continue
            try:
                row = json.loads(match.group("payload"))
            except json.JSONDecodeError as exc:
                raise ValueError(f"{path}:{line_no}: invalid TRI-PIXEL JSON: {exc}") from exc
            row["line"] = line_no
            rows.append(row)
    return rows


def parse_xy(value: str) -> tuple[int, int]:
    parts = value.lower().replace("x", ",").split(",")
    if len(parts) != 2:
        raise argparse.ArgumentTypeError("expected X,Y")
    try:
        return (int(parts[0], 0), int(parts[1], 0))
    except ValueError as exc:
        raise argparse.ArgumentTypeError("expected integer X,Y") from exc


def target_tuple(value: Any) -> tuple[int, int] | None:
    if not isinstance(value, list) or len(value) != 2:
        return None
    try:
        return (int(value[0]), int(value[1]))
    except (TypeError, ValueError):
        return None


def mean_abs_delta(delta: Any) -> float | None:
    if not isinstance(delta, list) or len(delta) < 3:
        return None
    try:
        return sum(abs(float(delta[index])) for index in range(3)) / 3.0
    except (TypeError, ValueError):
        return None


def compact_row(row: dict[str, Any]) -> dict[str, Any]:
    mode = row.get("mode") if isinstance(row.get("mode"), dict) else {}
    depth = row.get("depth") if isinstance(row.get("depth"), dict) else {}
    rect = row.get("rect") if isinstance(row.get("rect"), dict) else {}
    return {
        "line": row.get("line"),
        "frame": row.get("frame"),
        "tri": row.get("tri"),
        "serial": row.get("serial"),
        "target": row.get("target"),
        "inside": row.get("inside"),
        "changed": row.get("changed"),
        "mean_abs_delta": mean_abs_delta(row.get("delta")),
        "drawclass": row.get("drawclass"),
        "dl_room": row.get("dl_room"),
        "dl": row.get("dl"),
        "settex": row.get("settex"),
        "texnum": row.get("texnum"),
        "wh": row.get("wh"),
        "cc": row.get("cc"),
        "effcc": row.get("effcc"),
        "raw": row.get("raw"),
        "effmode": row.get("effmode"),
        "blend": row.get("blend"),
        "api_blend": row.get("api_blend"),
        "zmode": depth.get("zmode"),
        "zraw": depth.get("zraw"),
        "cvg": mode.get("cvg"),
        "force_bl": mode.get("force_bl"),
        "clr_on_cvg": mode.get("clr_on_cvg"),
        "fog": mode.get("fog"),
        "fog_fixed": mode.get("fog_fixed"),
        "roommtx": mode.get("roommtx"),
        "sky": mode.get("sky"),
        "rect_op": rect.get("op"),
        "pre": row.get("pre"),
        "post": row.get("post"),
        "delta": row.get("delta"),
        "screen_bbox": row.get("screen_bbox"),
    }


def summarize(rows: list[dict[str, Any]],
              target: tuple[int, int] | None,
              frame: int | None,
              changed_only: bool) -> dict[str, Any]:
    filtered = rows
    if target is not None:
        filtered = [row for row in filtered if target_tuple(row.get("target")) == target]
    if frame is not None:
        filtered = [row for row in filtered if row.get("frame") == frame]
    if changed_only:
        filtered = [row for row in filtered if row.get("changed")]

    frames = Counter()
    texnums = Counter()
    drawclasses = Counter()
    changed_rows = 0
    for row in filtered:
        frames[row.get("frame")] += 1
        texnums[row.get("texnum")] += 1
        drawclasses[row.get("drawclass")] += 1
        if row.get("changed"):
            changed_rows += 1

    rows_out = [compact_row(row) for row in filtered]
    return {
        "status": "pass",
        "total_rows": len(rows),
        "filtered_rows": len(filtered),
        "changed_rows": changed_rows,
        "target": list(target) if target is not None else None,
        "frame": frame,
        "frames": [
            {"frame": key, "records": count}
            for key, count in sorted(frames.items(), key=lambda item: (item[0] is None, item[0]))
        ],
        "texnums": [
            {"texnum": key, "records": count}
            for key, count in texnums.most_common()
        ],
        "drawclasses": [
            {"drawclass": key, "records": count}
            for key, count in drawclasses.most_common()
        ],
        "rows": rows_out,
        "interpretation": build_interpretation(rows_out),
    }


def build_interpretation(rows: list[dict[str, Any]]) -> list[str]:
    if not rows:
        return ["no TRI-PIXEL rows matched the selected filters"]
    notes = [
        f"{len(rows)} TRI-PIXEL rows matched the selected filters",
    ]
    first = rows[0]
    last = rows[-1]
    notes.append(
        "owner chain endpoints: "
        f"{first.get('pre')} -> {last.get('post')}"
    )
    changed = [row for row in rows if row.get("changed")]
    if changed:
        labels = [
            f"tri={row.get('tri')} tex={row.get('texnum')} "
            f"{row.get('pre')}->{row.get('post')}"
            for row in changed
        ]
        notes.append("changed rows: " + "; ".join(labels))
    return notes


def print_human(payload: dict[str, Any], max_rows: int) -> None:
    print(
        "native TRI-PIXEL chain: "
        f"rows={payload['filtered_rows']}/{payload['total_rows']} "
        f"changed={payload['changed_rows']} target={payload.get('target')} "
        f"frame={payload.get('frame')}"
    )
    rows = payload["rows"]
    if max_rows > 0 and len(rows) > max_rows:
        half = max_rows // 2
        display_rows = rows[:half] + rows[-(max_rows - half):]
        omitted = len(rows) - len(display_rows)
    else:
        display_rows = rows
        omitted = 0
    for row in display_rows:
        print(
            f"- frame={row.get('frame')} tri={row.get('tri')} "
            f"class={row.get('drawclass')} room={row.get('dl_room')} "
            f"tex={row.get('texnum')} cc={row.get('cc')} "
            f"blend={row.get('api_blend')} z={row.get('zmode')} "
            f"pre={row.get('pre')} post={row.get('post')} "
            f"delta={row.get('delta')} changed={row.get('changed')}"
        )
    if omitted:
        print(f"... omitted {omitted} unchanged/intermediate rows; use --max-rows 0 for all rows")
    print("interpretation:")
    for item in payload["interpretation"]:
        print(f"  {item}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path)
    parser.add_argument("--target", type=parse_xy)
    parser.add_argument("--frame", type=int)
    parser.add_argument("--changed-only", action="store_true")
    parser.add_argument("--max-rows", type=int, default=80, help="maximum rows to print in human output; 0 prints all")
    parser.add_argument("--json-out", type=Path)
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()

    payload = summarize(load_rows(args.log), args.target, args.frame, args.changed_only)
    if args.json_out:
        args.json_out.parent.mkdir(parents=True, exist_ok=True)
        args.json_out.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    if args.json:
        print(json.dumps(payload, indent=2, sort_keys=True))
    else:
        print_human(payload, args.max_rows)
    return 0 if payload["status"] == "pass" else 1


if __name__ == "__main__":
    raise SystemExit(main())
