"""T4 -- unit tests for tools/make_unlocked_eeprom.py.

The GoldenEye EEPROM save format (src/game/file.c, file2.c, crc.c) is a
512-byte (4Kbit) image: a 32-byte "smallSave" validity header followed by
five 96-byte save_data blocks. Every checksum is a from-scratch CRC
(fileGenerateCRC / randomGetNextFrom, src/game/crc.c + src/random.c) -- there
is no simple additive checksum to sanity-check against, so per the task
brief we mirror the algorithm TWICE: once in tools/make_unlocked_eeprom.py
(the generator) and once independently, right here, in this test file. If a
transcription error creeps into either copy, the two independent
implementations disagree and the test fails.

Nothing here touches a ROM, ares, or the native binary -- plain synthetic
unit tests, same convention as tools/tests/test_intro_parse_digest.py.

Run: python3 -m unittest tools.tests.test_make_unlocked_eeprom
 or: python3 tools/tests/test_make_unlocked_eeprom.py
"""
from __future__ import annotations

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from tools import make_unlocked_eeprom as eeprom  # noqa: E402


# --- Independent reference re-implementation ---------------------------------
#
# Transcribed directly from src/random.c's randomGetNextFrom and
# src/game/crc.c's fileGenerateCRC / src/game/file2.c's
# fileSetDifficultyStageTime, but written from scratch a second time (not
# imported from tools/make_unlocked_eeprom.py) so the test can catch a bug
# shared by both if one were copy-pasted from the other.

MASK64 = (1 << 64) - 1
MASK32 = (1 << 32) - 1


def ref_random_get_next_from(state: int) -> int:
    state &= MASK64
    t = ((state << 63) & MASK64) >> 31
    t |= ((state << 31) & MASK64) >> 32
    t ^= ((state << 44) & MASK64) >> 32
    new_state = (t ^ ((t >> 20) & 0xFFF)) & MASK64
    return new_state


def ref_generate_crc(data: bytes) -> tuple[int, int]:
    polynormal = 0x8F809F473108B3C1
    checksum1 = 0
    checksum2 = 0
    shift = 0
    for byte in data:
        polynormal = (polynormal + (byte << (shift & 0xF))) & MASK64
        polynormal = ref_random_get_next_from(polynormal)
        checksum1 ^= polynormal & MASK32
        shift += 7
    for byte in reversed(data):
        polynormal = (polynormal + (byte << (shift & 0xF))) & MASK64
        polynormal = ref_random_get_next_from(polynormal)
        checksum2 ^= polynormal & MASK32
        shift += 3
    # s32 storage: fold to two's-complement 32-bit range like a C `s32`.
    def to_s32(v: int) -> int:
        v &= MASK32
        return v - (1 << 32) if v & 0x80000000 else v

    return to_s32(checksum1), to_s32(checksum2)


def ref_set_stage_time(times: bytearray, levelid: int, difficulty: int, newtime: int, max_level: int = 20) -> None:
    if newtime == 0:
        newtime = 0x4F
    elif newtime > 0x3FF:
        newtime = 0x3FF
    offset = (difficulty * max_level + levelid) * 10
    index = offset >> 3
    case = 7 - (offset & 7)
    if case == 7:
        times[index] = ((times[index] & 0xFF00) | ((newtime >> 2) & 0xFF)) & 0xFF
        times[index + 1] = ((times[index + 1] & 0xFF3F) | ((newtime << 6) & 0xC0)) & 0xFF
    elif case == 5:
        times[index] = ((times[index] & 0xFFC0) | ((newtime >> 4) & 0x3F)) & 0xFF
        times[index + 1] = ((times[index + 1] & 0xFF0F) | ((newtime << 4) & 0xF0)) & 0xFF
    elif case == 3:
        times[index] = ((times[index] & 0xFFF0) | ((newtime >> 6) & 0x0F)) & 0xFF
        times[index + 1] = ((times[index + 1] & 0xFF03) | ((newtime << 2) & 0xFC)) & 0xFF
    elif case == 1:
        times[index] = ((times[index] & 0xFFFC) | ((newtime >> 8) & 0x03)) & 0xFF
        times[index + 1] = ((times[index + 1] & 0xFF00) | (newtime & 0xFFF)) & 0xFF
    else:
        raise AssertionError("unreachable case")


class RandomGetNextFromTest(unittest.TestCase):
    def test_matches_independent_reference_for_many_seeds(self) -> None:
        seeds = [0, 1, 0xFFFFFFFFFFFFFFFF, 0x8F809F473108B3C1, 0x1234_5678_9ABC_DEF0, 12345]
        for seed in seeds:
            with self.subTest(seed=seed):
                self.assertEqual(eeprom.random_get_next_from(seed), ref_random_get_next_from(seed))

    def test_output_is_64bit_masked(self) -> None:
        result = eeprom.random_get_next_from(0xFFFFFFFFFFFFFFFF)
        self.assertEqual(result, result & MASK64)


