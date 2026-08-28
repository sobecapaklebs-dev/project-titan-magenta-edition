"""T3 -- unit tests for the emulator-free intro-stream parse digest tool.

Covers the two ROM-free pieces of tools/intro_parse_digest.py:
  - canonicalization/digest: stable under record key order and dict-key
    insertion order, but changes whenever an actual record value changes.
  - the [INTRO-DIGEST] stderr-line parser: ignores unrelated lines (the
    existing human-readable [INTRO-PARSE]/[INTRO-WALK] trace and any other
    stdout/stderr noise), and fails loudly (never silently) on a malformed
    or incomplete digest stream.

Nothing here is ROM-derived or invokes the native binary -- these are plain
synthetic-fixture unit tests, same convention as
tools/tests/test_compare_intro_trace.py from T2.

Run: python3 -m unittest tools.tests.test_intro_parse_digest
 or: python3 tools/tests/test_intro_parse_digest.py
"""
from __future__ import annotations

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from tools import intro_parse_digest as ipd  # noqa: E402


def spawn_record(i: int, index: int = 0, is_demo_playback: int = 0, n64_off: int = 0) -> dict:
    return {
        "i": i,
        "type": ipd.INTROTYPE_SPAWN,
        "n64_off": n64_off,
        "w": [ipd.INTROTYPE_SPAWN, index, is_demo_playback],
    }


def swirl_record(i: int, flags: int = 0, n64_off: int = 0) -> dict:
    return {
        "i": i,
        "type": ipd.INTROTYPE_SWIRL,
        "n64_off": n64_off,
        "w": [ipd.INTROTYPE_SWIRL, flags, 0, 0, 0, 0, 0, 0],
    }


def camera_record(i: int, n64_off: int = 0) -> dict:
    return {
        "i": i,
        "type": ipd.INTROTYPE_CAMERA,
        "n64_off": n64_off,
        "w": [ipd.INTROTYPE_CAMERA, 1, 2, 3, 4, 5, 6, 7, 8, 0],
    }


def watch_record(i: int, hours: int, minutes: int, n64_off: int = 0) -> dict:
    return {
        "i": i,
        "type": ipd.INTROTYPE_WATCH,
        "n64_off": n64_off,
        "w": [ipd.INTROTYPE_WATCH, hours, minutes],
    }


def end_record(i: int, n64_off: int = 0) -> dict:
    return {"i": i, "type": ipd.INTROTYPE_END, "n64_off": n64_off, "w": [ipd.INTROTYPE_END]}


def render_stderr(records: list[dict]) -> str:
    """Build a synthetic stderr stream: a couple of noise lines, one
    [INTRO-DIGEST] line per record (in the given order), then the END
    summary line -- mirroring exactly what the C side emits."""
    import json

    lines = [
        "[SETUP-PC] Converted 4 AI lists",
        "[INTRO-PARSE] rec=0 off=0 type=0 raw=00000000",
    ]
    for rec in records:
        lines.append("[INTRO-DIGEST] " + json.dumps(rec))
    lines.append("[INTRO-DIGEST-END] " + json.dumps({"count": len(records)}))
    return "\n".join(lines) + "\n"


