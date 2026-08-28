#!/usr/bin/env python3
from pathlib import Path
import sys

root = Path(sys.argv[1] if len(sys.argv) > 1 else ".").resolve()
if root.name == "kernel":
    root = root.parent

src = (root / "ports/goldeneye/third_party/compiler-rt-builtins/fp_compare_impl.inc").read_text(encoding="utf-8")
mk = (root / "ports/goldeneye/Makefile.titan").read_text(encoding="utf-8")

errors = []
needle = "typedef int CMP_RESULT __attribute__((__mode__(__libgcc_cmp_return__)));"
if needle not in src:
    errors.append("compiler-rt compare result does not use GCC target mode")
if "_Static_assert(sizeof(GCC_CMP_RESULT) == sizeof(CMP_RESULT)" not in src:
    errors.append("compiler-rt ABI static assertion was removed")
if "-mabi=n32" not in mk:
    errors.append("TitanEYE is not compiled as N32")
if "-mabi=32" in mk:
    errors.append("TitanEYE O32 flag survived")

if errors:
    for e in errors:
        print("rc1-titaneye-n32-compiler-rt-cmp: FAIL:", e)
    raise SystemExit(1)

print("rc1-titaneye-n32-compiler-rt-cmp: PASS GCC __libgcc_cmp_return__ drives TitanEYE N32 compare builtins")