class GenerateCrcTest(unittest.TestCase):
    def test_matches_independent_reference_on_various_buffers(self) -> None:
        buffers = [
            bytes(24),
            bytes(88),
            bytes([0x42] + [0] * 23),
            bytes(range(24)),
            bytes([0xFF] * 88),
        ]
        for buf in buffers:
            with self.subTest(buf=buf[:8]):
                self.assertEqual(eeprom.generate_crc(buf), ref_generate_crc(buf))

    def test_different_input_gives_different_checksum(self) -> None:
        a = eeprom.generate_crc(bytes(24))
        b = eeprom.generate_crc(bytes([1] + [0] * 23))
        self.assertNotEqual(a, b)

    def test_deterministic(self) -> None:
        buf = bytes(range(88))
        self.assertEqual(eeprom.generate_crc(buf), eeprom.generate_crc(buf))


class StageTimeBitPackingTest(unittest.TestCase):
    def test_set_matches_independent_reference(self) -> None:
        times_a = bytearray(76)
        times_b = bytearray(76)
        for difficulty in range(3):
            for levelid in range(20):
                eeprom.set_stage_time(times_a, levelid, difficulty, 0x4F)
                ref_set_stage_time(times_b, levelid, difficulty, 0x4F)
        self.assertEqual(bytes(times_a), bytes(times_b))

    def test_round_trip_get_after_set_for_every_level_difficulty(self) -> None:
        times = bytearray(76)
        for difficulty in range(3):
            for levelid in range(20):
                eeprom.set_stage_time(times, levelid, difficulty, 0x4F)
        for difficulty in range(3):
            for levelid in range(20):
                with self.subTest(levelid=levelid, difficulty=difficulty):
                    self.assertEqual(eeprom.get_stage_time(times, levelid, difficulty), 0x4F)

    def test_writing_one_slot_does_not_clobber_neighboring_slots(self) -> None:
        """The bit-packed times[] array shares bytes between adjacent
        (difficulty, level) slots (10 bits each) -- setting one entry must
        never disturb an already-written neighbor."""
        times = bytearray(76)
        # Fill every slot with a distinct-ish value, then verify each is
        # still readable at the end (last-writer values, not corrupted).
        expected = {}
        for difficulty in range(3):
            for levelid in range(20):
                value = 0x40 + (difficulty * 20 + levelid) % 0x180
                eeprom.set_stage_time(times, levelid, difficulty, value)
                expected[(levelid, difficulty)] = value
        for (levelid, difficulty), value in expected.items():
            with self.subTest(levelid=levelid, difficulty=difficulty):
                self.assertEqual(eeprom.get_stage_time(times, levelid, difficulty), value)

    def test_zero_newtime_clamped_to_default(self) -> None:
        times = bytearray(76)
        eeprom.set_stage_time(times, 0, 0, 0)
        self.assertEqual(eeprom.get_stage_time(times, 0, 0), 0x4F)

    def test_overflow_newtime_clamped_to_max(self) -> None:
        times = bytearray(76)
        eeprom.set_stage_time(times, 0, 0, 0x7FFF)
        self.assertEqual(eeprom.get_stage_time(times, 0, 0), 0x3FF)


class SaveDataBlockTest(unittest.TestCase):
    def test_block_is_96_bytes(self) -> None:
        block = eeprom.build_unlocked_save_data_block(folder=0, slot=0, bond=0)
        self.assertEqual(len(block), 96)

    def test_checksum_field_matches_generate_crc_over_completion_through_end(self) -> None:
        block = eeprom.build_unlocked_save_data_block(folder=0, slot=0, bond=0)
        chk1, chk2 = eeprom.generate_crc(block[8:96])
        stored1 = int.from_bytes(block[0:4], "big", signed=True)
        stored2 = int.from_bytes(block[4:8], "big", signed=True)
        self.assertEqual((stored1, stored2), (chk1, chk2))

    def test_completion_bitflags_encodes_folder_slot_bond_active(self) -> None:
        block = eeprom.build_unlocked_save_data_block(folder=2, slot=1, bond=3)
        flags = block[8]
        self.assertEqual(flags & 0x07, 2, "folder occupies bits 0-2")
        self.assertEqual((flags & 0x18) >> 3, 1, "slot occupies bits 3-4")
        self.assertEqual((flags & 0x60) >> 5, 3, "bond occupies bits 5-6")
        self.assertEqual(flags & 0x80, 0, "must not be reset-flagged (it is the active save)")

    def test_all_20_stages_and_3_difficulties_report_completed(self) -> None:
        block = eeprom.build_unlocked_save_data_block(folder=0, slot=0, bond=0)
        times = block[18:94]
        for difficulty in range(3):
            for levelid in range(20):
                with self.subTest(levelid=levelid, difficulty=difficulty):
                    self.assertNotEqual(eeprom.get_stage_time(times, levelid, difficulty), 0)


