"""FID-0062 regression lane -- pins the sim-tick alignment of the combat
comparator (``tools/compare_combat_trace.py --align tick``).

The bug: ``--align move`` pairs records by INDEX, but the two combat emitters run
at different record cadences -- native emits exactly 1 record per game-frame while
the ares/stock emitter emits ~2 records per advancing g_GlobalTimer tick (intra-
frame AI substeps) AND interleaves EMPTY-roster sampling records. Index pairing
therefore skews the timelines ~2x in sim-tick space, inflating and misattributing
combat-oracle guard divergences. ``--align tick`` pairs by the move.global sim-tick
stamp (relative to shared motion onset) and collapses the ares interleave to one
canonical roster-bearing record per tick.

All fixtures are synthetic JSONL built in-test -- nothing here is ROM-derived.
Tests exercise both the internal alignment helpers and the CLI end-to-end.

The fail-on-revert guard: ``test_tick_align_has_no_phantom_divergences`` asserts
tick-alignment finds ZERO divergences on a construction where the two sides agree
per sim-tick. If ``--align tick`` is reverted to index pairing, the ~2x skew and
the empty-roster interleave both re-appear as divergences and this test reddens.

Run: python3 -m unittest tools.tests.test_compare_combat_trace
 or: python3 tools/tests/test_compare_combat_trace.py
"""
from __future__ import annotations

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from tools import compare_combat_trace as cct  # noqa: E402

SCRIPT = Path(__file__).resolve().parent.parent / "compare_combat_trace.py"

# Two guards; their actiontype is a deterministic function of the SIM-TICK, so any
# alignment that pairs the same sim-tick on both sides sees identical actiontypes
# (zero divergences), while any alignment that skews the tick mapping sees them
# differ.
GUARD_CHRNUMS = (6, 7)
SPEEDFRAMES = 3  # g_ClockTimer per game-frame on the Dam combat route


def guard(chrnum: int, rel_tick: int) -> dict:
    """A schema-complete guard whose actiontype/room encode the sim-tick."""
    phase = rel_tick // SPEEDFRAMES
    return {
        "chrnum": chrnum,
        "pos": [float(chrnum), 1.0, float(rel_tick)],
        "actiontype": phase % 16,
        "aimode": 1,
        "health": 4.0,
        "shotbondsum": 0.0,
        "flags_onscreen": 1,
        "target_visible": 1 if phase >= 10 else 0,
        "anim_hash": "0x0000000000000abc",
        "room": 135,
    }


def combat_oracle(rel_tick: int, *, empty_roster: bool) -> dict:
    guards = [] if empty_roster else [guard(cn, rel_tick) for cn in GUARD_CHRNUMS]
    return {
        "guards": guards,
        "guards_overflow": 0,
        "floor": {"stan_id": 777, "stan_room": 4, "stan_flags": 0, "height": -107.0},
        "combat": {
            "player_health": 700.0,
            "player_armor": 0.0,
            "shots_fired_total": 0,
            "hits_landed_total": 0,
            "rng_seed": "0x00000000",
        },
        "projectiles": [],
        "projectiles_overflow": 0,
    }


def record(global_tick: int, rel_tick: int, *, empty_roster: bool = False) -> dict:
    # Non-zero move.speed from the first record so first_moving_index == 0 on both
    # sides (motion onset at index 0); the alignment difference under test is the
    # tick-vs-index pairing, not the onset search.
    return {
        "f": global_tick,
        "p": 1,
        "move": {"global": global_tick, "clock": SPEEDFRAMES, "speed": [1.0, 0.0, 1.0]},
        "combat_oracle": combat_oracle(rel_tick, empty_roster=empty_roster),
    }


def native_stream(onset_global: int, n_ticks: int) -> list[dict]:
    """Native cadence: exactly 1 full-roster record per advancing sim-tick."""
    out = []
    for k in range(n_ticks):
        rel = k * SPEEDFRAMES
        out.append(record(onset_global + rel, rel))
    return out


def ares_stream(onset_global: int, n_ticks: int) -> list[dict]:
    """ares/stock cadence: for each advancing sim-tick, an EMPTY-roster sample
    followed by (sometimes two) full-roster substep records -- ~2 records/tick
    with the full/empty interleave that broke the naive tick align."""
    out = []
    for k in range(n_ticks):
        rel = k * SPEEDFRAMES
        g = onset_global + rel
        out.append(record(g, rel, empty_roster=True))       # empty sampling record
        out.append(record(g, rel))                          # full-roster substep
        if k % 2 == 0:
            out.append(record(g, rel))                      # extra intra-frame substep
    return out


