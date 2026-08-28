#!/usr/bin/env python3
"""T4 -- synthesize an all-stages-unlocked GoldenEye 007 EEPROM save image.

The patched-ares stock-ROM oracle (docs/ROM_COMPARISON.md) boots the REAL,
unmodified GoldenEye ROM. A fresh save only unlocks Dam (mission 1) -- every
other of the 20 solo stages requires an EEPROM save that already shows the
earlier stages completed. This tool builds that save file from scratch so
the ares oracle's own frontend menu navigation can reach any mission.

Ground truth for the on-disk format is the port's own (decompiled, faithful)
save code, NOT any GLOBAL_ASM/`#else` reference body:
  - src/game/file.h            struct save_data / smallSave layout
  - src/game/file2.c           fileValidateSaves / fileGetSaveForFoldernum /
                                fileSetDifficultyStageTime / fileGenerateCRC
                                call sites (byte ranges checksummed)
  - src/game/crc.c             fileGenerateCRC algorithm
  - src/random.c               randomGetNextFrom (the CRC's inner PRNG step)
  - src/joy.c + src/platform/stubs.c (osEepromLongRead/Write) -- confirms
    EEPROM_BLOCK_SIZE=8 (block-address units) and that GoldenEye (ROM id
    "NGE") uses a 512-byte (4Kbit) EEPROM chip (mia's cartridge database,
    build/ares-movement-oracle/ares/mia/medium/nintendo-64.cpp).

On-disk layout (512 bytes total, matching real N64 EEPROM addressing):
    [0:32)   smallSave header: chksum1(s32 BE), chksum2(s32 BE), unk[24]
             unk[0] must equal SAVEFLAGS_SET(FOLDER3, SAVESLOT1, BOND_CONNERY,
             FALSE) = 0x42 (a customization canary checked verbatim by
             fileValidateSaves); checksum = fileGenerateCRC(unk[0], unk[24]).
    [32:512) five 96-byte save_data blocks (SAVESLOT1..SAVESLOTRAMROM's
             validate-time window), each:
               chksum1(s32 BE), chksum2(s32 BE),          [0:8)
               completion_bitflags,                        [8]
                 bits0-2 = folder, bits3-4 = slot, bits5-6 = bond, bit7 = doreset
               flag_007, music_vol, sfx_vol,                [9:12)
               options(u16 BE),                             [12:14)
               unlocked_cheats_1/2/3, padding,               [14:18)
               times[76] (bit-packed 10-bit completion times, 3
                 sub-007 difficulties x 20 stages),          [18:94)
               (2 trailing bytes of natural struct alignment padding)
             checksum = fileGenerateCRC(completion_bitflags .. end-of-block).

All multi-byte integer fields are stored BIG-ENDIAN: this file seeds the
STOCK side (a real, unmodified N64 MIPS ROM running in ares), not the native
PC port's own little-endian ge007_eeprom.bin.

This is a GENERATED-artifact tool: the script is committed, its output
(.eep/.eeprom files) is not.

CLI:
    tools/make_unlocked_eeprom.py --out seed.eeprom
    tools/make_unlocked_eeprom.py --verify seed.eeprom
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

MASK64 = (1 << 64) - 1
MASK32 = (1 << 32) - 1

SP_LEVEL_MAX = 20          # src/bondconstants.h LEVEL_SOLO_SEQUENCE
DIFFICULTY_AGENT = 0
DIFFICULTY_SECRET = 1
DIFFICULTY_00 = 2
# DIFFICULTY_007 is intentionally excluded: fileSetDifficultyStageTime /
# fileGetSaveStageDifficultyTime special-case it (fileIs007ModeUnlocked)
# instead of reading/writing times[].
SUB_007_DIFFICULTIES = (DIFFICULTY_AGENT, DIFFICULTY_SECRET, DIFFICULTY_00)

FOLDER1, FOLDER2, FOLDER3 = 0, 1, 2
SAVESLOT1 = 0
BOND_BROSNAN, BOND_CONNERY = 0, 1

SAVEFLAG_FOLDER = 0x07
SAVEFLAG_SLOT = 0x18
SAVEFLAG_BOND = 0x60
SAVEFLAG_DORESET = 0x80

DEFAULT_TIME = 0x4F  # matches fileSetDifficultyStageTime's newtime==0 clamp default

SAVE_DATA_SIZE = 96      # sizeof(save_data); file2.c comment: "0x60 = sizeof(save_data)"
SMALL_SAVE_SIZE = 32     # sizeof(smallSave): 4 + 4 + 24
NUM_VALIDATED_SLOTS = 5  # fileValidateSaves reads/validates SAVESLOT1..<SAVESLOTRAMROM
EEPROM_IMAGE_SIZE = SMALL_SAVE_SIZE + NUM_VALIDATED_SLOTS * SAVE_DATA_SIZE  # 512

# SAVEFLAGS_SET(FOLDER3, SAVESLOT1, BOND_CONNERY, FALSE) -- the customization
# canary fileValidateSaves checks smallSave.unk[0] against verbatim.
# ((FOLDER3<<5)&0xE0) | ((SAVESLOT1*8)&0x18) | ((BOND_CONNERY<<1)&0x6) | 0
SMALL_SAVE_CANARY = ((FOLDER3 << 5) & 0xE0) | ((SAVESLOT1 * 8) & 0x18) | ((BOND_CONNERY << 1) & 0x6)
assert SMALL_SAVE_CANARY == 0x42


# --- src/random.c: randomGetNextFrom -----------------------------------------

def random_get_next_from(state: int) -> int:
    """Mirrors src/random.c's randomGetNextFrom(u64 *param_1) exactly.

    u64 t = (*param_1 << 63) >> 31;
    t    |= (*param_1 << 31) >> 32;
    t    ^= (*param_1 << 44) >> 32;
    *param_1 = t ^ ((t >> 20) & 0xFFF);
    return (u32)*param_1;

    Returns the new 64-bit state (the u32 low half is the "return value"
    used by the C caller as a randomGetNextFrom() result).
    """
    state &= MASK64
    t = ((state << 63) & MASK64) >> 31
    t |= ((state << 31) & MASK64) >> 32
    t ^= ((state << 44) & MASK64) >> 32
    return (t ^ ((t >> 20) & 0xFFF)) & MASK64


# --- src/game/crc.c: fileGenerateCRC ------------------------------------------

def generate_crc(data: bytes) -> tuple[int, int]:
    """Mirrors src/game/crc.c's fileGenerateCRC(addressA, addressB, retval)
    for the byte range `data` (== [addressA, addressB)). Returns
    (chksum1, chksum2) as C `s32` (two's-complement, matching what gets
    stored into save_data.chksum1/chksum2)."""
    polynormal = 0x8F809F473108B3C1
    checksum1 = 0
    checksum2 = 0
    shift = 0

    for byte in data:
        polynormal = (polynormal + ((byte << (shift & 0xF)) & MASK64)) & MASK64
        polynormal = random_get_next_from(polynormal)
        checksum1 ^= polynormal & MASK32
        shift += 7

    for byte in reversed(data):
        polynormal = (polynormal + ((byte << (shift & 0xF)) & MASK64)) & MASK64
        polynormal = random_get_next_from(polynormal)
        checksum2 ^= polynormal & MASK32
        shift += 3

    def to_s32(value: int) -> int:
        value &= MASK32
        return value - (1 << 32) if value & 0x80000000 else value

    return to_s32(checksum1), to_s32(checksum2)


# --- src/game/file2.c: fileSetDifficultyStageTime / fileGetSaveStageDifficultyTime --

def set_stage_time(
    times: bytearray, levelid: int, difficulty: int, newtime: int, max_level: int = SP_LEVEL_MAX
) -> None:
    """Mirrors fileSetDifficultyStageTime's bit-packing exactly (including
    the u8-array-vs-0xffXX-mask truncation quirk in the original C: e.g.
    `times[index] &= 0xff00` on a `u8 times[]` array always clears the byte,
    since a u8 has no bits at/above bit 8 to survive the mask)."""
    if newtime == 0:
        newtime = DEFAULT_TIME
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
    else:  # pragma: no cover -- offset&7 in {0,2,4,6} always yields case in {7,5,3,1}
        raise AssertionError(f"unreachable case for offset&7={offset & 7}")


def get_stage_time(times: bytes, levelid: int, difficulty: int, max_level: int = SP_LEVEL_MAX) -> int:
    """Mirrors fileGetSaveStageDifficultyTime's bit-unpacking exactly
    (excluding the DIFFICULTY_007 special case, which never touches
    times[])."""
    offset = (difficulty * max_level + levelid) * 10
    index = offset >> 3
    case = 7 - (offset & 7)

    if case == 7:
        return ((times[index] & 0xFF) << 2) | ((times[index + 1] & 0xC0) >> 6)
    elif case == 5:
        return ((times[index] & 0x3F) << 4) | ((times[index + 1] & 0xF0) >> 4)
    elif case == 3:
        return ((times[index] & 0x0F) << 6) | ((times[index + 1] & 0xFC) >> 2)
    elif case == 1:
        return ((times[index] & 0x3) << 8) | (times[index + 1] & 0xFFF)
    else:  # pragma: no cover
        raise AssertionError(f"unreachable case for offset&7={offset & 7}")


def is_stage_completed(times: bytes, levelid: int, difficulty: int, max_level: int = SP_LEVEL_MAX) -> bool:
    """Mirrors fileGetSaveStageCompletedForDifficulty for the sub-007
    difficulties (time != 0 means completed)."""
    return get_stage_time(times, levelid, difficulty, max_level) != 0


# --- save_data / smallSave block builders -------------------------------------

def _completion_bitflags(folder: int, slot: int, bond: int, reset: bool) -> int:
    """Mirrors the SAVEFLAG_* bitfield ACCESSOR functions (fileGetSaveFolder/
    fileSetSaveFoldernum, fileGetSaveFlagSlot/fileResetSaveFlagSlot,
    fileGetSelectedBond/fileSetSelectedBond, fileGetSaveFlagDoReset/
    fileSetSaveFlagDoReset) -- NOT the separate (and differently-shifted)
    SAVEFLAGS_SET() macro, which only real production code paths use for
    the smallSave canary and BLANKSAVEDATA's static initializer. Real
    save_data slots are always built up through the accessor functions
    (see fileBuildWriteNewSave), so that's what this mirrors."""
    value = folder & SAVEFLAG_FOLDER
    value |= (slot << 3) & SAVEFLAG_SLOT
    value |= (bond << 5) & SAVEFLAG_BOND
    if reset:
        value |= SAVEFLAG_DORESET
    return value & 0xFF