class CanonicalizationTest(unittest.TestCase):
    def test_stable_under_record_list_order(self) -> None:
        """canonicalize_records sorts by ordinal 'i', so records passed in
        out of stream order still canonicalize identically."""
        in_order = [spawn_record(0), swirl_record(1), end_record(2)]
        shuffled = [end_record(2), spawn_record(0), swirl_record(1)]
        self.assertEqual(
            ipd.canonicalize_records(in_order),
            ipd.canonicalize_records(shuffled),
        )

    def test_stable_under_dict_key_order(self) -> None:
        """Two records with identical content but different dict key
        insertion order canonicalize to the same bytes (sort_keys)."""
        a = {"i": 0, "type": 3, "n64_off": 0, "w": [3, 1, 2]}
        b = {"w": [3, 1, 2], "n64_off": 0, "type": 3, "i": 0}
        self.assertEqual(ipd.canonicalize_records([a]), ipd.canonicalize_records([b]))

    def test_canonical_form_has_no_incidental_whitespace(self) -> None:
        """The canonical encoding uses compact separators -- no space after
        ':' or ','-- so byte-for-byte comparisons aren't sensitive to
        json.dumps default-vs-compact whitespace choices upstream."""
        canonical = ipd.canonicalize_records([spawn_record(0)])
        self.assertNotIn(", ", canonical)
        self.assertNotIn(": ", canonical)

    def test_digest_stable_across_key_order_and_list_order(self) -> None:
        records1 = [spawn_record(0), swirl_record(1, flags=2), end_record(2)]
        records2 = [
            {"w": [3, 2, 0, 0, 0, 0, 0, 0], "type": 3, "n64_off": 0, "i": 1},
            {"type": 9, "w": [9], "i": 2, "n64_off": 0},
            {"n64_off": 0, "i": 0, "type": 0, "w": [0, 0, 0]},
        ]
        self.assertEqual(ipd.digest_sha256(records1), ipd.digest_sha256(records2))

    def test_digest_changes_when_a_record_value_changes(self) -> None:
        baseline = [spawn_record(0, index=5), swirl_record(1, flags=2), end_record(2)]
        changed = [spawn_record(0, index=6), swirl_record(1, flags=2), end_record(2)]
        self.assertNotEqual(ipd.digest_sha256(baseline), ipd.digest_sha256(changed))

    def test_digest_changes_when_swirl_flags_change(self) -> None:
        baseline = [swirl_record(0, flags=2), end_record(1)]
        changed = [swirl_record(0, flags=6), end_record(1)]
        self.assertNotEqual(ipd.digest_sha256(baseline), ipd.digest_sha256(changed))

    def test_digest_is_deterministic_sha256_hexdigest(self) -> None:
        records = [spawn_record(0), end_record(1)]
        digest = ipd.digest_sha256(records)
        self.assertEqual(len(digest), 64)
        int(digest, 16)  # raises ValueError if not valid hex


class ParseDigestStreamTest(unittest.TestCase):
    def test_ignores_non_digest_lines(self) -> None:
        records = [spawn_record(0), swirl_record(1), end_record(2)]
        stderr_text = render_stderr(records)
        parsed = ipd.parse_digest_stream(stderr_text)
        self.assertEqual(len(parsed), 3)
        self.assertEqual([r["i"] for r in parsed], [0, 1, 2])

    def test_ignores_completely_unrelated_noise(self) -> None:
        stderr_text = (
            "SDL note: some driver warning\n"
            "[INTRO-WALK] off=12 type=3 ptr=0x1234\n"
            "random garbage that is not json at all {{{\n"
            "[INTRO-DIGEST] " + '{"i":0,"type":9,"n64_off":0,"w":[9]}' + "\n"
            "[INTRO-DIGEST-END] " + '{"count":1}' + "\n"
        )
        parsed = ipd.parse_digest_stream(stderr_text)
        self.assertEqual(len(parsed), 1)
        self.assertEqual(parsed[0]["type"], 9)

    def test_malformed_digest_json_raises(self) -> None:
        stderr_text = (
            "[INTRO-DIGEST] {not valid json}\n"
            "[INTRO-DIGEST-END] " + '{"count":1}' + "\n"
        )
        with self.assertRaises(ipd.DigestParseError):
            ipd.parse_digest_stream(stderr_text)

    def test_malformed_end_json_raises(self) -> None:
        stderr_text = (
            "[INTRO-DIGEST] " + '{"i":0,"type":9,"n64_off":0,"w":[9]}' + "\n"
            "[INTRO-DIGEST-END] {not valid json}\n"
        )
        with self.assertRaises(ipd.DigestParseError):
            ipd.parse_digest_stream(stderr_text)

    def test_missing_end_line_raises(self) -> None:
        stderr_text = "[INTRO-DIGEST] " + '{"i":0,"type":9,"n64_off":0,"w":[9]}' + "\n"
        with self.assertRaises(ipd.DigestParseError):
            ipd.parse_digest_stream(stderr_text)

    def test_end_count_mismatch_raises(self) -> None:
        stderr_text = render_stderr([spawn_record(0), end_record(1)]).replace(
            '{"count": 2}', '{"count": 2}'
        )
        # Corrupt the END count directly to guarantee a mismatch regardless
        # of json.dumps spacing.
        import json

        lines = stderr_text.splitlines()
        lines[-1] = "[INTRO-DIGEST-END] " + json.dumps({"count": 99})
        stderr_text = "\n".join(lines) + "\n"
        with self.assertRaises(ipd.DigestParseError):
            ipd.parse_digest_stream(stderr_text)

    def test_empty_stream_raises(self) -> None:
        with self.assertRaises(ipd.DigestParseError):
            ipd.parse_digest_stream("")