class TickAlignmentHelpers(unittest.TestCase):
    def test_canonical_by_tick_drops_empty_roster(self) -> None:
        recs = ares_stream(1387, 5)
        canon = cct.canonical_by_tick(recs, 0, 1387)
        # One entry per advancing sim-tick (0,3,6,9,12), never an empty roster.
        self.assertEqual(sorted(canon), [0, 3, 6, 9, 12])
        for rel, rec in canon.items():
            self.assertEqual(cct.roster_size(rec), len(GUARD_CHRNUMS),
                             f"tick {rel} canonicalised to an empty/partial roster")

    def test_tick_align_pairs_by_simtick_not_index(self) -> None:
        base = ares_stream(1387, 20)     # ~2.5 rec/tick, different onset
        test = native_stream(241, 20)    # 1 rec/tick
        aligned = cct.align_records(base, test, "tick")
        # Paired keys are relative sim-ticks; both sides carry the same rel_tick.
        self.assertTrue(aligned)
        for rel, brec, trec in aligned:
            self.assertEqual(brec["move"]["global"] - 1387, rel)
            self.assertEqual(trec["move"]["global"] - 241, rel)
            self.assertEqual(cct.roster_size(brec), len(GUARD_CHRNUMS))
            self.assertEqual(cct.roster_size(trec), len(GUARD_CHRNUMS))


class TickVsMoveDivergence(unittest.TestCase):
    """The core FID-0062 assertion + fail-on-revert guard."""

    def setUp(self) -> None:
        self.tmp = tempfile.TemporaryDirectory()
        d = Path(self.tmp.name)
        self.baseline = d / "stock.jsonl"   # ares cadence
        self.test = d / "native.jsonl"      # native cadence
        write_jsonl(self.baseline, ares_stream(1387, 40))
        write_jsonl(self.test, native_stream(241, 40))

    def tearDown(self) -> None:
        self.tmp.cleanup()

    def _run(self, align: str) -> dict:
        with tempfile.NamedTemporaryFile("r", suffix=".json") as out:
            subprocess.run(
                [sys.executable, str(SCRIPT),
                 "--baseline", str(self.baseline),
                 "--test", str(self.test),
                 "--align", align,
                 "--json-out", out.name],
                capture_output=True, text=True, check=True,
            )
            return json.loads(Path(out.name).read_text())

    def test_tick_align_has_no_phantom_divergences(self) -> None:
        """Under sim-tick alignment the two sides agree per tick => 0 divergences.

        FAIL-ON-REVERT: revert --align tick to index pairing and both the ~2x
        skew and the empty-roster interleave reappear as divergences => this
        assertion reddens.
        """
        metrics = self._run("tick")
        self.assertEqual(metrics["divergences_total"], 0,
                         f"tick-align invented divergences: "
                         f"{metrics['divergences_by_field']}")
        self.assertGreaterEqual(metrics["aligned_frames"], 40)

    def test_move_align_shows_skew_and_phantom_present(self) -> None:
        """--align move (index pairing) MUST inflate divergences on the same
        cadence-mismatched pair -- the bug this fix removes."""
        metrics = self._run("move")
        self.assertGreater(metrics["divergences_total"], 0,
                           "move-align should surface the index-pairing skew")
        # The empty-roster interleave shows up as phantom guard 'present'
        # divergences under index pairing; tick-align must not.
        self.assertIn("guards.present", metrics["divergences_by_field"])

    def test_tick_strictly_fewer_divergences_than_move(self) -> None:
        tick = self._run("tick")["divergences_total"]
        move = self._run("move")["divergences_total"]
        self.assertLess(tick, move)


