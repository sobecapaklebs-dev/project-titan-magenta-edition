#!/usr/bin/env python3
from pathlib import Path
import sys

root = Path(sys.argv[1] if len(sys.argv) > 1 else ".").resolve()
if root.name == "kernel":
    root = root.parent

mk = (root / "ports/goldeneye/Makefile.titan").read_text(encoding="utf-8")
src = root / "ports/goldeneye/third_party/compiler-rt-builtins/ctzdi2.c"

errors = []
if "$(COMPILER_RT_DIR)/ctzdi2.c" not in mk:
    errors.append("ctzdi2.c missing from TitanEYE COMPILER_RT_SOURCES")
if not src.is_file():
    errors.append("vendored compiler-rt ctzdi2.c missing")
if "-mabi=n32" not in mk or "-mabi=32" in mk:
    errors.append("TitanEYE N32 build policy regressed")

if errors:
    for e in errors:
        print("rc1-titaneye-n32-ctzdi2: FAIL:", e)
    raise SystemExit(1)

print("rc1-titaneye-n32-ctzdi2: PASS __ctzdi2 provided by vendored compiler-rt under N32")
