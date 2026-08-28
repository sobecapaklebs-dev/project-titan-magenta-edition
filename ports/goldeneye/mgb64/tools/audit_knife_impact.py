#!/usr/bin/env python3
"""audit_knife_impact.py -- confirm a thrown knife actually impacted a guard.

Secondary assertion for knife_impact_smoke.sh. The smoke's PRIMARY assertion is
process survival (exit 0): the throwing-knife-vs-on-screen-guard branch
(object_interaction, chrobjhandler.c:9529-9547) dereferenced a NULL Mtxf before
the sp58C stack-matrix fix and SIGSEGV'd on every such hit. A run that exits 0
only because the knife MISSED would be a false pass -- so this audit requires a
registered Bond knife hit, proving the crashy branch actually executed.

Reads the top-level `hit` block emitted by --trace-state. Hit weapon lives at
event.hit.weapon (nested), is_player / accepted alongside it.

ROM-derived traces are local validation artifacts; do not commit them.
"""
import argparse
import json
import sys

ITEM_THROWKNIFE = 3


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--trace", required=True)
    ap.add_argument("--weapon", type=int, default=ITEM_THROWKNIFE,
                    help="weapon id to require (default 3 = ITEM_THROWKNIFE)")
    args = ap.parse_args()

    total_events = 0
    knife_player_hits = []
    with open(args.trace) as fh:
        for line in fh:
            line = line.strip()
            if not line.startswith("{"):
                continue
            try:
                rec = json.loads(line)
            except json.JSONDecodeError:
                continue
            block = rec.get("hit")
            if not block:
                continue
            for ev in block.get("events") or []:
                total_events += 1
                hit = ev.get("hit") or {}
                if (hit.get("weapon") == args.weapon
                        and hit.get("is_player") == 1
                        and hit.get("accepted") == 1):
                    knife_player_hits.append({
                        "frame": ev.get("frame"),
                        "chrnum": ev.get("chrnum"),
                        "hitpart": hit.get("final"),
                    })

    label = f"knife impact (weapon {args.weapon})"
    if not knife_player_hits:
        print(f"FAIL: {label}: no accepted Bond knife hit found "
              f"({total_events} total hit events). The no-crash exit would be a "
              f"false pass (knife missed -> crashy branch never ran). Adjust the "
              f"warp distance / fire window so the thrown knife lands on a guard.")
        return 1

    h = knife_player_hits[0]
    print(f"PASS: {label}: {len(knife_player_hits)} accepted Bond knife hit(s); "
          f"first at frame {h['frame']} on chr {h['chrnum']} (part {h['hitpart']}) "
          f"-> the object_interaction throwknife branch (sp58C) executed without "
          f"crashing.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
