#!/usr/bin/env python3
"""Compare the combat/floor oracle block (``combat_oracle``) between a native and an
ares (or any two) JSONL traces.

Emitted by both the instrumented-ares tracer and the native ``--trace-state`` path
(FID-0032). Schema parity is the contract; this tool aligns matched frames and reports
per-field divergences. Per S-Tier plan Task 1.1 step 3, *alignment succeeding* is the
pass criterion — divergences are the product (candidate findings), not a failure. Use
``--strict`` to also fail on any divergence.

Modelled on ``tools/compare_movement_trace.py`` (alignment + per-field tolerance table).
See ``docs/fidelity/combat_oracle_fields.md`` for the field/offset dossier.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any

# --- tolerance table (dossier §5) ------------------------------------------------

# Integer fields: compared exactly (mismatch = candidate finding).
COMBAT_INT_FIELDS = ["shots_fired_total", "hits_landed_total"]
COMBAT_HEX_FIELDS = ["rng_seed"]
COMBAT_FLOAT_FIELDS = ["player_health", "player_armor"]

FLOOR_INT_FIELDS = ["stan_id", "stan_room", "stan_flags"]
FLOOR_FLOAT_FIELDS = ["height"]

GUARD_INT_FIELDS = ["actiontype", "aimode", "flags_onscreen", "target_visible", "room"]
GUARD_HEX_FIELDS = ["anim_hash"]
GUARD_FLOAT_FIELDS = ["health", "shotbondsum"]

PROJ_INT_FIELDS = ["kind", "owner_chrnum"]


def load_trace(path: Path) -> list[dict[str, Any]]:
    records: list[dict[str, Any]] = []
    skipped = 0
    with path.open("r", encoding="utf-8", errors="replace") as handle:
        for line in handle:
            line = line.strip()
            if not line or not line.startswith("{"):
                continue
            try:
                records.append(json.loads(line))
            except json.JSONDecodeError:
                skipped += 1
    if skipped:
        print(f"WARNING: skipped {skipped} corrupted JSONL line(s) in {path}", file=sys.stderr)
    return records


def parse_int(value: Any) -> int | None:
    if isinstance(value, bool):
        return int(value)
    if isinstance(value, int):
        return value
    if isinstance(value, str):
        try:
            return int(value, 0)
        except ValueError:
            return None
    return None


def parse_float(value: Any) -> float | None:
    if isinstance(value, bool):
        return float(value)
    if isinstance(value, (int, float)):
        return float(value)
    if isinstance(value, str):
        try:
            return float(value)
        except ValueError:
            return None
    return None


def key_for(record: dict[str, Any], mode: str) -> Any:
    move = record.get("move", {})
    if mode == "global":
        return move.get("global") if isinstance(move, dict) else None
    if mode == "frame":
        return record.get("f")
    if mode == "clock":
        return move.get("clock") if isinstance(move, dict) else None
    raise AssertionError(mode)


def roster_size(record: dict[str, Any]) -> int:
    """Number of guard entries in a record's combat_oracle block.

    The ares combat emitter interleaves FULL-roster records with EMPTY-roster
    sampling records (menu / not-yet-populated / intra-frame samples): ~2789 of
    6500 records on the dam_combat_guard6 route carry an empty guard list. Native
    emits exactly one full-roster record per game-frame. This count is how
    ``--align tick`` distinguishes a canonical (roster-bearing) record from an
    empty sampling record so the interleave cannot create phantom
    ``guards[...].present`` divergences (the FID-0062 bug).
    """
    co = record.get("combat_oracle")
    if not isinstance(co, dict):
        return 0
    guards = co.get("guards")
    return len(guards) if isinstance(guards, list) else 0


def canonical_by_tick(
    records: list[dict[str, Any]], start: int, onset_global: Any
) -> dict[int, dict[str, Any]]:
    """Collapse ``records[start:]`` to one canonical record per SIM-TICK.

    Sim-tick = ``move.global`` (g_GlobalTimer) relative to the shared motion
    onset (``onset_global``). Because native emits 1 record/game-frame while ares
    emits ~2 records per advancing g_GlobalTimer tick (intra-frame AI substeps +
    empty-roster samples), pairing by record INDEX skews the two timelines ~2x in
    sim-tick space (FID-0062). Keying by the tick stamp instead makes the two
    cadences commensurate.

    Canonicalisation rule (handles the ares full/empty interleave):
      * EMPTY-roster records (roster_size == 0) are DROPPED — they never become
        the canonical record for a tick. A tick that has *only* empty-roster
        samples produces no entry and simply does not pair (dropped, not a
        divergence), instead of pairing an empty roster against native's full
        roster and inventing ~36 phantom ``guards[...].present`` divergences
        (the naive-tick artifact: 3492 hits, §11.4).
      * Among the roster-bearing records for a tick, keep the LAST one with the
        MAXIMUM roster size (the roster-complete, latest intra-frame substep) —
        this matches native's once-per-frame end-of-frame snapshot.
    """
    buckets: dict[int, tuple[int, dict[str, Any]]] = {}
    onset = parse_int(onset_global)
    if onset is None:
        return {}
    for record in records[start:]:
        move = record.get("move", {})
        if not isinstance(move, dict):
            continue
        g = parse_int(move.get("global"))
        if g is None:
            continue
        n = roster_size(record)
        if n == 0:
            continue  # drop empty-roster sampling records
        rel = g - onset
        prev = buckets.get(rel)
        if prev is None or n >= prev[0]:  # last record with >= max roster wins
            buckets[rel] = (n, record)
    return {rel: rec for rel, (n, rec) in buckets.items()}


def observed_ticks(records: list[dict[str, Any]], start: int, onset_global: Any) -> set[int]:
    """Every relative sim-tick where ``records[start:]`` has AT LEAST ONE record
    with a parseable ``move.global`` stamp -- regardless of roster size.

    This is the "did this side ever look at tick k" set. It is a superset of
    ``canonical_by_tick``'s keys (which additionally require a non-empty
    roster): a tick present here but absent from ``canonical_by_tick`` is a
    genuine "0 guards observed" sample (an empty-roster record actually
    emitted for that tick), as opposed to a tick this side never sampled at
    all -- a sampling-cadence gap. P1f's C1 fix (Lane C, 2026-07-11) uses this
    distinction to tell a real present-but-empty roster (synthesize a
    pairing) from a coverage gap (exclude + report, never fabricate).
    """
    onset = parse_int(onset_global)
    if onset is None:
        return set()
    out: set[int] = set()
    for record in records[start:]:
        move = record.get("move", {})
        if not isinstance(move, dict):
            continue
        g = parse_int(move.get("global"))
        if g is None:
            continue
        out.add(g - onset)
    return out


def _empty_roster_record(source: dict[str, Any]) -> dict[str, Any]:
    """A stand-in record for "this side never produced a full roster this
    tick" (P1f), shaped like ``source`` but with its ``combat_oracle.guards``
    zeroed out.

    Used only by ``align_records``'s ``tick`` mode when one side has a
    roster-bearing canonical record for a tick and the other does not: the
    synthetic record borrows the roster-bearing side's own combat/floor/
    projectiles context (so those blocks compare equal to themselves and add
    no noise) while presenting an empty guard list, so the comparison
    surfaces exactly the intended ``guards[...].present`` divergence.
    """
    co = source.get("combat_oracle") if isinstance(source, dict) else None
    co = dict(co) if isinstance(co, dict) else {}
    co["guards"] = []
    return {"combat_oracle": co}


def first_moving_index(records: list[dict[str, Any]], threshold: float) -> int:
    """First record whose player move.speed/raw exceeds ``threshold`` (motion onset).

    Mirrors ``compare_movement_trace.first_moving_index`` so a native direct-boot
    trace (g_GlobalTimer starts at 0 in gameplay) can be anchored against an ares
    menu-boot trace (gameplay begins well after the menu) by motion onset rather
    than absolute timer, which otherwise pairs native-gameplay with ares-menu frames.
    """
    for index, record in enumerate(records):
        move = record.get("move", {})
        if not isinstance(move, dict):
            continue
        speed = move.get("speed") or []
        raw = move.get("raw") or []
        for seq in (speed, raw):
            if isinstance(seq, list):
                for value in seq:
                    if isinstance(value, (int, float)) and abs(float(value)) > threshold:
                        return index
    return 0


def align_tick(
    baseline: list[dict[str, Any]],
    test: list[dict[str, Any]],
    motion_threshold: float = 0.01,
) -> tuple[list[tuple[Any, dict[str, Any], dict[str, Any]]], dict[str, Any]]:
    """Sim-tick alignment for the combat comparator (FID-0062, P1f Lane C fix
    2026-07-11 -- see C1/I1/I2/I3 in the 2026-07-10 review).

    Pairs by SIM-TICK (move.global relative to shared motion onset), collapsing
    the ares full/empty-roster interleave to one canonical record per tick (see
    ``canonical_by_tick``). This is the trustworthy combat-route alignment:
    ``--align move`` pairs by record index and skews the timelines ~2x because
    native emits 1 rec/game-frame while ares emits ~2 rec/advancing-tick,
    inflating+misattributing divergences.

    ``canonical_by_tick`` drops EMPTY-roster records so the WITHIN-a-side ares
    interleave collapses to one canonical record per tick -- that collapse is
    correct. But a tick where side A has a full roster and side B never
    produced a CANONICAL (roster-bearing) record this tick isn't a key in one
    of the two canonical dicts, so a plain intersection would silently drop
    it. Whether that drop is legitimate depends on why side B has no canonical
    record:

      * Side B genuinely OBSERVED the tick with an empty roster (a real
        "0 guards" sample exists in its raw stream, see ``observed_ticks``) --
        this is a genuine present-vs-empty divergence and MUST be paired
        against a synthetic empty-roster stand-in (``_empty_roster_record``)
        so the comparison surfaces exactly the ``guards[...].present``
        divergence.
      * Side B has NO record at all at that tick (its sampling lattice never
        landed there -- native samples every ``g_ClockTimer`` tick, ares/stock
        on a different, coarser and phase-shifted cadence: this is routine,
        not a bug). "No observation" is not evidence of "0 guards"; pairing it
        would fabricate a divergence out of a sampling-cadence gap (the C1
        bug -- 10404/10404 phantom ``guards.present`` hits on the reference
        dam_combat_guard6 capture were exactly this class). These ticks are
        EXCLUDED, never paired -- but per charter rule 9 ("no silent caps"),
        they are counted and range-reported in ``info`` below, not dropped
        without a trace.

    Bounded to the MUTUALLY-OBSERVED tick range (I3: computed from OBSERVED
    ticks -- ``observed_ticks``, i.e. any record, not just roster-bearing ones
    -- so a side that genuinely despawns its whole roster to empty mid-capture
    doesn't shrink the window and reclassify a real divergence as a coverage
    gap). Real captures routinely cover very different absolute sim-tick spans
    (one side's capture simply runs far longer); a tick outside the OTHER
    side's ever-observed range is a trace-length/coverage-window mismatch
    (instrumentation gap), not a mid-stream roster divergence, and surfacing
    it would flood real findings with noise proportional to the length
    mismatch. These out-of-window ticks are also excluded-and-reported (I1),
    distinctly from the in-window missing-record exclusions above.

    Returns ``(pairs, info)``. ``pairs`` is shaped like ``align_records``'s
    return value. ``info`` (charter rule 9 report, never silent):
      * ``real_pairs`` -- ticks where BOTH sides produced a canonical
        (roster-bearing) record: a genuine mutual observation. This is what
        ``--min-aligned`` should gate on (I2) -- synthetic pairs below are
        real divergence evidence but are not proof the two captures actually
        overlapped in sim-tick space.
      * ``synthetic_pairs`` -- one-sided ticks paired against a genuine
        present-but-empty observation on the other side.
      * ``excluded_missing_record`` -- per side: count + tick range of
        one-sided ticks excluded because the OTHER side never sampled that
        tick at all (sampling-cadence gap, C1).
      * ``excluded_out_of_window`` -- per side: count + tick range of
        one-sided ticks excluded because they fall outside the mutually
        observed coverage window (I1).
      * ``coverage_window`` -- per-side observed tick range and the overlap
        window the two exclusion classes above are computed against.
    """
    baseline_start = first_moving_index(baseline, motion_threshold)
    test_start = first_moving_index(test, motion_threshold)
    onset_b = baseline[baseline_start].get("move", {}).get("global") \
        if baseline_start < len(baseline) else None
    onset_t = test[test_start].get("move", {}).get("global") \
        if test_start < len(test) else None

    b_tick = canonical_by_tick(baseline, baseline_start, onset_b)
    t_tick = canonical_by_tick(test, test_start, onset_t)
    b_observed = observed_ticks(baseline, baseline_start, onset_b)
    t_observed = observed_ticks(test, test_start, onset_t)

    both = set(b_tick) & set(t_tick)
    pairs = [(k, b_tick[k], t_tick[k]) for k in both]

    coverage_window: dict[str, Any] = {
        "baseline_observed_range": [min(b_observed), max(b_observed)] if b_observed else None,
        "test_observed_range": [min(t_observed), max(t_observed)] if t_observed else None,
    }
    if b_observed and t_observed:
        overlap_lo = max(min(b_observed), min(t_observed))
        overlap_hi = min(max(b_observed), max(t_observed))
    else:
        overlap_lo = overlap_hi = None
    coverage_window["overlap"] = [overlap_lo, overlap_hi] if overlap_lo is not None else None

    excluded_missing_record: dict[str, list[int]] = {"baseline": [], "test": []}
    excluded_out_of_window: dict[str, list[int]] = {"baseline": [], "test": []}
    synthetic_ticks: list[int] = []

    for k in sorted(set(b_tick) ^ set(t_tick)):
        b_has = k in b_tick
        side = "baseline" if b_has else "test"  # the side HOLDING the one-sided canonical record
        in_window = overlap_lo is not None and overlap_lo <= k <= overlap_hi
        if not in_window:
            excluded_out_of_window[side].append(k)
            continue
        if b_has:
            if k in t_observed:
                pairs.append((k, b_tick[k], _empty_roster_record(b_tick[k])))
                synthetic_ticks.append(k)
            else:
                excluded_missing_record[side].append(k)
        else:
            if k in b_observed:
                pairs.append((k, _empty_roster_record(t_tick[k]), t_tick[k]))
                synthetic_ticks.append(k)
            else:
                excluded_missing_record[side].append(k)

    pairs.sort(key=lambda item: item[0])

    def _report(ticks: list[int]) -> dict[str, Any]:
        return {"count": len(ticks), "range": [min(ticks), max(ticks)] if ticks else None}

    info = {
        "coverage_window": coverage_window,
        "real_pairs": len(both),
        "synthetic_pairs": len(synthetic_ticks),
        "excluded_missing_record": {side: _report(ticks) for side, ticks in excluded_missing_record.items()},
        "excluded_out_of_window": {side: _report(ticks) for side, ticks in excluded_out_of_window.items()},
    }
    return pairs, info


def align_records(
    baseline: list[dict[str, Any]],
    test: list[dict[str, Any]],
    mode: str,
    motion_threshold: float = 0.01,
) -> list[tuple[Any, dict[str, Any], dict[str, Any]]]:
    if mode == "index":
        count = min(len(baseline), len(test))
        return [(i, baseline[i], test[i]) for i in range(count)]
    if mode == "move":
        baseline_start = first_moving_index(baseline, motion_threshold)
        test_start = first_moving_index(test, motion_threshold)
        count = min(len(baseline) - baseline_start, len(test) - test_start)
        return [
            (i, baseline[baseline_start + i], test[test_start + i])
            for i in range(max(0, count))
        ]
    if mode == "tick":
        pairs, _info = align_tick(baseline, test, motion_threshold)
        return pairs

    def by_key(records: list[dict[str, Any]]) -> dict[Any, dict[str, Any]]:
        out: dict[Any, dict[str, Any]] = {}
        for record in records:
            k = key_for(record, mode)
            if k is None:
                continue
            out.setdefault(k, record)
        return out

    b = by_key(baseline)
    t = by_key(test)
    keys = sorted(set(b) & set(t), key=lambda x: (isinstance(x, str), x))
    return [(k, b[k], t[k]) for k in keys]


def combat_records(records: list[dict[str, Any]]) -> list[dict[str, Any]]:
    return [r for r in records if isinstance(r.get("combat_oracle"), dict)]


class Divergence:
    __slots__ = ("frame", "path", "baseline", "test")

    def __init__(self, frame: Any, path: str, baseline: Any, test: Any) -> None:
        self.frame = frame
        self.path = path
        self.baseline = baseline
        self.test = test

    def as_dict(self) -> dict[str, Any]:
        return {"frame": self.frame, "field": self.path, "baseline": self.baseline, "test": self.test}


def cmp_int(frame, prefix, key, bval, tval, out: list[Divergence]) -> None:
    bi, ti = parse_int(bval), parse_int(tval)
    if bi != ti:
        out.append(Divergence(frame, f"{prefix}.{key}", bval, tval))


def cmp_float(frame, prefix, key, bval, tval, tol, out: list[Divergence]) -> None:
    bf, tf = parse_float(bval), parse_float(tval)
    if bf is None or tf is None:
        if bf is not tf:
            out.append(Divergence(frame, f"{prefix}.{key}", bval, tval))
        return
    if abs(bf - tf) > tol:
        out.append(Divergence(frame, f"{prefix}.{key}", bval, tval))


def compare_combat_block(frame, base, test, tols, out: list[Divergence]) -> None:
    b = base.get("combat", {}) if isinstance(base.get("combat"), dict) else {}
    t = test.get("combat", {}) if isinstance(test.get("combat"), dict) else {}
    for key in COMBAT_INT_FIELDS:
        cmp_int(frame, "combat", key, b.get(key), t.get(key), out)
    for key in COMBAT_HEX_FIELDS:
        cmp_int(frame, "combat", key, b.get(key), t.get(key), out)
    for key in COMBAT_FLOAT_FIELDS:
        cmp_float(frame, "combat", key, b.get(key), t.get(key), tols["health"], out)


def compare_floor_block(frame, base, test, tols, out: list[Divergence]) -> None:
    b = base.get("floor", {}) if isinstance(base.get("floor"), dict) else {}
    t = test.get("floor", {}) if isinstance(test.get("floor"), dict) else {}
    for key in FLOOR_INT_FIELDS:
        cmp_int(frame, "floor", key, b.get(key), t.get(key), out)
    for key in FLOOR_FLOAT_FIELDS:
        cmp_float(frame, "floor", key, b.get(key), t.get(key), tols["position"], out)


def guards_by_chrnum(block: dict[str, Any]) -> dict[int, dict[str, Any]]:
    out: dict[int, dict[str, Any]] = {}
    for g in block.get("guards", []) or []:
        if not isinstance(g, dict):
            continue
        cn = parse_int(g.get("chrnum"))
        if cn is None:
            continue
        out.setdefault(cn, g)
    return out


def compare_guards(frame, base, test, tols, out: list[Divergence]) -> None:
    b = guards_by_chrnum(base)
    t = guards_by_chrnum(test)
    for cn in sorted(set(b) | set(t)):
        if cn not in b or cn not in t:
            out.append(Divergence(frame, f"guards[chr={cn}].present",
                                   cn in b, cn in t))
            continue
        gb, gt = b[cn], t[cn]
        prefix = f"guards[chr={cn}]"
        for key in GUARD_INT_FIELDS:
            cmp_int(frame, prefix, key, gb.get(key), gt.get(key), out)
        for key in GUARD_HEX_FIELDS:
            cmp_int(frame, prefix, key, gb.get(key), gt.get(key), out)
        for key in GUARD_FLOAT_FIELDS:
            cmp_float(frame, prefix, key, gb.get(key), gt.get(key), tols["health"], out)
        pb, pt = gb.get("pos"), gt.get("pos")
        if isinstance(pb, list) and isinstance(pt, list) and len(pb) == 3 and len(pt) == 3:
            for i in range(3):
                cmp_float(frame, f"{prefix}.pos", str(i), pb[i], pt[i], tols["position"], out)


def compare_projectiles(frame, base, test, tols, out: list[Divergence]) -> None:
    pb = base.get("projectiles", []) or []
    pt = test.get("projectiles", []) or []
    if len(pb) != len(pt):
        out.append(Divergence(frame, "projectiles.count", len(pb), len(pt)))
    for i in range(min(len(pb), len(pt))):
        a, c = pb[i], pt[i]
        if not (isinstance(a, dict) and isinstance(c, dict)):
            continue
        prefix = f"projectiles[{i}]"
        for key in PROJ_INT_FIELDS:
            cmp_int(frame, prefix, key, a.get(key), c.get(key), out)
        qa, qc = a.get("pos"), c.get("pos")
        if isinstance(qa, list) and isinstance(qc, list) and len(qa) == 3 and len(qc) == 3:
            for k in range(3):
                cmp_float(frame, f"{prefix}.pos", str(k), qa[k], qc[k], tols["position"], out)


def compare(args: argparse.Namespace) -> int:
    baseline_path = Path(args.baseline)
    test_path = Path(args.test)
    baseline_all = load_trace(baseline_path)
    test_all = load_trace(test_path)

    baseline = combat_records(baseline_all)
    test = combat_records(test_all)

    tols = {
        "position": args.position_tolerance,
        "health": args.health_tolerance,
    }

    if args.align == "tick":
        aligned, align_info = align_tick(baseline, test, args.motion_threshold)
    else:
        aligned = align_records(baseline, test, args.align, args.motion_threshold)
        # I2: only --align tick can produce synthetic (one-sided) pairs; every
        # other mode's pairs are all real mutual observations by construction.
        align_info = {
            "coverage_window": None,
            "real_pairs": len(aligned),
            "synthetic_pairs": 0,
            "excluded_missing_record": None,
            "excluded_out_of_window": None,
        }

    divergences: list[Divergence] = []
    field_counts: dict[str, int] = {}
    for frame, brec, trec in aligned:
        base = brec["combat_oracle"]
        tst = trec["combat_oracle"]
        before = len(divergences)
        compare_combat_block(frame, base, tst, tols, divergences)
        compare_floor_block(frame, base, tst, tols, divergences)
        compare_guards(frame, base, tst, tols, divergences)
        compare_projectiles(frame, base, tst, tols, divergences)
        for d in divergences[before:]:
            # collapse per-field-family for the summary
            fam = d.path.split("[")[0].split(".")[0] + "." + d.path.rsplit(".", 1)[-1]
            field_counts[fam] = field_counts.get(fam, 0) + 1

    metrics: dict[str, Any] = {
        "baseline": str(baseline_path),
        "test": str(test_path),
        "align": args.align,
        "tolerances": tols,
        "record_counts": {
            "baseline_all": len(baseline_all),
            "test_all": len(test_all),
            "baseline_combat": len(baseline),
            "test_combat": len(test),
        },
        "aligned_frames": len(aligned),
        "aligned_real_pairs": align_info["real_pairs"],
        "aligned_synthetic_pairs": align_info["synthetic_pairs"],
        "coverage_window": align_info["coverage_window"],
        "excluded_missing_record": align_info["excluded_missing_record"],
        "excluded_out_of_window": align_info["excluded_out_of_window"],
        "divergences_total": len(divergences),
        "divergences_by_field": dict(sorted(field_counts.items(), key=lambda kv: -kv[1])),
        "sample_divergences": [d.as_dict() for d in divergences[: args.max_report]],
    }

    if args.json_out:
        Path(args.json_out).write_text(json.dumps(metrics, indent=2, sort_keys=True) + "\n",
                                       encoding="utf-8")

    print(json.dumps(metrics["divergences_by_field"], indent=2))
    print(f"aligned_frames={len(aligned)} divergences_total={len(divergences)}")
    print(f"aligned_real_pairs={align_info['real_pairs']} "
          f"aligned_synthetic_pairs={align_info['synthetic_pairs']}")

    # Charter rule 9 ("no silent caps"): --align tick bounds coverage (the
    # mutually-observed tick window, C1/I1) -- print what it excluded, never
    # just drop it.
    if args.align == "tick":
        emr = align_info["excluded_missing_record"]
        eow = align_info["excluded_out_of_window"]
        print(f"coverage_window={align_info['coverage_window']}")
        print(f"excluded_missing_record: baseline={emr['baseline']['count']} "
              f"(range {emr['baseline']['range']}) "
              f"test={emr['test']['count']} (range {emr['test']['range']})")
        print(f"excluded_out_of_window: baseline={eow['baseline']['count']} "
              f"(range {eow['baseline']['range']}) "
              f"test={eow['test']['count']} (range {eow['test']['range']})")

    # I2: gate success on REAL (mutually-observed) pairs only. Synthetic pairs
    # are legitimate divergence evidence but two captures with fully disjoint
    # tick lattices could otherwise report a healthy aligned_frames count with
    # zero mutually-observed ticks -- false confidence in the tool's own pass
    # criterion.
    if align_info["real_pairs"] < args.min_aligned:
        print(f"FAIL: only {align_info['real_pairs']} real aligned pairs "
              f"(< --min-aligned {args.min_aligned}); alignment did not succeed",
              file=sys.stderr)
        return 2

    if args.strict and divergences:
        print(f"FAIL(strict): {len(divergences)} field divergence(s)", file=sys.stderr)
        return 1

    print(f"OK: alignment succeeded ({align_info['real_pairs']} real pairs, "
          f"{align_info['synthetic_pairs']} synthetic, {len(aligned)} total); "
          f"{len(divergences)} divergence(s) reported as findings")
    return 0


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--baseline", required=True, help="ares (or reference) JSONL trace")
    p.add_argument("--test", required=True, help="native (or candidate) JSONL trace")
    p.add_argument("--align", default="global",
                   choices=["global", "frame", "clock", "index", "move", "tick"],
                   help="frame alignment key (default: global = g_GlobalTimer). "
                        "'tick' is the TRUSTWORTHY combat-route mode (FID-0062): it "
                        "anchors both sides on motion onset AND pairs by sim-tick "
                        "(move.global relative to onset), collapsing the ares "
                        "~2-rec/tick full/empty-roster interleave to one canonical "
                        "record per tick so the sampling-rate mismatch cannot invent "
                        "divergences. 'move' anchors on motion onset but pairs by "
                        "record INDEX — it skews the timelines ~2x on combat routes "
                        "(native 1 rec/frame vs ares ~2 rec/tick).")
    p.add_argument("--motion-threshold", type=float, default=0.01,
                   help="min |move.speed/raw| to count as motion onset for --align move")
    p.add_argument("--position-tolerance", type=float, default=0.5,
                   help="float position/height epsilon (world units)")
    p.add_argument("--health-tolerance", type=float, default=1.0,
                   help="float health/shotbondsum epsilon (raw units)")
    p.add_argument("--min-aligned", type=int, default=1,
                   help="minimum aligned frames for alignment to count as success")
    p.add_argument("--max-report", type=int, default=50,
                   help="max sample divergences embedded in the metrics JSON")
    p.add_argument("--strict", action="store_true",
                   help="also exit non-zero if any field diverges (default: divergences are findings)")
    p.add_argument("--json-out", help="write full metrics JSON to this path")
    return p


def main(argv: list[str]) -> int:
    return compare(build_parser().parse_args(argv))


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
