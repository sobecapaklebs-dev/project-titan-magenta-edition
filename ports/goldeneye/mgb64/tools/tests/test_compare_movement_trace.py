"""Movement-comparator tick-alignment lane (DAM_PARITY_DEEP_DIVE 2026-07-17 §3.1).

The bug: on combat/movement routes captured at speedframes 3, the native tracer
emits ONE movement record per rendered frame (``move.global`` advancing +3), while
the ares/stock tracer emits records per VI sample (several per advancing tick).
``--align move`` pairs records by INDEX from motion onset, so aligned index i maps
native sim-tick 3i against stock sim-tick i — a 3x time-scale skew.  Real casualty:
the sprint boost (engages after 3.0s at max speed) lands at aligned index 60 on the
native side but index ~180 on the stock side, manufacturing a "speed regression"
where both sides are tick-identical in real time (proven on the dam_combat_guard6
ares pair; Jul-5/Jul-16/HEAD binaries byte-identical).

The fix under test: ``--align move-global`` — dedupe each side to the LAST record
per ``move.global`` (end-of-tick state), rebase each side's global to its first
MOVING record (stock's counter includes the menu boot, native's starts at 0), and
key-pair on the rebased tick.  Same construction as the combat comparator's
``--align tick`` (FID-0062).

All fixtures are synthetic JSONL built in-test — nothing here is ROM-derived.

Run: python3 -m unittest tools.tests.test_compare_movement_trace
 or: python3 tools/tests/test_compare_movement_trace.py
"""
from __future__ import annotations

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

SCRIPT = Path(__file__).resolve().parent.parent / "compare_movement_trace.py"

SPEEDFRAMES = 3          # native records advance move.global by 3 (Dam combat route)
STOCK_MENU_TICKS = 1146  # stock's global counter includes the frontend menu boot
IDLE_TICKS = 39          # both sides idle this many ticks before the stick engages
MOVE_TICKS = 360         # 6 seconds of forward hold at 60Hz ticks

WALK_MAX = 1.08          # full walk speed (speedforwards * 1.08, boost 1.0)
BOOST_MAX = 1.35         # WALK_MAX * SPEED_RUN_MAX after 3s at max speed
BOOST_ONSET_TICKS = 180  # THREE_SECOND_TICKS
BOOST_STEP = 0.0108      # boost ramp per tick as seen in speed[0]


def speed_for(rel_tick: int) -> float:
    """Forward speed as a pure function of ticks since motion onset (both sides)."""
    if rel_tick < 0:
        return 0.0
    walk = min(WALK_MAX, (rel_tick + 1) * 0.054)  # accel to full walk in 20 ticks
    if rel_tick < BOOST_ONSET_TICKS:
        return walk
    return min(BOOST_MAX, WALK_MAX + (rel_tick - BOOST_ONSET_TICKS) * BOOST_STEP)


def record(frame: int, global_tick: int, rel_tick: int, moving: bool) -> dict:
    speed = speed_for(rel_tick) if moving else 0.0
    return {
        "f": frame,
        "p": 1,
        "pos": [float(max(rel_tick, 0)) * 10.0, 0.0, 0.0],
        "move": {
            "global": global_tick,
            "clock": 3,
            "dt": 0.0166,
            "max_t": 3,
            "speed": [speed, 0.0],
            "raw": [0.0, 80.0 if moving else 0.0],
            "boost": 1.0,
            "turn": 0.0,
            "pitch": 0.0,
            "head": [0.0, 0.0, 0.0],
            "prev": [float(max(rel_tick - 1, 0)) * 10.0, 0.0, 0.0],
        },
    }


def native_trace() -> list[dict]:
    """One record per rendered frame; move.global advances by SPEEDFRAMES."""
    records = []
    frame = 0
    tick = 0
    while tick < IDLE_TICKS:
        records.append(record(frame, tick, tick - IDLE_TICKS, moving=False))
        frame += 1
        tick += SPEEDFRAMES
    onset = tick
    while tick < onset + MOVE_TICKS:
        records.append(record(frame, tick, tick - onset, moving=True))
        frame += 1
        tick += SPEEDFRAMES
    return records

