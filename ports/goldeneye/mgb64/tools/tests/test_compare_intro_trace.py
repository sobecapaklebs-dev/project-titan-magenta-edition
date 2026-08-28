"""T2 (D27) -- pins the oracle-comparator hardening: full-window evaluation (no
early-return divergence cap), per-mode alignment (no cross-mode-boundary ghosts),
mode-duration assertions, and the waiver mechanism.

All fixtures are synthetic JSONL built in-test -- nothing here is ROM-derived, so
nothing is committed by running the suite. Tests that exercise the CLI end-to-end
invoke `tools/compare_intro_trace.py` as a subprocess so argparse wiring, JSON
emission, and exit codes are covered for real, not just the internal helpers.

Run: python3 -m unittest tools.tests.test_compare_intro_trace
 or: python3 tools/tests/test_compare_intro_trace.py
"""
from __future__ import annotations

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from tools import compare_intro_trace as cit  # noqa: E402

SCRIPT = Path(__file__).resolve().parent.parent / "compare_intro_trace.py"


def write_jsonl(path: Path, records: list[dict]) -> None:
    with path.open("w", encoding="utf-8") as handle:
        for record in records:
            handle.write(json.dumps(record) + "\n")


def run_cli(args: list[str]) -> subprocess.CompletedProcess:
    return subprocess.run(
        [sys.executable, str(SCRIPT), *args],
        capture_output=True,
        text=True,
    )


def scalar_record(cam: int, theta: float, floor: float = 0.0, stan_h: float = 0.0) -> dict:
    return {"p": 1, "cam": cam, "theta": theta, "floor": floor, "stan_h": stan_h}


def swirl_record(cam_delta_y: float = 0.0, cam_pos_x: float = 0.0) -> dict:
    return {
        "p": 1,
        "cam": 3,
        "cam_pos": [cam_pos_x, 0.0, 0.0],
        "cam_target": [0.0, 0.0, 0.0],
        "cam_up": [0.0, 1.0, 0.0],
        "cam_floor": [0.0, 0.0, 0.0],
        "cam_delta": [0.0, cam_delta_y, 0.0],
        "facing": [0.0, 0.0, 1.0],
    }


class FullEvaluationTest(unittest.TestCase):
    """1. No early-return: 30 divergent records, --max-divergences 5 must still
    report a total of 30, with printing capped at 5."""

    def test_full_window_reports_true_total_with_capped_printing(self):
        baseline = [scalar_record(cam=1, theta=0.0) for _ in range(30)]
        test = [scalar_record(cam=1, theta=10.0) for _ in range(30)]
        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = Path(tmp)
            baseline_path = tmp_path / "baseline.jsonl"
            test_path = tmp_path / "test.jsonl"
            json_out = tmp_path / "out.json"
            write_jsonl(baseline_path, baseline)
            write_jsonl(test_path, test)

            result = run_cli(
                [
                    "--align", "active-index",
                    "--profile", "scalar",
                    "--camera-modes", "intro",
                    "--max-divergences", "5",
                    "--json-out", str(json_out),
                    str(baseline_path), str(test_path),
                ]
            )

            self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
            printed_lines = [
                line for line in result.stdout.splitlines() if line.startswith("  key")
            ]
            self.assertEqual(len(printed_lines), 5, result.stdout)

            metrics = json.loads(json_out.read_text())
            self.assertEqual(metrics["divergence_count"], 30)
            self.assertEqual(metrics["verdict"], "fail")