class CoverageRowTest(unittest.TestCase):
    def test_dam_shaped_stream_sanity_counts(self) -> None:
        """Sanity-check against the known Dam walker-visible stream: 6x
        CAMERA, 1x WATCH, 3x SPAWN, 11x SWIRL, item/ammo pairs, 1x CUFF."""
        records = []
        i = 0
        for _ in range(6):
            records.append(camera_record(i))
            i += 1
        records.append(watch_record(i, hours=1, minutes=30))
        i += 1
        for _ in range(3):
            records.append(spawn_record(i, index=i))
            i += 1
        for _ in range(11):
            records.append(swirl_record(i, flags=2 if i % 2 == 0 else 4))
            i += 1
        records.append({"i": i, "type": ipd.INTROTYPE_CUFF, "n64_off": 0, "w": [ipd.INTROTYPE_CUFF, 0]})
        i += 1
        records.append(end_record(i))

        row = ipd.build_coverage_row(33, records)
        self.assertEqual(row["stage"], 33)
        self.assertEqual(row["slug"], "dam")
        self.assertEqual(row["counts"]["CAMERA"], 6)
        self.assertEqual(row["counts"]["WATCH"], 1)
        self.assertEqual(row["counts"]["SPAWN"], 3)
        self.assertEqual(row["counts"]["SWIRL"], 11)
        self.assertEqual(row["counts"]["CUFF"], 1)
        self.assertEqual(row["watch"], (1, 30))
        self.assertIn("&2", row["swirl_flags"])
        self.assertIn("&4", row["swirl_flags"])
        self.assertEqual(row["spawn_total"], 3)
        self.assertEqual(row["spawn_eligible"], 3)  # all is_demo_playback=0

    def test_swirl_flag_bits_report_only_bits_present(self) -> None:
        records = [swirl_record(0, flags=1), end_record(1)]
        row = ipd.build_coverage_row(9, records)
        self.assertEqual(row["swirl_flags"], "&1")

    def test_swirl_flag_bits_none_when_all_zero(self) -> None:
        records = [swirl_record(0, flags=0), end_record(1)]
        row = ipd.build_coverage_row(9, records)
        self.assertEqual(row["swirl_flags"], "none")

    def test_credits_and_anim_reporting(self) -> None:
        records = [
            {"i": 0, "type": ipd.INTROTYPE_ANIM, "n64_off": 0, "w": [ipd.INTROTYPE_ANIM, 7]},
            {"i": 1, "type": ipd.INTROTYPE_CREDITS, "n64_off": 0, "w": [ipd.INTROTYPE_CREDITS, 0]},
            end_record(2),
        ]
        row = ipd.build_coverage_row(41, records)
        self.assertEqual(row["anim_indices"], [7])
        self.assertTrue(row["credits_present"])
        self.assertEqual(row["slug"], "cradle")


class StageSlugMapTest(unittest.TestCase):
    def test_default_stages_all_have_slugs(self) -> None:
        for stage_id in ipd.DEFAULT_STAGES:
            self.assertIn(stage_id, ipd.STAGE_SLUGS, f"missing slug for stage {stage_id}")

    def test_default_stages_count_is_20(self) -> None:
        self.assertEqual(len(ipd.DEFAULT_STAGES), 20)
        self.assertEqual(len(set(ipd.DEFAULT_STAGES)), 20)


if __name__ == "__main__":
    unittest.main()
