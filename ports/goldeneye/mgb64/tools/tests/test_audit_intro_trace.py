"""T4 -- unit tests for the H17 audit invariant in tools/audit_intro_trace.py.

H17 (theory register): at the first
mode-3 (swirl) trace record, the applied_view-derived look/facing vector
must not still be the degenerate (1,0,0) seed. src/platform/port_trace.c
emits top-level "cam_pos"/"cam_target" arrays; during CAMERAMODE_SWIRL these
come from the live (non-frozen) branch where cam_target = cam_pos +
applied_view (port_trace.c:6495-6522), so applied_view is recoverable as
cam_target - cam_pos without any new traced field.

Nothing here touches a ROM, ares, or the native binary -- synthetic-trace
unit tests, same convention as tools/tests/test_intro_parse_digest.py and
tools/tests/test_make_unlocked_eeprom.py.

Run: python3 -m unittest tools.tests.test_audit_intro_trace
 or: python3 tools/tests/test_audit_intro_trace.py
"""
from __future__ import annotations

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from tools import audit_intro_trace as ait  # noqa: E402

TOOL = str(Path(__file__).resolve().parents[1] / "audit_intro_trace.py")


def record(cam: int, cam_pos: tuple, cam_target: tuple, frame: int = 1, p: int = 1, p_unk: int = 1) -> dict:
    return {"f": frame, "p": p, "p_unk": p_unk, "cam": cam, "cam_pos": list(cam_pos), "cam_target": list(cam_target)}


class AppliedViewFromRecordTest(unittest.TestCase):
    def test_subtracts_cam_pos_from_cam_target(self) -> None:
        rec = record(3, (10.0, 20.0, 30.0), (10.0, 20.0, 35.0))
        self.assertEqual(ait.applied_view_from_record(rec), (0.0, 0.0, 5.0))

    def test_returns_none_when_cam_pos_missing(self) -> None:
        rec = {"f": 1, "cam": 3, "cam_target": [1.0, 0.0, 0.0]}
        self.assertIsNone(ait.applied_view_from_record(rec))

    def test_returns_none_when_cam_target_malformed(self) -> None:
        rec = {"f": 1, "cam": 3, "cam_pos": [0.0, 0.0, 0.0], "cam_target": "nope"}
        self.assertIsNone(ait.applied_view_from_record(rec))


class IsDegenerateAppliedViewTest(unittest.TestCase):
    def test_exact_degenerate_seed_is_degenerate(self) -> None:
        self.assertTrue(ait.is_degenerate_applied_view((1.0, 0.0, 0.0)))

    def test_within_float_formatting_tolerance_is_degenerate(self) -> None:
        self.assertTrue(ait.is_degenerate_applied_view((1.00001, 0.00001, -0.00001)))

    def test_a_real_look_direction_is_not_degenerate(self) -> None:
        self.assertFalse(ait.is_degenerate_applied_view((0.0, 0.0, 1.0)))
        self.assertFalse(ait.is_degenerate_applied_view((0.7071, 0.0, 0.7071)))

    def test_close_to_but_not_within_tolerance_is_not_degenerate(self) -> None:
        self.assertFalse(ait.is_degenerate_applied_view((1.01, 0.0, 0.0)))


class FirstSwirlAppliedViewTest(unittest.TestCase):
    def test_finds_first_mode3_record_and_ignores_earlier_modes(self) -> None:
        records = [
            record(1, (0, 0, 0), (1, 0, 0), frame=10),   # INTRO -- not swirl
            record(2, (0, 0, 0), (1, 0, 0), frame=20),   # FADESWIRL -- not swirl
            record(3, (5.0, 0.0, 0.0), (5.0, 0.0, 1.0), frame=30),  # first SWIRL
            record(3, (6.0, 0.0, 0.0), (6.0, 0.0, 1.0), frame=31),  # later swirl, ignored
        ]
        frame, applied_view = ait.first_swirl_applied_view(records)
        self.assertEqual(frame, 30)
        self.assertEqual(applied_view, (0.0, 0.0, 1.0))

    def test_no_swirl_record_returns_none_none(self) -> None:
        records = [record(1, (0, 0, 0), (1, 0, 0)), record(2, (0, 0, 0), (1, 0, 0))]
        frame, applied_view = ait.first_swirl_applied_view(records)
        self.assertIsNone(frame)
        self.assertIsNone(applied_view)

    def test_swirl_record_missing_vectors_returns_frame_with_none_view(self) -> None:
        records = [{"f": 5, "cam": 3}]
        frame, applied_view = ait.first_swirl_applied_view(records)
        self.assertEqual(frame, 5)
        self.assertIsNone(applied_view)

    def test_skips_ineligible_swirl_record_when_require_player_and_frozen(self) -> None:
        # Same eligibility filtering as is_active(): a cam==3 record with
        # p=0/p_unk=0 must be skipped, even though it comes first.
        records = [
            record(3, (0.0, 0.0, 0.0), (1.0, 0.0, 0.0), frame=1, p=0, p_unk=0),  # ineligible, degenerate
            record(3, (5.0, 0.0, 0.0), (5.0, 0.0, 1.0), frame=2),  # eligible, healthy
        ]
        frame, applied_view = ait.first_swirl_applied_view(
            records, require_player=True, require_frozen=True
        )
        self.assertEqual(frame, 2)
        self.assertEqual(applied_view, (0.0, 0.0, 1.0))

    def test_uses_first_eligible_record_even_if_earlier_ineligible_is_healthy(self) -> None:
        records = [
            record(3, (5.0, 0.0, 0.0), (5.0, 0.0, 1.0), frame=1, p=0, p_unk=0),  # ineligible, healthy
            record(3, (0.0, 0.0, 0.0), (1.0, 0.0, 0.0), frame=2),  # eligible, degenerate
        ]
        frame, applied_view = ait.first_swirl_applied_view(
            records, require_player=True, require_frozen=True
        )
        self.assertEqual(frame, 2)
        self.assertEqual(applied_view, (1.0, 0.0, 0.0))

    def test_without_eligibility_flags_first_raw_swirl_record_is_used(self) -> None:
        # Default behavior (no require_player/require_frozen) is unchanged:
        # the first raw cam==3 record wins, regardless of p/p_unk.
        records = [
            record(3, (0.0, 0.0, 0.0), (1.0, 0.0, 0.0), frame=1, p=0, p_unk=0),
            record(3, (5.0, 0.0, 0.0), (5.0, 0.0, 1.0), frame=2),
        ]
        frame, applied_view = ait.first_swirl_applied_view(records)
        self.assertEqual(frame, 1)
        self.assertEqual(applied_view, (1.0, 0.0, 0.0))