class PerModeAlignmentTest(unittest.TestCase):
    """2. Per-mode alignment kills boundary ghosts: mode-1 lengths differ by 3
    records but per-mode content is identical -> 0 divergences under per-mode
    alignment, but active-index alignment (global index across the mode
    boundary) fabricates divergences."""

    def test_per_mode_alignment_eliminates_boundary_ghosts(self):
        modes = {1, 2, 3}
        baseline = (
            [{"p": 1, "cam": 1, "cam_pos": [1.0, 1.0, 1.0]} for _ in range(10)]
            + [{"p": 1, "cam": 2, "cam_pos": [2.0, 2.0, 2.0]} for _ in range(5)]
            + [{"p": 1, "cam": 3, "cam_pos": [3.0, 3.0, 3.0]} for _ in range(5)]
        )
        test = (
            [{"p": 1, "cam": 1, "cam_pos": [1.0, 1.0, 1.0]} for _ in range(13)]
            + [{"p": 1, "cam": 2, "cam_pos": [2.0, 2.0, 2.0]} for _ in range(5)]
            + [{"p": 1, "cam": 3, "cam_pos": [3.0, 3.0, 3.0]} for _ in range(5)]
        )
        specs = [("cam_pos", "vector")]

        per_mode_pairs = cit.align_per_mode(baseline, test, modes, None)
        per_mode_divergences, _ = cit.compare_pairs(per_mode_pairs, specs, 0.05, 0.005, 0.001, 0.02)
        self.assertEqual(per_mode_divergences, [], [d.message for d in per_mode_divergences])

        index_pairs = cit.align_by_index(baseline, test, 1, 1, None)
        index_divergences, _ = cit.compare_pairs(index_pairs, specs, 0.05, 0.005, 0.001, 0.02)
        self.assertGreater(len(index_divergences), 0)

    def test_per_mode_duplicate_timer_uses_settled_last_record(self):
        """ares emits four controller-poll records per VI. The first can
        precede the actor tick while the last carries the settled state for
        the same authored intro timer; align against that completed state."""
        stale = swirl_record()
        stale["intro"] = {"timer": 24.0, "bond_anim": {"frame": 10.0}}
        settled = swirl_record()
        settled["intro"] = {"timer": 24.0, "bond_anim": {"frame": 10.25}}
        native = swirl_record()
        native["intro"] = {"timer": 24.0, "bond_anim": {"frame": 10.25}}

        pairs = cit.align_per_mode([stale, settled], [native], {3}, None)

        self.assertEqual(len(pairs), 1)
        self.assertEqual(pairs[0][1]["intro"]["bond_anim"]["frame"], 10.25)
        divergences, _ = cit.compare_pairs(
            pairs,
            [("intro.bond_anim.frame", "anim")],
            0.05,
            0.005,
            0.001,
            0.03,
        )
        self.assertEqual(divergences, [])


