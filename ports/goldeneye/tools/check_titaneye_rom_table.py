#!/usr/bin/env python3
"""Validate the US ROM resource offsets and mandatory terminal sentinel."""

import csv
from pathlib import Path
import re
import sys


def fail(message: str) -> None:
    raise SystemExit(f"titaneye-rom-table: FAIL: {message}")


if len(sys.argv) != 3:
    fail("use: check_titaneye_rom_table.py rom_offsets.c filelist.u.csv")

source_path = Path(sys.argv[1])
filelist_path = Path(sys.argv[2])
source = source_path.read_text(encoding="utf-8")

assignment = re.compile(
    r"file_resource_table\[(\d+)\]\.hw_address\s*=\s*"
    r"TITAN_ROM_POINTER\(romData,\s*(0x[0-9a-fA-F]+)\)"
)
rows = [(int(index), int(offset, 16)) for index, offset in assignment.findall(source)]
if len(rows) != 726:
    fail(f"expected 726 resource offsets, found {len(rows)}")

for expected, (index, offset) in enumerate(rows, start=1):
    if index != expected:
        fail(f"resource index {index} appears where {expected} was expected")
    if expected > 1 and offset < rows[expected - 2][1]:
        fail(f"resource offset descends at index {index}")

with filelist_path.open(newline="", encoding="utf-8") as handle:
    file_rows = list(csv.reader(handle))
if not file_rows:
    fail("US ROM file list is empty")

last_offset = int(file_rows[-1][0])
last_size = int(file_rows[-1][1])
if rows[-1][1] != last_offset:
    fail("OBENDSEG offset does not match the US ROM file list")
sentinel = last_offset + last_size

sentinel_pattern = re.compile(
    r"file_resource_table\[OBJ_INDEX_END\]\.hw_address\s*=\s*"
    r"TITAN_ROM_POINTER\(romData,\s*(0x[0-9a-fA-F]+)\)"
)
match = sentinel_pattern.search(source)
if match is None:
    fail("terminal OBJ_INDEX_END sentinel is missing")
if int(match.group(1), 16) != sentinel:
    fail(
        f"terminal sentinel is {match.group(1)}, expected 0x{sentinel:08x}"
    )

print(
    f"titaneye-rom-table: OK {len(rows)} resources, "
    f"terminal sentinel 0x{sentinel:08x}"
)
