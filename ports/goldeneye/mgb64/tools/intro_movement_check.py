#!/usr/bin/env python3
"""intro_movement_check.py -- Post-intro Bond-movement validator.

Reads a --trace-state JSONL captured while a level intro plays to completion
and then a scripted forward stick is held (GE007_AUTO_FORWARD), and asserts
that the player actually moved once control was handed to first person.

This is the automated guard for the Silo post-intro "look works, WASD dead"
regression: the normal swirl->FP handoff used to seed the FP gameplay collision
anchor from the drifted intro root-motion pos/stan instead of the authored
gameplay spawn, so MoveBond's collision rejected all stick input on levels whose
drift landed off a walkable tile (the static-establishing intros, e.g. Silo).

Metric (FP-aware, so the intro->FP handoff teleport is never counted as
"movement"): the baseline is the player position at the first first-person
frame (cam == CAMERAMODE_FP == 4) at or after --forward-start; the reported
delta is the max horizontal (X/Z) distance from that baseline across the rest
of the trace. A frozen player stays bit-identical to the baseline -> delta 0.

Exit 0 = verdict matched expectation, 1 = mismatch, 2 = usage/trace error.
ROM-derived traces are local artifacts; do not commit them.
"""
import argparse
import json
import math
import sys

CAMERAMODE_FP = 4


def load(path):
    recs = []
    with open(path) as fh:
        for line in fh:
            line = line.strip()
            if not line:
                continue
            try:
                recs.append(json.loads(line))
            except json.JSONDecodeError:
                continue
    return recs


def horiz(a, b):
    return math.hypot(a[0] - b[0], a[2] - b[2])


def main():
    ap = argparse.ArgumentParser(description="Validate post-intro Bond movement.")
    ap.add_argument("trace", help="JSONL trace from --trace-state")
    ap.add_argument("--label", default="")
    ap.add_argument("--forward-start", type=int, required=True,
                    help="frame at which GE007_AUTO_FORWARD begins")
    ap.add_argument("--min-horizontal-delta", type=float, default=10.0,
                    help="minimum post-FP X/Z displacement to count as moving")
    ap.add_argument("--frozen-epsilon", type=float, default=1.0,
                    help="max delta tolerated when --expect-frozen")
    ap.add_argument("--expect-frozen", action="store_true",
                    help="negative control: assert the player did NOT move")
    ap.add_argument("--json-out", default="")
    args = ap.parse_args()

    recs = load(args.trace)
    if not recs:
        print(f"FAIL: {args.label}: empty/unreadable trace {args.trace}")
        return 2

    def cam(r):
        return r.get("cam")

    # First first-person frame at or after the forward window opens. Falls back
    # to the first FP frame overall so a short intro is still measured.
    baseline = None
    for r in recs:
        if cam(r) == CAMERAMODE_FP and r.get("f", 0) >= args.forward_start and r.get("pos"):
            baseline = r
            break
    if baseline is None:
        for r in recs:
            if cam(r) == CAMERAMODE_FP and r.get("pos"):
                baseline = r
                break

    if baseline is None:
        print(f"FAIL: {args.label}: intro never handed control to first person "
              f"(no cam==FP frame); last cam={cam(recs[-1])} f={recs[-1].get('f')}")
        if args.json_out:
            with open(args.json_out, "w") as fh:
                json.dump({"label": args.label, "reached_fp": False,
                           "max_horizontal_delta": 0.0}, fh)
        return 1

    base_pos = baseline["pos"]
    base_f = baseline.get("f")
    max_delta = 0.0
    max_f = base_f
    moving_records = 0
    prev = base_pos
    for r in recs:
        if r.get("f", 0) < base_f or not r.get("pos"):
            continue
        p = r["pos"]
        d = horiz(p, base_pos)
        if d > max_delta:
            max_delta = d
            max_f = r.get("f")
        if horiz(p, prev) > 1e-4:
            moving_records += 1
        prev = p

    metrics = {
        "label": args.label,
        "reached_fp": True,
        "baseline_frame": base_f,
        "baseline_pos": [base_pos[0], base_pos[1], base_pos[2]],
        "max_horizontal_delta": round(max_delta, 4),
        "max_delta_frame": max_f,
        "moving_records": moving_records,
    }
    if args.json_out:
        with open(args.json_out, "w") as fh:
            json.dump(metrics, fh)

    summary = (f"{args.label}: FP@{base_f} base=[{base_pos[0]:.1f},{base_pos[2]:.1f}] "
               f"maxHorizDelta={max_delta:.2f}@f{max_f} movingRecords={moving_records}")

    if args.expect_frozen:
        if max_delta <= args.frozen_epsilon:
            print(f"OK (frozen as expected): {summary}")
            return 0
        print(f"FAIL: {args.label}: expected frozen (<= {args.frozen_epsilon}) but "
              f"player moved {max_delta:.2f}")
        print(f"  {summary}")
        return 1

    if max_delta >= args.min_horizontal_delta and moving_records > 0:
        print(f"PASS: {summary}")
        return 0
    print(f"FAIL: {args.label}: post-intro movement dead "
          f"(maxHorizDelta={max_delta:.2f} < {args.min_horizontal_delta}, "
          f"movingRecords={moving_records})")
    print(f"  {summary}")
    return 1


if __name__ == "__main__":
    sys.exit(main())
