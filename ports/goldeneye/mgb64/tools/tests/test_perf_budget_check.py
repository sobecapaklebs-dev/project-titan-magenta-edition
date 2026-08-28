"""AUDIT-0019 regression lane -- pins the two-policy contract of
``tools/perf_budget_check.py``: a PORTABLE absolute-floor gate (machine
independent) versus opt-in relative REGRESSION detection gated on a matching
host fingerprint.

The defect: the checker treated any >15% delta from a single committed cold-host
baseline as a hard failure, so a valid build that cleared the 60 fps floor could
fail release validation purely for host/thermal variance. The fix makes baseline
deltas ADVISORY by default and only enforces them under ``--regression`` when
``--fingerprint`` matches the baseline's ``# fingerprint:`` line; a missing or
mismatched fingerprint SKIPS the relative check (never a false regression
verdict). The absolute 60 fps floor and data-presence remain hard failures in
every mode.

All fixtures are synthetic -- nothing here is ROM-derived.

Run: python3 -m unittest tools.tests.test_perf_budget_check
"""
from __future__ import annotations

import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CHECK = ROOT / "tools" / "perf_budget_check.py"

BASELINE = (
    "# test baseline\n"
    "# fingerprint: TEST-HOST-A\n"
    "level,default_ms,default_fps,xluoff_ms,speedup\n"
    "dam,10.00,100,10.0,1.0x\n"
)
# 12ms: under the 16.6ms floor but +20% over the 10ms baseline.
CENSUS_FAST = "level,default_ms,default_fps,xluoff_ms,speedup\ndam,12.00,83,12.0,1.0x\n"
# 20ms: over the 16.6ms absolute floor.
CENSUS_SLOW = "level,default_ms,default_fps,xluoff_ms,speedup\ndam,20.00,50,20.0,1.0x\n"
# NA: missing measurement.
CENSUS_NA = "level,default_ms,default_fps,xluoff_ms,speedup\ndam,NA,NA,NA,NA\n"


class PerfBudgetCheckContract(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        d = Path(self.tmp.name)
        self.base = d / "base.csv"
        self.fast = d / "fast.csv"
        self.slow = d / "slow.csv"
        self.na = d / "na.csv"
        self.base.write_text(BASELINE)
        self.fast.write_text(CENSUS_FAST)
        self.slow.write_text(CENSUS_SLOW)
        self.na.write_text(CENSUS_NA)

    def tearDown(self):
        self.tmp.cleanup()

    def run_check(self, *args):
        r = subprocess.run(
            [sys.executable, str(CHECK), *map(str, args)],
            capture_output=True, text=True,
        )
        return r.returncode, r.stdout + r.stderr

    # --- Portable default gate (machine independent) ---

    def test_portable_passes_despite_baseline_delta(self):
        """All stages under the floor pass even at +20% vs another host's baseline."""
        rc, out = self.run_check(self.fast, "--baseline", self.base)
        self.assertEqual(rc, 0, out)
        self.assertIn("ADVISORY", out)  # delta shown but not enforced

    def test_absolute_floor_hard_fails_portable(self):
        rc, _ = self.run_check(self.slow)
        self.assertEqual(rc, 1)

    def test_missing_measurement_hard_fails(self):
        rc, _ = self.run_check(self.na)
        self.assertEqual(rc, 1)

    def test_allow_missing_excludes_na(self):
        rc, _ = self.run_check(self.na, "--allow-missing", "dam")
        self.assertEqual(rc, 0)

    # --- Opt-in relative regression, fingerprint-gated ---

    def test_regression_enforced_when_fingerprint_matches(self):
        rc, out = self.run_check(self.fast, "--baseline", self.base,
                                 "--regression", "--fingerprint", "TEST-HOST-A")
        self.assertEqual(rc, 1, out)
        self.assertIn("REGRESSION", out)

    def test_regression_skipped_on_fingerprint_mismatch(self):
        rc, out = self.run_check(self.fast, "--baseline", self.base,
                                 "--regression", "--fingerprint", "WRONG-HOST")
        self.assertEqual(rc, 0, out)
        self.assertIn("SKIPPED", out)

    def test_regression_skipped_when_fingerprint_absent(self):
        rc, out = self.run_check(self.fast, "--baseline", self.base, "--regression")
        self.assertEqual(rc, 0, out)
        self.assertIn("SKIPPED", out)

    def test_absolute_floor_still_hard_fails_in_regression_mode(self):
        rc, _ = self.run_check(self.slow, "--baseline", self.base,
                               "--regression", "--fingerprint", "TEST-HOST-A")
        self.assertEqual(rc, 1)

    # --- Strict target behavior (unchanged contract) ---

    def test_strict_makes_target_miss_fail(self):
        # fast.csv (12ms) is above the 8.3ms target but under the floor.
        rc_default, _ = self.run_check(self.fast)
        rc_strict, _ = self.run_check(self.fast, "--strict")
        self.assertEqual(rc_default, 0)
        self.assertEqual(rc_strict, 1)

    # --- Fingerprint loader ---

    def test_real_baseline_has_fingerprint(self):
        sys.path.insert(0, str(ROOT))
        from tools import perf_budget_check as c  # noqa: E402
        fp = c.load_fingerprint(str(ROOT / "baselines" / "perf_census_baseline.csv"))
        self.assertTrue(fp, "committed baseline must carry a '# fingerprint:' line")


if __name__ == "__main__":
    unittest.main()