def build_unlocked_save_data_block(
    folder: int = FOLDER1,
    slot: int = SAVESLOT1,
    bond: int = BOND_BROSNAN,
    time: int = DEFAULT_TIME,
) -> bytes:
    """Builds one 96-byte save_data block with every solo stage marked
    completed at every sub-007 difficulty (Agent/Secret Agent/00 Agent) --
    this makes fileIsStageUnlockedAtDifficulty() return STAGESTATUS_COMPLETED
    (>= STAGESTATUS_UNLOCKED) for all 20 stages, including the bonus-stage
    gates (Aztec needs Secret-Agent-complete, Egypt needs 00-Agent-complete)
    which are satisfied automatically since direct completion short-circuits
    those checks."""
    times = bytearray(76)
    for difficulty in SUB_007_DIFFICULTIES:
        for levelid in range(SP_LEVEL_MAX):
            set_stage_time(times, levelid, difficulty, time)

    body = bytearray()
    body.append(_completion_bitflags(folder, slot, bond, reset=False))  # completion_bitflags
    body.append(0)      # flag_007
    body.append(0xFF)   # music_vol (BLANKSAVEDATA convention)
    body.append(0xFF)   # sfx_vol (BLANKSAVEDATA convention)
    options = 0x0002 | 0x0008 | 0x0010 | 0x0020  # DEFAULT_OPTIONS (file2.h)
    body += options.to_bytes(2, "big")
    body.append(0)      # unlocked_cheats_1
    body.append(0)      # unlocked_cheats_2
    body.append(0)      # unlocked_cheats_3
    body.append(0)      # padding
    body += times        # [18:94)
    body += bytes(2)     # trailing struct-alignment padding -> 96 bytes total
    assert len(body) == SAVE_DATA_SIZE - 8

    chk1, chk2 = generate_crc(bytes(body))
    header = chk1.to_bytes(4, "big", signed=True) + chk2.to_bytes(4, "big", signed=True)
    block = header + bytes(body)
    assert len(block) == SAVE_DATA_SIZE
    return block


