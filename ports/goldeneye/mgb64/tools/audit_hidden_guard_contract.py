#!/usr/bin/env python3
"""audit_hidden_guard_contract.py -- assert the chrTickBeams hidden-guard contract.

Reads a --trace-state JSONL produced with GE007_TRACE_CHRNUM=<guard> while a
deterministic scenario (a) warps the guard near Bond, (b) gives it a firing AI
list so it actively shoots Bond, then (c) sets CHRFLAG_HIDDEN on it at a frame.
The hide frame is auto-detected from the trace (first frame where
track.flag_hidden==1), so the input/trace timing offset does not matter.

Two gates enforce the contract in chrTickBeams, and which one this run exercises
depends on whether CHRFLAG_00040000 ("update guard action") is set when hidden:

  --mode h2  (hide with CHRFLAG_00040000 CLEAR):
      The H2 AI-tick gate (chr.c:4946) freezes the guard's AI. Asserts the AI is
      frozen (action + ai.offset constant) AND, as a consequence, no phantom
      activity (firecount frozen, Bond health stable). NOTE: with the AI frozen
      the guard never queues a discharge, so the firecount freeze here is
      H2-mediated -- this mode does NOT independently exercise the H1
      fire-discharge gate (chr.c:5046).

  --mode h1  (hide with CHRFLAG_00040000 SET):
      The AI keeps ticking (action/ai.offset CHANGE) but the H1 gate (chr.c:5046)
      blocks chrlvTriggerFireWeapon, so firecount stays frozen. This isolates H1:
      it FAILS if the AI was actually frozen (then the freeze would be H2's doing,
      not H1's) and FAILS if firecount climbs (H1 regressed).

Common to both: an anti-vacuity precondition (the guard actually fired while
visible), a track-present check, and a hide-occurred check, so a misconfigured
run fails loudly instead of passing vacuously.

It deliberately does NOT assert g_OnScreenPropList membership: a mid-life hide
leaves the guard listed because PROPFLAG_ONSCREEN is re-set by a separate camera
pass (see render.on_proplist; that is the separate H1b fix, tracked elsewhere).

ROM-derived traces are local validation artifacts; do not commit them.
"""
import argparse
import json
import sys

EPS = 1e-4