class ModeDurationAssertionTest(unittest.TestCase):
    """3. Duration assertion: native (test) mode count outside expected+/-tol
    fails; within tolerance passes."""

    def test_duration_within_tolerance_passes(self):
        baseline = [scalar_record(cam=1, theta=0.0) for _ in range(5)]
        test = [scalar_record(cam=1, theta=0.0) for _ in range(5)]
        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = Path(tmp)
            baseline_path = tmp_path / "baseline.jsonl"
            test_path = tmp_path / "test.jsonl"
            write_jsonl(baseline_path, baseline)
            write_jsonl(test_path, test)

            result = run_cli(
                [
                    "--align", "active-index",
                    "--profile", "scalar",
                    "--camera-modes", "intro",
                    "--expect-mode-durations", "1:5:0",
                    str(baseline_path), str(test_path),
                ]
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_duration_outside_tolerance_fails(self):
        baseline = [scalar_record(cam=1, theta=0.0) for _ in range(5)]
        test = [scalar_record(cam=1, theta=0.0) for _ in range(5)]
        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = Path(tmp)
            baseline_path = tmp_path / "baseline.jsonl"
            test_path = tmp_path / "test.jsonl"
            json_out = tmp_path / "out.json"
            write_jsonl(baseline_path, baseline)
            write_jsonl(test_path, test)

            result = run_cli(
                [
                    "--align", "active-index",
                    "--profile", "scalar",
                    "--camera-modes", "intro",
                    "--expect-mode-durations", "1:10:0",
                    "--json-out", str(json_out),
                    str(baseline_path), str(test_path),
                ]
            )
            self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
            metrics = json.loads(json_out.read_text())
            self.assertEqual(metrics["verdict"], "fail")
            self.assertTrue(any("mode 1" in d for d in metrics["divergences"]))


class WaiverTest(unittest.TestCase):
    """4. Waivers: a field divergence matching a field:<name>:mode<N> scope is
    reported WAIVED with its ledger ID and the run exits success; an unmatched
    divergence alongside it still fails."""

    def test_waiver_absorbs_matching_divergence(self):
        baseline = [swirl_record(cam_delta_y=0.0)]
        test = [swirl_record(cam_delta_y=100.0)]
        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = Path(tmp)
            baseline_path = tmp_path / "baseline.jsonl"
            test_path = tmp_path / "test.jsonl"
            json_out = tmp_path / "out.json"
            write_jsonl(baseline_path, baseline)
            write_jsonl(test_path, test)

            result = run_cli(
                [
                    "--align", "active-index",
                    "--profile", "path",
                    "--camera-modes", "swirl",
                    "--waivers", json.dumps({"field:cam_delta[1]:mode3": "D31"}),
                    "--json-out", str(json_out),
                    str(baseline_path), str(test_path),
                ]
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("WAIVED (D31)", result.stdout)

            metrics = json.loads(json_out.read_text())
            self.assertEqual(metrics["verdict"], "pass")
            self.assertEqual(len(metrics["waived"]), 1)
            self.assertEqual(metrics["waived"][0]["ledger_id"], "D31")
            self.assertGreaterEqual(metrics["waived"][0]["max_delta"], 99.9)

    def test_unwaived_divergence_alongside_a_waived_one_still_fails(self):
        baseline = [swirl_record(cam_delta_y=0.0, cam_pos_x=0.0)]
        test = [swirl_record(cam_delta_y=100.0, cam_pos_x=5.0)]
        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = Path(tmp)
            baseline_path = tmp_path / "baseline.jsonl"
            test_path = tmp_path / "test.jsonl"
            json_out = tmp_path / "out.json"
            write_jsonl(baseline_path, baseline)
            write_jsonl(test_path, test)

            result = run_cli(
                [
                    "--align", "active-index",
                    "--profile", "path",
                    "--camera-modes", "swirl",
                    "--waivers", json.dumps({"field:cam_delta[1]:mode3": "D31"}),
                    "--json-out", str(json_out),
                    str(baseline_path), str(test_path),
                ]
            )
            self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
            self.assertIn("WAIVED (D31)", result.stdout)

            metrics = json.loads(json_out.read_text())
            self.assertEqual(metrics["verdict"], "fail")
            self.assertEqual(len(metrics["waived"]), 1)
            self.assertEqual(metrics["unwaived_divergence_count"], 1)


class VerdictJsonTest(unittest.TestCase):
    """5. Verdict JSON: --json-out always contains verdict, per-mode aligned
    counts, and the waived list."""

    def test_json_out_contains_verdict_per_mode_counts_and_waived_list(self):
        baseline = (
            [scalar_record(cam=1, theta=0.0) for _ in range(4)]
            + [scalar_record(cam=3, theta=0.0) for _ in range(2)]
        )
        test = (
            [scalar_record(cam=1, theta=0.0) for _ in range(4)]
            + [scalar_record(cam=3, theta=0.0) for _ in range(2)]
        )
        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = Path(tmp)
            baseline_path = tmp_path / "baseline.jsonl"
            test_path = tmp_path / "test.jsonl"
            json_out = tmp_path / "out.json"
            write_jsonl(baseline_path, baseline)
            write_jsonl(test_path, test)

            result = run_cli(
                [
                    "--align", "per-mode",
                    "--profile", "scalar",
                    "--camera-modes", "intro,swirl",
                    "--json-out", str(json_out),
                    str(baseline_path), str(test_path),
                ]
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

            metrics = json.loads(json_out.read_text())
            self.assertEqual(metrics["verdict"], "pass")
            self.assertEqual(metrics["waived"], [])
            self.assertEqual(metrics["per_mode_aligned_counts"].get("1"), 4)
            self.assertEqual(metrics["per_mode_aligned_counts"].get("3"), 2)


def bond_swirl_record(seg: int, timer: float, action: int, frame: float = 0.0) -> dict:
    """A mode-3 swirl record carrying the bond-anim family.

    Phase 2 (action 1) = the 48f idle loop; phase 3 (action 3) = the 150f
    weapon-draw pose. Camera/path fields are identical on both sides so only
    the bond family can diverge. Phase-3 `frame` is animation-age-relative so
    tests exercise the event-relative frame pairing used for real captures.
    """
    record = swirl_record()
    phase3 = action == 3
    record["intro"] = {
        "timer": timer,
        "setup": {"swirl": {"current": {"index": seg}}},
        "bond_action": action,
        "bond_anim": {
            "valid": 1,
            "frames": 150 if phase3 else 48,
            "hash": "0x79F92FB064997857" if phase3 else "0x06028BC2EF592635",
            "entry_offset": 31420 if phase3 else 33144,
            "bits_offset": 31444 if phase3 else 33168,
            "frame": frame,
            "end": 96.0 if phase3 else 47.0,
            "speed": 0.5 if phase3 else 0.25,
            "abs_speed": 0.5 if phase3 else 0.25,
            "looping": 0 if phase3 else 1,
            "gunhand": 0,
        },
    }
    return record


def bond_trace(onset_timer: float, revert_timer: float | None = None) -> list[dict]:
    """Mode-3 segment, timers 1..260: phase 2 until onset, phase 3 after —
    optionally reverting to phase 2 at revert_timer (the swirl-end regression)."""
    records = []
    for t in range(1, 261):
        action = 3 if t >= onset_timer else 1
        if revert_timer is not None and t >= revert_timer:
            action = 1
        frame = min((t - onset_timer + 1) * 0.5, 96.0) if action == 3 else 0.0
        records.append(bond_swirl_record(4, float(t), action, frame))
    return records


class BondIdleOnsetToleranceTest(unittest.TestCase):
    def _pairs(self, baseline_onset: int, test_onset: int):
        baseline = [
            bond_swirl_record(1, float(t), 1 if t >= baseline_onset else 23)
            for t in range(90, 111)
        ]
        test = [
            bond_swirl_record(1, float(t), 1 if t >= test_onset else 23)
            for t in range(90, 111)
        ]
        return cit.align_per_mode(baseline, test, {3}, None)

    def test_three_tick_retail_batch_boundary_is_absorbed(self):
        absorbed, metrics, divergences = cit.apply_bond_idle_onset_alignment(
            self._pairs(baseline_onset=102, test_onset=99), 3.0
        )
        self.assertEqual(divergences, [])
        self.assertEqual(metrics["delta"], 3.0)
        self.assertEqual(len(absorbed), 3)

    def test_idle_boundary_beyond_one_retail_batch_fails(self):
        _absorbed, metrics, divergences = cit.apply_bond_idle_onset_alignment(
            self._pairs(baseline_onset=105, test_onset=99), 3.0
        )
        self.assertEqual(metrics["delta"], 6.0)
        self.assertEqual(len(divergences), 1)
        self.assertEqual(divergences[0].field, "intro.bond_anim.idle_onset")


class BondAnimOnsetToleranceTest(unittest.TestCase):
    """DAM_PARITY_DEEP_DIVE 2026-07-17 §3.3: retail fires phase 3 from an
    RNG-jittered AI sleep-wake boundary while native (D43) fires it at a fixed
    swirl timer, so the phase-3 ONSET varies between captures while the
    animation itself matches. --bond-anim-onset-tolerance replaces timer-based
    Bond comparison from the earlier onset onward, gates the onset delta, then
    compares phase 3 by animation age. Trailing phase mismatches (e.g. native's
    swirl-end revert to ACT_STAND) stay real divergences."""

    def _run(self, baseline: list[dict], test: list[dict], extra: list[str]):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        tmp_path = Path(tmp.name)
        baseline_path = tmp_path / "baseline.jsonl"
        test_path = tmp_path / "test.jsonl"
        json_out = tmp_path / "out.json"
        write_jsonl(baseline_path, baseline)
        write_jsonl(test_path, test)
        result = run_cli(
            [
                "--align", "per-mode",
                "--profile", "path",
                "--camera-modes", "swirl",
                "--compare-bond-anim",
                "--json-out", str(json_out),
                *extra,
                str(baseline_path),
                str(test_path),
            ]
        )
        metrics = json.loads(json_out.read_text()) if json_out.exists() else None
        return result, metrics

    def test_onset_within_tolerance_matches(self):
        result, metrics = self._run(
            bond_trace(onset_timer=57), bond_trace(onset_timer=41),
            ["--bond-anim-onset-tolerance", "20"],
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        onset = metrics.get("bond_anim_onset")
        self.assertIsNotNone(onset, metrics)
        self.assertAlmostEqual(onset["delta"], 16.0, places=3)
        self.assertEqual(onset["phase3_frame_alignment"]["aligned"], 192)

    def test_onset_beyond_tolerance_fails(self):
        result, _ = self._run(
            bond_trace(onset_timer=57), bond_trace(onset_timer=41),
            ["--bond-anim-onset-tolerance", "10"],
        )
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("phase-3 onset delta", result.stdout)

    def test_feature_off_preserves_old_behavior(self):
        result, _ = self._run(
            bond_trace(onset_timer=57), bond_trace(onset_timer=41), [],
        )
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)

    def test_trailing_revert_still_fails(self):
        result, _ = self._run(
            bond_trace(onset_timer=41),
            bond_trace(onset_timer=41, revert_timer=80),
            ["--bond-anim-onset-tolerance", "20"],
        )
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)

    def test_pre_onset_idle_frame_shift_still_fails(self):
        baseline = bond_trace(onset_timer=57)
        test = bond_trace(onset_timer=41)
        test[10]["intro"]["bond_anim"]["frame"] = 0.25
        result, _ = self._run(
            baseline, test, ["--bond-anim-onset-tolerance", "20"]
        )
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("intro.bond_anim.frame", result.stdout)

    def test_post_onset_metadata_regression_still_fails(self):
        baseline = bond_trace(onset_timer=57)
        test = bond_trace(onset_timer=41)
        # Animation frame 10 is compared event-relatively despite its timer
        # being different on each side.
        test[59]["intro"]["bond_anim"]["speed"] = 0.75
        result, _ = self._run(
            baseline, test, ["--bond-anim-onset-tolerance", "20"]
        )
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("intro.bond_anim.speed", result.stdout)


if __name__ == "__main__":
    unittest.main()