def build_blank_reset_block() -> bytes:
    """Mirrors BLANKSAVEDATA (folder=0, slot=0, bond=BOND_BROSNAN, reset=1,
    music/sfx=0xFF, options=DEFAULT_OPTIONS, everything else zero) with a
    correctly-computed checksum -- unlike the literal C initializer (which
    hardcodes chksum1=chksum2=0, relying on a later fileResetSave() call to
    fix it up), this is already self-consistent so fileValidateSaves() never
    needs to touch it."""
    body = bytearray(SAVE_DATA_SIZE - 8)
    body[0] = _completion_bitflags(FOLDER1, SAVESLOT1, BOND_BROSNAN, reset=True)
    body[1] = 0        # flag_007
    body[2] = 0xFF      # music_vol
    body[3] = 0xFF      # sfx_vol
    options = 0x0002 | 0x0008 | 0x0010 | 0x0020
    body[4] = (options >> 8) & 0xFF
    body[5] = options & 0xFF
    # unlocked_cheats_1/2/3, padding, times[76], trailing padding: all zero.

    chk1, chk2 = generate_crc(bytes(body))
    header = chk1.to_bytes(4, "big", signed=True) + chk2.to_bytes(4, "big", signed=True)
    block = header + bytes(body)
    assert len(block) == SAVE_DATA_SIZE
    return block


