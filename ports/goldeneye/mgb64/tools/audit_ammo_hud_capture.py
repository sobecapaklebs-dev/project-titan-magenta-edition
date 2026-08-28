#!/usr/bin/env python3
"""Audit an ammo HUD capture triplet for one ammo type (backlog M2.6 / audit R6).

Inputs are three deterministic same-route screenshots that differ in exactly
one controlled variable, so pixel diffs isolate one HUD element each:

  --shot-a      equipped, ammo values A (mag=3/reserve=12)
  --shot-b      equipped, ammo values B (mag=8/reserve=25)
  --shot-fault  equipped, ammo values B, GE007_AMMO_ICON_FAULT=<type>

Checks:
  icon    diff(shot_b, shot_fault) >= min-icon-diff. Ammo digits use the same
          values in both, so the diff isolates "real icon" vs "fallback
          placeholder" at the icon anchor (plus the small digit re-anchor from
          the placeholder's width). Zero/low diff means the real icon did NOT
          render (e.g. the mapping regressed to NULL and both runs drew the
          placeholder) -> FAIL.
  digits  diff(shot_a, shot_b) >= min-digit-diff. Same weapon, same fault
          state, only the displayed ammo numbers differ, so the diff isolates
          digit glyph pixels. Zero/low diff means digits did not render.

The viewmodel is identical inside each pair (deterministic route, same
weapon, no firing), so diffs cannot be polluted by first-person weapon
geometry — this is why the audit never diffs against an unarmed baseline.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path


def load_rgb(path: Path):
    try:
        from PIL import Image
    except ImportError:
        print("FAIL: python3 PIL (pillow) is required", file=sys.stderr)
        raise SystemExit(2)
    with Image.open(path) as image:
        converted = image.convert("RGB")
        return converted.size, converted.tobytes()


def diff_stats(size_a, data_a, size_b, data_b):
    if size_a != size_b:
        return None
    width = size_a[0]
    xs = []
    ys = []
    for i in range(0, len(data_a), 3):
        if data_a[i : i + 3] != data_b[i : i + 3]:
            p = i // 3
            xs.append(p % width)
            ys.append(p // width)
    if not xs:
        return {"count": 0, "bbox": None}
    return {
        "count": len(xs),
        "bbox": [min(xs), min(ys), max(xs), max(ys)],
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--label", required=True)
    parser.add_argument("--shot-a", type=Path, required=True)
    parser.add_argument("--shot-b", type=Path, required=True)
    parser.add_argument("--shot-fault", type=Path, required=True)
    parser.add_argument("--min-icon-diff", type=int, default=150)
    parser.add_argument("--min-digit-diff", type=int, default=40)
    parser.add_argument("--json-out", type=Path)
    args = parser.parse_args()

    size_a, data_a = load_rgb(args.shot_a)
    size_b, data_b = load_rgb(args.shot_b)
    size_f, data_f = load_rgb(args.shot_fault)

    failures = []

    icon = diff_stats(size_b, data_b, size_f, data_f)
    digits = diff_stats(size_a, data_a, size_b, data_b)
    if icon is None or digits is None:
        failures.append(
            f"screenshot sizes differ: a={size_a} b={size_b} fault={size_f}"
        )
        icon = icon or {"count": 0, "bbox": None}
        digits = digits or {"count": 0, "bbox": None}
    else:
        if icon["count"] < args.min_icon_diff:
            failures.append(
                "icon check failed: icon-vs-placeholder diff "
                f"{icon['count']} < {args.min_icon_diff} "
                "(real icon did not render distinct from the fallback)"
            )
        if digits["count"] < args.min_digit_diff:
            failures.append(
                "digit check failed: ammo-value diff "
                f"{digits['count']} < {args.min_digit_diff} "
                "(ammo digits did not render)"
            )

    result = {
        "label": args.label,
        "status": "fail" if failures else "pass",
        "icon": icon,
        "digits": digits,
        "thresholds": {
            "min_icon_diff": args.min_icon_diff,
            "min_digit_diff": args.min_digit_diff,
        },
        "failures": failures,
    }

    if args.json_out is not None:
        args.json_out.write_text(json.dumps(result, indent=2) + "\n")

    if failures:
        print(f"FAIL: {args.label}")
        for failure in failures:
            print(f"  {failure}")
        return 1

    print(
        f"PASS: {args.label}: icon diff {icon['count']} px "
        f"(bbox {icon['bbox']}), digit diff {digits['count']} px "
        f"(bbox {digits['bbox']})"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