def write_trace(records: list[dict]) -> str:
    tmp = tempfile.NamedTemporaryFile(mode="w", suffix=".jsonl", delete=False)
    for rec in records:
        tmp.write(json.dumps(rec) + "\n")
    tmp.close()
    return tmp.name


class RequireH17CliTest(unittest.TestCase):
    def _run(self, records: list[dict], extra_args: list[str] | None = None) -> subprocess.CompletedProcess:
        path = write_trace(records)
        args = [sys.executable, TOOL, "--require-h17-swirl-facing", "--camera-modes", "intro,fadeswirl,swirl"]
        if extra_args:
            args += extra_args
        args.append(path)
        return subprocess.run(args, capture_output=True, text=True, check=False)

    def test_passes_when_first_swirl_tick_has_a_real_look_direction(self) -> None:
        records = [
            record(1, (0, 0, 0), (1, 0, 0), frame=1),
            record(3, (5.0, 0.0, 0.0), (5.0, 0.0, 1.0), frame=2),
        ]
        result = self._run(records)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("PASS", result.stdout)

    def test_fails_when_first_swirl_tick_is_still_degenerate_seed(self) -> None:
        records = [
            record(1, (0, 0, 0), (1, 0, 0), frame=1),
            record(3, (0.0, 0.0, 0.0), (1.0, 0.0, 0.0), frame=2),
        ]
        result = self._run(records)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("H17", result.stderr)

    def test_fails_when_no_swirl_record_present(self) -> None:
        records = [record(1, (0, 0, 0), (1, 0, 0), frame=1)]
        result = self._run(records)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("no swirl-mode", result.stderr)

    def test_json_out_reports_h17_frame_and_applied_view(self) -> None:
        records = [
            record(1, (0, 0, 0), (1, 0, 0), frame=1),
            record(3, (5.0, 0.0, 0.0), (5.0, 0.0, 1.0), frame=2),
        ]
        path = write_trace(records)
        with tempfile.NamedTemporaryFile(suffix=".json", delete=False) as out_tmp:
            out_path = out_tmp.name
        result = subprocess.run(
            [
                sys.executable, TOOL,
                "--require-h17-swirl-facing",
                "--camera-modes", "intro,fadeswirl,swirl",
                "--json-out", out_path,
                path,
            ],
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        metrics = json.loads(Path(out_path).read_text(encoding="utf-8"))
        self.assertEqual(metrics["h17"]["swirl_frame"], 2)
        self.assertEqual(metrics["h17"]["applied_view"], [0.0, 0.0, 1.0])

    def test_passes_when_ineligible_earlier_swirl_record_is_degenerate_but_first_eligible_is_healthy(self) -> None:
        # Routes that enable H17 also set native_intro_require_player /
        # native_intro_require_frozen: true, so the H17 lookup must apply
        # the same require_player/require_frozen filtering as every other
        # counted metric -- an ineligible (p=0/p_unk=0) cam==3 record with a
        # degenerate applied_view must be skipped in favor of the first
        # eligible one, even though it appears earlier in the trace.
        records = [
            record(3, (0.0, 0.0, 0.0), (1.0, 0.0, 0.0), frame=1, p=0, p_unk=0),  # ineligible, degenerate
            record(3, (5.0, 0.0, 0.0), (5.0, 0.0, 1.0), frame=2),  # eligible, healthy
        ]
        result = self._run(records, extra_args=["--require-player", "--require-frozen"])
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("PASS", result.stdout)

    def test_fails_when_first_eligible_swirl_record_is_degenerate_even_if_earlier_ineligible_is_healthy(self) -> None:
        # Inverse: the first ELIGIBLE swirl record is the one that must be
        # checked -- a degenerate applied_view there fails H17 even though an
        # earlier ineligible record had a healthy (non-degenerate) view.
        records = [
            record(3, (5.0, 0.0, 0.0), (5.0, 0.0, 1.0), frame=1, p=0, p_unk=0),  # ineligible, healthy
            record(3, (0.0, 0.0, 0.0), (1.0, 0.0, 0.0), frame=2),  # eligible, degenerate
        ]
        result = self._run(records, extra_args=["--require-player", "--require-frozen"])
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("H17", result.stderr)

    def test_not_required_by_default(self) -> None:
        """Without --require-h17-swirl-facing, a degenerate swirl tick does
        not fail the audit (no accidental behavior change for existing
        routes that don't opt in)."""
        records = [record(3, (0.0, 0.0, 0.0), (1.0, 0.0, 0.0), frame=1)]
        path = write_trace(records)
        result = subprocess.run(
            [sys.executable, TOOL, "--camera-modes", "intro,fadeswirl,swirl", path],
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