def build_small_save_header() -> bytes:
    """Builds the 32-byte smallSave validity header: unk[0] = the
    SAVEFLAGS_SET(FOLDER3, SAVESLOT1, BOND_CONNERY, FALSE) canary
    fileValidateSaves() checks verbatim, rest zero, checksum over
    unk[0:24)."""
    unk = bytearray(24)
    unk[0] = SMALL_SAVE_CANARY
    chk1, chk2 = generate_crc(bytes(unk))
    header = chk1.to_bytes(4, "big", signed=True) + chk2.to_bytes(4, "big", signed=True) + bytes(unk)
    assert len(header) == SMALL_SAVE_SIZE
    return header


def build_eeprom_image(folder: int = FOLDER1, slot: int = SAVESLOT1, bond: int = BOND_BROSNAN, time: int = DEFAULT_TIME) -> bytes:
    """Builds the full 512-byte GoldenEye EEPROM image: the smallSave
    header followed by NUM_VALIDATED_SLOTS save_data blocks. `slot`
    (0..NUM_VALIDATED_SLOTS-1) selects which physical block index holds the
    active, all-stages-unlocked save for `folder`; every other slot is a
    correctly-checksummed, reset-flagged blank so fileValidateSaves() never
    needs to rewrite (and never disturbs) the active slot."""
    if not (0 <= slot < NUM_VALIDATED_SLOTS):
        raise ValueError(f"slot must be in [0, {NUM_VALIDATED_SLOTS}): got {slot}")

    image = bytearray(build_small_save_header())
    for slot_index in range(NUM_VALIDATED_SLOTS):
        if slot_index == slot:
            image += build_unlocked_save_data_block(folder=folder, slot=slot_index, bond=bond, time=time)
        else:
            image += build_blank_reset_block()
    assert len(image) == EEPROM_IMAGE_SIZE
    return bytes(image)


# --- verification (independent read-back, mirroring file.c's checks) ---------