def load_tracks(path):
    out = []
    with open(path) as fh:
        for line in fh:
            line = line.strip()
            if not line.startswith("{"):
                continue
            try:
                rec = json.loads(line)
            except json.JSONDecodeError:
                continue
            track = rec.get("track")
            if not track:
                continue
            combat = rec.get("combat") or {}
            health = combat.get("health") or {}
            out.append((rec.get("f"), track, health.get("bond")))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--trace", required=True)
    ap.add_argument("--mode", choices=["h1", "h2"], default="h2")
    ap.add_argument("--chrnum", type=int, default=None, help="for messages only")
    ap.add_argument("--min-fires", type=int, default=5)
    args = ap.parse_args()

    label = f"hidden-guard contract [{args.mode}]"
    if args.chrnum is not None:
        label += f" (chr {args.chrnum})"
    failures = []

    tracks = load_tracks(args.trace)
    if not tracks:
        print(f"FAIL: {label}: no 'track' block on any frame "
              f"(GE007_TRACE_CHRNUM unset, or guard not present)")
        return 1
    if not any(t.get("present") for _, t, _ in tracks):
        print(f"FAIL: {label}: track present flag never set (chr never resolved)")
        return 1

    hidden = [(f, t, bh) for f, t, bh in tracks if t.get("flag_hidden") == 1]
    visible = [(f, t, bh) for f, t, bh in tracks if t.get("flag_hidden") == 0]
    if not hidden:
        print(f"FAIL: {label}: flag_hidden never became 1 "
              f"(GE007_AUTO_SET_CHRFLAG hook did not apply)")
        return 1
    if not visible:
        print(f"FAIL: {label}: guard was hidden on every frame "
              f"(need a visible window to establish firing)")
        return 1
    hide_frame = hidden[0][0]

    # --- anti-vacuity: the guard actually fired while visible ---
    max_fire_visible = max((t.get("firecount") or 0) for _, t, _ in visible)
    if max_fire_visible < args.min_fires:
        print(f"FAIL: {label}: guard only reached firecount={max_fire_visible} "
              f"while visible (< --min-fires {args.min_fires}); the no-fire-while-"
              f"hidden check would be vacuous. Use a firing AI list / closer warp.")
        return 1
    print(f"  precondition: PASS (guard fired, peak firecount={max_fire_visible} "
          f"while visible; hidden at frame {hide_frame})")

    # --- no phantom fire: firecount must not increase while hidden ---
    prev = None
    max_inc = 0
    for _, t, _ in hidden:
        fc = t.get("firecount") or 0
        if prev is not None and fc > prev:
            max_inc = max(max_inc, fc - prev)
        prev = fc
    enforcer = "H1 fire-discharge gate" if args.mode == "h1" else "H2 AI-freeze gate"
    if max_inc > 0:
        failures.append(f"firecount INCREASED by up to {max_inc} while hidden "
                        f"(hidden guard still firing -> phantom fire; "
                        f"{enforcer} regressed)")
    else:
        print(f"  no-phantom-fire (firecount frozen while hidden): PASS "
              f"(enforced by the {enforcer})")

    # --- no phantom damage: Bond health does not drop after the hide ---
    bh_at_hide = next((bh for _, _, bh in hidden if bh is not None), None)
    bh_after = [bh for _, _, bh in hidden if bh is not None]
    if bh_at_hide is not None and bh_after:
        min_after = min(bh_after)
        if min_after < bh_at_hide - EPS:
            failures.append(f"Bond health dropped from {bh_at_hide:.4f} to "
                            f"{min_after:.4f} while the guard was hidden "
                            f"(phantom damage)")
        else:
            print(f"  no-phantom-damage (Bond health stable while hidden): PASS "
                  f"(>= {bh_at_hide:.4f})")
    else:
        print("  no-phantom-damage (Bond health): SKIP (no bond health samples)")

    actions = {t.get("action") for _, t, _ in hidden}
    offsets = {(t.get("ai") or {}).get("offset") for _, t, _ in hidden}
    ai_ticked = len(actions) > 1 or len(offsets) > 1

    if args.mode == "h2":
        # H2: AI frozen while hidden AND update-action bit clear.
        h2 = [(f, t) for f, t, _ in hidden if t.get("flag_update_action") == 0]
        if not h2:
            # The H2 scenario sets CLR=0x40000 precisely so this is non-empty.
            failures.append("H2 not exercised: no hidden frame had "
                            "flag_update_action==0 (CHRFLAG_00040000 was not "
                            "cleared); H2 would be a silent no-op pass")
        else:
            a = {t.get("action") for _, t in h2}
            o = {(t.get("ai") or {}).get("offset") for _, t in h2}
            if len(a) > 1 or len(o) > 1:
                failures.append(f"H2: AI not frozen while hidden — action="
                                f"{sorted(x for x in a if x is not None)}, "
                                f"ai.offset={sorted(x for x in o if x is not None)} "
                                f"(AI ticked on a hidden guard)")
            else:
                print(f"  H2 (AI frozen while hidden): PASS "
                      f"(action={a.pop()}, ai.offset={o.pop()})")
    else:
        # H1 isolation: AI must be TICKING (else the firecount freeze would be
        # H2's doing, not H1's), and firecount stayed frozen (asserted above).
        if not ai_ticked:
            failures.append("H1 not isolated: the AI was frozen while hidden "
                            "(action/ai.offset constant), so the firecount freeze "
                            "cannot be attributed to the H1 gate. Hide with "
                            "CHRFLAG_00040000 SET so the AI keeps ticking.")
        else:
            print(f"  H1 isolation (AI ticking while hidden, fire still blocked): "
                  f"PASS (action={sorted(x for x in actions if x is not None)}, "
                  f"ai.offset changed) -> firecount freeze is the H1 gate's doing")

    if failures:
        print(f"FAIL: {label}: {len(failures)} violation(s):")
        for msg in failures:
            print(f"    - {msg}")
        return 1
    print(f"PASS: {label}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