def stock_trace() -> list[dict]:
    """Per-tick records with a stale mid-tick duplicate preceding each final one.

    The duplicate carries the PREVIOUS tick's speed, so any pairing that does not
    keep the LAST record per move.global sees stale values.
    """
    records = []
    frame = 0
    for tick in range(STOCK_MENU_TICKS, STOCK_MENU_TICKS + IDLE_TICKS):
        records.append(record(frame, tick, -1, moving=False))
        frame += 1
    onset = STOCK_MENU_TICKS + IDLE_TICKS
    for tick in range(onset, onset + MOVE_TICKS):
        rel = tick - onset
        stale = record(frame, tick, max(rel - 1, 0), moving=True)  # mid-tick sample
        records.append(stale)
        final = record(frame, tick, rel, moving=True)
        records.append(final)
        frame += 1
    return records


def write_jsonl(path: Path, records: list[dict]) -> None:
    with path.open("w") as fh:
        for rec in records:
            fh.write(json.dumps(rec) + "\n")


def run_compare(baseline: Path, test: Path, align: str) -> subprocess.CompletedProcess:
    return subprocess.run(
        [
            sys.executable,
            str(SCRIPT),
            str(baseline),
            str(test),
            "--align",
            align,
            "--profile",
            "scalar-speed",
        ],
        capture_output=True,
        text=True,
    )


class MoveGlobalAlignmentTest(unittest.TestCase):
    def setUp(self) -> None:
        self._tmp = tempfile.TemporaryDirectory()
        tmp = Path(self._tmp.name)
        self.baseline = tmp / "stock.jsonl"
        self.test = tmp / "native.jsonl"
        write_jsonl(self.baseline, stock_trace())
        write_jsonl(self.test, native_trace())

    def tearDown(self) -> None:
        self._tmp.cleanup()

    def test_move_index_align_shows_the_skew(self) -> None:
        """Documents the defect: index pairing diverges on tick-identical sides.

        This proves the fixture discriminates — if this ever passes, the fixture
        no longer models the cadence mismatch and the lane is meaningless.
        """
        proc = run_compare(self.baseline, self.test, "move")
        self.assertEqual(proc.returncode, 1, proc.stdout + proc.stderr)
        self.assertIn("divergent", proc.stdout)

    def test_move_global_zero_divergence_on_tick_identical_sides(self) -> None:
        """Fail-on-revert guard: onset-rebased tick pairing sees zero divergences."""
        proc = run_compare(self.baseline, self.test, "move-global")
        self.assertEqual(
            proc.returncode,
            0,
            "move-global alignment must pass on tick-identical sides:\n"
            + proc.stdout
            + proc.stderr,
        )

    def test_move_global_keeps_last_record_per_tick(self) -> None:
        """Stock emits a stale mid-tick duplicate; last-per-global must win.

        Reverse the duplicate order (final first, stale last) and the compare must
        now FAIL — proving the mode reads the LAST record per global, not the first.
        """
        flipped = []
        records = stock_trace()
        index = 0
        while index < len(records):
            rec = records[index]
            nxt = records[index + 1] if index + 1 < len(records) else None
            if (
                nxt is not None
                and rec["move"]["global"] == nxt["move"]["global"]
            ):
                flipped.extend([nxt, rec])
                index += 2
            else:
                flipped.append(rec)
                index += 1
        flipped_path = Path(self._tmp.name) / "stock_flipped.jsonl"
        write_jsonl(flipped_path, flipped)
        proc = run_compare(flipped_path, self.test, "move-global")
        self.assertEqual(proc.returncode, 1, proc.stdout + proc.stderr)


if __name__ == "__main__":
    unittest.main()
