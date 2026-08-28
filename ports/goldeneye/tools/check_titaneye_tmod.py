#!/usr/bin/env python3
"""Audit TitanEYE ET_REL size, exports, relocations and closed import surface."""

from __future__ import annotations

import struct
import sys
from pathlib import Path

ALLOWED_IMPORTS = {
    "memcpy", "memset", "memcmp", "memmove", "strlen", "strcmp", "strncmp",
    "strchr", "strrchr", "strstr", "strcpy", "strncpy", "strcat", "strncat",
    "strspn", "strcspn", "strpbrk", "strcasecmp", "strncasecmp", "atoi", "atol",
    "strtol", "strtoul", "strtoll", "strtoull", "strtod", "strtof", "abs", "labs",
    "qsort", "bsearch", "snprintf", "vsnprintf", "sprintf", "vsprintf", "sscanf",
    "sinf", "cosf", "tanf", "asinf", "acosf", "atanf", "atan2f", "sqrtf",
    "fabsf", "floorf", "ceilf", "fmodf", "powf", "logf", "log10f", "expf",
    "ldexpf", "frexpf", "lroundf", "sin", "cos", "tan", "asin", "acos", "atan",
    "atan2", "sqrt", "fabs", "floor", "ceil", "fmod", "pow", "log", "log10",
    "exp", "ldexp", "frexp", "lround",
}
REQUIRED_EXPORTS = {"titan_app_init", "titan_app_update", "titan_app_draw"}
ALLOWED_RELOCS = {0, 2, 4, 5, 6}


def cstr(data: bytes, start: int) -> str:
    end = data.find(b"\0", start)
    if end < 0:
        raise ValueError("unterminated ELF string")
    return data[start:end].decode("ascii")


def main(path: Path) -> None:
    data = path.read_bytes()
    if len(data) > 8 * 1024 * 1024:
        raise ValueError("module file exceeds Titan's 8 MiB limit")
    if data[:6] != b"\x7fELF\x01\x01" or len(data) < 52:
        raise ValueError("expected ELF32 little-endian")
    shoff = struct.unpack_from("<I", data, 32)[0]
    shentsize, shnum = struct.unpack_from("<HH", data, 46)
    if shentsize != 40 or not 0 < shnum <= 64 or shoff + shnum * 40 > len(data):
        raise ValueError("invalid section table")
    sections = [struct.unpack_from("<10I", data, shoff + i * 40) for i in range(shnum)]
    symtabs = [section for section in sections if section[1] == 2]
    if len(symtabs) != 1:
        raise ValueError("expected one symbol table")
    symtab = symtabs[0]
    strtab = sections[symtab[6]]
    strings = data[strtab[4] : strtab[4] + strtab[5]]
    imports: set[str] = set()
    exports: set[str] = set()
    for offset in range(symtab[4], symtab[4] + symtab[5], 16):
        name_at, value, size = struct.unpack_from("<III", data, offset)
        info = data[offset + 12]
        shndx = struct.unpack_from("<H", data, offset + 14)[0]
        del value, size
        if name_at == 0:
            continue
        name = cstr(strings, name_at)
        if shndx == 0:
            imports.add(name)
        elif (info & 0x0F) == 2:
            exports.add(name)
    unknown = imports - ALLOWED_IMPORTS
    if unknown:
        raise ValueError("unapproved imports: " + ", ".join(sorted(unknown)))
    missing = REQUIRED_EXPORTS - exports
    if missing:
        raise ValueError("missing exports: " + ", ".join(sorted(missing)))
    for section in sections:
        if section[1] != 9:
            continue
        if section[9] != 8 or section[5] % 8:
            raise ValueError("invalid REL table")
        for offset in range(section[4], section[4] + section[5], 8):
            info = struct.unpack_from("<I", data, offset + 4)[0]
            if (info & 0xFF) not in ALLOWED_RELOCS:
                raise ValueError(f"unsupported relocation type {info & 0xFF}")
    print(f"titaneye-tmod: OK ({len(imports)} imports, {shnum} sections, {len(data)} bytes)")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("usage: check_titaneye_tmod.py MODULE.tmod")
    try:
        main(Path(sys.argv[1]))
    except (OSError, UnicodeDecodeError, ValueError) as exc:
        raise SystemExit(f"titaneye-tmod: FAIL: {exc}") from exc