class ResetBlockTest(unittest.TestCase):
    def test_reset_block_is_96_bytes_and_flagged_reset(self) -> None:
        block = eeprom.build_blank_reset_block()
        self.assertEqual(len(block), 96)
        self.assertEqual(block[8] & 0x80, 0x80)

    def test_reset_block_checksum_is_internally_consistent(self) -> None:
        block = eeprom.build_blank_reset_block()
        chk1, chk2 = eeprom.generate_crc(block[8:96])
        stored1 = int.from_bytes(block[0:4], "big", signed=True)
        stored2 = int.from_bytes(block[4:8], "big", signed=True)
        self.assertEqual((stored1, stored2), (chk1, chk2))


class SmallSaveHeaderTest(unittest.TestCase):
    def test_header_is_32_bytes(self) -> None:
        header = eeprom.build_small_save_header()
        self.assertEqual(len(header), 32)

    def test_canary_byte_is_0x42(self) -> None:
        # SAVEFLAGS_SET(FOLDER3=2, SAVESLOT1=0, BOND_CONNERY=1, FALSE)
        #   = ((2<<5)&0xE0) | ((0*8)&0x18) | ((1<<1)&0x6) | 0 = 0x42
        header = eeprom.build_small_save_header()
        self.assertEqual(header[8], 0x42)

    def test_header_checksum_matches_generate_crc_over_unk24(self) -> None:
        header = eeprom.build_small_save_header()
        chk1, chk2 = eeprom.generate_crc(header[8:32])
        stored1 = int.from_bytes(header[0:4], "big", signed=True)
        stored2 = int.from_bytes(header[4:8], "big", signed=True)
        self.assertEqual((stored1, stored2), (chk1, chk2))


class BuildEepromImageTest(unittest.TestCase):
    def test_image_is_512_bytes(self) -> None:
        image = eeprom.build_eeprom_image()
        self.assertEqual(len(image), 512)

    def test_header_occupies_first_32_bytes(self) -> None:
        image = eeprom.build_eeprom_image()
        header = eeprom.build_small_save_header()
        self.assertEqual(image[0:32], header)

    def test_slot_zero_is_the_unlocked_active_save(self) -> None:
        image = eeprom.build_eeprom_image(folder=0, slot=0)
        block = image[32:128]
        self.assertEqual(block[8] & 0x87, 0x00)  # folder=0, not reset

    def test_other_four_slots_are_reset_flagged(self) -> None:
        image = eeprom.build_eeprom_image(folder=0, slot=0)
        for slot_index in range(1, 5):
            start = 32 + slot_index * 96
            block = image[start:start + 96]
            with self.subTest(slot_index=slot_index):
                self.assertEqual(block[8] & 0x80, 0x80)

    def test_verify_reports_all_stages_unlocked(self) -> None:
        image = eeprom.build_eeprom_image(folder=0, slot=0)
        report = eeprom.verify_eeprom_image(image, folder=0)
        self.assertTrue(report["header_checksum_ok"])
        self.assertTrue(report["active_slot_checksum_ok"])
        self.assertEqual(report["active_folder"], 0)
        self.assertEqual(len(report["unlocked"]), 20)
        self.assertTrue(all(report["unlocked"].values()))

    def test_verify_detects_corruption(self) -> None:
        image = bytearray(eeprom.build_eeprom_image(folder=0, slot=0))
        image[32 + 8] ^= 0xFF  # corrupt the active slot's completion_bitflags
        report = eeprom.verify_eeprom_image(bytes(image), folder=0)
        self.assertFalse(report["active_slot_checksum_ok"])

    def test_different_folder_argument_changes_active_folder_bits(self) -> None:
        image = eeprom.build_eeprom_image(folder=1, slot=0)
        block = image[32:128]
        self.assertEqual(block[8] & 0x07, 1)


class CliTest(unittest.TestCase):
    def test_main_writes_a_512_byte_file(self) -> None:
        import subprocess
        import tempfile
        import os

        tool = str(Path(__file__).resolve().parents[1] / "make_unlocked_eeprom.py")
        with tempfile.TemporaryDirectory() as tmp:
            out_path = os.path.join(tmp, "seed.eeprom")
            result = subprocess.run(
                [sys.executable, tool, "--out", out_path],
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            with open(out_path, "rb") as handle:
                data = handle.read()
            self.assertEqual(len(data), 512)

    def test_main_verify_flag_reports_pass(self) -> None:
        import subprocess
        import tempfile
        import os

        tool = str(Path(__file__).resolve().parents[1] / "make_unlocked_eeprom.py")
        with tempfile.TemporaryDirectory() as tmp:
            out_path = os.path.join(tmp, "seed.eeprom")
            subprocess.run([sys.executable, tool, "--out", out_path], check=True)
            result = subprocess.run(
                [sys.executable, tool, "--verify", out_path],
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("PASS", result.stdout)


if __name__ == "__main__":
    unittest.main()