def verify_eeprom_image(data: bytes, folder: int = FOLDER1) -> dict:
    """Independently re-derives what fileValidateSaves()/
    fileGetSaveForFoldernum()/fileIsStageUnlockedAtDifficulty() would see:
    header canary+checksum, the active slot for `folder` (if any) and its
    checksum, and completion status for all 20 stages. Does not mutate or
    assume anything about the generator above -- this is the read-side
    mirror used both by the CLI --verify flag and by the unit tests."""
    if len(data) != EEPROM_IMAGE_SIZE:
        raise ValueError(f"expected a {EEPROM_IMAGE_SIZE}-byte EEPROM image, got {len(data)} bytes")

    header = data[0:SMALL_SAVE_SIZE]
    header_unk = header[8:32]
    header_chk1 = int.from_bytes(header[0:4], "big", signed=True)
    header_chk2 = int.from_bytes(header[4:8], "big", signed=True)
    expect_chk1, expect_chk2 = generate_crc(bytes(header_unk))
    header_checksum_ok = (header_chk1, header_chk2) == (expect_chk1, expect_chk2)
    header_canary_ok = header_unk[0] == SMALL_SAVE_CANARY

    active_slot = None
    active_folder = None
    active_slot_checksum_ok = False
    unlocked: dict[int, bool] = {}

    for slot_index in range(NUM_VALIDATED_SLOTS):
        start = SMALL_SAVE_SIZE + slot_index * SAVE_DATA_SIZE
        block = data[start:start + SAVE_DATA_SIZE]
        flags = block[8]
        reset = bool(flags & SAVEFLAG_DORESET)
        slot_folder = flags & SAVEFLAG_FOLDER
        if reset or slot_folder != folder:
            continue
        chk1 = int.from_bytes(block[0:4], "big", signed=True)
        chk2 = int.from_bytes(block[4:8], "big", signed=True)
        expect1, expect2 = generate_crc(bytes(block[8:96]))
        active_slot = slot_index
        active_folder = slot_folder
        active_slot_checksum_ok = (chk1, chk2) == (expect1, expect2)
        times = block[18:94]
        for levelid in range(SP_LEVEL_MAX):
            unlocked[levelid] = all(
                is_stage_completed(times, levelid, difficulty) for difficulty in SUB_007_DIFFICULTIES
            )
        break

    return {
        "header_canary_ok": header_canary_ok,
        "header_checksum_ok": header_checksum_ok,
        "active_slot": active_slot,
        "active_folder": active_folder,
        "active_slot_checksum_ok": active_slot_checksum_ok,
        "unlocked": unlocked,
    }


# --- CLI -----------------------------------------------------------------

def _cli_build(args: argparse.Namespace) -> int:
    image = build_eeprom_image(folder=args.folder, slot=args.slot, bond=args.bond, time=args.time)
    out_path = Path(args.out)
    out_path.write_bytes(image)
    print(f"wrote {len(image)}-byte EEPROM image to {out_path} (folder={args.folder} slot={args.slot})")
    return 0


def _cli_verify(args: argparse.Namespace) -> int:
    data = Path(args.verify).read_bytes()
    report = verify_eeprom_image(data, folder=args.folder)
    ok = (
        report["header_canary_ok"]
        and report["header_checksum_ok"]
        and report["active_slot"] is not None
        and report["active_slot_checksum_ok"]
        and len(report["unlocked"]) == SP_LEVEL_MAX
        and all(report["unlocked"].values())
    )
    print(f"header_canary_ok={report['header_canary_ok']}")
    print(f"header_checksum_ok={report['header_checksum_ok']}")
    print(f"active_slot={report['active_slot']} active_folder={report['active_folder']}")
    print(f"active_slot_checksum_ok={report['active_slot_checksum_ok']}")
    unlocked_count = sum(1 for v in report["unlocked"].values() if v)
    print(f"stages_unlocked={unlocked_count}/{SP_LEVEL_MAX}")
    print("PASS" if ok else "FAIL")
    return 0 if ok else 1


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--out", help="write a synthesized all-unlocked EEPROM image to this path")
    parser.add_argument("--verify", help="read back and verify an EEPROM image at this path instead of building one")
    parser.add_argument("--folder", type=int, default=FOLDER1, help="save folder index (0=FOLDER1, default)")
    parser.add_argument("--slot", type=int, default=SAVESLOT1, help="physical EEPROM slot for the active save (default 0)")
    parser.add_argument("--bond", type=int, default=BOND_BROSNAN, help="selected Bond actor index (cosmetic only, default 0)")
    parser.add_argument("--time", type=int, default=DEFAULT_TIME, help="completion time recorded for every stage/difficulty (default 0x4f)")
    args = parser.parse_args(argv)

    if args.verify:
        return _cli_verify(args)
    if args.out:
        return _cli_build(args)

    parser.error("one of --out or --verify is required")
    return 2  # pragma: no cover -- argparse.error() exits


if __name__ == "__main__":
    raise SystemExit(main())
