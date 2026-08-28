#!/usr/bin/env python3
from __future__ import annotations
import struct
import sys
from pathlib import Path

ELF_MAGIC = b"\x7fELF"
ELFCLASS32 = 1
ELFDATA2LSB = 1
ET_REL = 1
EM_MIPS = 8
EF_MIPS_ABI2 = 0x00000020
EF_MIPS_ABI_MASK = 0x0000F000
EF_MIPS_ABI_O32 = 0x00001000
EF_MIPS_PIC = 0x00000002
EF_MIPS_CPIC = 0x00000004

def fail(path: Path, message: str) -> int:
    print(f"titan-native-n32: FAIL {path}: {message}", file=sys.stderr)
    return 1

def check(path: Path) -> int:
    try:
        data = path.read_bytes()
    except OSError as exc:
        return fail(path, str(exc))
    if len(data) < 52:
        return fail(path, "truncated ELF32 header")
    if data[:4] != ELF_MAGIC:
        return fail(path, "not ELF")
    if data[4] != ELFCLASS32:
        return fail(path, f"ELF class {data[4]} != ELF32")
    if data[5] != ELFDATA2LSB:
        return fail(path, f"ELF data encoding {data[5]} != little-endian")
    e_type, e_machine = struct.unpack_from("<HH", data, 16)
    e_flags = struct.unpack_from("<I", data, 36)[0]
    if e_type != ET_REL:
        return fail(path, f"e_type={e_type} != ET_REL")
    if e_machine != EM_MIPS:
        return fail(path, f"e_machine={e_machine} != EM_MIPS")
    abi_nibble = e_flags & EF_MIPS_ABI_MASK
    if (e_flags & EF_MIPS_ABI2) == 0:
        return fail(path, f"EF_MIPS_ABI2 missing (e_flags=0x{e_flags:08x})")
    if abi_nibble != 0:
        kind = "O32" if abi_nibble == EF_MIPS_ABI_O32 else f"legacy-ABI-0x{abi_nibble:04x}"
        return fail(path, f"{kind} forbidden; Titan Native ABI is N32/ABI2 (e_flags=0x{e_flags:08x})")
    if e_flags & (EF_MIPS_PIC | EF_MIPS_CPIC):
        return fail(path, f"PIC/CPIC forbidden for in-process Titan modules (e_flags=0x{e_flags:08x})")
    print(f"titan-native-n32: PASS {path} e_flags=0x{e_flags:08x} ABI2/N32")
    return 0

def main() -> int:
    if len(sys.argv) < 2:
        print(f"usage: {sys.argv[0]} module.tmod [module2.tmod ...]", file=sys.stderr)
        return 2
    rc = 0
    for name in sys.argv[1:]:
        rc |= check(Path(name))
    return rc

if __name__ == "__main__":
    raise SystemExit(main())