class OneSidedRosterAlignment(unittest.TestCase):
    """P1f (FID-0062 follow-up, Lane C): a tick where ONE side has a full
    roster and the other side has ONLY empty-roster records (or no record at
    all) must surface as a divergence, not silently vanish pre-intersection.
    Distinct from (a) the legitimate WITHIN-a-side collapse of ares' ~2-rec/
    tick interleave (pinned green above by ``TickAlignmentHelpers`` /
    ``TickVsMoveDivergence``) and (b) a genuinely BOTH-sides-empty tick, which
    must stay a non-divergence (0 guards vs 0 guards -- nothing to compare).
    """

    def _gap_stream(self, onset: int, n: int, gap_rel: int, *, drop: bool = False,
                     empty: bool = False) -> list[dict]:
        stream = native_stream(onset, n)
        if drop:
            return [r for r in stream if r["move"]["global"] - onset != gap_rel]
        if empty:
            out = []
            for r in stream:
                if r["move"]["global"] - onset == gap_rel:
                    out.append(record(r["move"]["global"], gap_rel, empty_roster=True))
                else:
                    out.append(r)
            return out
        return stream

    def test_one_sided_full_vs_empty_tick_is_a_divergence(self) -> None:
        """(a) Baseline full roster at every tick; test side's gap tick has
        ONLY an empty-roster sample. FAIL-ON-REVERT: pre-fix, the gap tick is
        dropped by the plain intersection in ``align_records``'s tick branch
        and this assertion reddens (no pairing => no divergence found)."""
        n, gap_rel = 10, 3 * 4
        baseline = native_stream(1000, n)
        test = self._gap_stream(1000, n, gap_rel, empty=True)

        aligned = cct.align_records(baseline, test, "tick")
        keys = [k for k, _, _ in aligned]
        self.assertIn(gap_rel, keys,
                      "one-sided full-vs-empty tick must not be dropped pre-intersection")

        _, brec, trec = next(item for item in aligned if item[0] == gap_rel)
        divs: list[cct.Divergence] = []
        cct.compare_guards(gap_rel, brec["combat_oracle"], trec["combat_oracle"],
                            {"health": 1.0, "position": 0.5}, divs)
        paths = {d.path for d in divs}
        for cn in GUARD_CHRNUMS:
            self.assertIn(f"guards[chr={cn}].present", paths,
                          f"expected guards[chr={cn}].present divergence on the gap tick")

        # End-to-end: the CLI must report it too.
        with tempfile.TemporaryDirectory() as tmp:
            d = Path(tmp)
            b_path, t_path = d / "b.jsonl", d / "t.jsonl"
            write_jsonl(b_path, baseline)
            write_jsonl(t_path, test)
            with tempfile.NamedTemporaryFile("r", suffix=".json") as out:
                subprocess.run(
                    [sys.executable, str(SCRIPT), "--baseline", str(b_path),
                     "--test", str(t_path), "--align", "tick", "--json-out", out.name],
                    capture_output=True, text=True, check=True,
                )
                metrics = json.loads(Path(out.name).read_text())
        self.assertIn("guards.present", metrics["divergences_by_field"])
        self.assertGreater(metrics["divergences_by_field"]["guards.present"], 0)

    def test_one_sided_full_vs_missing_record_is_excluded_and_reported(self) -> None:
        """(a) variant: the gap tick has NO record at all on the test side
        (not even an empty-roster sample). Unlike the genuine full-vs-empty
        case above, "no observation" is NOT evidence of "0 guards" -- it's a
        sampling-cadence gap (the two emitters simply have different tick
        lattices; on the real dam_combat_guard6 captures this class was
        289 one-sided ticks x 36 guards = 10404 phantom guards.present
        divergences, 0 of which were genuine). C1 fix (2026-07-11 review):
        this must be EXCLUDED from the aligned pairs (no divergence
        fabricated) and loudly reported via ``excluded_missing_record``
        (charter rule 9), not silently dropped either.

        FAIL-ON-REVERT: pre-fix, this tick was treated identically to the
        genuine full-vs-empty case above and synthesized into a pairing,
        fabricating a phantom guards[].present divergence. This assertion
        reddens against the pre-fix comparator."""
        n, gap_rel = 10, 3 * 4
        baseline = native_stream(1000, n)
        test = self._gap_stream(1000, n, gap_rel, drop=True)

        aligned = cct.align_records(baseline, test, "tick")
        keys = [k for k, _, _ in aligned]
        self.assertNotIn(gap_rel, keys,
                         "missing-record tick must be EXCLUDED (never synthesized into "
                         "a phantom divergence) -- there is no 'other side' observation "
                         "to compare against")

        _, info = cct.align_tick(baseline, test)
        # "baseline" here names the side HOLDING the one-sided canonical record
        # (baseline/native_stream has tick 12; test dropped it entirely).
        self.assertEqual(info["excluded_missing_record"]["baseline"]["count"], 1,
                          "the missing-record tick must be counted, not dropped silently")
        self.assertEqual(info["excluded_missing_record"]["baseline"]["range"], [gap_rel, gap_rel])
        self.assertEqual(info["synthetic_pairs"], 0)

        # End-to-end: zero divergences (nothing to compare), and the CLI's JSON
        # report must surface the exclusion (rule 9 -- never silent).
        with tempfile.TemporaryDirectory() as tmp:
            d = Path(tmp)
            b_path, t_path = d / "b.jsonl", d / "t.jsonl"
            write_jsonl(b_path, baseline)
            write_jsonl(t_path, test)
            with tempfile.NamedTemporaryFile("r", suffix=".json") as out:
                subprocess.run(
                    [sys.executable, str(SCRIPT), "--baseline", str(b_path),
                     "--test", str(t_path), "--align", "tick", "--json-out", out.name],
                    capture_output=True, text=True, check=True,
                )
                metrics = json.loads(Path(out.name).read_text())
        self.assertEqual(metrics["divergences_total"], 0)
        self.assertEqual(metrics["excluded_missing_record"]["baseline"]["count"], 1)

    def test_both_sides_empty_tick_is_not_a_divergence(self) -> None:
        """(b) Both sides have ONLY an empty-roster sample at the same tick
        (0 guards vs 0 guards) -- must remain a non-divergence, not be
        promoted into a spurious one-sided pairing by the P1f fix."""
        n, gap_rel = 10, 3 * 4
        baseline = self._gap_stream(1000, n, gap_rel, empty=True)
        test = self._gap_stream(2000, n, gap_rel, empty=True)

        aligned = cct.align_records(baseline, test, "tick")
        keys = [k for k, _, _ in aligned]
        self.assertNotIn(gap_rel, keys,
                         "both-sides-empty tick has nothing to compare and must stay out of "
                         "the aligned set")

        # End-to-end: zero divergences overall, same as the plain no-gap case.
        with tempfile.TemporaryDirectory() as tmp:
            d = Path(tmp)
            b_path, t_path = d / "b.jsonl", d / "t.jsonl"
            write_jsonl(b_path, baseline)
            write_jsonl(t_path, test)
            with tempfile.NamedTemporaryFile("r", suffix=".json") as out:
                subprocess.run(
                    [sys.executable, str(SCRIPT), "--baseline", str(b_path),
                     "--test", str(t_path), "--align", "tick", "--json-out", out.name],
                    capture_output=True, text=True, check=True,
                )
                metrics = json.loads(Path(out.name).read_text())
        self.assertEqual(metrics["divergences_total"], 0)

    def test_one_sided_tick_outside_mutual_coverage_window_is_not_surfaced(self) -> None:
        """A tick that's one-sided only because the OTHER side's capture never
        ran that long (a trace-length/coverage-window mismatch) must NOT be
        surfaced -- re-validating this fix against the real dam_combat_guard6
        both-sides capture showed the two traces routinely cover very
        different absolute sim-tick spans, and without this bound the P1f fix
        drowns real mid-stream findings in coverage-gap noise. A one-sided
        tick INSIDE the mutually-observed range must still surface (this is
        the actual P1f bug fix, asserted below alongside the out-of-range
        tick to prove the bound doesn't just suppress everything)."""
        baseline = native_stream(1000, 30)          # covers rel 0..87
        test = native_stream(1000, 10)               # covers rel 0..27 only
        # Make one in-range tick (rel=12) one-sided (empty on test) and rely
        # on the many ticks beyond rel=27 (test's last tick) to exercise the
        # out-of-range case already.
        gap_rel = 12
        test = [
            record(r["move"]["global"], gap_rel, empty_roster=True)
            if r["move"]["global"] - 1000 == gap_rel else r
            for r in test
        ]

        aligned = cct.align_records(baseline, test, "tick")
        keys = {k for k, _, _ in aligned}
        self.assertIn(gap_rel, keys,
                      "in-range one-sided tick must still surface")
        out_of_range = {k for k in keys if k > 27}
        self.assertEqual(out_of_range, set(),
                         "ticks beyond test's own coverage window must not be "
                         "surfaced as one-sided divergences (trace-length "
                         "mismatch, not a mid-stream roster divergence)")


class DisjointLatticeRegression(unittest.TestCase):
    """C1 fix regression (2026-07-11 review): the actual real-capture failure
    mode. Each side samples a DIFFERENT sim-tick residue class (a genuine
    disjoint sampling lattice, e.g. native's fixed 3-stride vs ares/stock's
    variable ~2-stride on dam_combat_guard6) -- neither side ever produces a
    genuine empty-roster observation, every record either side DOES produce
    is full-roster, and at whatever ticks the two lattices happen to land on
    the same value the rosters are identical. On the real captures this
    pattern (289 one-sided ticks x 36 guards) was misread by the pre-fix
    comparator as 10404 whole-roster divergences; 0/289 were a genuine
    present-vs-empty observation on the other side.

    FAIL-ON-REVERT: pre-fix, ``align_records``'s tick branch synthesized an
    empty-roster pairing for EVERY one-sided tick regardless of whether the
    other side had ever observed it, so this construction reddens (nonzero
    divergences) against the pre-fix comparator. Reproduced via ``git
    stash`` in the task report.
    """

    @staticmethod
    def _lattice_stream(onset: int, ticks: list[int]) -> list[dict]:
        """A record stream sampling EXACTLY the given relative ticks (a
        disjoint-lattice fixture) -- always full-roster, never empty."""
        return [record(onset + rel, rel) for rel in ticks]

    def test_disjoint_lattices_zero_divergences_nonzero_excluded(self) -> None:
        onset = 5000
        a_ticks = list(range(0, 31, 3))   # 0,3,6,...,30  (native-like 3-stride)
        b_ticks = list(range(0, 31, 2))   # 0,2,4,...,30  (ares/stock-like 2-stride)
        co_sampled = sorted(set(a_ticks) & set(b_ticks))   # multiples of 6
        one_sided = sorted(set(a_ticks) ^ set(b_ticks))
        self.assertTrue(co_sampled, "fixture must share at least one sim-tick")
        self.assertTrue(one_sided, "fixture must have at least one lattice-mismatch tick")

        side_a = self._lattice_stream(onset, a_ticks)
        side_b = self._lattice_stream(onset, b_ticks)

        aligned, info = cct.align_tick(side_a, side_b)
        keys = {k for k, _, _ in aligned}
        self.assertEqual(keys, set(co_sampled),
                         "only co-sampled ticks may pair; every lattice-mismatch tick "
                         "must be excluded, not synthesized into a phantom divergence")
        self.assertEqual(info["synthetic_pairs"], 0,
                         "neither side ever observes an empty roster here -- there is "
                         "no genuine present-vs-empty class in this fixture, only "
                         "sampling-lattice gaps")
        excluded_total = (info["excluded_missing_record"]["baseline"]["count"]
                          + info["excluded_missing_record"]["test"]["count"])
        self.assertEqual(excluded_total, len(one_sided),
                         "every lattice-mismatch tick must be excluded AND counted "
                         "(charter rule 9 -- never silently dropped)")
        self.assertGreater(excluded_total, 0)

        divs: list[cct.Divergence] = []
        for k, brec, trec in aligned:
            cct.compare_guards(k, brec["combat_oracle"], trec["combat_oracle"],
                                {"health": 1.0, "position": 0.5}, divs)
        self.assertEqual(divs, [],
                         f"co-sampled ticks carry identical rosters and must show zero "
                         f"divergences: {[d.as_dict() for d in divs]}")

        # End-to-end CLI: zero divergences, nonzero excluded-missing-record count
        # surfaced in the JSON report (never silent per charter rule 9).
        with tempfile.TemporaryDirectory() as tmp:
            d = Path(tmp)
            b_path, t_path = d / "b.jsonl", d / "t.jsonl"
            write_jsonl(b_path, side_a)
            write_jsonl(t_path, side_b)
            with tempfile.NamedTemporaryFile("r", suffix=".json") as out:
                subprocess.run(
                    [sys.executable, str(SCRIPT), "--baseline", str(b_path),
                     "--test", str(t_path), "--align", "tick", "--json-out", out.name],
                    capture_output=True, text=True, check=True,
                )
                metrics = json.loads(Path(out.name).read_text())
        self.assertEqual(metrics["divergences_total"], 0)
        self.assertEqual(metrics["aligned_synthetic_pairs"], 0)
        cli_excluded_total = (metrics["excluded_missing_record"]["baseline"]["count"]
                              + metrics["excluded_missing_record"]["test"]["count"])
        self.assertGreater(cli_excluded_total, 0)


def write_jsonl(path: Path, records: list[dict]) -> None:
    with path.open("w", encoding="utf-8") as handle:
        for r in records:
            handle.write(json.dumps(r) + "\n")


if __name__ == "__main__":
    unittest.main()
